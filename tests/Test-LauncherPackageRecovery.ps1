#requires -Version 7.5
$ErrorActionPreference='Stop'
$repo=Split-Path $PSScriptRoot -Parent
. (Join-Path $repo 'launcher/Launcher.Core.ps1')
$root=Join-Path $repo ('test-temp/package-recovery-'+[guid]::NewGuid().ToString('N'))
$saved=@{};foreach($key in @('TEMP','TMP','TMPDIR','PYTHONDONTWRITEBYTECODE')){$saved[$key]=[Environment]::GetEnvironmentVariable($key,'Process')}
$script:checks=0
function Check([bool]$Good,[string]$Name){if(-not $Good){throw "FAILED: $Name"};$script:checks++;Write-Host "PASS $Name"}
function Reject([scriptblock]$Work,[string]$Name){$message=$null;try{& $Work|Out-Null}catch{$message=$_.Exception.Message};Check ([bool]$message) $Name;return $message}
function Fixture([string]$Version){
    $folder=Join-Path $root ('fixture-'+$Version);$null=[IO.Directory]::CreateDirectory((Join-Path $folder 'app'));$null=[IO.Directory]::CreateDirectory((Join-Path $folder 'runtime'))
    foreach($name in @('Launcher.ps1','Worker.ps1','Launcher.Core.ps1','Launcher.Feedback.ps1','Launcher.Storage.ps1','Launcher.Settings.ps1')){Copy-Item -LiteralPath (Join-Path $repo ('launcher/'+$name)) -Destination (Join-Path $folder ('app/'+$name))}
    # Only transport/discovery are replaced in this isolated fixture. Worker,
    # archive verification, install transaction and runtime gating stay real.
    $discovery=@'
function Find-FvrUpdate([string]$Root,[switch]$IncludePrerelease) {
    [IO.File]::WriteAllText((Join-Path $Root 'data/test-query-repository.txt'),$script:FvrRepository)
    if($script:FvrRepository -cne 'justiceofrip/BC2VR'){throw 'Fixture refuses a foreign repository.'}
    return Read-FvrJson (Join-Path $Root 'data/test-update.json')
}
function Receive-FvrFile([string]$Url,[string]$Destination,[long]$MaxBytes) {
    $record=Read-FvrJson (Join-Path (Split-Path $Destination -Parent) 'data/test-update.json')
    Assert-FvrAssetUrl $Url $record.manifest.asset
    if((Get-Item -LiteralPath $record.fixture_zip).Length -gt $MaxBytes){throw 'Fixture exceeds download bound.'}
    [IO.File]::Copy($record.fixture_zip,$Destination,$false)
}
'@
    [IO.File]::AppendAllText((Join-Path $folder 'app/Launcher.Core.ps1'),"`n"+$discovery)
    [IO.File]::WriteAllText((Join-Path $folder 'runtime/Entry.ps1'),"throw 'TEST RUNTIME MUST NOT EXECUTE'")
    $files=@(foreach($file in Get-ChildItem -LiteralPath $folder -File -Recurse){@{path=[IO.Path]::GetRelativePath($folder,$file.FullName).Replace('\','/');sha256=(Get-FileHash -LiteralPath $file.FullName).Hash.ToLowerInvariant()}})
    Write-FvrJson (Join-Path $folder 'package.json') @{schema='fvr.desktop.package.v1';version=$Version;runtime_entry='runtime/Entry.ps1';release_repository='justiceofrip/BC2VR';feedback_repository='justiceofrip/BC2VR';files=$files}
    $zip=$folder+'.zip';[IO.Compression.ZipFile]::CreateFromDirectory($folder,$zip)
    $archive=[IO.Compression.ZipFile]::OpenRead($zip);try{$expanded=[long]($archive.Entries|Measure-Object Length -Sum).Sum}finally{$archive.Dispose()}
    return @{folder=$folder;zip=$zip;manifest=@{schema='fvr.desktop.release.v1';version=$Version;asset="BC2VR-$Version-win64.zip";bytes=(Get-Item -LiteralPath $zip).Length;unpacked_bytes=$expanded;sha256=(Get-FileHash -LiteralPath $zip).Hash.ToLowerInvariant()}}
}
function Run-Worker([string]$Install,[string]$Version,[string]$Action){
    $start=[Diagnostics.ProcessStartInfo]::new((Join-Path $PSHOME 'pwsh.exe'));$start.UseShellExecute=$false;$start.CreateNoWindow=$true;$start.RedirectStandardOutput=$true;$start.RedirectStandardError=$true
    foreach($arg in @('-NoProfile','-NonInteractive','-File',(Join-Path $Install ("versions/$Version/app/Worker.ps1")),'-InstallRoot',$Install,'-Action',$Action,'-Prerelease')){$start.ArgumentList.Add($arg)}
    $child=[Diagnostics.Process]::Start($start);$out=$child.StandardOutput.ReadToEndAsync();$err=$child.StandardError.ReadToEndAsync()
    try{if(-not $child.WaitForExit(30000)){throw 'Fixture worker timed out.'};return @{code=$child.ExitCode;stdout=$out.GetAwaiter().GetResult();stderr=$err.GetAwaiter().GetResult();status=(Read-FvrJson (Join-Path $Install 'data/operation.json'))}}
    finally{$child.Dispose()}
}
function Corrupt-Identity([string]$Install,[string]$Version){
    $path=Join-Path $Install ("versions/$Version/package.json");$value=Read-FvrJson $path
    $value.version='9.9.9';$value.release_repository='untrusted/Other';$value.feedback_repository='untrusted/Other';Write-FvrJson $path $value
}
try{
    $null=Set-FvrLauncherStorage $root
    $v15=Fixture '0.1.5';$v16=Fixture '0.1.6';$v17=Fixture '0.1.7'
    $schemaFixture=Join-Path $root 'schema-check';$null=[IO.Directory]::CreateDirectory($schemaFixture)
    Write-FvrJson (Join-Path $schemaFixture 'package.json') @{schema='fvr.desktop.settings.v1';version='0.1.6'}
    $schemaError=Reject {Test-FvrPackage $schemaFixture '0.1.6'} 'wrong schema is diagnosed without trusting manifest fields'
    Check ($schemaError.Contains("schema 'fvr.desktop.package.v1'") -and $schemaError.Contains("schema 'fvr.desktop.settings.v1'")) 'schema diagnostic names both expected and actual formats'
    [IO.File]::WriteAllText((Join-Path $schemaFixture 'package.json'),'{malformed')
    $malformed=Get-FvrInstalledPackageState $schemaFixture '0.1.6'
    Check (-not $malformed.verified -and $malformed.package.feedback_repository -ceq 'justiceofrip/BC2VR') 'malformed package JSON still produces safe recovery metadata'
    $install=Join-Path $root 'installed';$null=[IO.Directory]::CreateDirectory((Join-Path $install 'data'))
    Install-FvrPackage $install $v15.zip $v15.manifest;Install-FvrPackage $install $v16.zip $v16.manifest
    $settingsPath=Join-Path $install 'data/settings.json';Write-FvrJson $settingsPath @{game_path='G:\Fixture Game';local_backend=$null;custom=@{keep='unchanged'}}
    $settingsHash=(Get-FileHash -LiteralPath $settingsPath).Hash
    Corrupt-Identity $install '0.1.6'
    $badPath=Join-Path $install 'versions/0.1.6/package.json';$badHash=(Get-FileHash -LiteralPath $badPath).Hash
    $error=Reject {Test-FvrPackage (Split-Path $badPath) '0.1.6'} 'identity mismatch remains a strict rejection'
    Check ($error.Contains("version '0.1.6'") -and $error.Contains("version '9.9.9'") -and $error.Contains('Install update')) 'identity diagnostic names expected and actual versions and recovery action'
    $state=Get-FvrInstalledPackageState (Split-Path $badPath) '0.1.6'
    Check (-not $state.verified -and -not $state.package.runtime_entry -and $state.package.release_repository -ceq 'justiceofrip/BC2VR' -and $state.package.feedback_repository -ceq 'justiceofrip/BC2VR') 'failed manifest cannot supply runtime or recovery repository'
    foreach($action in @('Launch','Validate','Stop')){
        $result=Run-Worker $install '0.1.6' $action
        Check ($result.code -ne 0 -and $result.status.message.Contains('Package identity mismatch') -and -not $result.stderr.Contains('TEST RUNTIME MUST NOT EXECUTE')) ("real Worker $action rejects damaged package before runtime entry")
    }
    $feedback=Run-Worker $install '0.1.6' 'Feedback'
    Check ($feedback.code -eq 0 -and $feedback.status.state -eq 'feedback_ready' -and (Test-Path -LiteralPath (Join-Path $install 'data/feedback/BC2VR-diagnostics.zip'))) 'real Worker creates local feedback despite identity damage'
    $report=[IO.File]::ReadAllText((Join-Path $install 'data/feedback/Report.txt'))
    Check ($report.Contains('package-check.json') -and $report.Contains('expected_version') -and $report.Contains("version '9.9.9'")) 'feedback preserves bounded expected/actual package diagnosis for support'
    Write-FvrJson (Join-Path $install 'data/test-update.json') @{manifest=$v16.manifest;url='https://github.com/justiceofrip/BC2VR/releases/download/v0.1.6/BC2VR-0.1.6-win64.zip';fixture_zip=$v16.zip}
    $check=Run-Worker $install '0.1.6' 'Check'
    Check ($check.code -eq 0 -and $check.status.state -eq 'update_available' -and $check.status.message.Contains('repair')) 'equal-version update check offers repair instead of claiming healthy/latest'
    Check (([IO.File]::ReadAllText((Join-Path $install 'data/test-query-repository.txt'))) -ceq 'justiceofrip/BC2VR') 'damaged repository metadata cannot redirect the real Worker query'
    $corrupt=$v16.manifest.Clone();$corrupt.sha256='a'*64
    $null=Reject {Install-FvrPackage $install $v16.zip $corrupt} 'same-version repair rejects corrupt archive before replacement'
    Check ((Get-FileHash -LiteralPath $badPath).Hash -ceq $badHash -and (Read-FvrJson (Join-Path $install 'active.json')).previous -eq '0.1.5') 'rejected replacement preserves original package and last previous'
    $invalidStage=Join-Path $root 'invalid-replacement';Expand-FvrPackage $v16.zip $invalidStage $v16.manifest.unpacked_bytes
    [IO.File]::AppendAllText((Join-Path $invalidStage 'runtime/Entry.ps1'),'# modified after hash')
    $invalidZip=$invalidStage+'.zip';[IO.Compression.ZipFile]::CreateFromDirectory($invalidStage,$invalidZip)
    $archive=[IO.Compression.ZipFile]::OpenRead($invalidZip);try{$expanded=[long]($archive.Entries|Measure-Object Length -Sum).Sum}finally{$archive.Dispose()}
    $invalidManifest=$v16.manifest.Clone();$invalidManifest.sha256=(Get-FileHash -LiteralPath $invalidZip).Hash.ToLowerInvariant();$invalidManifest.bytes=(Get-Item -LiteralPath $invalidZip).Length;$invalidManifest.unpacked_bytes=$expanded
    $null=Reject {Install-FvrPackage $install $invalidZip $invalidManifest} 'archive hash alone cannot authorize a replacement with failed payload hashes'
    Check ((Get-FileHash -LiteralPath $badPath).Hash -ceq $badHash -and -not(Test-Path -LiteralPath (Join-Path $install 'package-repair.json'))) 'invalid staged replacement never moves original or begins repair transaction'
    $repair=Run-Worker $install '0.1.6' 'Update'
    Check ($repair.code -eq 0 -and $repair.status.state -eq 'restart_required') 'real Worker performs fully verified same-version repair'
    $null=Test-FvrPackage (Split-Path $badPath) '0.1.6';$active=Read-FvrJson (Join-Path $install 'active.json')
    Check ($active.current -eq '0.1.6' -and $active.previous -eq '0.1.5' -and (Get-FileHash -LiteralPath $settingsPath).Hash -ceq $settingsHash) 'repair keeps distinct verified previous and exact settings bytes'
    Check (-not(Test-Path -LiteralPath (Join-Path $install 'package-repair.json')) -and @(Get-ChildItem -LiteralPath $install -Directory -Filter 'repair-backup-*').Count -eq 0) 'committed repair retires journal and backup only after verification'
    $null=Reject {Install-FvrPackage $install $v16.zip $v16.manifest} 'healthy existing package cannot be silently replaced'
    $runtimePath=Join-Path $install 'versions/0.1.6/runtime/Entry.ps1';[IO.File]::AppendAllText($runtimePath,'# corrupt payload')
    $payloadState=Get-FvrInstalledPackageState (Split-Path $badPath) '0.1.6'
    Check (-not $payloadState.verified -and $payloadState.error.Contains('Package file verification failed')) 'payload damage uses recovery route without weakening file hashes'
    $payloadFeedback=Run-Worker $install '0.1.6' 'Feedback'
    Check ($payloadFeedback.code -eq 0 -and $payloadFeedback.status.state -eq 'feedback_ready') 'actual Worker feedback also survives payload hash failure'
    Install-FvrPackage $install $v16.zip $v16.manifest

    # Real filesystem snapshots model interruption at each rename boundary.
    foreach($boundary in @('prepared','original-moved','replacement-moved','committed')){
        Corrupt-Identity $install '0.1.6';$originalHash=(Get-FileHash -LiteralPath $badPath).Hash
        $activePath=Join-Path $install 'active.json';$before=[IO.File]::ReadAllBytes($activePath);$activeHash=(Get-FileHash -LiteralPath $activePath).Hash
        $id=[guid]::NewGuid().ToString('N');$stage=Join-Path $install ('staging-'+$id);$backup=Join-Path $install ('repair-backup-'+$id);$target=Split-Path $badPath
        Expand-FvrPackage $v16.zip $stage $v16.manifest.unpacked_bytes
        $journal=@{schema='fvr.desktop.package-repair.v1';id=$id;version='0.1.6';phase='prepared';active_before=[Convert]::ToBase64String($before)}
        Write-FvrJson (Join-Path $install 'package-repair.json') $journal
        if($boundary -ne 'prepared'){[IO.Directory]::Move($target,$backup)}
        if($boundary -in @('replacement-moved','committed')){[IO.Directory]::Move($stage,$target);Write-FvrJson $activePath @{current='0.1.6';previous='0.1.5'}}
        if($boundary -eq 'committed'){$journal.phase='committed';Write-FvrJson (Join-Path $install 'package-repair.json') $journal}
        if($boundary -eq 'committed'){
            Switch-FvrPrevious $install
            $afterRollback=Read-FvrJson $activePath
            Check ($afterRollback.current -eq '0.1.5' -and $afterRollback.previous -eq '0.1.6' -and -not(Test-Path -LiteralPath (Join-Path $install 'package-repair.json'))) 'rollback settles committed repair journal before changing active version'
            Switch-FvrPrevious $install
        }else{Restore-FvrPackageRepair $install}
        if($boundary -eq 'committed'){$null=Test-FvrPackage $target '0.1.6';Check $true 'committed interrupted repair keeps verified replacement'}
        else{Check ((Get-FileHash -LiteralPath $badPath).Hash -ceq $originalHash -and (Get-FileHash -LiteralPath $activePath).Hash -ceq $activeHash) ("interrupted repair restores exact original directory/pointer at $boundary")}
        Check (-not(Test-Path -LiteralPath (Join-Path $install 'package-repair.json')) -and -not(Test-Path -LiteralPath $stage) -and -not(Test-Path -LiteralPath $backup)) ("interruption recovery clears bounded transaction files at $boundary")
    }

    # Inject one pointer-commit error into the actual repair, then prove rollback.
    Corrupt-Identity $install '0.1.6';$originalHash=(Get-FileHash -LiteralPath $badPath).Hash;$activeHash=(Get-FileHash -LiteralPath (Join-Path $install 'active.json')).Hash
    $script:writeJsonOriginal=(Get-Command Write-FvrJson).ScriptBlock;$script:failPointer=$true
    function Write-FvrJson([string]$Path,$Value){if($script:failPointer -and $Path -eq (Join-Path $install 'active.json')){$script:failPointer=$false;throw 'Fixture pointer commit failure'};& $script:writeJsonOriginal $Path $Value}
    $error=Reject {Install-FvrPackage $install $v16.zip $v16.manifest} 'actual repair rolls back a pointer commit exception'
    Check ($error.Contains('were restored') -and (Get-FileHash -LiteralPath $badPath).Hash -ceq $originalHash -and (Get-FileHash -LiteralPath (Join-Path $install 'active.json')).Hash -ceq $activeHash) 'failed pointer commit restores exact original bytes and leaves settings untouched'
    Install-FvrPackage $install $v17.zip $v17.manifest
    $active=Read-FvrJson (Join-Path $install 'active.json')
    Check ($active.current -eq '0.1.7' -and $active.previous -eq '0.1.5') 'upgrade from damaged current preserves last verified previous'
    Corrupt-Identity $install '0.1.7'
    $rollback=Run-Worker $install '0.1.7' 'Rollback'
    $active=Read-FvrJson (Join-Path $install 'active.json')
    Check ($rollback.code -eq 0 -and $active.current -eq '0.1.5' -and $null -eq $active.previous) 'real Worker rollback works from damaged current without advertising it as previous'
    Check ((Get-FileHash -LiteralPath $settingsPath).Hash -ceq $settingsHash) 'all recovery operations preserve settings exactly'
    Write-Host "$script:checks package recovery checks passed; no network, game, VR or desktop interaction."
}finally{
    foreach($key in $saved.Keys){[Environment]::SetEnvironmentVariable($key,$saved[$key],'Process')}
    Remove-FvrOwnedTree (Split-Path $root -Parent) (Split-Path $root -Leaf)
}
