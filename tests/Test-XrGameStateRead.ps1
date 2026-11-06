#requires -Version 7.5
param([string]$RunnerPath=(Join-Path $PSScriptRoot '../tools/Invoke-Bc2XrGameSimulation.ps1'))
Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'
if(-not $IsWindows){throw 'Windows file sharing regression requires Windows'}
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
foreach($name in 'Require','ReadSimulatorStateText','Json','ObserveBounds'){. ([scriptblock]::Create((FunctionText $ast $name)))}
# Only exception classification uses injection; sharing tests use actual Windows handles.
$injected=(FunctionText $ast 'Json').Replace('function Json(','function JsonInjected(').Replace('[IO.File]::ReadAllText($Path)','(Read-Injected $Path)').Replace('ReadSimulatorStateText $Path $Maximum','Read-Injected $Path')
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
$temporaryState=$statePath+'.tmp'
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
$deleteOwner=$null;$heldReader=$null
try{
    Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
using Microsoft.Win32.SafeHandles;
public static class FvrStateReadTestIo {
    [DllImport("kernel32.dll", CharSet=CharSet.Unicode, SetLastError=true)]
    public static extern SafeFileHandle CreateFileW(string path, uint access, uint share, IntPtr security, uint creation, uint flags, IntPtr template);
    [DllImport("kernel32.dll", CharSet=CharSet.Unicode, SetLastError=true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    public static extern bool MoveFileExW(string source, string destination, uint flags);
}
'@
    Reset
    # Match the DELETE access used by the atomic publisher. This owner stays
    # open through both reads, so retrying cannot explain the successful read.
    $deleteOwner=[FvrStateReadTestIo]::CreateFileW($statePath,0x10000,7,[IntPtr]::Zero,3,0x80,[IntPtr]::Zero)
    Check (-not $deleteOwner.IsInvalid) 'real DELETE-access publisher handle opened'
    $ioClock=[Diagnostics.Stopwatch]::StartNew()
    Attempt {$null=[IO.File]::ReadAllText($statePath)}
    Check (SharingFailure) 'legacy ReadAllText refuses the open DELETE owner with real Windows sharing error 32'
    $contract=@{legacy_hresult=$caught.Exception.GetBaseException().HResult;legacy_seconds=$ioClock.Elapsed.TotalSeconds}
    $clock=$ioClock
    $read=Json $statePath -SharingDeadlineSeconds ($clock.Elapsed.TotalSeconds+0.25)
    $contract.fixed_seconds=$ioClock.Elapsed.TotalSeconds
    Check ($read.frame -eq 10 -and $f.sleeps -eq 0 -and -not $deleteOwner.IsClosed) 'actual live reader reads complete state while DELETE owner remains open without retry'
    $script:clock=[pscustomobject]@{Elapsed=[pscustomobject]@{TotalSeconds=100.0};ElapsedMilliseconds=100000L}
    Attempt {$null=Json $statePath}
    Check (SharingFailure) 'generic reader retains its sharing policy'
    $deleteOwner.Dispose();$deleteOwner=$null
    Reset
    # Use the producer's exact API, keeping its failed-replacement possibility
    # explicit: sharing DELETE does not guarantee replacement of an open target.
    [IO.File]::WriteAllText($temporaryState,$f.recoveredText,$utf8)
    $heldReader=[IO.File]::Open($statePath,[IO.FileMode]::Open,[IO.FileAccess]::Read,([IO.FileShare]::Read -bor [IO.FileShare]::Delete))
    $replaced=[FvrStateReadTestIo]::MoveFileExW($temporaryState,$statePath,1)
    $replaceError=if($replaced){0}else{[Runtime.InteropServices.Marshal]::GetLastWin32Error()}
    $contract.replace_while_open=$replaced;$contract.replace_win32_error=$replaceError
    $reader=[IO.StreamReader]::new($heldReader);$snapshot=$reader.ReadToEnd();$reader.Dispose();$heldReader=$null
    Check ($snapshot -ceq $valid) 'opened snapshot remains complete through actual replacement attempt'
    if(-not $replaced){
        Check ($replaceError -in @(5,32) -and [IO.File]::ReadAllText($statePath) -ceq $valid) 'failed replacement preserves original published state'
        $replaced=[FvrStateReadTestIo]::MoveFileExW($temporaryState,$statePath,1)
    }
    Check ($replaced -and (Json $statePath -SharingDeadlineSeconds 100.25).frame -eq 20) 'closed-reader replacement succeeds and next read observes newly published state'
    Write-Output ('Windows state sharing contract: '+($contract|ConvertTo-Json -Compress))
    Reset;ObserveBounds
    Check ($f.observed -eq 1 -and $f.state.frame -eq 10 -and $f.sleeps -eq 0) 'uncontended current state observed normally'
    Reset;Lock-State;Attempt {$null=Json $statePath}
    Check ((SharingFailure) -and $f.sleeps -eq 4) 'generic reads retain original five-attempt refusal'
    Reset;Lock-State;$f.releaseAt=5;ObserveBounds
    Check ($f.observed -eq 1 -and $f.state.frame -eq 20 -and $f.sleeps -eq 5) 'real sharing violation beyond old limit recovers only newly read state'
    Reset;Lock-State;Attempt {ObserveBounds}
    Check ((SharingFailure) -and $f.sleeps -eq 24 -and $f.observed -eq 0) 'persistent sharing violation refuses after 25 reads / 240 ms sleep'
    $detail=$caught.Exception.Data['fvr_json_read']
    Check ($detail.attempts -eq 25 -and $detail.started_seconds -eq 100 -and $detail.deadline_seconds -eq 100.25 -and [Math]::Abs($detail.failed_seconds-100.24) -lt 0.00001 -and $detail.hresult -eq -2147024864 -and $detail.stop_reason -ceq 'attempt_limit') 'failure retains exact attempt count, original clock, deadline and Windows exception'
    Reset;$f.lock=[IO.File]::Open($statePath,[IO.FileMode]::Open,[IO.FileAccess]::ReadWrite,([IO.FileShare]::ReadWrite -bor [IO.FileShare]::Delete));Attempt {ObserveBounds}
    Check ((SharingFailure) -and $f.observed -eq 0) 'live reader still denies an in-place writer rather than reading partial JSON'
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
    $detail=$caught.Exception.Data['fvr_json_read']
    Check ($detail.attempts -eq 1 -and $detail.deadline_seconds -eq 100.25 -and [Math]::Abs($detail.stopped_seconds-101.01) -lt 0.00001 -and $detail.stop_reason -ceq 'deadline_after_sleep') 'oversleep diagnostic preserves original deadline and actual stop clock'
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
    if($deleteOwner){$deleteOwner.Dispose()};if($heldReader){$heldReader.Dispose()}
    if($f -and $f.lock){$f.lock.Dispose()}
    if([IO.File]::Exists($temporaryState)){[IO.File]::Delete($temporaryState)}
    [IO.File]::Delete($statePath)
    [IO.Directory]::Delete($fixture)
}
