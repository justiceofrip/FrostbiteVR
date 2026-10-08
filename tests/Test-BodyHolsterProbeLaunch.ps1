param([string]$ParserExe)
$ErrorActionPreference='Stop';$root=Split-Path $PSScriptRoot -Parent;$checks=0
function Assert([bool]$value,[string]$why){if(-not $value){throw $why};$script:checks++}
function Prefix([string]$file,[string]$boundary){
    $path=Join-Path $root $file;$tokens=$null;$errors=$null
    $null=[Management.Automation.Language.Parser]::ParseFile($path,[ref]$tokens,[ref]$errors)
    Assert ($errors.Count -eq 0) "$file parse failed"
    $text=Get-Content -LiteralPath $path -Raw;$at=$text.IndexOf($boundary,[StringComparison]::Ordinal)
    Assert ($at -gt 0) 'Missing pre-action boundary'
    [ScriptBlock]::Create($text.Substring(0,$at)+"`n"+'[pscustomobject]@{probe=[bool]$BodyHolsterProbe;xm8=[bool]$BodyHolsterXm8Probe;fire=[bool]$BodyHolsterXm8FireProbe;body=[bool]$BodyInventory;physical=[bool]$PhysicalReload;hands=[bool]$Hands;observe=[bool]$ControlsObserve}')
}
$start=Prefix 'Start-NativeTrace.ps1' '$PSNativeCommandUseErrorActionPreference='
$stream=Prefix 'Test-NativeStream.ps1' 'if(Get-Process BC2XrHost'
$s=& $start -BodyHolsterProbe -Seconds 15 -FrameChannel offline
Assert ($s.probe -and $s.body -and $s.physical -and $s.hands) 'Holster must include inert physical consumer/body dependencies'
$s=& $stream -BodyHolsterProbe -Seconds 15 -Pairs 240 -StaticPose -Async
Assert ($s.probe -and $s.observe) 'Holster carrier must be neutral'
$s=& $start -BodyHolsterXm8Probe -Seconds 15 -FrameChannel offline
Assert ($s.probe -and $s.xm8 -and $s.body -and $s.physical) 'XM8 must use the bounded holster diagnostic dependencies'
$s=& $stream -BodyHolsterXm8Probe -Seconds 15 -Pairs 240 -StaticPose -Async
Assert ($s.probe -and $s.xm8 -and $s.observe) 'XM8 must use the neutral carrier'
$s=& $start -BodyHolsterXm8FireProbe -Seconds 15 -FrameChannel offline
Assert ($s.probe -and $s.xm8 -and $s.fire -and $s.body) 'Explicit post-draw fire dependencies missing'
$s=& $stream -BodyHolsterXm8FireProbe -Seconds 15 -Pairs 240 -StaticPose -Async
Assert ($s.fire -and $s.observe) 'Fire diagnostic must retain neutral receiver'
foreach($map in @(@{BodyHolsterProbe=$true},@{BodyHolsterXm8Probe=$true})){
 $map.BodyHolsterXm8FireProbe=$true;$map.Seconds=15;$map.FrameChannel='offline'
 $rejected=$false;try{$null=& $start @map}catch{$rejected=$true};Assert $rejected 'Conflicting explicit diagnostic selectors accepted'
}
foreach($switch in @('WeaponVisibilityProbe','PhysicalReloadProbe','PhysicalReloadRepeatProbe','ReloadRequestProbe','ReloadRoundProbe','ReloadHoldProbe','OpticFilterObserve','SightFlip','RigPulse','DeathProbe','EquipProbe','PassEvidence','UntilHostExit')){
    $m=@{BodyHolsterProbe=$true;Seconds=15;FrameChannel='offline'};$m[$switch]=$true
    $rejected=$false;try{$null=& $start @m}catch{$rejected=$true};Assert $rejected "Accepted incompatible $switch"
}
foreach($switch in @('WeaponVisibilityProbe','PhysicalReloadRepeatProbe','PhysicalReloadProbe','OpticFilterObserve','SupportPrepareShots','SupportReload','ControlsHands','ControlsFire','ControlsUse','PassEvidence')){
    $m=@{BodyHolsterProbe=$true;Seconds=15;Pairs=240;StaticPose=$true;Async=$true};$m[$switch]=$true
    $rejected=$false;try{$null=& $stream @m}catch{$rejected=$true};Assert $rejected "Carrier accepted $switch"
}
foreach($m in @(@{Seconds=14},@{Pairs=239},@{StaticPose=$false},@{Async=$false})){
    $argsMap=@{BodyHolsterProbe=$true;Seconds=15;Pairs=240;StaticPose=$true;Async=$true};foreach($k in $m.Keys){$argsMap[$k]=$m[$k]}
    $rejected=$false;try{$null=& $stream @argsMap}catch{$rejected=$true};Assert $rejected 'Unbounded carrier accepted'
}
if($ParserExe){
    $dll=Join-Path $root 'never-present.dll';$report=Join-Path $root 'never-present.json'
    Assert (-not (Test-Path -LiteralPath $dll) -and -not (Test-Path -LiteralPath $report)) 'Sentinel exists'
    foreach($profile in @('--body-holster-probe','--body-holster-xm8-probe','--body-holster-xm8-fire-probe')){
    foreach($extra in @('', '--weapon-visibility-probe','--physical-reload-repeat-probe','--optic-filter-observe','--pass-evidence','--body-holster-probe','--body-holster-xm8-probe','--body-holster-xm8-fire-probe')){
        $si=[Diagnostics.ProcessStartInfo]::new();$si.FileName=$ParserExe;$si.UseShellExecute=$false;$si.CreateNoWindow=$true;$si.RedirectStandardError=$true;$si.RedirectStandardOutput=$true
        foreach($v in @('--game',(Join-Path $root 'does-not-exist'),'--dll',$dll,'--report',$report,'--stream','offline','--seconds','15',$profile)){$si.ArgumentList.Add($v)}
        if($extra){$si.ArgumentList.Add($extra)}
        $p=[Diagnostics.Process]::Start($si);$errorText=$p.StandardError.ReadToEnd();$null=$p.StandardOutput.ReadToEnd();$p.WaitForExit()
        Assert ($p.ExitCode -eq 1) 'Unexpected parser exit'
        Assert (($errorText -like '*Probe DLL missing or report already exists*') -eq (-not $extra)) "Unexpected result $errorText";$p.Dispose()
    }
    }
}
[pscustomobject]@{checks=$checks;native_attach=$false;game_actions=$false}|ConvertTo-Json
