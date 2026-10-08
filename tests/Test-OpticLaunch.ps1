$ErrorActionPreference='Stop'
$candidate=Split-Path $PSScriptRoot -Parent
$scriptChecks=0;$cliChecks=0
function Assert([bool]$Value,[string]$Message){if(-not $Value){throw $Message}}
function Prefix([string]$Name,[string]$Stop,[string]$Result){
 $path=Join-Path $candidate $Name
 $tokens=$null;$errors=$null
 $null=[Management.Automation.Language.Parser]::ParseFile($path,[ref]$tokens,[ref]$errors)
 Assert ($errors.Count -eq 0) "$Name has syntax errors"
 $text=Get-Content -LiteralPath $path -Raw
 $end=$text.IndexOf($Stop,[StringComparison]::Ordinal)
 Assert ($end -gt 0) "Missing safe prefix boundary in $Name"
 # Compile ONLY option validation/assignments. The process/file/launch portion
 # is deliberately excluded, so these tests cannot touch BC2 or start helpers.
 return [ScriptBlock]::Create($text.Substring(0,$end)+"`n"+$Result)
}
$start=Prefix 'Start-NativeTrace.ps1' '$PSNativeCommandUseErrorActionPreference=' '[pscustomobject]@{optic=[bool]$OpticFilterObserve;controllers=[bool]$Controllers;aim=[bool]$MotionAim;body=[bool]$BodyFollow;poses=[bool]$PoseObserve;hands=[bool]$Hands;fire=[bool]$MuzzleFire;sight=[bool]$SightFlip}'
$test=Prefix 'Test-NativeStream.ps1' 'if(Get-Process BC2XrHost' '[pscustomobject]@{optic=[bool]$OpticFilterObserve;observe=[bool]$ControlsObserve;handsFixture=[bool]$ControlsHands;aimFixture=[bool]$ControlsAim;static=[bool]$StaticPose;async=[bool]$Async}'
$enabled=& $start -OpticFilterObserve -FrameChannel 'offline-fixture' -Seconds 15
Assert ($enabled.optic -and $enabled.controllers -and $enabled.aim -and $enabled.body -and $enabled.poses -and $enabled.hands -and -not $enabled.fire -and -not $enabled.sight) 'Start option prerequisites/default behavior incorrect';$scriptChecks++
$default=& $start
Assert (-not $default.optic -and -not $default.hands -and -not $default.aim) 'Default Start path changed';$scriptChecks++
foreach($bad in @(@{Seconds=16},@{UntilHostExit=$true;HostPid=100},@{HostPid=100},@{PhysicalReload=$true},@{PhysicalReloadProbe=$true},@{ReloadHoldProbe=$true},@{ReloadRoundProbe=$true},@{ReloadRequestProbe=$true},@{RigPulse=$true},@{EquipProbe=$true},@{DeathProbe=$true},@{FrameChannel=''})){
 $argsMap=@{OpticFilterObserve=$true;FrameChannel='offline-fixture';Seconds=15};foreach($key in $bad.Keys){$argsMap[$key]=$bad[$key]}
 $rejected=$false;try{$null=& $start @argsMap}catch{$rejected=$true}
 Assert $rejected "Start option accepted incompatible arguments: $($bad.Keys -join ',')";$scriptChecks++
}
$neutral=& $test -OpticFilterObserve -Seconds 15 -Pairs 240
Assert ($neutral.optic -and $neutral.observe -and $neutral.static -and $neutral.async -and -not $neutral.handsFixture -and -not $neutral.aimFixture) 'Test option did not select neutral tracked input';$scriptChecks++
$default=& $test
Assert (-not $default.optic -and -not $default.observe -and -not $default.static -and -not $default.async) 'Default Test path changed';$scriptChecks++
foreach($name in @('PhysicalReloadProbe','ReloadRequestProbe','ReloadHoldProbe','ReloadRoundProbe','SupportReload','SupportPrepareShots','ShotReload','MuzzleFireProbe','ControlsShotProbe','ControlsEquip','DeathProbe','ControlsSightFlip','SightStartSecondary','ControlsWeaponMode','ControlsWeaponCycle','ControlsUse','ControlsVehicle','ControlsFire','ControlsButtons','ControlsPulse','RigPulse','ControlsTrackingRecovery','ControlsHandIndependence','ControlsSupportGrip','ControlsTurn','ControlsRoomscale','ControlsAim','ControlsHands','Asymmetric','FovSweep')){
 $argsMap=@{OpticFilterObserve=$true;Seconds=15};$argsMap[$name]=$true;$rejected=$false
 try{$null=& $test @argsMap}catch{$rejected=$true}
 Assert $rejected "Test option accepted action/motion fixture: $name";$scriptChecks++
}
foreach($bad in @(@{Seconds=16},@{SupportPrimaryDirection=1})){
 $argsMap=@{OpticFilterObserve=$true;Seconds=15};foreach($key in $bad.Keys){$argsMap[$key]=$bad[$key]};$rejected=$false
 try{$null=& $test @argsMap}catch{$rejected=$true};Assert $rejected 'Test option accepted invalid bound/selection';$scriptChecks++
}
# Execute the real compiled CLI parser, always with a nonexistent DLL. Every
# accepted option path stops at filesystem validation BEFORE FindProcess,
# OpenProcess, remote allocation, writes or any native attach.
$exe=Join-Path $candidate 'build\x86\BC2NativeTrace.exe'
$missingDll=Join-Path $candidate 'does-not-exist\never-a-probe.dll'
$missingReport=Join-Path $candidate 'does-not-exist\never-a-report.json'
Assert (-not (Test-Path -LiteralPath $missingDll) -and -not (Test-Path -LiteralPath $missingReport)) 'CLI sentinel path must not exist'
$base=@('--game',(Join-Path $candidate 'does-not-exist'),'--dll',$missingDll,'--report',$missingReport)
function Invoke-CliFixture([string[]]$Options,[bool]$Accepted){
 $si=[Diagnostics.ProcessStartInfo]::new();$si.FileName=$exe;$si.UseShellExecute=$false;$si.CreateNoWindow=$true;$si.RedirectStandardOutput=$true;$si.RedirectStandardError=$true
 foreach($value in ($base+$Options)){$si.ArgumentList.Add($value)}
 $p=[Diagnostics.Process]::Start($si);$errorText=$p.StandardError.ReadToEnd();$null=$p.StandardOutput.ReadToEnd();$p.WaitForExit()
 Assert ($p.ExitCode -eq 1) 'CLI parser sentinel must stop with exit1'
 if($Accepted){Assert ($errorText -like '*Probe DLL missing or report already exists*') "Valid CLI options failed parsing: $errorText"}
 else {Assert ($errorText -notlike '*Probe DLL missing or report already exists*') 'Invalid CLI options reached filesystem sentinel'}
 $p.Dispose();$script:cliChecks++
}
Invoke-CliFixture @('--stream','offline-fixture','--seconds','15','--optic-filter-observe') $true
Invoke-CliFixture @('--optic-filter-observe','--stream','offline-fixture') $true
Invoke-CliFixture @('--stream','offline-fixture','--seconds','3') $true
Invoke-CliFixture @('--optic-filter-observe') $false
Invoke-CliFixture @('--stream','offline-fixture','--seconds','16','--optic-filter-observe') $false
Invoke-CliFixture @('--stream','offline-fixture','--optic-filter-observe','--until-host-exit','100') $false
foreach($bad in @('--physical-reload','--physical-reload-probe','--reload-hold-probe','--reload-round-probe','--reload-request-probe','--rig-pulse','--equip-probe','--death-probe')){
 Invoke-CliFixture @('--stream','offline-fixture','--seconds','15','--optic-filter-observe',$bad) $false
}
$startText=Get-Content -LiteralPath (Join-Path $candidate 'Start-NativeTrace.ps1') -Raw
$testText=Get-Content -LiteralPath (Join-Path $candidate 'Test-NativeStream.ps1') -Raw
Assert ($startText.Contains("if(`$OpticFilterObserve){`$probeArguments+='--optic-filter-observe'}")) 'Start option not forwarded'
Assert ($testText.Contains('-OpticFilterObserve:$OpticFilterObserve')) 'Test option not forwarded'
$scriptChecks+=2
[pscustomobject]@{powershell_cases=$scriptChecks;real_cli_parser_cases=$cliChecks;native_attach_attempted=$false;game_or_input_actions=$false} | ConvertTo-Json
