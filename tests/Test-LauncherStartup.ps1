#requires -Version 7.5
param([string]$PayloadRoot)
$ErrorActionPreference='Stop'
$repo=Split-Path $PSScriptRoot -Parent
$app=if($PayloadRoot){Join-Path $PayloadRoot 'app'}else{Join-Path $repo 'launcher'}
$runtime=if($PayloadRoot){Join-Path $PayloadRoot 'runtime'}else{Join-Path $repo 'launcher/runtime'}
. (Join-Path $app 'Launcher.Core.ps1')
. (Join-Path $runtime 'Runtime.Core.ps1')
. (Join-Path $runtime 'Startup.ps1')
$root=Join-Path $repo ('test-temp/startup-'+[guid]::NewGuid().ToString('N'))
$saved=@{};foreach($key in @('TEMP','TMP','TMPDIR','PYTHONDONTWRITEBYTECODE','XR_RUNTIME_JSON')){$saved[$key]=[Environment]::GetEnvironmentVariable($key,'Process')}
$script:checks=0
function Check([bool]$Good,[string]$Name){if(-not $Good){throw "FAILED: $Name"};$script:checks++;Write-Host "PASS $Name"}
function Reject([scriptblock]$Work,[string]$Message,[string]$Name){$errorText='';try{& $Work|Out-Null}catch{$errorText=$_.Exception.Message};Check ($errorText -like ('*'+$Message+'*')) $Name}
try {
    $null=Set-FvrLauncherStorage $root
    $activation=Get-Bc2ActivationProcessOutcome ([pscustomobject]@{HasExited=$true;ExitCode=[int]-1073741819})
    Check ($activation.schema -ceq 'bc2vr.activation-process.v1' -and -not $activation.ownership_verified -and
        $activation.original_process_state -ceq 'exited' -and $activation.exit_observed -and
        $activation.exit_code -is [long] -and $activation.exit_code -eq 3221225477) 'retained original exit preserves typed Windows process code without claiming verified ownership'
    $activation=Get-Bc2ActivationProcessOutcome ([pscustomobject]@{HasExited=$true;ExitCode=0})
    Check ($activation.exit_observed -and $activation.exit_code -is [long] -and $activation.exit_code -eq 0) 'observed original zero-code exit remains distinct from unavailable exit code'
    $script:activationExitReads=0
    $liveOriginal=[pscustomobject]@{HasExited=$false}
    $liveOriginal|Add-Member ScriptProperty ExitCode {$script:activationExitReads++;throw 'private fixture path must not be retained'}
    $activation=Get-Bc2ActivationProcessOutcome $liveOriginal
    Check ($activation.original_process_state -ceq 'live' -and -not $activation.exit_observed -and
        $null -eq $activation.exit_code -and $script:activationExitReads -eq 0) 'live original with failed ownership does not read an invalid ExitCode or claim exit'
    $unavailableOriginal=[pscustomobject]@{}
    $unavailableOriginal|Add-Member ScriptProperty HasExited {throw 'private fixture path must not be retained'}
    $activation=Get-Bc2ActivationProcessOutcome $unavailableOriginal
    Check ($activation.original_process_state -ceq 'unavailable' -and -not $activation.exit_observed -and
        $null -eq $activation.exit_code -and ($activation|ConvertTo-Json -Compress) -notmatch 'private fixture') 'unavailable original observation is bounded and contains no accessor exception details'
    $exitedOriginal=[pscustomobject]@{HasExited=$true}
    $exitedOriginal|Add-Member ScriptProperty ExitCode {throw 'private fixture path must not be retained'}
    $activation=Get-Bc2ActivationProcessOutcome $exitedOriginal
    Check ($activation.original_process_state -ceq 'exited' -and $activation.exit_observed -and $null -eq $activation.exit_code) 'unreadable exit code does not erase the observed exit or fabricate success'
    foreach($invalidCode in @($null,'0',0.5,$false,[long]4294967296)){
        $activation=Get-Bc2ActivationProcessOutcome ([pscustomobject]@{HasExited=$true;ExitCode=$invalidCode})
        Check ($activation.exit_observed -and $null -eq $activation.exit_code) 'malformed original exit code is not converted into a process result'
    }
    $activation=Get-Bc2ActivationProcessOutcome ([pscustomobject]@{HasExited='false';ExitCode=0})
    Check ($activation.original_process_state -ceq 'unavailable' -and -not $activation.exit_observed -and $null -eq $activation.exit_code) 'nonboolean original exit observation cannot claim exit'
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
    # Synthetic PE bytes plus a narrowly mocked file hash exercise admission
    # policy only. They are not an inspected or accepted Steam game executable.
    $steamDir=Join-Path $root 'steam-fixture';$null=[IO.Directory]::CreateDirectory($steamDir)
    $script:steamExe=Join-Path $steamDir 'BFBC2Game.exe'
    $candidateHash='22e96001382eb847d3ab0a02b59ec7bb79ba47e04146e74c6c49e38465f38c96'
    function New-SteamPeFixture {
        $b=[byte[]]::new(1024)
        function Put16([int]$At,[uint16]$Value){[BitConverter]::GetBytes($Value).CopyTo($b,$At)}
        function Put32([int]$At,[uint32]$Value){[BitConverter]::GetBytes($Value).CopyTo($b,$At)}
        Put16 0 0x5a4d;Put32 60 128;Put32 128 0x4550;Put16 132 0x14c;Put16 134 1
        Put32 136 1314210909;Put16 148 224;Put16 150 0x123;Put16 152 0x10b
        Put32 168 0x1000;Put32 180 0x400000;Put32 208 0x1913000;Put32 212 512
        Put32 384 512;Put32 388 0x1000;Put32 392 512;Put32 396 512;Put32 412 0x60000020
        return ,$b
    }
    $peBytes=New-SteamPeFixture;[IO.File]::WriteAllBytes($script:steamExe,$peBytes)
    $layout=Get-Bc2ExecutablePeLayout $script:steamExe
    Check ($layout.valid -and $layout.machine -eq 0x14c -and $layout.timestamp -eq 1314210909 -and
        $layout.image_base -eq 0x400000 -and $layout.sections.Count -eq 1 -and $layout.file_bytes -eq 1024) 'bounded file-only parser retains typed PE evidence'
    $script:steamMockHash=$candidateHash;$script:steamHashReads=0
    function Get-FileHash {
        param([string]$LiteralPath,[string]$Algorithm='SHA256')
        if($LiteralPath -ceq $script:steamExe){$script:steamHashReads++;return [pscustomobject]@{Hash=$script:steamMockHash}}
        Microsoft.PowerShell.Utility\Get-FileHash -LiteralPath $LiteralPath -Algorithm $Algorithm
    }
    try {
        Reject {Test-Bc2ExecutableAdmission -Exe $script:steamExe -Hash $candidateHash -Config $config} 'Enable the experimental Steam option' 'exact candidate remains opt-in by default'
        foreach($flag in @($null,'true',1,$false)){
            Reject {Test-Bc2ExecutableAdmission -Exe $script:steamExe -Hash $candidateHash -Config $config -ExperimentalSteam $flag} 'Enable the experimental Steam option' 'nonboolean or false opt-in cannot admit candidate'
        }
        Reject {Test-Bc2ExecutableAdmission -Exe $script:steamExe -Hash ('d'*64) -Config $config -ExperimentalSteam $true} 'not been verified' 'experimental Steam option cannot admit an unknown fingerprint'
        Reject {Test-Bc2ExecutableAdmission -Exe $script:steamExe -Hash $candidateHash -Config @{game_sha256=@($candidateHash)}} 'Enable the experimental Steam option' 'candidate cannot silently become verified through config allowlist'
        $admission=Test-Bc2ExecutableAdmission -Exe $script:steamExe -Hash $candidateHash -Config $config -ExperimentalSteam $true
        Check ($admission.experimental_steam -and -not $admission.game_verified -and $admission.profile -ceq 'experimental_steam_795745' -and
            $admission.pe_layout.valid -and $script:steamHashReads -eq 1) 'opted-in exact candidate is explicitly experimental and rechecks current file hash'
        $installation=Test-Bc2GameInstallation @{game_path=$steamDir;steam_compatibility=$true} $config $root
        $report=Read-FvrJson (Join-Path $root 'launch-check.json')
        Check ($installation.sha256 -ceq $candidateHash -and $installation.experimental_steam -and $report.game_supported -and
            -not $report.game_verified -and $report.experimental_steam -and $report.pe_layout.valid) 'candidate report retains observed hash and separates startup admission from verification'
        Reject {Test-Bc2GameInstallation @{game_path=$steamDir;steam_compatibility='true'} $config $root} 'Enable the experimental Steam option' 'settings requires a JSON boolean true, not a truthy string'
        $report=Read-FvrJson (Join-Path $root 'launch-check.json')
        Check (-not $report.game_supported -and -not $report.game_verified -and $report.pe_layout.valid -and
            $report.pe_layout.timestamp -eq 1314210909 -and ($report|ConvertTo-Json -Depth 6) -notlike ('*'+$steamDir+'*')) 'rejection still reports structured PE evidence without private paths'
        $script:steamMockHash='f'*64
        Reject {Test-Bc2ExecutableAdmission -Exe $script:steamExe -Hash $candidateHash -Config $config -ExperimentalSteam $true} 'changed during' 'candidate replacement between inspection and admission is rejected'
        $script:steamMockHash=$candidateHash
        foreach($change in @(
            @{at=0;size=2;value=0;name='missing MZ'},@{at=60;size=4;value=32;name='bad PE offset'},
            @{at=132;size=2;value=0x8664;name='wrong architecture'},@{at=136;size=4;value=1;name='different timestamp'},
            @{at=152;size=2;value=0x20b;name='PE32 plus'},@{at=180;size=4;value=0x500000;name='relocated preferred base'},
            @{at=208;size=4;value=0x1914000;name='different image size'},@{at=134;size=2;value=97;name='unbounded section count'},
            @{at=148;size=2;value=223;name='short optional header'},@{at=396;size=4;value=1000;name='raw file overflow'},
            @{at=388;size=4;value=[long]4294967280;name='virtual image overflow'},@{at=396;size=4;value=64;name='raw header overlap'}
        )){
            $mutated=[byte[]]$peBytes.Clone()
            $replacement=if($change.size -eq 2){[BitConverter]::GetBytes([uint16]$change.value)}else{[BitConverter]::GetBytes([uint32]$change.value)}
            $replacement.CopyTo($mutated,$change.at);[IO.File]::WriteAllBytes($script:steamExe,$mutated)
            Reject {Test-Bc2GameInstallation @{game_path=$steamDir;steam_compatibility=$true} $config $root} 'Experimental Steam startup refused' ('candidate rejects '+$change.name)
            $report=Read-FvrJson (Join-Path $root 'launch-check.json')
            Check (-not $report.game_supported -and -not $report.pe_layout.valid -and $report.pe_layout.error) ('rejected '+$change.name+' retains bounded PE failure detail')
        }
        $overlap=[byte[]]$peBytes.Clone();[BitConverter]::GetBytes([uint16]2).CopyTo($overlap,134)
        [Array]::Copy($overlap,376,$overlap,416,40)
        [IO.File]::WriteAllBytes($script:steamExe,$overlap)
        Reject {Test-Bc2ExecutableAdmission -Exe $script:steamExe -Hash $candidateHash -Config $config -ExperimentalSteam $true} 'sections overlap' 'candidate cannot alias two section ranges'
        [IO.File]::WriteAllBytes($script:steamExe,$peBytes)
        $known=Test-Bc2GameInstallation @{game_path=$game;steam_compatibility=$true} $config $root
        Check ($known.sha256 -ceq $hash -and -not $known.experimental_steam -and $known.game_verified) 'known configured build keeps original admission behavior even with Steam option enabled'
    } finally {Remove-Item Function:\Get-FileHash}
    $sample=Join-Path $root 'session-telemetry.jsonl'
    Check ($null -eq (Get-Bc2NativeStartupSample $sample)) 'process existence alone does not mark VR started'
    [IO.File]::WriteAllText($sample,"{`"input_samples`":0,`"gpu_failure_stage`":0,`"camera_restore_failures`":0}`n")
    Check ($null -eq (Get-Bc2NativeStartupSample $sample)) 'native adapter with no XR input stays pending'
    [IO.File]::AppendAllText($sample,"{`"input_samples`":5,`"gpu_failure_stage`":0,`"camera_restore_failures`":0,`"published`":0}`n{`"partial`":")
    Check ((Get-Bc2NativeStartupSample $sample).input_samples -eq 5) 'world-native input succeeds with zero completed world pairs and partial trailing log row'
    $menuSample=@{input_samples=0;gpu_failure_stage=0;camera_restore_failures=0;utc_filetime_100ns=[DateTime]::UtcNow.ToFileTimeUtc();menu_connection=@{ready=$true;input_samples=12;published=4;graphics_failures=0}}
    [IO.File]::WriteAllText($sample,($menuSample|ConvertTo-Json -Compress -Depth 4)+"`n")
    Check ((Get-Bc2NativeStartupSample $sample).menu_connection.ready) 'menu input handshake succeeds without any gameplay callback or world pairs'
    $menuSample.menu_connection.input_samples=0
    [IO.File]::WriteAllText($sample,($menuSample|ConvertTo-Json -Compress -Depth 4)+"`n")
    Check ($null -eq (Get-Bc2NativeStartupSample $sample)) 'a visible menu alone is not an input connection'
    $menuSample.menu_connection.input_samples=12;$menuSample.menu_connection.published=0
    [IO.File]::WriteAllText($sample,($menuSample|ConvertTo-Json -Compress -Depth 4)+"`n")
    Check ($null -eq (Get-Bc2NativeStartupSample $sample)) 'menu control alone is not a presented native menu'
    $menuSample.menu_connection.published=4;$menuSample.utc_filetime_100ns=[DateTime]::UtcNow.AddSeconds(-10).ToFileTimeUtc()
    [IO.File]::WriteAllText($sample,($menuSample|ConvertTo-Json -Compress -Depth 4)+"`n")
    Check ($null -eq (Get-Bc2NativeStartupSample $sample)) 'expired menu startup evidence stays pending'
    $menuSample.utc_filetime_100ns=[DateTime]::UtcNow.ToFileTimeUtc()
    [IO.File]::WriteAllText($sample,($menuSample|ConvertTo-Json -Compress -Depth 4)+"`n")
    $menuSample.menu_connection.ready=$false
    [IO.File]::AppendAllText($sample,($menuSample|ConvertTo-Json -Compress -Depth 4)+"`n{`"partial`":")
    Check ($null -eq (Get-Bc2NativeStartupSample $sample)) 'latest loss of menu readiness wins over an older ready sample'
    $menuSample.menu_connection=@{ready=$true}
    [IO.File]::WriteAllText($sample,($menuSample|ConvertTo-Json -Compress -Depth 4)+"`n")
    Check ($null -eq (Get-Bc2NativeStartupSample $sample)) 'incomplete menu telemetry is ignored under strict mode'
    $menuSample.menu_connection=@{ready=$true;input_samples=12;published=4;graphics_failures=1}
    [IO.File]::WriteAllText($sample,($menuSample|ConvertTo-Json -Compress -Depth 4)+"`n")
    Reject {Get-Bc2NativeStartupSample $sample} 'menu bridge' 'menu GPU failure never reports running'
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
