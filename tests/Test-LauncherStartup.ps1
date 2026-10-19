#requires -Version 7.5
$ErrorActionPreference='Stop'
$repo=Split-Path $PSScriptRoot -Parent
. (Join-Path $repo 'launcher/Launcher.Core.ps1')
. (Join-Path $repo 'launcher/runtime/Runtime.Core.ps1')
. (Join-Path $repo 'launcher/runtime/Startup.ps1')
$root=Join-Path $repo ('test-temp/startup-'+[guid]::NewGuid().ToString('N'))
$saved=@{};foreach($key in @('TEMP','TMP','TMPDIR','PYTHONDONTWRITEBYTECODE','XR_RUNTIME_JSON')){$saved[$key]=[Environment]::GetEnvironmentVariable($key,'Process')}
$script:checks=0
function Check([bool]$Good,[string]$Name){if(-not $Good){throw "FAILED: $Name"};$script:checks++;Write-Host "PASS $Name"}
function Reject([scriptblock]$Work,[string]$Message,[string]$Name){$errorText='';try{& $Work|Out-Null}catch{$errorText=$_.Exception.Message};Check ($errorText -like ('*'+$Message+'*')) $Name}
try {
    $null=Set-FvrLauncherStorage $root
    $script:registered='';$script:registryReads=0
    function Get-Bc2RegisteredOpenXrRuntime {$script:registryReads++;return $script:registered}
    $env:XR_RUNTIME_JSON=$null
    Reject {Get-Bc2OpenXrSelection} 'No active OpenXR' 'missing runtime gives actionable error without starting Steam'
    $library=Join-Path $root 'virtual-desktop.dll';[IO.File]::WriteAllText($library,'fixture only')
    $manifest=Join-Path $root 'virtual-desktop.json'
    Write-FvrJson $manifest @{file_format_version='1.0.0';runtime=@{library_path='virtual-desktop.dll';name='Virtual Desktop fixture'}}
    $script:registered=$manifest
    $selection=Get-Bc2OpenXrSelection
    Check ($selection.library -eq $library -and $selection.label -eq 'Virtual Desktop fixture') 'registry-selected non-Steam runtime resolves relative library'
    $reads=$script:registryReads;$env:XR_RUNTIME_JSON=$manifest
    $selection=Get-Bc2OpenXrSelection
    Check ($selection.source -eq 'XR_RUNTIME_JSON override' -and $script:registryReads -eq $reads) 'environment selection takes precedence exactly as OpenXR loader'
    Write-FvrJson $manifest @{runtime=@{library_path=$library}}
    Check ((Get-Bc2OpenXrSelection).label -eq 'virtual-desktop') 'optional runtime name is not required'
    $env:XR_RUNTIME_JSON=Join-Path $root 'missing.json'
    Reject {Get-Bc2OpenXrSelection} 'missing runtime manifest' 'invalid explicit runtime does not silently fall back to another runtime'
    $env:XR_RUNTIME_JSON=$manifest
    Write-FvrJson $manifest @{runtime=@{}}
    Reject {Get-Bc2OpenXrSelection} 'no runtime library' 'malformed runtime manifest rejected'
    Write-FvrJson $manifest @{runtime=@{library_path='missing.dll'}}
    Reject {Get-Bc2OpenXrSelection} 'library is missing' 'missing runtime library rejected'
    $game=Join-Path $root 'game';$null=[IO.Directory]::CreateDirectory($game)
    $config=@{version='0.1.6';game_sha256=@('a'*64)}
    Reject {Test-Bc2GameInstallation @{game_path=''} $config $root} 'Choose game folder' 'no game selection fails before any launch'
    Reject {Test-Bc2GameInstallation @{game_path=$game} $config $root} 'not found' 'missing game executable has clear recovery'
    $exe=Join-Path $game 'BFBC2Game.exe';[IO.File]::WriteAllText($exe,'not an executable; fixture only')
    Reject {Test-Bc2GameInstallation @{game_path=$game} $config $root} 'not been verified' 'unverified edition cannot start or attach'
    $report=Read-FvrJson (Join-Path $root 'launch-check.json')
    $hash=(Get-FileHash -LiteralPath $exe).Hash.ToLowerInvariant()
    Check ($report.game_found -and -not $report.game_supported -and $report.game_sha256 -eq $hash -and -not $report.ContainsKey('game_path')) 'unverified executable fingerprint retained without personal path'
    $config.game_sha256=@($hash)
    Check ((Test-Bc2GameInstallation @{game_path=$game} $config $root).sha256 -eq $hash) 'exact admitted fingerprint passes preflight'
    $sample=Join-Path $root 'session-telemetry.jsonl'
    Check ($null -eq (Get-Bc2NativeStartupSample $sample)) 'process existence alone does not mark VR started'
    [IO.File]::WriteAllText($sample,"{`"input_samples`":0,`"gpu_failure_stage`":0,`"camera_restore_failures`":0}`n")
    Check ($null -eq (Get-Bc2NativeStartupSample $sample)) 'native adapter with no XR input stays pending'
    [IO.File]::AppendAllText($sample,"{`"input_samples`":5,`"gpu_failure_stage`":0,`"camera_restore_failures`":0,`"published`":0}`n{`"partial`":")
    Check ((Get-Bc2NativeStartupSample $sample).input_samples -eq 5) 'menu-native input succeeds with zero world pairs and partial trailing log row'
    [IO.File]::WriteAllText($sample,"{`"input_samples`":5,`"gpu_failure_stage`":2,`"camera_restore_failures`":0}`n")
    Reject {Get-Bc2NativeStartupSample $sample} 'graphics bridge' 'native GPU failure never reports running'
    [IO.File]::WriteAllText($sample,"{`"input_samples`":5,`"gpu_failure_stage`":0,`"camera_restore_failures`":1}`n")
    Reject {Get-Bc2NativeStartupSample $sample} 'graphics bridge' 'restore failure never reports running'
    # Execute the real headset-preflight policy with a fake child only. No
    # registry, Steam, game or headset process is touched by these tests.
    $script:childCalls=0;$script:probeResult=@{instance_created=$true;system_available=$true;error=''};$script:probeExit=0
    function Start-Bc2Child([string]$File,[string[]]$Arguments,[string]$Folder,[string]$LogName,[hashtable]$Environment=@{}) {
        $script:childCalls++;$script:lastChild=@{file=$File;arguments=$Arguments;environment=$Environment}
        Write-FvrJson (Join-Path $Folder ($LogName+'.stdout.log')) $script:probeResult
        $p=[pscustomobject]@{ExitCode=$script:probeExit};$p|Add-Member ScriptMethod WaitForExit {param($Timeout)return $true};return $p
    }
    $selected=@{manifest=$manifest;label='Virtual Desktop fixture'}
    $r=Invoke-Bc2OpenXrPreflight $root $root $selected
    Check ($r.system_available -and $script:childCalls -eq 1 -and $script:lastChild.file.EndsWith('BC2XrHost.exe') -and $script:lastChild.arguments[0] -eq '--probe') 'VDXR path invokes only generic OpenXR probe; no Steam dependency'
    Check ($script:lastChild.environment.XR_RUNTIME_JSON -eq $manifest) 'probe explicitly retains selected runtime in child environment'
    $script:probeExit=1;$script:probeResult=@{instance_created=$true;system_available=$false;error='Find HMD system: XR_ERROR_FORM_FACTOR_UNAVAILABLE'}
    Reject {Invoke-Bc2OpenXrPreflight $root $root $selected} 'Find HMD system' 'headset error preserves actual runtime failure before game launch'
    $cache=Join-Path $root 'cache';$null=[IO.Directory]::CreateDirectory($cache)
    $diagnostic=Join-Path $cache 'preparation.json'
    $result=Save-Bc2AssetPreparationResult $cache $root ('a'*64) ('b'*64) 1 $false
    Check ($result.state -eq 'failed' -and $result.stage -eq 'asset tool startup' -and $result.error -like '*code 1*') 'missing Python diagnostic still identifies process failure'
    Write-FvrJson $diagnostic @{schema='bc2vr.asset-preparation.v1';recipe_sha256=('a'*64);game_sha256=('b'*64);stage='body equipment';state='failed';error='Required mesh resource differs'}
    $result=Save-Bc2AssetPreparationResult $cache $root ('a'*64) ('b'*64) 1 $false
    Check ($result.stage -eq 'body equipment' -and $result.error -eq 'Required mesh resource differs') 'actual asset failure stage and cause reach launcher status and feedback record'
    $result=Save-Bc2AssetPreparationResult $cache $root ('c'*64) ('b'*64) 1 $false
    Check ($result.stage -eq 'asset tool startup') 'different preparation attempt cannot supply error cause'
    Write-FvrJson $diagnostic @{schema='bc2vr.asset-preparation.v1';recipe_sha256=('a'*64);game_sha256=('b'*64);stage='complete';state='complete'}
    $result=Save-Bc2AssetPreparationResult $cache $root ('a'*64) ('b'*64) 0 $false
    Check ($result.state -eq 'failed' -and $result.stage -eq 'cache verification') 'claimed completion cannot override failed independent cache verification'
    $result=Save-Bc2AssetPreparationResult $cache $root ('a'*64) ('b'*64) 2 $true
    Check ($result.state -eq 'failed') 'valid prior files cannot mask unsuccessful preparation exit'
    $result=Save-Bc2AssetPreparationResult $cache $root ('a'*64) ('b'*64) 0 $true
    Check ($result.state -eq 'complete' -and -not $result.error) 'clean exit and independently verified cache admit completion'
    Write-Host "Launcher startup checks passed: $script:checks"
} finally {
    foreach($key in $saved.Keys){[Environment]::SetEnvironmentVariable($key,$saved[$key],'Process')}
    Remove-FvrOwnedTree (Split-Path $root -Parent) (Split-Path $root -Leaf)
}
