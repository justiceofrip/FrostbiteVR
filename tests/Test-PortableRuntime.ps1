#requires -Version 7.5
$ErrorActionPreference='Stop'
$repo=Split-Path $PSScriptRoot -Parent
. (Join-Path $repo 'launcher/Launcher.Core.ps1')
. (Join-Path $repo 'launcher/runtime/Runtime.Core.ps1')
$root=Join-Path $repo ('test-temp/portable-'+[guid]::NewGuid().ToString('N'))
$saved=@{};foreach($key in @('TEMP','TMP','TMPDIR','PYTHONDONTWRITEBYTECODE','BC2_WEATHER_TRANSLATION','BC2_WEATHER_OBSERVER','BC2_WEATHER_TRANSLATION_EXPERIMENT','BC2_WEATHER_OBSERVER_DELAY_MS')){$saved[$key]=[Environment]::GetEnvironmentVariable($key,'Process')}
$script:checks=0
function Check([bool]$Value,[string]$Name){if(-not $Value){throw "FAILED: $Name"};$script:checks++;Write-Host "PASS $Name"}
function Reject([scriptblock]$Work,[string]$Name){$bad=$false;try{& $Work|Out-Null}catch{$bad=$true};Check $bad $Name}
try {
    $null=Set-FvrLauncherStorage $root
    foreach($file in @(Get-ChildItem (Join-Path $repo 'launcher/runtime') -Filter '*.ps1')+@(Get-Item (Join-Path $repo 'launcher/Bootstrap.ps1'))+@(Get-Item (Join-Path $repo 'tools/Prepare-DesktopRuntime.ps1'))){
        $errors=$null;$tokens=$null;$null=[Management.Automation.Language.Parser]::ParseFile($file.FullName,[ref]$tokens,[ref]$errors)
        Check ($errors.Count -eq 0) ('parse '+$file.Name+': '+($errors -join '; '))
    }
    $childScript=Join-Path $root 'echo-arguments.ps1'
    [IO.File]::WriteAllText($childScript,'[IO.File]::WriteAllText((Join-Path $PSScriptRoot "arguments.json"), (@($args)|ConvertTo-Json)); [IO.File]::WriteAllText((Join-Path $PSScriptRoot "environment.json"), (@{temp=$env:TEMP;marker=$env:BC2_TEST_MARKER}|ConvertTo-Json))')
    $expected=@('plain','two words','quote"middle','trailing\','two\\','', '$(not executable); & <tag>', 'unicode-ß')
    $p=Start-Bc2Child (Join-Path $PSHOME 'pwsh.exe') (@('-NoProfile','-NonInteractive','-File',$childScript)+$expected) $root 'child' @{BC2_TEST_MARKER='local-only'}
    Check ($p.WaitForExit(15000) -and $p.ExitCode -eq 0) 'harmless child process exits'
    $actual=@(Read-FvrJson (Join-Path $root 'arguments.json'))
    Check ($actual.Count -eq $expected.Count) 'argument count preserved including empty argument'
    for($i=0;$i -lt $expected.Count;$i++){Check ($actual[$i] -ceq $expected[$i]) ('exact argument '+$i)}
    $environment=Read-FvrJson (Join-Path $root 'environment.json')
    Check ($environment.marker -eq 'local-only' -and $environment.temp.StartsWith($root)) 'child environment stays on selected data drive'
    Check (-not $env:BC2_TEST_MARKER) 'child-only environment does not mutate caller'
    $policyScript=Join-Path $root 'policy-child.ps1'
    [IO.File]::WriteAllText($policyScript,'[IO.File]::WriteAllText((Join-Path $PSScriptRoot "policy-result.txt"), (Get-ExecutionPolicy -Scope Process).ToString())')
    $env:BC2_TEST_POLICY_CHILD=$policyScript
    $env:BC2_TEST_POLICY_PWSH=Join-Path $PSHOME 'pwsh.exe'
    try {
        & (Join-Path $PSHOME 'pwsh.exe') -NoProfile -NonInteractive -ExecutionPolicy Restricted -Command '& $env:BC2_TEST_POLICY_PWSH -NoProfile -NonInteractive -ExecutionPolicy Bypass -File $env:BC2_TEST_POLICY_CHILD; exit $LASTEXITCODE'
        Check ($LASTEXITCODE -eq 0 -and (Get-Content -LiteralPath (Join-Path $root 'policy-result.txt') -Raw) -eq 'Bypass') 'owned script executes under Restricted parent through process-only bypass'
    } finally {Remove-Item Env:BC2_TEST_POLICY_CHILD,Env:BC2_TEST_POLICY_PWSH -ErrorAction SilentlyContinue}
    Reject {ConvertTo-Bc2CommandArgument ('bad'+[char]0)} 'NUL process argument rejected'
    $identity=Get-Bc2ProcessIdentity (Get-Process -Id $PID)
    Check ((Find-Bc2OwnedProcess $identity).Id -eq $PID) 'exact process identity resolves'
    $wrong=$identity.Clone();$wrong.start_utc=[DateTime]::UtcNow.AddDays(-1).ToString('o')
    Check ($null -eq (Find-Bc2OwnedProcess $wrong)) 'reused PID with different creation time rejected'
    $wrong=$identity.Clone();$wrong.path=Join-Path $root 'different.exe'
    Check ($null -eq (Find-Bc2OwnedProcess $wrong)) 'same PID with different executable rejected'
    # Exercise the real packaged Worker -> Entry Stop path. This test process is
    # the harmless supervisor identity; Stop may only create its fixture request.
    $stopInstall=Join-Path $root 'stop-install';$stopVersion=Join-Path $stopInstall 'versions/0.0.1'
    $stopRuntime=Join-Path $stopInstall 'data/runtime';$stopSession=Join-Path $stopRuntime 'sessions/fixture'
    foreach($folder in @('app','runtime')){$null=[IO.Directory]::CreateDirectory((Join-Path $stopVersion $folder))}
    $null=[IO.Directory]::CreateDirectory($stopSession)
    foreach($name in @('Launcher.ps1','Worker.ps1','Launcher.Core.ps1','Launcher.Feedback.ps1','Launcher.Storage.ps1','Launcher.Settings.ps1')){
        Copy-Item -LiteralPath (Join-Path $repo ('launcher/'+$name)) -Destination (Join-Path $stopVersion ('app/'+$name))
    }
    foreach($name in @('Entry.ps1','Runtime.Core.ps1','Dependencies.ps1','Startup.ps1','Resolution.ps1')){
        Copy-Item -LiteralPath (Join-Path $repo ('launcher/runtime/'+$name)) -Destination (Join-Path $stopVersion ('runtime/'+$name))
    }
    $stopFiles=@(foreach($file in Get-ChildItem -LiteralPath $stopVersion -File -Recurse){@{path=[IO.Path]::GetRelativePath($stopVersion,$file.FullName).Replace('\','/');sha256=(Get-FileHash -LiteralPath $file.FullName).Hash.ToLowerInvariant()}})
    Write-FvrJson (Join-Path $stopVersion 'package.json') @{schema='fvr.desktop.package.v1';version='0.0.1';runtime_entry='runtime/Entry.ps1';files=$stopFiles}
    Write-FvrJson (Join-Path $stopInstall 'active.json') @{current='0.0.1'}
    foreach($stopCase in @('missing','stale','owned')){
        if($stopCase -ne 'missing'){
            $supervisor=$identity.Clone()
            if($stopCase -eq 'stale'){$supervisor.start_utc=[DateTime]::UtcNow.AddDays(-1).ToString('o')}
            Write-FvrJson (Join-Path $stopRuntime 'active-session.json') @{relative_folder='sessions/fixture';supervisor=$supervisor}
        }
        $commandId=[guid]::NewGuid().ToString()
        $stopWorker=Start-Bc2Child (Join-Path $PSHOME 'pwsh.exe') @('-NoProfile','-NonInteractive','-ExecutionPolicy','Bypass','-File',(Join-Path $stopVersion 'app/Worker.ps1'),'-InstallRoot',$stopInstall,'-Action','Stop','-CommandId',$commandId) $root ('stop-'+$stopCase)
        try{Check ($stopWorker.WaitForExit(15000) -and $stopWorker.ExitCode -eq 0) ('real Stop worker exits: '+$stopCase)}finally{$stopWorker.Dispose()}
        $operation=Read-FvrJson (Join-Path $stopInstall 'data/operation.json')
        $requested=$stopCase -eq 'owned'
        $expectedOutcome=if($requested){'requested'}else{'no_owned_session'}
        $expectedMessage=if($requested){'VR stop requested. BC2 and SteamVR remain open; headset shutdown is not yet confirmed.'}else{'No owned BC2 VR session is running.'}
        Check ($operation.state -eq 'done' -and $operation.stop_outcome -eq $expectedOutcome -and $operation.message -ceq $expectedMessage) ('Stop preserves actual result without claiming host exit: '+$stopCase)
        Check ($operation.command_id -eq $commandId -and $operation.worker_pid -gt 0 -and [bool]$operation.started_utc) ('Stop operation carries command identity and start: '+$stopCase)
        Check ((Test-Path -LiteralPath (Join-Path $stopSession 'stop.request')) -eq $requested) ('Stop requests only the live owned supervisor: '+$stopCase)
    }
    Check ((Find-Bc2OwnedProcess $identity).Id -eq $PID) 'Stop leaves the owned supervisor alive for cooperative cleanup'
    $cache=Join-Path $root 'cache';$null=New-Item -ItemType Directory -Path $cache
    $files=@(1..6|ForEach-Object{$name="file$_.txt";$path=Join-Path $cache $name;[IO.File]::WriteAllText($path,"asset$_");@{path=$name;sha256=(Get-FileHash -LiteralPath $path).Hash.ToLowerInvariant()}})
    $manifest=@{schema='bc2vr.local-assets.v1';recipe_sha256='a'*64;game_sha256='b'*64;files=$files}
    Write-FvrJson (Join-Path $cache 'assets.json') $manifest
    Check (Test-Bc2AssetCache $cache ('a'*64) ('b'*64)) 'complete content-pinned local cache accepted'
    Check (-not(Test-Bc2AssetCache $cache ('c'*64) ('b'*64))) 'different preparation recipe invalidates cache'
    Check (-not(Test-Bc2AssetCache $cache ('a'*64) ('c'*64))) 'different game build invalidates cache'
    [IO.File]::WriteAllText((Join-Path $cache 'file3.txt'),'tampered')
    Check (-not(Test-Bc2AssetCache $cache ('a'*64) ('b'*64))) 'changed cache content rejected'
    $manifest.files[0].path='../outside';Write-FvrJson (Join-Path $cache 'assets.json') $manifest
    Check (-not(Test-Bc2AssetCache $cache ('a'*64) ('b'*64))) 'cache traversal refused'
    $audio=Get-Bc2AudioEnvironment $cache
    Check ($audio.BC2_MECHANISM_AUDIO_CACHE -eq (Join-Path $cache 'pump') -and $audio.BC2_SHELL_RELOAD_AUDIO_CACHE -eq (Join-Path $cache 'shell')) 'audio caches share selected data drive'
    Check ($audio.BC2_MECHANISM_AUDIO_REPLACE_SPAS -eq '1' -and $audio.BC2_SHELL_RELOAD_AUDIO_REPLACE -eq '1') 'both independently verified audio routes enabled'
    # Exercise actual child inheritance/removal with the same Start-Process
    # environment map as Session.ps1, without launching or accessing BC2.
    $weatherKeys=@('BC2_WEATHER_TRANSLATION','BC2_WEATHER_OBSERVER','BC2_WEATHER_TRANSLATION_EXPERIMENT','BC2_WEATHER_OBSERVER_DELAY_MS')
    foreach($key in $weatherKeys){[Environment]::SetEnvironmentVariable($key,'inherited-diagnostic','Process')}
    $weatherScript=Join-Path $root 'weather-child.ps1'
    [IO.File]::WriteAllText($weatherScript,'$r=@{};foreach($key in @("BC2_WEATHER_TRANSLATION","BC2_WEATHER_OBSERVER","BC2_WEATHER_TRANSLATION_EXPERIMENT","BC2_WEATHER_OBSERVER_DELAY_MS")){$r[$key]=[Environment]::GetEnvironmentVariable($key,"Process")};[IO.File]::WriteAllText((Join-Path $PSScriptRoot "weather.json"),($r|ConvertTo-Json))')
    $weather=Start-Bc2Child (Join-Path $PSHOME 'pwsh.exe') @('-NoProfile','-NonInteractive','-File',$weatherScript) $root 'weather' (Get-Bc2AudioEnvironment $cache)
    Check ($weather.WaitForExit(15000) -and $weather.ExitCode -eq 0) 'harmless weather inheritance child exits'
    $observedWeather=Read-FvrJson (Join-Path $root 'weather.json')
    Check ($observedWeather.BC2_WEATHER_TRANSLATION -eq '1') 'child enables validated runtime cloud correction'
    foreach($key in $weatherKeys | Select-Object -Skip 1){Check ($null -eq $observedWeather[$key]) ('child clears '+$key)}
    foreach($key in $weatherKeys){Check ([Environment]::GetEnvironmentVariable($key,'Process') -eq 'inherited-diagnostic') ('parent environment unchanged: '+$key)}
    Write-FvrJson (Join-Path $root 'active-session.json') @{relative_folder='../escape';supervisor=$identity}
    Reject {Get-Bc2Session $root} 'session pointer traversal refused'
    Write-FvrJson (Join-Path $root 'active-session.json') @{relative_folder='cache/elsewhere';supervisor=$identity}
    Reject {Get-Bc2Session $root} 'session pointer cannot select cache'
    Write-Host "Portable runtime checks passed: $script:checks"
} finally {
    foreach($key in $saved.Keys){[Environment]::SetEnvironmentVariable($key,$saved[$key],'Process')}
    Remove-FvrOwnedTree (Split-Path $root -Parent) (Split-Path $root -Leaf)
}
