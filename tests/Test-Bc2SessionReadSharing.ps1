#requires -Version 7.5
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$ScratchRoot,
    [string]$LauncherCorePath=(Join-Path $PSScriptRoot '../launcher/Launcher.Core.ps1'),
    [string]$ReceiptPath
)
Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'
if(-not $IsWindows){throw 'This regression requires Windows file-sharing semantics.'}
if(-not [IO.Path]::IsPathFullyQualified($ScratchRoot)){throw 'Use an absolute scratch directory.'}
$ScratchRoot=[IO.Path]::GetFullPath($ScratchRoot)
$null=[IO.Directory]::CreateDirectory($ScratchRoot)
$fixture=Join-Path $ScratchRoot ('session-sharing-'+[guid]::NewGuid().ToString('N'))
$null=[IO.Directory]::CreateDirectory($fixture)
$statePath=Join-Path $fixture 'session.json'
$utf8=[Text.UTF8Encoding]::new($false)
$checks=[Collections.Generic.List[string]]::new()
$cases=[Collections.Generic.List[object]]::new()
$script:f=$null
function Check([bool]$Good,[string]$Name){if(-not $Good){throw "FAILED: $Name"};$checks.Add($Name)}
function FunctionText([string]$Path,[string]$Name){
    $tokens=$null;$errors=$null
    $ast=[Management.Automation.Language.Parser]::ParseFile($Path,[ref]$tokens,[ref]$errors)
    if($errors.Count){throw ($errors|Out-String)}
    $fn=$ast.Find({param($n) $n -is [Management.Automation.Language.FunctionDefinitionAst] -and $n.Name -ceq $Name}.GetNewClosure(),$true)
    if(-not $fn){throw "Missing function $Name in $Path"};return $fn.Extent.Text
}
$publisher=FunctionText $LauncherCorePath 'Write-FvrJson'
. ([scriptblock]::Create($publisher))
# Observe each attempt without replacing File.Move or its original temp contents.
$move='[IO.File]::Move($temp, $Path, $true)'
if(($publisher.Split([string[]]@($move),[StringSplitOptions]::None)).Count -ne 2){throw 'Expected one production Move site.'}
$observed=$publisher.Replace('function Write-FvrJson(','function Write-ObservedFvrJson(').Replace($move,('Observe-Attempt $temp $Path;'+$move))
. ([scriptblock]::Create($observed))
# Only the deadline-edge cases replace the clock; all moves/handles stay real.
$timed=$observed.Replace('function Write-ObservedFvrJson(','function Write-TimedFvrJson(').Replace('[Diagnostics.Stopwatch]::StartNew()','(New-TestClock)')
. ([scriptblock]::Create($timed))
function Observe-Attempt([string]$Temporary,[string]$Target){
    $f.attempts++
    $text=[IO.File]::ReadAllText($Temporary)
    if($f.temporary){Check ($Temporary -ceq $f.temporary -and $text -ceq $f.image) 'retry keeps exactly one serialized temporary image'}
    else{$f.temporary=$Temporary;$f.image=$text}
    if($f.injected){throw $f.injected}
}
function New-TestClock {$f.fake_clock=$true;$f.clocks++;return $f.clock}
function Start-Sleep([int]$Milliseconds){
    $f.sleeps++;$f.clock.Elapsed.TotalMilliseconds+=$Milliseconds+$f.oversleep
    if($f.sleeps -eq 1 -and $f.mutateValue){$f.mutateValue.generation=99;$f.mutateValue.payload='changed after serialization'}
    if(-not $f.fake_clock){Microsoft.PowerShell.Utility\Start-Sleep -Milliseconds $Milliseconds}
    if($f.sleeps -eq $f.releaseAt){$f.held.Dispose();$f.held=$null}
}
function Reset {
    if($script:f -and $f.held){$f.held.Dispose()}
    $script:f=@{held=$null;attempts=0;sleeps=0;releaseAt=0;temporary=$null;image=$null;injected=$null;mutateValue=$null;fake_clock=$false;clocks=0;oversleep=0;clock=[pscustomobject]@{Elapsed=[pscustomobject]@{TotalMilliseconds=0.0}}}
    if([IO.File]::Exists($statePath)){[IO.File]::SetAttributes($statePath,[IO.FileAttributes]::Normal)}
    Write-FvrJson $statePath @{generation=1;payload='old image'}
}
function Hold([IO.FileShare]$Share=[IO.FileShare]::Read){$f.held=[IO.File]::Open($statePath,[IO.FileMode]::Open,[IO.FileAccess]::Read,$Share)}
function Attempt([scriptblock]$Action){$script:caught=$null;try{& $Action}catch{$script:caught=$_}}
function BaseException {if($script:caught){return $script:caught.Exception.GetBaseException()};return $null}
function Read-State {return ([IO.File]::ReadAllText($statePath)|ConvertFrom-Json -AsHashtable)}
function Check-Preserved([string]$Name){
    Check ((Read-State).generation -eq 1 -and -not [IO.File]::Exists($f.temporary)) $Name
}
try{
    Reset;Hold
    Attempt {Write-ObservedFvrJson $statePath @{generation=2} }
    Check ((BaseException) -is [UnauthorizedAccessException] -and (BaseException).HResult -eq -2147024891 -and $f.attempts -eq 1 -and $f.sleeps -eq 0) 'generic publication remains one-shot under actual open-reader denial'
    Check-Preserved 'generic refusal retains complete old target and removes temporary'

    foreach($share in @([IO.FileShare]::Read,[IO.FileShare]::ReadWrite,([IO.FileShare]::ReadWrite -bor [IO.FileShare]::Delete))){
        Reset;Hold $share;$f.releaseAt=3
        $publication=@{generation=2;payload='complete current image'};$f.mutateValue=$publication
        Write-ObservedFvrJson $statePath $publication -RetrySessionPublication
        Check ($f.attempts -eq 4 -and $f.sleeps -eq 3 -and -not $f.held) ("session publication recovers after actual $share reader closes during bounded wait")
        $current=Read-State
        Check ($current.generation -eq 2 -and $current.payload -ceq 'complete current image' -and -not [IO.File]::Exists($f.temporary)) 'recovered publication exposes the complete current image'
        $cases.Add(@{reader_share=$share.ToString();attempts=$f.attempts;sleeps=$f.sleeps;outcome='recovered'})
    }

    Reset;Hold
    $wall=[Diagnostics.Stopwatch]::StartNew()
    Attempt {Write-ObservedFvrJson $statePath @{generation=2} -RetrySessionPublication}
    $wall.Stop()
    Check ((BaseException) -is [UnauthorizedAccessException] -and (BaseException).HResult -eq -2147024891 -and $f.attempts -le 25 -and $f.sleeps -le 24) 'persistent actual reader is refused within bounded attempts'
    Check-Preserved 'persistent reader refusal preserves complete old target and removes temporary'
    $detail=(BaseException).Data['fvr_json_write']
    Check ($detail.path -ceq $statePath -and $detail.stage -ceq 'atomic_move' -and $detail.hresult -eq -2147024891 -and $detail.attempts -eq $f.attempts -and $detail.temporary -ceq $f.temporary) 'publication failure preserves exact path, stage, original error and bounded attempt count'
    $cases.Add(@{reader_share='Read';attempts=$f.attempts;sleeps=$f.sleeps;elapsed_ms=$wall.Elapsed.TotalMilliseconds;outcome='bounded_refusal'})

    Reset;Hold
    Attempt {Write-TimedFvrJson $statePath @{generation=2} -RetrySessionPublication}
    Check ($caught -and $f.attempts -eq 25 -and $f.sleeps -eq 24 -and $f.clocks -eq 1 -and $f.clock.Elapsed.TotalMilliseconds -eq 240) 'one original monotonic clock bounds 25 attempts / 240 ms requested sleep'
    Check-Preserved 'attempt-bound refusal preserves old target'
    Reset;Hold;$f.clock.Elapsed.TotalMilliseconds=235
    Attempt {Write-TimedFvrJson $statePath @{generation=2} -RetrySessionPublication}
    Check ($caught -and $f.attempts -eq 2 -and $f.sleeps -eq 1 -and $f.clocks -eq 1) 'retry allowance is never renewed after a failed attempt'
    Reset;Hold;$f.releaseAt=1;$f.oversleep=500
    Attempt {Write-TimedFvrJson $statePath @{generation=2} -RetrySessionPublication}
    Check ($caught -and $f.attempts -eq 1 -and $f.sleeps -eq 1 -and -not $f.held) 'scheduler oversleep cannot start a late retry even after reader release'
    Check-Preserved 'late release does not replace old target after retry deadline'

    Reset
    [IO.File]::SetAttributes($statePath,[IO.FileAttributes]::ReadOnly)
    Attempt {Write-ObservedFvrJson $statePath @{generation=2} -RetrySessionPublication}
    Check ((BaseException) -is [UnauthorizedAccessException] -and $f.attempts -eq 1 -and $f.sleeps -eq 0) 'actual readonly destination remains an immediate failure'
    Check-Preserved 'readonly refusal preserves target'
    Reset
    $f.injected=[UnauthorizedAccessException]::new('persistent ACL denial fixture')
    Attempt {Write-TimedFvrJson $statePath @{generation=2} -RetrySessionPublication}
    Check ((BaseException).Message -ceq 'persistent ACL denial fixture' -and $f.attempts -eq 25 -and $f.sleeps -eq 24) 'persistent access denial remains the original failure after bounded wait'
    Check-Preserved 'persistent permission failure does not replace target'
    foreach($failure in @(
        [IO.IOException]::new('unrelated I/O fixture',-2147024891),
        [IO.IOException]::new('disk-full fixture',-2147024784),
        [InvalidOperationException]::new('unrelated failure fixture')
    )){
        Reset;$f.injected=$failure
        Attempt {Write-TimedFvrJson $statePath @{generation=2} -RetrySessionPublication}
        Check ((BaseException).Message -ceq $failure.Message -and $f.attempts -eq 1 -and $f.sleeps -eq 0) 'other exception types and I/O errors retain immediate refusal'
        Check-Preserved 'unrelated failure preserves target'
    }
    Reset
    Attempt {Write-ObservedFvrJson (Join-Path $fixture 'missing/session.json') @{generation=2} -RetrySessionPublication}
    Check ((BaseException) -is [IO.DirectoryNotFoundException] -and $f.attempts -eq 0 -and $f.sleeps -eq 0) 'missing parent directory is not recovered or retried'
    $detail=(BaseException).Data['fvr_json_write']
    Check ($detail.path -ceq (Join-Path $fixture 'missing/session.json') -and $detail.stage -ceq 'serialize_temporary' -and $detail.attempts -eq 0) 'temporary-write failure is distinguished from atomic replacement'
    if(-not ('FvrFailingExceptionData' -as [type])){
        Add-Type 'public sealed class FvrFailingExceptionData : System.Exception { public FvrFailingExceptionData() : base("original diagnostic failure fixture") {} public override System.Collections.IDictionary Data { get { throw new System.InvalidOperationException("diagnostic Data unavailable"); } } }'
    }
    Reset;$originalFailure=[FvrFailingExceptionData]::new();$f.injected=$originalFailure
    Attempt {Write-ObservedFvrJson $statePath @{generation=2} -RetrySessionPublication}
    Check ([object]::ReferenceEquals((BaseException),$originalFailure) -and $f.attempts -eq 1 -and $f.sleeps -eq 0) 'diagnostic recording failure never masks or retries the original exception'
    Reset
    [IO.File]::Delete($statePath)
    Write-ObservedFvrJson $statePath @{generation=2;payload='first publication'} -RetrySessionPublication
    Check ((Read-State).generation -eq 2 -and $f.attempts -eq 1 -and $f.sleeps -eq 0) 'first session publication succeeds without a destination'
    $sessionPath=Join-Path (Split-Path -Parent $LauncherCorePath) 'runtime/Session.ps1'
    Check ((FunctionText $sessionPath 'Save-State') -match 'Write-FvrJson \$stateFile \$state -RetrySessionPublication') 'only session Save-State selects bounded publication retry'
    $receipt=[ordered]@{schema='fvr.session-read-sharing-test.v2';passed=$true;evidence_level='deterministic_windows_filesystem';checks=$checks.Count;details=$checks.ToArray();cases=$cases.ToArray();powershell=$PSVersionTable.PSVersion.ToString();native_game_or_headset_test=$false}
    if($ReceiptPath){[IO.File]::WriteAllText($ReceiptPath,($receipt|ConvertTo-Json -Depth 6)+"`n",$utf8)}
    $receipt|ConvertTo-Json -Depth 6
}catch{
    $cause=$_.Exception.GetBaseException()
    $failure=[ordered]@{schema='fvr.session-read-sharing-test.v2';passed=$false;checks=$checks.Count;details=$checks.ToArray();error=$cause.Message;exception_type=$cause.GetType().FullName;hresult=$cause.HResult;stack=$_.ScriptStackTrace;position=$_.InvocationInfo.PositionMessage;native_game_or_headset_test=$false}
    if($ReceiptPath){[IO.File]::WriteAllText($ReceiptPath,($failure|ConvertTo-Json -Depth 6)+"`n",$utf8)}
    throw
}finally{
    if($f -and $f.held){$f.held.Dispose()}
    if([IO.File]::Exists($statePath)){[IO.File]::SetAttributes($statePath,[IO.FileAttributes]::Normal);[IO.File]::Delete($statePath)}
    [IO.Directory]::Delete($fixture)
}
