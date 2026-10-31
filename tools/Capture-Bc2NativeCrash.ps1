#requires -Version 7.5
<# Root-only diagnostic companion to the normal game wrapper. Start this hidden
   after the current run publishes game-identity.json and reaches the menu. Wait
   for capture-ready.json BEFORE issuing Resume. This script sends no game input.
   Cached recorder: first/second-chance faults, original NOT_HANDLED disposition,
   30 seconds maximum, eight 64 KiB records, 256 modules, zero dump bytes.
   Raw stack bytes are evidence, not a symbolized or trustworthy unwound stack.
   -PrepareOnly verifies cached tooling/symbol pins without attaching or writing.
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$RepositoryRoot,
    [Parameter(Mandatory)][string]$RunDirectory,
    [Parameter(Mandatory)][string]$RuntimeReceipt,
    [Parameter(Mandatory)][ValidatePattern('^[a-fA-F0-9]{64}$')][string]$RuntimeReceiptSha256,
    [string]$WatcherReceipt,
    [ValidateRange(1,30)][int]$Seconds=30,
    [switch]$PrepareOnly
)
Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot 'StorageBudget.ps1')

function Assert-Capture([bool]$Condition,[string]$Message) {
    if(-not $Condition){throw $Message}
}
function Resolve-CapturePath([string]$Path) {
    Assert-Capture ([IO.Path]::IsPathFullyQualified($Path)) 'Absolute path required.'
    $full=[IO.Path]::GetFullPath($Path)
    for($part=$full;$part;$part=Split-Path -Parent $part){
        if(Test-Path -LiteralPath $part){
            $item=Get-Item -LiteralPath $part -Force
            if($item.Attributes -band [IO.FileAttributes]::ReparsePoint){
                Assert-Capture (-not $item.PSIsContainer) "Linked capture directory refused: $part"
                # Reuse storage's OPEN_REPARSE_POINT tag inspection. It admits
                # only ordinary WOF compression (0x80000017), never a redirect.
                Initialize-FvrStorageNative
                $null=[FvrStorage.NativeSize]::Get($part,$true)
            }
        }
    }
    return $full
}
function Read-CaptureJson([string]$Path,[long]$Limit=65536) {
    $item=Get-Item -LiteralPath (Resolve-CapturePath $Path)
    Assert-Capture (-not $item.PSIsContainer -and $item.Length -le $Limit) "Oversized/non-file JSON: $Path"
    return [IO.File]::ReadAllText($item.FullName)|ConvertFrom-Json -AsHashtable -DateKind String
}
function Assert-CapturePin($Pin) {
    $item=Get-Item -LiteralPath (Resolve-CapturePath $Pin.path)
    Assert-Capture (-not $item.PSIsContainer -and $item.Length -eq $Pin.bytes) "Pinned file size differs: $($Pin.path)"
    Assert-Capture ((Get-FileHash -LiteralPath $item.FullName -Algorithm SHA256).Hash -ieq $Pin.sha256) "Pinned hash differs: $($Pin.path)"
}
function Assert-CaptureIdentity($Actual,$Expected) {
    Assert-Capture ([long]$Expected.pid -gt 0 -and [long]$Actual.pid -eq [long]$Expected.pid) 'Game PID differs.'
    $actualTime=[DateTimeOffset]::Parse($Actual.start_utc).UtcTicks
    $expectedTime=[DateTimeOffset]::Parse($Expected.start_utc).UtcTicks
    Assert-Capture ($actualTime -eq $expectedTime) 'Game creation time differs (PID reuse refused).'
    Assert-Capture ([IO.Path]::IsPathFullyQualified($Expected.path) -and [IO.Path]::GetFileName($Expected.path) -ieq 'BFBC2Game.exe') 'Expected exact BC2 executable path.'
    Assert-Capture ($Actual.path -ieq $Expected.path) 'Game executable path differs.'
}
function Get-CaptureIdentity([Diagnostics.Process]$Process) {
    $Process.Refresh()
    Assert-Capture (-not $Process.HasExited) 'Exact game process already exited.'
    return @{pid=$Process.Id;start_utc=$Process.StartTime.ToUniversalTime().ToString('o');path=$Process.Path}
}
function Convert-CaptureException($Record,[long]$GamePid) {
    Assert-Capture ([long]$Record.pid -eq $GamePid) 'Exception belongs to another process.'
    Assert-Capture ([long]$Record.thread -gt 0 -and $Record.first_chance -in @(0,1)) 'Invalid exception thread/chance.'
    Assert-Capture ($Record.json_only -eq $true -and $Record.minidump_written -eq $false) 'Unexpected capture format.'
    Assert-Capture (@($Record.modules).Count -le 256 -and @($Record.memory).Count -le 8) 'Capture structure exceeds pinned limits.'
    $module=@($Record.modules|Where-Object {[long]$Record.address -ge [long]$_.base -and [long]$Record.address -lt ([long]$_.base+[long]$_.size)})
    return @{thread=$Record.thread;first_chance=$Record.first_chance;code=('0x{0:x8}' -f [long]$Record.code);
        address=('0x{0:x8}' -f [long]$Record.address);parameters=$Record.parameters;context_valid=$Record.context_valid;
        module=$(if($module.Count -eq 1){@{name=$module[0].name;rva=('0x{0:x}' -f ([long]$Record.address-[long]$module[0].base))}}else{$null});
        raw_stack_present=(@($Record.memory|Where-Object {$_.address -eq $Record.registers.esp -and $_.hex.Length -gt 0}).Count -gt 0);
        unwound_stack=$false;modules_truncated=$Record.modules_truncated}
}
function Save-CaptureJson([string]$Name,$Value) {
    $text=$Value|ConvertTo-Json -Depth 12
    Assert-Capture ([Text.Encoding]::UTF8.GetByteCount($text) -le 1MB) 'Capture metadata exceeds limit.'
    [IO.File]::WriteAllText((Join-Path $output $Name),$text,[Text.UTF8Encoding]::new($false))
}

$RepositoryRoot=Resolve-CapturePath $RepositoryRoot
$RunDirectory=Resolve-CapturePath $RunDirectory
$RuntimeReceipt=Resolve-CapturePath $RuntimeReceipt
Assert-Capture ($RunDirectory.StartsWith((Join-Path $RepositoryRoot 'reports')+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase)) 'Run must belong to this repository reports directory.'
if(-not $WatcherReceipt){$WatcherReceipt=Join-Path (Split-Path -Parent $RepositoryRoot) 'bc2vr-recovery/performance436/death-retry-r13/watcher-build-r2/build-receipt.json'}
$WatcherReceipt=Resolve-CapturePath $WatcherReceipt
Assert-Capture ((Get-FileHash -LiteralPath $WatcherReceipt -Algorithm SHA256).Hash -ieq '77f8301d451f44e76844129b12db23cb9ab1e1fc5e6ad6bf74ccf69b0bd10392') 'Unreviewed watcher receipt refused.'
$watch=Read-CaptureJson $WatcherReceipt
Assert-Capture ($watch.schema -ceq 'fvr.exception-json436.build.v1' -and $watch.self_test_passed -and -not $watch.minidumps -and $watch.maximum_records -eq 8 -and $watch.maximum_record_bytes -eq 65536 -and $watch.maximum_modules -eq 256) 'Watcher contract differs.'
foreach($pin in @($watch.binary,$watch.source,$watch.recipe,$watch.self_test,$watch.plan)){Assert-CapturePin $pin}
$selfTest=Read-CaptureJson $watch.self_test.path
Assert-Capture ($selfTest.passed -and $selfTest.live_detach -and $selfTest.original_seh_continued -and $selfTest.oversized_rejected_before_file_creation -and -not $selfTest.minidumps) 'Watcher preservation self-test differs.'
Assert-CapturePin $selfTest.record
Assert-Capture ((Get-FileHash -LiteralPath $RuntimeReceipt -Algorithm SHA256).Hash -ieq $RuntimeReceiptSha256) 'Runtime receipt hash differs.'
$runtime=Read-CaptureJson $RuntimeReceipt
Assert-CapturePin $runtime.binaries.probe
Assert-CapturePin $runtime.symbols_by_binary.probe
$output=Join-Path $RunDirectory 'native-crash-capture'
Assert-Capture (-not (Test-Path -LiteralPath $output)) 'Fresh capture directory required; preserve earlier evidence.'
$plan=@{schema='fvr.native-crash-capture.v1';status='prepared';watcher=$watch.binary;watcher_source=$watch.source;
    runtime_receipt=$RuntimeReceipt;runtime_receipt_sha256=$RuntimeReceiptSha256;probe=$runtime.binaries.probe;pdb=$runtime.symbols_by_binary.probe;
    seconds=$Seconds;maximum_exception_records=8;maximum_record_bytes=65536;maximum_modules=256;maximum_dump_bytes=0;
    first_chance_observation=$true;original_exception_not_handled=$true;performance_measurement=$false;unwound_stack=$false;output=$output}
if($PrepareOnly){$plan|ConvertTo-Json -Depth 8;return}

# Storage admission precedes output creation and debugger launch. No shared receipt bypass.
$storage=Assert-FvrStorageBudget -RepositoryRoot $RepositoryRoot -OutputPath $output -Operation Capture -RawCaptureBytes 0 -PlannedBytes 66MB
$expected=Read-CaptureJson (Join-Path $RunDirectory 'game-identity.json') 4096
$game=[Diagnostics.Process]::GetProcessById([int]$expected.pid)
$watcher=$null;$started=$false;$ready=$false
try {
    # Retain the original process handle across verification, launch and detach;
    # an exited process cannot be silently replaced by a same-name/PID target.
    $retainedHandle=$game.Handle
    Assert-CaptureIdentity (Get-CaptureIdentity $game) $expected
    $module=@($game.Modules|Where-Object {$_.ModuleName -ieq 'BC2NativeProbe.dll'})
    Assert-Capture ($module.Count -eq 1) 'Expected one loaded native probe; attach after menu readiness.'
    Assert-Capture ((Get-FileHash -LiteralPath $module[0].FileName -Algorithm SHA256).Hash -ieq $runtime.binaries.probe.sha256) 'Loaded probe file differs from pinned runtime.'
    $null=[IO.Directory]::CreateDirectory($output)
    Save-CaptureJson 'storage.json' $storage
    $plan.game=$expected;$plan.loaded_probe_path=$module[0].FileName
    Save-CaptureJson 'selection.json' $plan
    $info=[Diagnostics.ProcessStartInfo]::new($watch.binary.path)
    $info.UseShellExecute=$false;$info.CreateNoWindow=$true
    $info.RedirectStandardOutput=$true;$info.RedirectStandardError=$true
    foreach($arg in @('--pid',[string]$expected.pid,'--seconds',[string]$Seconds,'--output',(Join-Path $output 'exceptions'))){$info.ArgumentList.Add($arg)}
    Assert-CaptureIdentity (Get-CaptureIdentity $game) $expected
    $watcher=[Diagnostics.Process]::Start($info);$started=$true
    $stdout=$watcher.StandardOutput.ReadToEndAsync();$stderr=$watcher.StandardError.ReadToEndAsync()
    $watcherIdentity=Get-CaptureIdentity $watcher
    Save-CaptureJson 'watcher-process.json' $watcherIdentity
    $clock=[Diagnostics.Stopwatch]::StartNew()
    while($clock.Elapsed.TotalSeconds -lt ($Seconds+5)){
        $watcher.Refresh()
        if($watcher.HasExited){break}
        if(-not $ready -and (Test-Path -LiteralPath (Join-Path $output 'exceptions/ready.json'))){
            $marker=Read-CaptureJson (Join-Path $output 'exceptions/ready.json') 4096
            Assert-Capture ($marker.pid -eq $expected.pid -and $marker.attached) 'Debugger ready identity differs.'
            Assert-CaptureIdentity (Get-CaptureIdentity $game) $expected
            $ready=$true
            Save-CaptureJson 'capture-ready.json' @{game=$expected;watcher=$watcherIdentity;attached=$true;seconds=$Seconds;remaining_seconds=[Math]::Max(0,$Seconds-$clock.Elapsed.TotalSeconds);ready_utc=[DateTime]::UtcNow.ToString('o')}
        }
        Assert-Capture ($ready -or $clock.Elapsed.TotalSeconds -le 5) 'Debugger not ready within five seconds; no Resume authorized.'
        Start-Sleep -Milliseconds 25
    }
    Assert-Capture $watcher.HasExited 'Watcher exceeded bounded wait; root must inspect exact watcher identity. No process was forcibly terminated.'
    $stdOutText=$stdout.GetAwaiter().GetResult();$stdErrText=$stderr.GetAwaiter().GetResult()
    Assert-Capture ($stdOutText.Length -le 65536 -and $stdErrText.Length -le 65536) 'Unexpected recorder log size.'
    Save-CaptureJson 'watcher-logs.json' @{stdout=$stdOutText;stderr=$stdErrText;exit_code=$watcher.ExitCode}
    Assert-Capture ($watcher.ExitCode -eq 0) 'Exception recorder failed; inspect watcher logs.'
    $completion=Read-CaptureJson (Join-Path $output 'exceptions/completion.json') 4096
    Assert-Capture ($completion.detached -eq $true) 'Debugger detach unproved.'
    $files=@(Get-ChildItem -LiteralPath (Join-Path $output 'exceptions') -File)
    Assert-Capture (@($files|Where-Object {$_.Name -notmatch '^(exception-[0-7]\.json|ready\.json|completion\.json|exit\.json|capture-error\.txt)$'}).Count -eq 0) 'Unexpected capture artifact.'
    Assert-Capture (@($files|Where-Object {$_.Length -gt 65536}).Count -eq 0) 'Capture file exceeds bound.'
    Assert-Capture (-not (Test-Path -LiteralPath (Join-Path $output 'exceptions/capture-error.txt'))) 'Recorder reported an incomplete capture.'
    $records=@($files|Where-Object {$_.Name -match '^exception-[0-7]\.json$'}|Sort-Object Name)
    Assert-Capture ($records.Count -eq $completion.exceptions -and $records.Count -le 8) 'Capture count differs.'
    $summary=@($records|ForEach-Object {Convert-CaptureException (Read-CaptureJson $_.FullName) $expected.pid})
    $status=if(@($summary|Where-Object {$_.code -eq '0xc0000005'}).Count){'av_recorded'}else{'no_av_recorded'}
    Save-CaptureJson 'result.json' @{schema=$plan.schema;status=$status;game=$expected;watcher=$watcherIdentity;completion=$completion;records=$summary;record_limit_reached=($records.Count -eq 8);ready_observed=$ready;native_drain_proved=$false;unwound_stack=$false;performance_measurement=$false}
    Write-Output "$status : $output"
} catch {
    if(Test-Path -LiteralPath $output){Save-CaptureJson 'failure.json' @{status='failed';error=$_.Exception.Message;watcher_started=$started;ready_observed=$ready;forced_termination=$false;root_attention_required=$started;native_drain_proved=$false}}
    throw
} finally {
    # Do not kill the game or watcher and do not swallow a pending exception.
    # The pinned recorder owns normal continuation and timed detach. A failed
    # readiness/timeout never authorizes Resume or another capture attempt.
    if($watcher){$watcher.Dispose()}
    $game.Dispose()
}
