#requires -Version 7.5
param([string]$RunnerPath=(Join-Path $PSScriptRoot '../tools/Invoke-Bc2XrGameSimulation.ps1'))
Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'
$utf8=[Text.UTF8Encoding]::new($false)
$original=[IO.File]::ReadAllText($RunnerPath).Replace("`r`n","`n")
$source=$original
$tokens=$null;$errors=$null
$ast=[Management.Automation.Language.Parser]::ParseInput($source,[ref]$tokens,[ref]$errors)
if($errors.Count){throw ($errors|Out-String)}
$script:checks=0
function Check([bool]$Good,[string]$Name){if(-not $Good){throw "FAILED: $Name"};$script:checks++}
function FunctionText($Tree,[string]$Name){
    $fn=$Tree.Find({param($n) $n -is [Management.Automation.Language.FunctionDefinitionAst] -and $n.Name -ceq $Name}.GetNewClosure(),$true)
    if(-not $fn){throw "Missing function $Name"};return $fn.Extent.Text
}
foreach($name in 'Require','Json','ObserveBounds'){. ([scriptblock]::Create((FunctionText $ast $name)))}
# Only the exception-injection variant replaces the OS read; lock tests below use real ReadAllText.
$injected=(FunctionText $ast 'Json').Replace('function Json(','function JsonInjected(').Replace('[IO.File]::ReadAllText($Path)','(Read-Injected $Path)')
. ([scriptblock]::Create($injected))
function Read-Injected([string]$Path){$script:f.reads++;throw $script:f.injected}
function Start-Sleep([int]$Milliseconds){
    $script:f.sleeps++;$script:clock.Elapsed.TotalSeconds+=$Milliseconds/1000.0+$script:f.oversleep
    $script:clock.ElapsedMilliseconds=[long]($script:clock.Elapsed.TotalSeconds*1000)
    if($script:f.sleeps -eq $script:f.releaseAt){
        $script:f.lock.Dispose();$script:f.lock=$null
        [IO.File]::WriteAllText($statePath,$script:f.recoveredText,$utf8)
    }
}
function Get-ChildItem {return @()} # Bounds scans only the isolated empty fixture directories.
function Assert-FvrNoLinks([string]$Path){}
function ObserveSimulatorStartup($Sim){$script:f.observed++;$script:f.state=$Sim}
$fixture=Join-Path ([IO.Path]::GetTempPath()) ('fvr-state-read-'+[guid]::NewGuid().ToString('N'))
if([IO.Directory]::Exists($fixture)){throw 'Preserve existing fixture directory'}
$null=[IO.Directory]::CreateDirectory($fixture)
$statePath=Join-Path $fixture 'state.json'
$valid='{"pid":7,"runtime":"xr-sim","system":"fixture","headsetConfig":"fixture-profile","errors":0,"lastCmdError":"","captureSeq":0,"frame":10}'
$script:f=$null
function Reset([double]$Now=100){
    if($script:f -and $script:f.lock){$script:f.lock.Dispose()}
    $script:f=@{lock=$null;sleeps=0;releaseAt=0;oversleep=0.0;observed=0;state=$null;recoveredText=$valid.Replace('"frame":10','"frame":20');reads=0;injected=$null}
    [IO.File]::WriteAllText($statePath,$valid,$utf8)
    $script:clock=[pscustomobject]@{Elapsed=[pscustomobject]@{TotalSeconds=$Now};ElapsedMilliseconds=[long]($Now*1000)}
    $script:lastScan=-1000L;$script:OutputDir=$fixture;$script:folder=$null;$script:simDir=$fixture
    $script:plannedBytes=1MB;$script:hostStartedAt=90.0;$script:simObserved=$true;$script:stopReason=$null
    $script:owned=@{host=@{identity=@{pid=7}}};$script:TimeoutSeconds=420;$script:Seconds=300
    $script:systemName='fixture';$script:profilePath='fixture-profile';$script:CaptureCount=4;$script:startup=$null
}
function Lock-State {$script:f.lock=[IO.File]::Open($statePath,[IO.FileMode]::Open,[IO.FileAccess]::ReadWrite,[IO.FileShare]::None)}
function Attempt([scriptblock]$Action){$script:caught=$null;try{& $Action}catch{$script:caught=$_}}
function SharingFailure {return $script:caught -and $script:caught.Exception.GetBaseException() -is [IO.IOException] -and $script:caught.Exception.GetBaseException().HResult -eq -2147024864}
try{
    Reset;ObserveBounds
    Check ($f.observed -eq 1 -and $f.state.frame -eq 10 -and $f.sleeps -eq 0) 'uncontended current state observed normally'
    Reset;Lock-State;Attempt {$null=Json $statePath}
    Check ((SharingFailure) -and $f.sleeps -eq 4) 'generic reads retain original five-attempt refusal'
    Reset;Lock-State;$f.releaseAt=5;ObserveBounds
    Check ($f.observed -eq 1 -and $f.state.frame -eq 20 -and $f.sleeps -eq 5) 'real sharing violation beyond old limit recovers only newly read state'
    Reset;Lock-State;Attempt {ObserveBounds}
    Check ((SharingFailure) -and $f.sleeps -eq 24 -and $f.observed -eq 0) 'persistent sharing violation refuses after 25 reads / 240 ms sleep'
    Reset;Lock-State;$f.releaseAt=5;$f.recoveredText='{';Attempt {ObserveBounds}
    Check ($caught -and -not (SharingFailure) -and $f.sleeps -eq 5 -and $f.observed -eq 0) 'malformed recovered JSON still fails immediately'
    Reset;[IO.File]::Delete($statePath);Attempt {$null=Json $statePath -SharingDeadlineSeconds 100.25}
    Check ($caught -and $f.sleeps -eq 0) 'missing direct read still fails without retry'
    Reset;[IO.File]::Delete($statePath);ObserveBounds
    Check ($f.observed -eq 0 -and $f.sleeps -eq 0) 'missing polled state preserves existing absence behavior'
    Reset;[IO.File]::WriteAllText($statePath,('x'*131073),$utf8);Attempt {ObserveBounds}
    Check ($caught.Exception.Message -like 'JSON exceeds bound:*' -and $f.sleeps -eq 0) 'size bound remains immediate refusal'
    foreach($item in @(
        @{name='lock violation 33';exception=[IO.IOException]::new('fixture lock',-2147024863);reads=5},
        @{name='other I/O';exception=[IO.IOException]::new('fixture I/O',-2147024891);reads=1},
        @{name='access denied';exception=[UnauthorizedAccessException]::new('fixture denied');reads=1},
        @{name='non-I/O';exception=[InvalidOperationException]::new('fixture error');reads=1}
    )){
        Reset;$f.injected=$item.exception;Attempt {$null=JsonInjected $statePath -SharingDeadlineSeconds 100.25}
        Check ($caught -and $f.reads -eq $item.reads -and $f.sleeps -eq $item.reads-1) ($item.name+' keeps refusal/retry classification')
    }
    foreach($name in 'cleanup reserve','host duration','startup twenty seconds','handshake fifteen seconds'){
        Reset
        switch($name){
            'cleanup reserve' {$clock.Elapsed.TotalSeconds=359.985;$clock.ElapsedMilliseconds=359985;$hostStartedAt=100.0}
            'host duration' {$hostStartedAt=85.015;$Seconds=15}
            'startup twenty seconds' {$simObserved=$false;$hostStartedAt=80.015}
            'handshake fifteen seconds' {$simObserved=$false;$startup=@{sent_at_seconds=85.015}}
        }
        Lock-State;Attempt {ObserveBounds}
        Check ((SharingFailure) -and $f.sleeps -eq 1 -and $f.observed -eq 0) ($name+' stops retry inside original clock')
    }
    Reset;Lock-State;$f.releaseAt=1;$f.oversleep=1.0;Attempt {ObserveBounds}
    Check ((SharingFailure) -and $f.sleeps -eq 1 -and $f.observed -eq 0) 'scheduler oversleep cannot turn a late read into success'
    Reset;Lock-State;$clock.Elapsed.TotalSeconds=360.0;Attempt {ObserveBounds}
    Check ((SharingFailure) -and $f.sleeps -eq 0) 'expired cleanup allowance never sleeps'
    foreach($change in @(
        @{name='runtime identity';old='"runtime":"xr-sim"';new='"runtime":"other"'},
        @{name='system identity';old='"system":"fixture"';new='"system":"other"'},
        @{name='profile identity';old='"headsetConfig":"fixture-profile"';new='"headsetConfig":"other"'},
        @{name='runtime error';old='"errors":0';new='"errors":1'},
        @{name='command error';old='"lastCmdError":""';new='"lastCmdError":"bad"'},
        @{name='capture bound';old='"captureSeq":0';new='"captureSeq":5'},
        @{name='missing identity field';old='"runtime":"xr-sim",';new=''}
    )){
        Reset;Lock-State;$f.releaseAt=5;$f.recoveredText=$valid.Replace($change.old,$change.new);Attempt {ObserveBounds}
        Check ($caught -and $f.sleeps -eq 5 -and $f.observed -eq 0) ($change.name+' still refuses recovered state')
    }
    Reset;Lock-State;$f.releaseAt=5;$f.recoveredText=$valid.Replace('"pid":7','"pid":8');ObserveBounds
    Check ($f.observed -eq 0 -and $f.sleeps -eq 5) 'foreign/preflight PID does not become actual-host evidence'
    Check ([IO.File]::ReadAllText($RunnerPath).Replace("`r`n","`n") -ceq $original) 'shared runner remains unchanged'
    Write-Output "XR game state-read checks passed: $script:checks; deterministic offline only; no game, subprocesses or shared source writes."
}finally{
    if($f -and $f.lock){$f.lock.Dispose()}
    [IO.File]::Delete($statePath)
    [IO.Directory]::Delete($fixture)
}
