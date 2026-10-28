#requires -Version 7.5
$ErrorActionPreference='Stop'
$repo=Split-Path $PSScriptRoot -Parent
. (Join-Path $repo 'launcher/Launcher.Core.ps1')
. (Join-Path $repo 'launcher/Launcher.Feedback.ps1')
$root=Join-Path $repo ('test-temp/feedback-'+[guid]::NewGuid().ToString('N'))
$saved=@{};foreach($key in @('TEMP','TMP','TMPDIR','PYTHONDONTWRITEBYTECODE')){$saved[$key]=[Environment]::GetEnvironmentVariable($key,'Process')}
$script:checks=0
function Check([bool]$Good,[string]$Name){if(-not $Good){throw "FAILED: $Name"};$script:checks++;Write-Host "PASS $Name"}
function Reject([scriptblock]$Work,[string]$Name){$bad=$false;try{& $Work|Out-Null}catch{$bad=$true};Check $bad $Name}
try {
    $null=Set-FvrLauncherStorage $root
    $package=@{version='0.1.4';runtime_entry='runtime/Entry.ps1';runtime_version='416';runtime_build='abc123';release_repository='justiceofrip/BC2VR';feedback_repository='justiceofrip/BC2VR'}
    $settings=@{local_backend=$null;game_path='G:\Private Person\Game'}
    $draft=Get-FvrFeedbackDraft $package $settings
    $uri=[uri]$draft.url
    Check ($uri.Host -eq 'github.com' -and $uri.AbsolutePath -eq '/justiceofrip/BC2VR/issues/new') 'feedback targets the separate BC2 VR repository'
    Check ($draft.body.Contains('Launcher/package: 0.1.4') -and $draft.body.Contains('runtime_version: 416') -and $draft.body.Contains('Steps to reproduce:')) 'draft includes installed build and useful reproduction prompts'
    Check (-not $draft.body.Contains('Private Person') -and -not $draft.body.Contains('FrostbiteVR')) 'issue draft contains no settings paths or framework destination'
    $custom=$package.Clone();$custom.feedback_repository='example/BC2-VR'
    Check (([uri](Get-FvrFeedbackDraft $custom $settings).url).AbsolutePath -eq '/example/BC2-VR/issues/new') 'package feedback destination is configurable'
    foreach($bad in @('https://github.com/evil/repo','owner/repo?x=1','owner/../escape','owner/repo/issues','owner\repo')){Reject {Assert-FvrRepository $bad} ('reject repository '+$bad)}
    Set-FvrPackageRepositories $package
    Assert-FvrAssetUrl 'https://github.com/justiceofrip/BC2VR/releases/download/v0.1.4-prealpha.1/a.zip' 'a.zip'
    Check $true 'pre-alpha release tag is valid for numeric package version'
    Reject {Assert-FvrAssetUrl 'https://github.com/justiceofrip/FrostbiteVR/releases/download/v1/a.zip' 'a.zip'} 'updater refuses framework assets after product selection'
    $private=@'
log G:\Private Person\my session\runtime.log value=23
{"path":"C:\\Users\\Private Person\\AppData\\log.txt"}
UNC \\server\Private Share\log.txt
unix /home/private_person/report
email private@example.test
token=secret-token
{"password":"json-password-value"}
{"username":"other-private-name"}
Authorization: Bearer secret-token
github_pat_supersecret
url https://example.test/log?signature=secret-value
Steam 76561191234567890
frames=245 errors=0
'@
    $clean=Protect-FvrFeedbackText $private
    foreach($secret in @('Private Person','server','private_person','private@example.test','secret-token','json-password-value','other-private-name','supersecret','secret-value','76561191234567890')){Check (-not $clean.Contains($secret)) ('sanitize '+$secret)}
    Check ($clean.Contains('frames=245 errors=0')) 'sanitization retains ordinary counters'
    $data=Join-Path $root 'data';$runtime=Join-Path $data 'runtime';$run=Join-Path $runtime 'sessions/test-one'
    $null=New-Item -ItemType Directory -Path $run -Force
    [IO.File]::WriteAllText((Join-Path $data 'launcher-last.log'),$private)
    [IO.File]::WriteAllText((Join-Path $run 'host.stderr.log'),('old-secret-not-at-tail'+'x'*1MB+"`n"+$private))
    [IO.File]::WriteAllText((Join-Path $run 'host.stdout.log'),'runtime frames=90')
    [IO.File]::WriteAllBytes((Join-Path $run 'native.stdout.log'),[byte[]]@(0,5,1,0))
    [IO.File]::WriteAllText((Join-Path $run 'native.stderr.log'),'native errors=0')
    [IO.File]::WriteAllText((Join-Path $run 'supervisor.stdout.log'),'supervisor ready')
    [IO.File]::WriteAllText((Join-Path $run 'supervisor.stderr.log'),'supervisor no errors')
    Write-FvrJson (Join-Path $run 'activation.json') @{passed=$false;outcome='foreground_request_not_observed';activation_attempts=1;path='F:\Private Person\Game\BFBC2Game.exe'}
    [IO.File]::WriteAllText((Join-Path $run 'activation.log'),('OLD-ACTIVATION-NOT-AT-TAIL'+'x'*32KB+"`nactivation failed: target identity unavailable`nF:\Private Person\Game\BFBC2Game.exe"))
    [IO.File]::WriteAllText((Join-Path $run 'memory.dmp'),'MUST-NOT-LEAK-DUMP')
    [IO.File]::WriteAllText((Join-Path $run 'native-trace.json'),'MUST-NOT-LEAK-LARGE-REPORT')
    Write-FvrJson (Join-Path $runtime 'active-session.json') @{relative_folder='sessions/test-one'}
    $bundle=New-FvrFeedbackBundle $root $package $settings
    $text=[IO.File]::ReadAllText($bundle.report)
    Check ($bundle.uploaded -eq $false -and $bundle.bytes -lt 512KB) 'bundle is bounded and remains local'
    Check ($text.Contains('runtime frames=90') -and $text.Contains('native errors=0')) 'only launcher-owned session text logs included'
    Check ($text.Contains('--- activation.json') -and $text.Contains('foreground_request_not_observed') -and $text.Contains('--- activation.log') -and $text.Contains('target identity unavailable')) 'activation failure evidence is included before host/native startup'
    Check (-not $text.Contains('OLD-ACTIVATION-NOT-AT-TAIL') -and -not $text.Contains('Private Person')) 'activation tails are bounded and redact personal paths'
    Check (-not $text.Contains('old-secret-not-at-tail') -and -not $text.Contains('MUST-NOT-LEAK')) 'log tails bounded and dumps/reports excluded'
    Check ($text.Contains('[binary content omitted]') -and -not $text.Contains('Private Person')) 'binary logs omitted and personal paths removed'
    $zip=[IO.Compression.ZipFile]::OpenRead($bundle.zip)
    try{Check (($zip.Entries.FullName -join ',') -eq 'Report.txt,README.txt') 'ZIP contains only reviewable text files'}finally{$zip.Dispose()}
    $outside=Join-Path $root 'not-runtime';$null=New-Item -ItemType Directory -Path $outside
    [IO.File]::WriteAllText((Join-Path $outside 'host.stdout.log'),'MUST-NOT-READ-FOREIGN-SESSION')
    Write-FvrJson (Join-Path $runtime 'active-session.json') @{relative_folder='sessions/../../not-runtime'}
    $again=New-FvrFeedbackBundle $root $package $settings
    Check (-not ([IO.File]::ReadAllText($again.report)).Contains('MUST-NOT-READ')) 'session record cannot escape own runtime state'
    Check (@(Get-ChildItem -LiteralPath $again.folder -File).Count -eq 3) 'repeated bundle generation retains exactly one bundle and two review files'
    $linked=Join-Path $runtime 'sessions/linked-run'
    $null=New-Item -ItemType Junction -Path $linked -Target $outside
    Write-FvrJson (Join-Path $runtime 'active-session.json') @{relative_folder='sessions/linked-run'}
    $linkedBundle=New-FvrFeedbackBundle $root $package $settings
    Check (-not ([IO.File]::ReadAllText($linkedBundle.report)).Contains('MUST-NOT-READ')) 'linked session cannot bypass confinement'
    Remove-Item -LiteralPath $linked
    # Exercise the actual worker against a tiny isolated install. Feedback must
    # finish before SteamVR, native runtime validation or local-backend routing.
    $versionRoot=Join-Path $root 'versions/0.1.4';$null=New-Item -ItemType Directory -Path (Join-Path $versionRoot 'app') -Force
    $package.files=@(foreach($name in @('Launcher.ps1','Worker.ps1','Launcher.Core.ps1','Launcher.Feedback.ps1','Launcher.Storage.ps1','Launcher.Settings.ps1')) {
        $dest=Join-Path $versionRoot ('app/'+$name)
        Copy-Item -LiteralPath (Join-Path $repo ('launcher/'+$name)) -Destination $dest
        @{path=('app/'+$name);sha256=(Get-FileHash -LiteralPath $dest).Hash.ToLowerInvariant()}
    })
    $package.schema='fvr.desktop.package.v1';$package.runtime_entry=$null
    Write-FvrJson (Join-Path $versionRoot 'package.json') $package
    Write-FvrJson (Join-Path $root 'active.json') @{current='0.1.4'}
    Write-FvrJson (Join-Path $data 'settings.json') $settings
    $workerResult=& (Join-Path $PSHOME 'pwsh.exe') -NoProfile -NonInteractive -File (Join-Path $versionRoot 'app/Worker.ps1') -InstallRoot $root -Action Feedback
    Check ($LASTEXITCODE -eq 0 -and (Read-FvrJson (Join-Path $data 'operation.json')).state -eq 'feedback_ready') 'real feedback worker completes without a runtime or local profile'
    Check (($workerResult|ConvertFrom-Json -AsHashtable).uploaded -eq $false) 'worker reports local bundle and no upload'
    $broken='{"game_path": "D:\bad\path", "local_backend": ???}'
    [IO.File]::WriteAllText((Join-Path $data 'settings.json'),$broken)
    Write-FvrJson (Join-Path $runtime 'launch-check.json') @{game_supported=$false;game_sha256=('e'*64)}
    Write-FvrJson (Join-Path $runtime 'launcher-placement.json') @{schema='bc2vr.launcher-placement.v1';allowed=$false;relationship='game_root';error='Install BC2 VR in a separate launcher folder.'}
    Write-FvrJson (Join-Path $runtime 'xr-selection.json') @{label='Virtual Desktop fixture';manifest='C:\Users\Private Person\runtime.json'}
    Write-FvrJson (Join-Path $runtime 'asset-preparation.json') @{stage='body equipment';error='Required mesh differs';private_path='D:\Private Person\game'}
    Write-FvrJson (Join-Path $data 'package-check.json') @{expected_version='0.1.6';verified=$false;error='Package identity mismatch fixture'}
    $workerResult=& (Join-Path $PSHOME 'pwsh.exe') -NoProfile -NonInteractive -File (Join-Path $versionRoot 'app/Worker.ps1') -InstallRoot $root -Action Feedback
    Check ($LASTEXITCODE -eq 0 -and (Read-FvrJson (Join-Path $data 'operation.json')).state -eq 'feedback_ready') 'malformed settings do not block actual feedback worker'
    Check ([IO.File]::ReadAllText((Join-Path $data 'settings.json')) -ceq $broken) 'feedback leaves unrecoverable settings untouched'
    $report=Get-Content -LiteralPath (Join-Path $data 'feedback/Report.txt') -Raw
    Check ($report.Contains('e'*64) -and $report.Contains('Virtual Desktop fixture') -and -not $report.Contains('Private Person')) 'startup build fingerprint and runtime diagnostics survive path redaction'
    Check ($report.Contains('Required mesh differs') -and $report.Contains('package-check.json') -and $report.Contains('asset-preparation.json')) 'asset failure and package identity evidence included without sharing game files'
    Check ($report.Contains('launcher-placement.json') -and $report.Contains('game_root')) 'path-free placement failure is included in saved diagnostics'
    Add-Type -AssemblyName PresentationFramework,PresentationCore,WindowsBase
    $uiSource=[IO.File]::ReadAllText((Join-Path $repo 'launcher/Launcher.ps1'))
    $markupBlocks=[regex]::Matches($uiSource,'(?s)\[xml\]\$(?:markup|feedbackMarkup)=@''\r?\n(.*?)\r?\n''@')
    Check ($markupBlocks.Count -eq 2) 'both launcher and feedback dialog XAML located'
    foreach($block in $markupBlocks) {
        [xml]$xml=$block.Groups[1].Value
        $window=[Windows.Markup.XamlReader]::Load([Xml.XmlNodeReader]::new($xml))
        Check ($null -ne $window) 'launcher/feedback XAML instantiates without showing a window'
        $window.Close()
    }
    foreach($path in @('launcher/Launcher.ps1','launcher/Worker.ps1','launcher/Launcher.Core.ps1','launcher/Launcher.Feedback.ps1','launcher/Launcher.Storage.ps1','launcher/Launcher.Settings.ps1')) {
        $errors=$null;$tokens=$null;$null=[Management.Automation.Language.Parser]::ParseFile((Join-Path $repo $path),[ref]$tokens,[ref]$errors)
        Check ($errors.Count -eq 0) ('parse '+$path)
    }
    Write-Host "$script:checks feedback checks passed; no browser, network, game or VR process started."
} finally {
    foreach($key in $saved.Keys){[Environment]::SetEnvironmentVariable($key,$saved[$key],'Process')}
    Remove-FvrOwnedTree (Split-Path $root -Parent) (Split-Path $root -Leaf)
}
