$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$checks=0
function Assert([bool]$okay,[string]$reason){if(-not $okay){throw $reason};$script:checks++}
function Prefix([string]$file,[string]$boundary){
    $path=Join-Path $root $file;$tokens=$null;$errors=$null
    $null=[Management.Automation.Language.Parser]::ParseFile($path,[ref]$tokens,[ref]$errors)
    Assert ($errors.Count -eq 0) "$file parse failed"
    $text=Get-Content -Raw -LiteralPath $path;$at=$text.IndexOf($boundary,[StringComparison]::Ordinal)
    Assert ($at -gt 0) 'Missing pre-action boundary'
    [ScriptBlock]::Create($text.Substring(0,$at)+"`n"+'[pscustomobject]@{repeat=[bool]$PhysicalReloadRepeatProbe;probe=[bool]$PhysicalReloadProbe}')
}
$start=Prefix 'Start-NativeTrace.ps1' '$PSNativeCommandUseErrorActionPreference='
$stream=Prefix 'Test-NativeStream.ps1' 'if(Get-Process BC2XrHost'
$r=& $start -PhysicalReloadRepeatProbe -Seconds 30 -FrameChannel offline
Assert ($r.repeat -and $r.probe) 'Repeat flag did not select physical consumer'
$r=& $stream -PhysicalReloadRepeatProbe -Seconds 30 -Pairs 240 -StaticPose -Async
Assert ($r.repeat -and $r.probe) 'Test stream did not preserve repeat option'
$r=& $start -PhysicalReloadProbe -Seconds 30 -FrameChannel offline
Assert ($r.probe -and -not $r.repeat) 'Single-shell default changed'
foreach($extra in @(@{Seconds=15},@{BodyInventory=$true},@{OpticFilterObserve=$true},@{UntilHostExit=$true;HostPid=123})){
    $argsMap=@{PhysicalReloadRepeatProbe=$true;Seconds=30;FrameChannel='offline'}
    foreach($key in $extra.Keys){$argsMap[$key]=$extra[$key]}
    $rejected=$false;try{$null=& $start @argsMap}catch{$rejected=$true}
    Assert $rejected 'Unsafe repeat combination accepted'
}
$exe=Join-Path $root 'build\x86\BC2NativeTrace.exe'
$dll=Join-Path $root 'does-not-exist\never.dll';$report=Join-Path $root 'does-not-exist\never.json'
Assert (-not (Test-Path -LiteralPath $dll) -and -not (Test-Path -LiteralPath $report)) 'Parser sentinel exists'
function Invoke-RepeatParserFixture([string[]]$options,[bool]$valid){
    $si=[Diagnostics.ProcessStartInfo]::new();$si.FileName=$exe;$si.UseShellExecute=$false;$si.CreateNoWindow=$true;$si.RedirectStandardError=$true;$si.RedirectStandardOutput=$true
    foreach($v in (@('--game',(Join-Path $root 'does-not-exist'),'--dll',$dll,'--report',$report)+$options)){$si.ArgumentList.Add($v)}
    $p=[Diagnostics.Process]::Start($si);$errorText=$p.StandardError.ReadToEnd();$null=$p.StandardOutput.ReadToEnd();$p.WaitForExit()
    Assert ($p.ExitCode -eq 1) 'Unexpected parser exit'
    Assert (($errorText -like '*Probe DLL missing or report already exists*') -eq $valid) "Unexpected parser result: $errorText"
    $p.Dispose()
}
$base=@('--stream','offline','--seconds','30','--physical-reload-repeat-probe')
Invoke-RepeatParserFixture $base $true
Invoke-RepeatParserFixture @('--stream','offline','--seconds','30','--physical-reload-probe') $true
foreach($extra in @('--body-inventory','--optic-filter-observe','--sight-flip','--equip-probe','--reload-request-probe')){Invoke-RepeatParserFixture ($base+@($extra)) $false}
Invoke-RepeatParserFixture ($base+@('--until-host-exit','123')) $false
Invoke-RepeatParserFixture ($base+@('--seconds','15')) $false
Invoke-RepeatParserFixture @('--physical-reload-repeat-probe','--seconds','30') $false
[pscustomobject]@{checks=$checks;native_attach=$false;game_actions=$false}|ConvertTo-Json
