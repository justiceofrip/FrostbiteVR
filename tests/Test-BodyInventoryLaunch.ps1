$ErrorActionPreference='Stop'
$candidate=Split-Path $PSScriptRoot -Parent
$scriptChecks=0;$cliChecks=0
function Assert([bool]$Value,[string]$Message){if(-not $Value){throw $Message}}
function Prefix([string]$Name,[string]$Stop,[string]$Result){
 $path=Join-Path $candidate $Name;$tokens=$null;$errors=$null
 $null=[Management.Automation.Language.Parser]::ParseFile($path,[ref]$tokens,[ref]$errors)
 Assert ($errors.Count -eq 0) "$Name syntax errors"
 $text=Get-Content -LiteralPath $path -Raw;$end=$text.IndexOf($Stop,[StringComparison]::Ordinal)
 Assert ($end -gt 0) 'Missing safe prefix boundary'
 # Prefix ends before config/file/process/window/native actions.
 [ScriptBlock]::Create($text.Substring(0,$end)+"`n"+$Result)
}
$result='[pscustomobject]@{body=[bool]$BodyInventory;physical=[bool]$PhysicalReload;hands=[bool]$Hands;controllers=[bool]$Controllers;aim=[bool]$MotionAim;follow=[bool]$BodyFollow;grip=[bool]$TwoHandGrip;fire=[bool]$MuzzleFire}'
$start=Prefix 'Start-NativeTrace.ps1' '$PSNativeCommandUseErrorActionPreference=' $result
$session=Prefix 'Start-BC2VRSession.ps1' '$PSNativeCommandUseErrorActionPreference=' $result
foreach($prefix in @($start,$session)){
 $value=if($prefix -eq $start){& $prefix -BodyInventory -FrameChannel 'offline'}else{& $prefix -BodyInventory}
 Assert ($value.body -and $value.physical -and $value.hands -and $value.controllers -and $value.aim -and $value.follow -and $value.grip -and $value.fire) 'Missing body prerequisites';$scriptChecks++
 $default=& $prefix;Assert (-not $default.body -and -not $default.physical -and -not $default.grip) 'Default behavior changed';$scriptChecks++
}
foreach($name in @('PhysicalReloadProbe','ReloadRequestProbe','ReloadHoldProbe','ReloadRoundProbe','RigPulse','EquipProbe','DeathProbe','OpticFilterObserve')){
 $argsMap=@{BodyInventory=$true;FrameChannel='offline'};$argsMap[$name]=$true;$reject=$false
 try{$null=& $start @argsMap}catch{$reject=$true};Assert $reject "Accepted synthetic fixture $name";$scriptChecks++
}
$reject=$false;try{$null=& $start -BodyInventory}catch{$reject=$true};Assert $reject 'Accepted no stream';$scriptChecks++
$continuous=& $start -BodyInventory -UntilHostExit -HostPid 123 -FrameChannel 'offline';Assert $continuous.body 'Rejected real continuous stream';$scriptChecks++
$exe=Join-Path $candidate 'build\x86\BC2NativeTrace.exe'
$missingDll=Join-Path $candidate 'does-not-exist\never.dll';$report=Join-Path $candidate 'does-not-exist\never.json'
Assert (-not (Test-Path -LiteralPath $missingDll) -and -not (Test-Path -LiteralPath $report)) 'Sentinels must not exist'
$base=@('--game',(Join-Path $candidate 'does-not-exist'),'--dll',$missingDll,'--report',$report)
function Invoke-CliFixture([string[]]$Options,[bool]$Accepted){
 $si=[Diagnostics.ProcessStartInfo]::new();$si.FileName=$exe;$si.UseShellExecute=$false;$si.CreateNoWindow=$true;$si.RedirectStandardError=$true;$si.RedirectStandardOutput=$true
 foreach($value in ($base+$Options)){$si.ArgumentList.Add($value)}
 $p=[Diagnostics.Process]::Start($si);$text=$p.StandardError.ReadToEnd();$null=$p.StandardOutput.ReadToEnd();$p.WaitForExit()
 Assert ($p.ExitCode -eq 1) 'Parser sentinel exit changed'
 Assert (($text -like '*Probe DLL missing or report already exists*') -eq $Accepted) "Unexpected parser result: $text"
 $p.Dispose();$script:cliChecks++
}
Invoke-CliFixture @('--body-inventory','--stream','offline') $true
Invoke-CliFixture @('--body-inventory','--stream','offline','--until-host-exit','123') $true
Invoke-CliFixture @('--body-inventory','--stream','offline','--sight-flip') $true
Invoke-CliFixture @('--stream','offline') $true
Invoke-CliFixture @('--body-inventory') $false
foreach($bad in @('--physical-reload-probe','--reload-request-probe','--reload-hold-probe','--reload-round-probe','--rig-pulse','--equip-probe','--death-probe','--optic-filter-observe')){
 Invoke-CliFixture @('--body-inventory','--stream','offline','--seconds','30',$bad) $false
}
$startText=Get-Content -LiteralPath (Join-Path $candidate 'Start-NativeTrace.ps1') -Raw
$sessionText=Get-Content -LiteralPath (Join-Path $candidate 'Start-BC2VRSession.ps1') -Raw
Assert ($startText.Contains("if(`$BodyInventory){`$probeArguments+='--body-inventory'}")) 'Body CLI forwarding missing'
Assert ($sessionText.Contains('-PhysicalReload:$PhysicalReload -BodyInventory:$BodyInventory')) 'Session forwarding missing';$scriptChecks+=2
[pscustomobject]@{powershell_cases=$scriptChecks;cli_cases=$cliChecks;native_attach=$false;game_actions=$false} | ConvertTo-Json
