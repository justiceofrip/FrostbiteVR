#requires -Version 7.5
# Exercises the real installer and packaged Worker -> Entry path. Only external
# process/network/registry boundaries are replaced in hash-verified fixture copies.
# No game, native helper, headset, network, shortcut, or system setting is used.
# Python 3.11+ is required only for Entry's real version check; select it with
# -Python, FVR_PYTHON, or PATH. All generated files stay on this checkout's G: drive.
# Successful runs retain receipts and delete their own fixtures unless
# -KeepArtifacts is supplied. Failed runs retain all evidence. -PlanOnly writes
# nothing. When staged elsewhere, supply -RepositoryRoot explicitly.
param([string]$RepositoryRoot=(Split-Path $PSScriptRoot -Parent),
      [string]$Python=$env:FVR_PYTHON,
      [switch]$KeepArtifacts,
      [switch]$PlanOnly)
$ErrorActionPreference='Stop'
$repo=[IO.Path]::GetFullPath($RepositoryRoot).TrimEnd('\','/')
function Sha([string]$Path){(Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()}
function Load([string]$Path){Get-Content -LiteralPath $Path -Raw|ConvertFrom-Json -AsHashtable}
function Save([string]$Path,$Value){[IO.File]::WriteAllText($Path,($Value|ConvertTo-Json -Depth 50),[Text.UTF8Encoding]::new($false))}
function RestoreFixtureEnvironment([hashtable]$Environment) {
    foreach($key in $Environment.Keys){
        # PowerShell converts an ordinary $null argument to an empty string.
        # Recent .NET preserves that empty variable, so use a typed null to
        # restore an originally absent variable without changing user settings.
        if($null -eq $Environment[$key]){[Environment]::SetEnvironmentVariable($key,[NullString]::Value,'Process')}
        else{[Environment]::SetEnvironmentVariable($key,$Environment[$key],'Process')}
    }
}
function AssertNoLinks([string]$Path) {
    for($part=[IO.Path]::GetFullPath($Path);$part;$part=Split-Path $part -Parent){
        if((Test-Path -LiteralPath $part) -and ((Get-Item -LiteralPath $part -Force).Attributes -band [IO.FileAttributes]::ReparsePoint)){throw ('Linked test path refused: '+$part)}
    }
}
if(-not $IsWindows -or [IO.Path]::GetPathRoot($repo) -ine 'G:\'){throw 'This repository test requires its checkout and all generated files on G: (local development policy, not a public installer restriction).'}
AssertNoLinks $repo
$relativeSources=@('launcher/Install-BC2VRLauncher.ps1','launcher/Start-Launcher.ps1',
    'launcher/Launcher.ps1','launcher/Worker.ps1','launcher/Launcher.Core.ps1',
    'launcher/Launcher.Feedback.ps1','launcher/Launcher.Storage.ps1','launcher/Launcher.Settings.ps1',
    'launcher/runtime/Entry.ps1','launcher/runtime/Runtime.Core.ps1','launcher/runtime/Startup.ps1',
    'launcher/runtime/Dependencies.ps1','config/ffmpeg-download.json','tools/StorageBudget.ps1')
$sourceFiles=@(foreach($relative in $relativeSources){
    $path=Join-Path $repo $relative;AssertNoLinks $path
    @{relative=$relative;path=$path;sha256=(Sha $path)}
})
# Keep the production resolver's strict-mode setting in its own scope.
$resolvedPython=& { . (Join-Path $repo 'launcher/runtime/Runtime.Core.ps1');Resolve-Bc2Tool $Python @('python.exe','python3.exe') 'Python 3.11+' }
$pins=@{schema='fvr.user-setup-matrix-inputs.v1';prepared_utc=[DateTime]::UtcNow.ToString('o');
    files=$sourceFiles;python=@{path=$resolvedPython;sha256=(Sha $resolvedPython)};real_game_or_headset_validation=$false}
$runName='user-setups-'+[guid]::NewGuid().ToString('N')
$testBase=Join-Path $repo 'test-temp'
$runRoot=Join-Path $testBase $runName
$OutputPath=Join-Path $runRoot 'fixtures'
$SourcePins=Join-Path $runRoot 'source-pins.json'
$SourcePinsSha256=$null
AssertNoLinks $runRoot
if(Test-Path -LiteralPath $runRoot){throw 'Fresh test output required'}
if($PlanOnly){[pscustomobject]@{prepared=$true;executed=$false;inputs=$pins;output=$runRoot;maximum_bytes=32MB;game_started=$false}|ConvertTo-Json -Depth 8;return}
$saved=@{};foreach($key in 'TEMP','TMP','TMPDIR','PYTHONDONTWRITEBYTECODE'){$saved[$key]=[Environment]::GetEnvironmentVariable($key,'Process')}
. (Join-Path $repo 'tools/StorageBudget.ps1')
try {
    $budget=Assert-FvrStorageBudget -RepositoryRoot $repo -OutputPath $runRoot -Operation Session -PlannedBytes 32MB -RawCaptureBytes 0
    if($null -eq $budget.generated.allocated_bytes){throw 'Complete storage accounting required'}
    AssertNoLinks $runRoot
    if(Test-Path -LiteralPath $runRoot){throw 'Test output was created before admission completed'}
    $null=[IO.Directory]::CreateDirectory($OutputPath)
    Save (Join-Path $runRoot 'storage.json') $budget
    Save $SourcePins $pins
    $SourcePinsSha256=Sha $SourcePins
    Copy-Item -LiteralPath $PSCommandPath -Destination (Join-Path $runRoot 'runner.ps1')
    $scratch=Join-Path $OutputPath 'scratch';$null=[IO.Directory]::CreateDirectory($scratch)
    $env:TEMP=$scratch;$env:TMP=$scratch;$env:TMPDIR=$scratch;$env:PYTHONDONTWRITEBYTECODE='1'
} catch {
    RestoreFixtureEnvironment $saved
    throw
}
$results=[Collections.Generic.List[object]]::new();$calls=0;$failure=$null
$clock=[Diagnostics.Stopwatch]::StartNew()
function Check([bool]$Good,[string]$Name){$results.Add(@{name=$Name;passed=$Good});if(-not $Good){throw ('FAILED: '+$Name)}}
function Source([string]$Relative){
    $pin=@($pins.files|Where-Object {$_.relative -ceq $Relative})
    if($pin.Count -ne 1){throw ('Missing exact source pin: '+$Relative)}
    if((Sha $pin[0].path) -cne $pin[0].sha256){throw ('Source changed during fixture: '+$Relative)}
    return $pin[0].path
}
function CopySource([string]$Relative,[string]$Destination){
    $null=[IO.Directory]::CreateDirectory((Split-Path $Destination -Parent))
    [IO.File]::Copy((Source $Relative),$Destination,$false)
    $expected=@($pins.files|Where-Object {$_.relative -ceq $Relative})[0].sha256
    if((Sha $Destination) -cne $expected){throw ('Copied source identity changed: '+$Relative)}
}
function AssertBound {
    if($clock.Elapsed.TotalSeconds -gt 120){throw 'Two-minute total fixture budget exceeded'}
    $files=@(Get-ChildItem -LiteralPath $runRoot -File -Recurse -Force)
    # Reserve one MiB for the final receipt/failure evidence within the32MiB cap.
    if([long]($files|Measure-Object Length -Sum).Sum -gt 31MB){throw 'Fixture exceeded31MiB plus oneMiB receipt reserve'}
}
# Only OS/process/network boundaries are replaced. All package verification,
# storage, settings, install/update/rollback and Worker/Entry policy stays real.
$boundaries=@'
function Write-CompatibilityBoundary([string]$Kind,$Value) {
    if(-not $env:FVR_COMPAT_OUTPUT -or [IO.Path]::GetFullPath($env:FVR_COMPAT_OUTPUT) -cne $script:CompatibilityFixtureRoot){throw 'Exact fixture boundary root unavailable'}
    $path=Join-Path $env:FVR_COMPAT_OUTPUT 'boundary-events.jsonl'
    if((Test-Path -LiteralPath $path) -and (Get-Item -LiteralPath $path).Length -gt 256KB){throw 'Boundary journal budget exceeded'}
    [IO.File]::AppendAllText($path,(@{kind=$Kind;value=$Value}|ConvertTo-Json -Depth 8 -Compress)+"`n",[Text.UTF8Encoding]::new($false))
}
function Get-Process {
    param([string[]]$Name,[int]$Id,$ErrorAction)
    Write-CompatibilityBoundary 'process_query' @{names=$Name;id=$Id}
    if(@($Name|Where-Object {$_ -match 'steam|vrserver'}).Count){throw 'PACKAGED ROUTE MUST NOT REQUIRE STEAM'}
    return $null # Empty fixture process table, not a claim about this workstation.
}
function Get-ItemProperty {throw 'PACKAGED ROUTE MUST NOT QUERY STEAM REGISTRY'}
function Start-Process {throw 'Actual OS process launch is forbidden inside fixture policy'}
function Invoke-WebRequest {throw 'Network is forbidden in this fixture'}
function Invoke-RestMethod {throw 'Network is forbidden in this fixture'}
function Receive-FvrFile {throw 'Download is forbidden in this fixture'}
'@
# Embed the exact generated destination into fixture copies; the environment
# alone cannot redirect their journals elsewhere.
$boundaries="`n`$script:CompatibilityFixtureRoot='"+$OutputPath.Replace("'","''")+"'`n"+$boundaries
$runtimeBoundary=@'
function Start-Bc2Child([string]$File,[string[]]$Arguments,[string]$Folder,[string]$LogName,[hashtable]$Environment=@{}) {
    Write-CompatibilityBoundary 'child_intercept' @{file=$File;arguments=$Arguments;environment=$Environment}
    if([IO.Path]::GetFileName($File) -cne 'BC2XrHost.exe' -or $LogName -cne 'xr-preflight' -or $Arguments[0] -cne '--probe'){throw 'Unexpected child request; no game/native helper may run'}
    Write-FvrJson (Join-Path $Folder 'xr-preflight.stdout.log') @{instance_created=$true;system_available=$false;error='Fixture XR_ERROR_FORM_FACTOR_UNAVAILABLE'}
    $p=[pscustomobject]@{ExitCode=1};$p|Add-Member ScriptMethod WaitForExit {param($Milliseconds)return $true};return $p
}
'@
$registryBoundary=@'
function Get-Bc2RegisteredOpenXrRuntime {throw 'Explicit runtime must not consult registry in this fixture'}
'@
function InvokeFixture([string]$Script,[string[]]$Arguments,[string]$Label) {
    AssertBound
    $Script=[IO.Path]::GetFullPath($Script)
    if(-not $Script.StartsWith(($OutputPath+'\'),[StringComparison]::OrdinalIgnoreCase) -or [IO.Path]::GetExtension($Script) -ine '.ps1'){throw 'Only this run''s copied PowerShell fixture may execute'}
    AssertNoLinks $Script
    if((Sha $pins.python.path) -cne $pins.python.sha256){throw 'Selected Python executable changed during fixture'}
    $script:calls++
    $start=[Diagnostics.ProcessStartInfo]::new((Join-Path $PSHOME 'pwsh.exe'))
    $start.UseShellExecute=$false;$start.CreateNoWindow=$true;$start.RedirectStandardOutput=$true;$start.RedirectStandardError=$true
    foreach($arg in @('-NoProfile','-NonInteractive','-ExecutionPolicy','Bypass','-File',$Script)+$Arguments){$null=$start.ArgumentList.Add($arg)}
    $start.Environment['FVR_COMPAT_OUTPUT']=$OutputPath
    $start.Environment['FVR_PYTHON']=$pins.python.path
    $start.Environment['XR_RUNTIME_JSON']=$script:xrManifest
    $child=[Diagnostics.Process]::Start($start)
    $out=$child.StandardOutput.ReadToEndAsync();$err=$child.StandardError.ReadToEndAsync()
    try {
        $remaining=[int][Math]::Min(15000,[Math]::Max(1,120000-$clock.ElapsedMilliseconds))
        if(-not $child.WaitForExit($remaining)){$child.Kill($true);$null=$child.WaitForExit(3000);throw 'Owned fixture process exceeded its15second/total120second budget'}
        $stdout=$out.GetAwaiter().GetResult();$stderr=$err.GetAwaiter().GetResult()
        if([Text.Encoding]::UTF8.GetByteCount($stdout)+[Text.Encoding]::UTF8.GetByteCount($stderr) -gt 512KB){throw 'Child log budget exceeded'}
        [IO.File]::WriteAllText((Join-Path $OutputPath ($Label+'.stdout.log')),$stdout)
        [IO.File]::WriteAllText((Join-Path $OutputPath ($Label+'.stderr.log')),$stderr)
        return @{code=$child.ExitCode;stdout=$stdout;stderr=$stderr}
    } finally {$child.Dispose()}
}
function Package([string]$Version) {
    $folder=Join-Path $OutputPath ('fixture-'+$Version)
    foreach($name in 'Launcher.ps1','Worker.ps1','Launcher.Core.ps1','Launcher.Feedback.ps1','Launcher.Storage.ps1','Launcher.Settings.ps1'){
        CopySource ('launcher/'+$name) (Join-Path $folder ('app/'+$name))
    }
    foreach($name in 'Entry.ps1','Runtime.Core.ps1','Startup.ps1','Dependencies.ps1'){
        CopySource ('launcher/runtime/'+$name) (Join-Path $folder ('runtime/'+$name))
    }
    [IO.File]::AppendAllText((Join-Path $folder 'app/Launcher.Core.ps1'),"`n"+$boundaries)
    [IO.File]::AppendAllText((Join-Path $folder 'runtime/Runtime.Core.ps1'),"`n"+$runtimeBoundary)
    [IO.File]::AppendAllText((Join-Path $folder 'runtime/Startup.ps1'),"`n"+$registryBoundary)
    CopySource 'config/ffmpeg-download.json' (Join-Path $folder 'runtime/config/ffmpeg-download.json')
    $runtime=Join-Path $folder 'runtime'
    $runtimeFiles=@(foreach($file in Get-ChildItem -LiteralPath $runtime -File -Recurse){@{path=[IO.Path]::GetRelativePath($runtime,$file.FullName).Replace('\','/');sha256=(Sha $file.FullName)}})
    Save (Join-Path $runtime 'runtime.json') @{schema='bc2vr.runtime.v1';version=$Version;diagnostic_only=$false;source_sha256=('c'*64);asset_recipe_sha256=('a'*64);game_sha256=@($script:gameHash);files=$runtimeFiles;fixture_only=$true}
    $files=@(foreach($file in Get-ChildItem -LiteralPath $folder -File -Recurse){@{path=[IO.Path]::GetRelativePath($folder,$file.FullName).Replace('\','/');sha256=(Sha $file.FullName)}})
    Save (Join-Path $folder 'package.json') @{schema='fvr.desktop.package.v1';version=$Version;runtime_entry='runtime/Entry.ps1';release_repository='justiceofrip/BC2VR';feedback_repository='justiceofrip/BC2VR';files=$files;fixture_only=$true}
    $zip=$folder+'.zip';[IO.Compression.ZipFile]::CreateFromDirectory($folder,$zip)
    $archive=[IO.Compression.ZipFile]::OpenRead($zip)
    try{$expanded=[long]($archive.Entries|Measure-Object Length -Sum).Sum}finally{$archive.Dispose()}
    $manifest=Join-Path $OutputPath ('fixture-'+$Version+'.json')
    Save $manifest @{schema='fvr.desktop.release.v1';version=$Version;asset=('BC2VR-'+$Version+'-win64.zip');bytes=(Get-Item -LiteralPath $zip).Length;unpacked_bytes=$expanded;sha256=(Sha $zip)}
    Check ((Sha (Join-Path $folder 'app/Worker.ps1')) -ceq (Sha (Source 'launcher/Worker.ps1'))) ('Worker remains byte-exact '+$Version)
    Check ((Sha (Join-Path $folder 'runtime/Entry.ps1')) -ceq (Sha (Source 'launcher/runtime/Entry.ps1'))) ('packaged Entry remains byte-exact '+$Version)
    return @{zip=$zip;manifest=$manifest}
}
function RunWorker([string]$Version,[string]$Action,[string]$Label){
    return InvokeFixture (Join-Path $script:install ('versions/'+$Version+'/app/Worker.ps1')) @('-InstallRoot',$script:install,'-Action',$Action) $Label
}
try {
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $script:install=Join-Path $OutputPath 'Install é 漢字 (2) & local'
    $installer=Join-Path $OutputPath 'installer'
    foreach($name in 'Install-BC2VRLauncher.ps1','Launcher.Core.ps1','Launcher.Storage.ps1','Launcher.Settings.ps1','Start-Launcher.ps1'){
        CopySource ('launcher/'+$name) (Join-Path $installer $name)
    }
    [IO.File]::AppendAllText((Join-Path $installer 'Launcher.Core.ps1'),"`n"+$boundaries)
    $games=@(foreach($relative in 'SteamLibrary/steamapps/common/Battlefield Bad Company 2','EA Games/Battlefield Bad Company 2','Jeux é 漢字 (2)'){
        $path=Join-Path $OutputPath $relative;$null=[IO.Directory]::CreateDirectory($path)
        [IO.File]::WriteAllText((Join-Path $path 'BFBC2Game.exe'),'fixture bytes, not executable; never launched')
        $path
    })
    $script:gameHash=Sha (Join-Path $games[0] 'BFBC2Game.exe')
    $script:xrManifest=Join-Path $OutputPath 'VDXR é 漢字/runtime.json'
    $null=[IO.Directory]::CreateDirectory((Split-Path $script:xrManifest -Parent))
    [IO.File]::WriteAllText((Join-Path (Split-Path $script:xrManifest -Parent) 'runtime.dll'),'fixture bytes; never loaded')
    Save $script:xrManifest @{runtime=@{name='VDXR explicit fixture';library_path='runtime.dll'}}
    $v1=Package '0.0.1';$v2=Package '0.0.2'
    $r=InvokeFixture (Join-Path $installer 'Install-BC2VRLauncher.ps1') @('-InstallRoot',$script:install,'-PackageZip',$v1.zip,'-ManifestPath',$v1.manifest,'-GamePath',$games[0],'-NoShortcut') 'fresh-install'
    Check ($r.code -eq 0) 'real package installer succeeds at spaces/Unicode/ampersand path'
    Check ((Load (Join-Path $script:install 'active.json')).current -eq '0.0.1') 'fresh active pointer selects verified first fixture'
    $settingsPath=Join-Path $script:install 'data/settings.json'
    $settings=Load $settingsPath;$settings['custom']=@{unicode='préservé 漢字';enabled=$true};Save $settingsPath $settings
    foreach($n in 0..2){
        # Use the actual settings save function, in its own scoped harmless child.
        $select=Join-Path $OutputPath 'select-game.ps1'
        if(-not(Test-Path -LiteralPath $select)){[IO.File]::WriteAllText($select,@'
param([string]$Root,[string]$Selection)
$ErrorActionPreference='Stop'
. (Join-Path $Root 'versions/0.0.1/app/Launcher.Core.ps1')
. (Join-Path $Root 'versions/0.0.1/app/Launcher.Settings.ps1')
Save-FvrGameFolder -InstallRoot $Root -Selection $Selection|ConvertTo-Json -Depth 20
'@)}
        $selection=if($n -eq 2){Join-Path $games[$n] 'BFBC2Game.exe'}else{$games[$n]}
        $s=InvokeFixture $select @('-Root',$script:install,'-Selection',$selection) ('select-'+$n)
        Check ($s.code -eq 0 -and (Load $settingsPath).game_path -ceq $games[$n]) ('manual selection preserves layout '+$n)
        $r=RunWorker '0.0.1' Validate ('validate-layout-'+$n)
        $status=Load (Join-Path $script:install 'data/operation.json')
        $xr=Load (Join-Path $script:install 'data/runtime/xr-selection.json')
        Check ($r.code -eq 0 -and $status.state -eq 'done' -and $r.stdout.Contains('"validated": true')) ('actual Worker to packaged Entry validation layout '+$n)
        Check ($xr.source -ceq 'XR_RUNTIME_JSON override' -and $xr.manifest -ceq $script:xrManifest -and $xr.label -ceq 'VDXR explicit fixture') ('selected VDXR path remains exact '+$n)
    }
    Check (-not(Test-Path -LiteralPath (Join-Path $script:install 'data/runtime/tools'))) 'cold Validate does not download or create decoder cache'
    $r=RunWorker '0.0.1' Launch 'launch-headset-unavailable'
    $status=Load (Join-Path $script:install 'data/operation.json')
    Check ($r.code -ne 0 -and $status.state -eq 'error' -and $status.message.Contains('Fixture XR_ERROR_FORM_FACTOR_UNAVAILABLE')) 'real Worker and Entry preserve intercepted headset failure without Steam or game startup'
    Check (-not(Test-Path -LiteralPath (Join-Path $script:install 'data/runtime/sessions'))) 'failed headset preflight cannot create a game session'
    $r=InvokeFixture (Join-Path $installer 'Install-BC2VRLauncher.ps1') @('-InstallRoot',$script:install,'-PackageZip',$v2.zip,'-ManifestPath',$v2.manifest,'-NoShortcut') 'update-install'
    $active=Load (Join-Path $script:install 'active.json');$settings=Load $settingsPath
    Check ($r.code -eq 0 -and $active.current -eq '0.0.2' -and $active.previous -eq '0.0.1') 'real installer update retains verified previous at Unicode path'
    Check ($settings.game_path -ceq $games[2] -and $settings.custom.unicode -ceq 'préservé 漢字' -and $settings.custom.enabled) 'update preserves selected game and unknown Unicode settings'
    $r=RunWorker '0.0.2' Validate 'validate-updated'
    Check ($r.code -eq 0) 'updated packaged Entry validates through actual Worker'
    $settingsHash=Sha $settingsPath;$r=RunWorker '0.0.2' Rollback 'rollback'
    $active=Load (Join-Path $script:install 'active.json')
    Check ($r.code -eq 0 -and $active.current -eq '0.0.1' -and $active.previous -eq '0.0.2') 'actual Worker rollback restores verified previous'
    Check ((Sha $settingsPath) -ceq $settingsHash) 'rollback preserves exact settings bytes'
    $r=RunWorker '0.0.1' Validate 'validate-rollback';Check ($r.code -eq 0) 'rolled-back packaged Entry validates'
    $events=@(Get-Content -LiteralPath (Join-Path $OutputPath 'boundary-events.jsonl')|ForEach-Object {$_|ConvertFrom-Json -AsHashtable})
    $children=@($events|Where-Object {$_.kind -ceq 'child_intercept'})
    Check ($children.Count -eq 1 -and $children[0].value.environment.XR_RUNTIME_JSON -ceq $script:xrManifest) 'only intercepted generic OpenXR probe requested, using exact explicit runtime'
    Check (@($events|Where-Object {$_.kind -ceq 'process_query' -and @($_.value.names|Where-Object {$_ -match 'steam|vrserver'}).Count}).Count -eq 0) 'packaged flow never queries Steam process'
    foreach($pin in $pins.files){if((Sha $pin.path) -cne $pin.sha256){throw ('Source changed during run: '+$pin.relative)}}
    AssertBound
} catch {$failure=$_.Exception.Message}
finally {
    RestoreFixtureEnvironment $saved
    $bytes=[long](@(Get-ChildItem -LiteralPath $runRoot -File -Recurse -Force)|Measure-Object Length -Sum).Sum
    $cleaned=$false
    if($null -eq $failure -and -not $KeepArtifacts){
        try {
            # Never clean a caller-supplied directory. The deletion target must
            # still be this fresh GUID run's exact fixtures child, without links.
            if([IO.Path]::GetFullPath($runRoot) -cne (Join-Path $testBase $runName) -or $runName -notmatch '^user-setups-[a-f0-9]{32}$' -or
               [IO.Path]::GetFullPath($OutputPath) -cne (Join-Path $runRoot 'fixtures')){throw 'Cleanup target escaped its generated root'}
            AssertNoLinks $OutputPath
            if(@(Get-ChildItem -LiteralPath $OutputPath -Recurse -Force|Where-Object {$_.Attributes -band [IO.FileAttributes]::ReparsePoint}).Count){throw 'Cleanup refuses linked fixture contents'}
            Remove-Item -LiteralPath $OutputPath -Recurse -Force
            $cleaned=$true
        } catch {$failure='Fixture cleanup failed; remaining evidence retained: '+$_.Exception.Message}
    }
    $elevated=([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
    Save (Join-Path $runRoot 'result.json') @{schema='fvr.user-setup-matrix.v1';passed=($null -eq $failure);error=$failure;checks=$results.ToArray();source_pins=$SourcePins;source_pins_sha256=$SourcePinsSha256;runner_sha256=(Sha $PSCommandPath);seconds=$clock.Elapsed.TotalSeconds;fixture_bytes_before_cleanup=$bytes;fixtures_cleaned=$cleaned;artifacts_retained=(-not $cleaned);child_powershell_calls=$calls;python_version_probe_real=$true;elevated_process=$elevated;boundaries_mocked=@('process table','Steam registry rejection','network transport rejection','OpenXR child launch','OpenXR registry rejection');game_started=$false;headset_tested=$false;native_compatibility_verified=$false;steam_edition_verified=$false;ea_edition_verified=$false;standard_user_permissions_verified=$false;c_drive_writes=$false;downloads=$false;source_modified=$false;published=$false}
}
if($failure){throw ($failure+'; evidence: '+$runRoot)}
Write-Output ('Setup matrix passed '+$results.Count+' checks; no game, headset, network or source edits. Receipt: '+(Join-Path $runRoot 'result.json'))
