param([string]$ParserExe)
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent;$checks=0
function Assert([bool]$value,[string]$why){if(-not $value){throw $why};$script:checks++}
function Prefix([string]$file,[string]$boundary){
    $path=Join-Path $root $file;$tokens=$null;$errors=$null
    $null=[Management.Automation.Language.Parser]::ParseFile($path,[ref]$tokens,[ref]$errors)
    Assert ($errors.Count -eq 0) "$file parse failed"
    $text=Get-Content -Raw -LiteralPath $path;$at=$text.IndexOf($boundary,[StringComparison]::Ordinal)
    Assert ($at -gt 0) 'Missing pre-action boundary'
    [ScriptBlock]::Create($text.Substring(0,$at)+"`n"+'[pscustomobject]@{visibility=[bool]$WeaponVisibilityProbe;repeat=[bool]$PhysicalReloadRepeatProbe;hands=[bool]$Hands;observe=[bool]$ControlsObserve}')
}
$start=Prefix 'Start-NativeTrace.ps1' '$PSNativeCommandUseErrorActionPreference='
$stream=Prefix 'Test-NativeStream.ps1' 'if(Get-Process BC2XrHost'
$s=& $start -WeaponVisibilityProbe -Seconds 15 -FrameChannel offline
Assert ($s.visibility -and $s.hands -and -not $s.repeat) 'Visibility did not select ordinary tracked hands'
$s=& $stream -WeaponVisibilityProbe -Seconds 15 -Pairs 240 -StaticPose -Async
Assert ($s.visibility -and $s.observe -and -not $s.repeat) 'Visibility did not select neutral receiver'
foreach($switch in @('PhysicalReloadRepeatProbe','PhysicalReloadProbe','ReloadRequestProbe','ReloadRoundProbe','ReloadHoldProbe','BodyInventory','OpticFilterObserve','SightFlip','RigPulse','DeathProbe','EquipProbe','PassEvidence')){
    $argsMap=@{WeaponVisibilityProbe=$true;Seconds=15;FrameChannel='offline'};$argsMap[$switch]=$true
    $rejected=$false;try{$null=& $start @argsMap}catch{$rejected=$true};Assert $rejected "Accepted incompatible $switch"
}
foreach($switch in @('PhysicalReloadRepeatProbe','PhysicalReloadProbe','OpticFilterObserve','SupportPrepareShots','ControlsHands','ControlsFire','ControlsUse','PassEvidence')){
    $argsMap=@{WeaponVisibilityProbe=$true;Seconds=15;Pairs=240;StaticPose=$true;Async=$true};$argsMap[$switch]=$true
    $rejected=$false;try{$null=& $stream @argsMap}catch{$rejected=$true};Assert $rejected "Receiver accepted $switch"
}
# Both existing one-shell and repeat launch parsing stay independent.
$s=& $start -PhysicalReloadRepeatProbe -Seconds 30 -FrameChannel offline
Assert ($s.repeat -and -not $s.visibility) 'Repeat launch changed'
if($ParserExe){
    $dll=Join-Path $root 'does-not-exist\never.dll';$report=Join-Path $root 'does-not-exist\never.json'
    Assert (-not (Test-Path -LiteralPath $dll) -and -not (Test-Path -LiteralPath $report)) 'Sentinel exists'
    foreach($extra in @('', '--physical-reload-repeat-probe','--body-inventory','--optic-filter-observe','--pass-evidence')){
        $si=[Diagnostics.ProcessStartInfo]::new();$si.FileName=$ParserExe;$si.UseShellExecute=$false;$si.CreateNoWindow=$true;$si.RedirectStandardError=$true;$si.RedirectStandardOutput=$true
        foreach($v in @('--game',(Join-Path $root 'does-not-exist'),'--dll',$dll,'--report',$report,'--stream','offline','--seconds','15','--weapon-visibility-probe')){$si.ArgumentList.Add($v)}
        if($extra){$si.ArgumentList.Add($extra)}
        $p=[Diagnostics.Process]::Start($si);$errorText=$p.StandardError.ReadToEnd();$null=$p.StandardOutput.ReadToEnd();$p.WaitForExit()
        Assert ($p.ExitCode -eq 1) 'Unexpected parser exit'
        Assert (($errorText -like '*Probe DLL missing or report already exists*') -eq (-not $extra)) "Unexpected parser result $errorText"
        $p.Dispose()
    }
}
[pscustomobject]@{checks=$checks;native_attach=$false;game_actions=$false}|ConvertTo-Json
