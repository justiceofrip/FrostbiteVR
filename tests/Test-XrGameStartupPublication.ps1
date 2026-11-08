#requires -Version 7.5
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$ScratchRoot,
    [string]$ReceiptPath,
    [string]$LauncherCorePath=(Join-Path $PSScriptRoot '../launcher/Launcher.Core.ps1'),
    [string]$RunnerPath=(Join-Path $PSScriptRoot '../tools/Invoke-Bc2XrGameSimulation.ps1')
)
Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'
if(-not $IsWindows){throw 'Real Windows open-reader semantics required.'}
if(-not [IO.Path]::IsPathFullyQualified($ScratchRoot)){throw 'Use an absolute scratch directory.'}
$ScratchRoot=[IO.Path]::GetFullPath($ScratchRoot)
$null=[IO.Directory]::CreateDirectory($ScratchRoot)
if(-not $ReceiptPath){$ReceiptPath=Join-Path $ScratchRoot 'startup-publication-result.json'}
$utf8=[Text.UTF8Encoding]::new($false)
$checks=[Collections.Generic.List[string]]::new()
$cases=[Collections.Generic.List[object]]::new()
$fixture=Join-Path $ScratchRoot ('startup-publication-'+[guid]::NewGuid().ToString('N'))
$null=[IO.Directory]::CreateDirectory($fixture)
$OutputDir=$fixture
$TimeoutSeconds=480;$Seconds=420;$hostStartedAt=0.0
$startup=@{sent_at_seconds=0.0}
$clock=[Diagnostics.Stopwatch]::StartNew()
$f=$null;$caught=$null
function Check([bool]$Good,[string]$Name){if(-not $Good){throw ('FAILED: '+$Name)};$checks.Add($Name)}
function FunctionText([string]$Path,[string]$Name){
    $tokens=$null;$errors=$null
    $ast=[Management.Automation.Language.Parser]::ParseFile($Path,[ref]$tokens,[ref]$errors)
    if($errors.Count){throw ($errors|Out-String)}
    $fn=$ast.Find({param($n) $n -is [Management.Automation.Language.FunctionDefinitionAst] -and $n.Name -ceq $Name}.GetNewClosure(),$true)
    if(-not $fn){throw "Missing function $Name"};return $fn.Extent.Text
}
$core=$LauncherCorePath
$runner=$RunnerPath
$publisher=FunctionText $core 'Write-FvrJson'
$move='[IO.File]::Move($temp, $Path, $true)'
Check (($publisher.Split([string[]]@($move),[StringSplitOptions]::None)).Count -eq 2) 'candidate has one actual atomic Move site'
$observed=$publisher.Replace($move,('Observe-Attempt $temp $Path;'+$move))
$timed=$observed.Replace('[Diagnostics.Stopwatch]::StartNew()','(New-TestClock)').Replace('$PublicationDeadlineClock.Elapsed.TotalSeconds','(OriginalSeconds)')
. ([scriptblock]::Create($observed))
. ([scriptblock]::Create((FunctionText $runner 'Save')))
function Observe-Attempt([string]$Temporary,[string]$Target){
    $f.attempts++;$f.target=$Target
    $text=[IO.File]::ReadAllText($Temporary)
    if($f.temporary){Check ($Temporary -ceq $f.temporary -and $text -ceq $f.image) 'retry retains one serialized temporary image'}
    else{$f.temporary=$Temporary;$f.image=$text}
    if($f.injected){throw $f.injected}
}
function New-TestClock {return $f.localClock}
function OriginalSeconds {return $f.originalSeconds}
function Start-Sleep([int]$Milliseconds){
    $f.sleeps++
    $f.localClock.Elapsed.TotalMilliseconds+=$Milliseconds+$f.oversleep
    $f.originalSeconds+=($Milliseconds+$f.oversleep)/1000.0
    if(-not $f.fake){Microsoft.PowerShell.Utility\Start-Sleep -Milliseconds $Milliseconds}
    if($f.mutate){$f.mutate.generation=99}
    if($f.sleeps -eq $f.releaseAt){$f.held.Dispose();$f.held=$null}
}
function Reset([string]$Name='sim-startup.json'){
    if($f -and $f.held){$f.held.Dispose()}
    $script:f=@{held=$null;attempts=0;sleeps=0;releaseAt=0;temporary=$null;image=$null;target=$null;injected=$null;mutate=$null;fake=$false;oversleep=0;originalSeconds=0.0;localClock=[pscustomobject]@{Elapsed=[pscustomobject]@{TotalMilliseconds=0.0}}}
    $script:statePath=Join-Path $fixture $Name
    if([IO.File]::Exists($statePath)){[IO.File]::SetAttributes($statePath,[IO.FileAttributes]::Normal)}
    [IO.File]::WriteAllText($statePath,'{"generation":1}',$utf8)
    $script:startup=@{sent_at_seconds=0.0};$script:hostStartedAt=0.0;$script:TimeoutSeconds=480;$script:Seconds=420
    $clock.Restart()
}
function Hold {$f.held=[IO.File]::Open($statePath,[IO.FileMode]::Open,[IO.FileAccess]::Read,([IO.FileShare]::ReadWrite -bor [IO.FileShare]::Delete))}
function Attempt([scriptblock]$Action){$script:caught=$null;try{& $Action}catch{$script:caught=$_}}
function Cause {if($caught){return $caught.Exception.GetBaseException()};return $null}
function State {return ([IO.File]::ReadAllText($statePath)|ConvertFrom-Json -AsHashtable)}
try{
    foreach($name in @('sim-startup.json','session-link.json')){
        Reset $name;Hold;$f.releaseAt=2
        $value=@{generation=2;status='ready'};$f.mutate=$value
        Save $name $value
        Check ($f.attempts -eq 3 -and $f.sleeps -eq 2 -and -not $f.held -and (State).generation -eq 2) ($name+' recovers after actual delete-sharing reader closes')
        $cases.Add(@{name=$name;reader='ReadWrite, Delete';attempts=$f.attempts;outcome='recovered'})
    }
    foreach($name in @('result.json','sim-host-initial.json','SIM-STARTUP.JSON')){
        Reset $name;Hold
        Attempt {Save $name @{generation=2}}
        Check ((Cause) -is [UnauthorizedAccessException] -and (Cause).HResult -eq -2147024891 -and $f.attempts -eq 1 -and $f.sleeps -eq 0 -and (State).generation -eq 1) ($name+' keeps generic one-shot refusal under actual reader denial')
    }
    Reset;Hold
    Attempt {Save 'sim-startup.json' @{generation=2}}
    $detail=(Cause).Data['fvr_json_write']
    Check ((Cause) -is [UnauthorizedAccessException] -and $f.attempts -le 25 -and $f.sleeps -le 24 -and (State).generation -eq 1) 'persistent real reader retains bounded failure and old target'
    Check ($detail.path -ceq $statePath -and $detail.temporary -ceq $f.temporary -and $detail.stage -ceq 'atomic_move' -and $detail.hresult -eq -2147024891 -and $detail.attempts -eq $f.attempts) 'failure preserves exact destination, temporary, stage, HRESULT and Move count'
    # Execute the exact candidate receipt statements against this real exception.
    $runnerText=[IO.File]::ReadAllText($runner)
    $begin=$runnerText.IndexOf('    $receipt.failure_context=@{message=$_.Exception.Message;')
    $end=$runnerText.IndexOf("`n}`nfinally{",$begin)
    Check ($begin -ge 0 -and $end -gt $begin) 'candidate failure-context block is identifiable'
    $receipt=@{}
    try{throw $caught}catch{. ([scriptblock]::Create($runnerText.Substring($begin,$end-$begin)))}
    Check ($receipt.failure_context.json_write.path -ceq $statePath -and $receipt.failure_context.json_write.hresult -eq -2147024891) 'runner retains base-exception publication diagnostics'

    . ([scriptblock]::Create($timed))
    foreach($boundary in @('handshake','host_startup','host_duration','outer_cleanup')){
        Reset;Hold;$f.fake=$true;$f.releaseAt=1
        switch($boundary){
            handshake {$f.originalSeconds=14.995}
            host_startup {$startup=$null;$f.originalSeconds=19.995}
            host_duration {$startup=$null;$Seconds=15;$f.originalSeconds=14.995}
            outer_cleanup {$startup=$null;$hostStartedAt=$null;$TimeoutSeconds=90;$f.originalSeconds=29.995}
        }
        Attempt {Save 'sim-startup.json' @{generation=2}}
        Check ((Cause) -is [UnauthorizedAccessException] -and $f.attempts -eq 1 -and $f.sleeps -eq 0 -and (State).generation -eq 1) ($boundary+' deadline refuses a sleep that cannot fit original clock')
    }
    Reset;Hold;$f.fake=$true;$f.releaseAt=1;$f.oversleep=40;$f.originalSeconds=14.98
    Attempt {Save 'sim-startup.json' @{generation=2}}
    Check ((Cause) -is [UnauthorizedAccessException] -and $f.attempts -eq 1 -and $f.sleeps -eq 1 -and -not $f.held -and (State).generation -eq 1) 'original deadline crossed during sleep prevents late Move after reader release'
    Reset;$f.fake=$true;$f.originalSeconds=15.0
    Attempt {Save 'sim-startup.json' @{generation=2}}
    Check ((Cause) -is [TimeoutException] -and $f.attempts -eq 0 -and $f.sleeps -eq 0 -and (State).generation -eq 1 -and (Cause).Data['fvr_json_write'].attempts -eq 0) 'expired original deadline refuses first Move and records zero attempts'
    Reset;Hold;$f.fake=$true
    Attempt {Save 'sim-startup.json' @{generation=2}}
    Check ((Cause) -is [UnauthorizedAccessException] -and $f.attempts -eq 25 -and $f.sleeps -eq 24 -and (State).generation -eq 1) 'existing 25-attempt 250 ms local bound remains without renewing clock'
    Reset;$f.fake=$true;$f.injected=[IO.IOException]::new('disk full fixture',-2147024784)
    Attempt {Save 'sim-startup.json' @{generation=2}}
    Check ((Cause).HResult -eq -2147024784 -and $f.attempts -eq 1 -and $f.sleeps -eq 0 -and (State).generation -eq 1) 'unrelated I/O error remains immediate refusal'
    Reset;$f.fake=$true;[IO.File]::SetAttributes($statePath,[IO.FileAttributes]::ReadOnly)
    Attempt {Save 'sim-startup.json' @{generation=2}}
    Check ((Cause) -is [UnauthorizedAccessException] -and $f.attempts -eq 1 -and $f.sleeps -eq 0 -and (State).generation -eq 1) 'readonly target remains immediate refusal'
    $result=@{passed=$true;checks=$checks.Count;details=$checks.ToArray();cases=$cases.ToArray();fixture=$fixture;evidence='deterministic Windows filesystem and controlled deadline edges';actual_game_run=$false;tracked_source_changed=$false}
    [IO.File]::WriteAllText($ReceiptPath,($result|ConvertTo-Json -Depth 8)+"`n",$utf8)
    $result|ConvertTo-Json -Depth 8
}catch{
    $result=@{passed=$false;checks=$checks.Count;details=$checks.ToArray();fixture=$fixture;error=$_.Exception.Message;stack=$_.ScriptStackTrace}
    [IO.File]::WriteAllText($ReceiptPath,($result|ConvertTo-Json -Depth 8)+"`n",$utf8)
    throw
}finally{
    if($f -and $f.held){$f.held.Dispose()}
    # Retain report fixtures; no source or actual-session cleanup occurs.
}
