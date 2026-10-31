#requires -Version 7.2
<#
Host-only OpenXR diagnostic. Contracts: xr-sim ca14f5b1c549cf49dca49233250b8f3cdad013f4
and xr-tape 419c30d64d72ff9a41f00701ffe39c3c94bf3ac3. See xr-sim/README.txt.
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$OutputDirectory,
    [string]$HostPath,
    [string]$LoaderPath,
    [string]$RuntimeManifest,
    [string]$TapeManifestDirectory,
    [string]$TapeLayerName,
    [string]$TapeChecker,
    [string]$Python,
    [string]$LocalConfig,
    [ValidateRange(8,30)][int]$Seconds=12,
    [ValidateRange(15,60)][int]$TimeoutSeconds=35
)
Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'
$repo=[IO.Path]::GetFullPath((Split-Path -Parent $PSScriptRoot))
$utf8=[Text.UTF8Encoding]::new($false)
$savedTemporary=@{}
foreach($name in @('TEMP','TMP','TMPDIR','PYTHONDONTWRITEBYTECODE')){
    $savedTemporary[$name]=[Environment]::GetEnvironmentVariable($name,'Process')
}
$owned=[Collections.Generic.List[object]]::new()
$failures=[Collections.Generic.List[string]]::new()
$commands=[Collections.Generic.List[object]]::new()
$clock=[Diagnostics.Stopwatch]::new()
$lastScan=-1000L
$admitted=$false
$outputReady=$false
$safeRefusalPath=$null
$hostRun=$null
$receipt=[ordered]@{
    schema='fvr.xr-simulation.v1';status='failed';evidence_level='host_only_simulated_runtime';
    started_utc=[DateTime]::UtcNow.ToString('o');actual_game_checked=$false;headset_accepted=$false;
    source_contract=@{xr_sim='ca14f5b1c549cf49dca49233250b8f3cdad013f4';xr_tape='419c30d64d72ff9a41f00701ffe39c3c94bf3ac3'};
    limits=@{seconds=$Seconds;deadline_seconds=$TimeoutSeconds;planned_bytes=128MB;capture_frames=1;eye_width=320;eye_height=240;tape_max_frames=6000};
    commands=$commands;failures=$failures;cleanup=@()
}
function Write-Json([string]$Path,$Value){
    [IO.File]::WriteAllText($Path,($Value|ConvertTo-Json -Depth 30),$utf8)
}
function Require([bool]$Condition,[string]$Message){if(-not $Condition){throw $Message}}
function Gate([bool]$Condition,[string]$Message){if(-not $Condition){$failures.Add($Message)}}
function Full-Path([string]$Path){
    Require ([IO.Path]::IsPathFullyQualified($Path)) "An absolute path is required: $Path"
    $full=[IO.Path]::GetFullPath($Path)
    $part=$full
    while($part){
        if([IO.File]::Exists($part) -or [IO.Directory]::Exists($part)){
            if(([IO.File]::GetAttributes($part) -band [IO.FileAttributes]::ReparsePoint) -ne 0){
                Require ([IO.File]::Exists($part) -and -not [IO.Directory]::Exists($part)) "Linked directory refused: $part"
                # Existing storage policy verifies IO_REPARSE_TAG_WOF; all other file links fail.
                $null=Get-FvrFileAllocatedBytes -File ([IO.FileInfo]::new($part))
            }
        }
        $part=[IO.Path]::GetDirectoryName($part)
    }
    return $full
}
function Existing-File([string]$Path){
    $full=Full-Path $Path
    Require ([IO.File]::Exists($full)) "Required file is missing: $full"
    return $full
}
function Read-Json([string]$Path){return [IO.File]::ReadAllText($Path)|ConvertFrom-Json}
function Pe-Machine([string]$Path){
    $stream=[IO.File]::OpenRead($Path)
    try{
        $reader=[IO.BinaryReader]::new($stream)
        Require ($reader.ReadUInt16() -eq 0x5a4d) "Not a PE image: $Path"
        $stream.Position=0x3c;$offset=$reader.ReadInt32()
        Require ($offset -ge 64 -and $offset -lt $stream.Length-6) "Invalid PE offset: $Path"
        $stream.Position=$offset
        Require ($reader.ReadUInt32() -eq 0x00004550) "Invalid PE signature: $Path"
        return $reader.ReadUInt16()
    }finally{$stream.Dispose()}
}
function Manifest-Library([string]$Manifest,[string]$Library){
    Require (-not [string]::IsNullOrWhiteSpace($Library)) "Manifest has no library_path: $Manifest"
    if(-not [IO.Path]::IsPathFullyQualified($Library)){$Library=Join-Path ([IO.Path]::GetDirectoryName($Manifest)) $Library}
    return Existing-File $Library
}
function Check-Bounds {
    Require ($clock.Elapsed.TotalSeconds -lt $TimeoutSeconds) "Total watchdog deadline exceeded ($TimeoutSeconds seconds)."
    if($clock.ElapsedMilliseconds-$script:lastScan -ge 250){
        $script:lastScan=$clock.ElapsedMilliseconds
        $bytes=0L
        foreach($file in Get-ChildItem -LiteralPath $OutputDirectory -File -Recurse -Force){
            Require (($file.Attributes -band [IO.FileAttributes]::ReparsePoint) -eq 0) "Linked output refused: $($file.FullName)"
            $bytes+=$file.Length
        }
        Require ($bytes -le 128MB) "Output crossed the 128 MiB watchdog cap ($bytes bytes)."
    }
}
function Start-Owned([string]$Executable,[string[]]$Arguments,[string]$Name,[hashtable]$Environment){
    $psi=[Diagnostics.ProcessStartInfo]::new()
    $psi.FileName=$Executable;$psi.WorkingDirectory=$OutputDirectory
    $psi.UseShellExecute=$false;$psi.CreateNoWindow=$true;$psi.WindowStyle=[Diagnostics.ProcessWindowStyle]::Hidden
    $psi.RedirectStandardOutput=$true;$psi.RedirectStandardError=$true
    foreach($arg in $Arguments){$psi.ArgumentList.Add($arg)}
    # Do not inherit an unrelated test's runtime, profile, recorder, or private IPC switches.
    foreach($key in @($psi.Environment.Keys)){
        if($key -match '^(XR_|XRSIM_|XRTAPE_|BVR_XRSIM_|FVR_PRIVATE_)'){$null=$psi.Environment.Remove($key)}
    }
    foreach($key in $Environment.Keys){$psi.Environment[$key]=[string]$Environment[$key]}
    foreach($key in @('TEMP','TMP','TMPDIR')){$psi.Environment[$key]=$scratch}
    $psi.Environment['PYTHONDONTWRITEBYTECODE']='1'
    $process=[Diagnostics.Process]::new();$process.StartInfo=$psi
    $entry=[pscustomobject]@{name=$Name;process=$process;started=$false;pid=0;start_ticks=0L;exe=$Executable;forced=$false;exit_code=$null;streams=@();copies=@()}
    $owned.Add($entry)
    Require ($process.Start()) "Could not start $Name"
    $entry.started=$true;$entry.pid=$process.Id;$entry.start_ticks=$process.StartTime.ToUniversalTime().Ticks
    foreach($kind in @('stdout','stderr')){
        $path=Join-Path $OutputDirectory "$Name.$kind.txt"
        $stream=[IO.FileStream]::new($path,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::ReadWrite)
        $entry.streams+=,$stream
        $source=if($kind -eq 'stdout'){$process.StandardOutput.BaseStream}else{$process.StandardError.BaseStream}
        $entry.copies+=,$source.CopyToAsync($stream)
    }
    return $entry
}
function Finish-Owned($Entry,[bool]$Force){
    if(-not $Entry.started){return}
    if(-not $Entry.process.HasExited -and $Force){
        # This retained Process handle identifies only this invocation; never kill by name/tree.
        Require ($Entry.process.StartTime.ToUniversalTime().Ticks -eq $Entry.start_ticks) 'Owned process identity changed.'
        $Entry.process.Kill();$Entry.forced=$true
        Require ($Entry.process.WaitForExit(3000)) "Owned process $($Entry.pid) did not exit after termination."
    }
    if($Entry.process.HasExited){
        $Entry.exit_code=$Entry.process.ExitCode
        foreach($copy in $Entry.copies){Require ($copy.Wait(3000)) 'Timed out draining process output.'}
        foreach($stream in $Entry.streams){$stream.Flush();$stream.Dispose()}
        $Entry.streams=@();$Entry.copies=@()
    }
}
function Wait-Sim([scriptblock]$Condition,[string]$Description,[int]$WaitSeconds=5){
    $until=$clock.Elapsed.TotalSeconds+$WaitSeconds
    do{
        Check-Bounds
        Require (-not $hostRun.process.HasExited) "Host exited while waiting for $Description"
        if([IO.File]::Exists($statePath)){
            $state=$null
            try{$state=Read-Json $statePath}catch [IO.IOException]{}
            if($null -ne $state){
                Require ($state.pid -eq $hostRun.pid) 'Simulator state PID does not match the owned host.'
                Require ($state.runtime -eq 'xr-sim') "Unexpected simulator runtime: $($state.runtime)"
                Require ([int]$state.errors -eq 0) "Simulator reported $($state.errors) errors: $($state.lastCmdError)"
                if(& $Condition $state){return $state}
            }
        }
        Start-Sleep -Milliseconds 25
    }while($clock.Elapsed.TotalSeconds -lt $until)
    throw "Timed out waiting for $Description"
}
function Send-Commands([string]$Name,[string[]]$Lines,[scriptblock]$Predicate){
    $before=Read-Json $statePath
    $target=[long]$before.cmdSeq+$Lines.Count
    # The official watcher keys on mtime; keep writes at least one 16 ms tick apart.
    Start-Sleep -Milliseconds 25
    $temporary=Join-Path $simDir 'command.txt.tmp'
    [IO.File]::WriteAllText($temporary,($Lines -join "`n")+"`n",$utf8)
    [IO.File]::Move($temporary,(Join-Path $simDir 'command.txt'),$true)
    $ack=Wait-Sim {param($s) $s.cmdSeq -ge $target} "$Name command acknowledgement"
    Require ($ack.cmdSeq -eq $target) 'Unexpected command sequence; another writer may own this simulator.'
    Require ([string]::IsNullOrEmpty($ack.lastCmdError)) "$Name command failed: $($ack.lastCmdError)"
    $settled=Wait-Sim {param($s) $s.frame -ge ([long]$ack.frame+2) -and (& $Predicate $s)} "$Name committed state"
    $row=[ordered]@{name=$Name;lines=$Lines;before_sequence=$before.cmdSeq;ack_sequence=$ack.cmdSeq;ack_frame=$ack.frame;state=$settled;ack_text=[IO.File]::ReadAllText((Join-Path $simDir 'ack.txt'))}
    $commands.Add($row)
    Write-Json (Join-Path $OutputDirectory 'commands.json') $commands
    return $settled
}
try{
    Require $IsWindows 'This host diagnostic requires Windows and PowerShell 7.2+.'
    . (Join-Path $PSScriptRoot 'StorageBudget.ps1')
    # The same minimal bootstrap used by admission keeps its WOF helper compilation on G:.
    $null=Set-FvrProjectTemporaryStorage -RepositoryRoot $repo
    Require ($TimeoutSeconds -ge $Seconds+10) 'TimeoutSeconds must reserve at least 10 seconds beyond the host duration.'
    $OutputDirectory=Full-Path $OutputDirectory
    $reportRoot=(Full-Path (Join-Path $repo 'reports')).TrimEnd('\')+'\'
    Require ($OutputDirectory.StartsWith($reportRoot,[StringComparison]::OrdinalIgnoreCase)) 'OutputDirectory must be a fresh child of this repository reports directory.'
    Require ([IO.Path]::GetPathRoot($OutputDirectory) -eq 'G:\') 'Local development captures must remain on G:.'
    $safeRefusalPath="$OutputDirectory.refused.json"
    Require (-not [IO.Directory]::Exists($OutputDirectory) -and -not [IO.File]::Exists($OutputDirectory)) 'OutputDirectory already exists; prior episodes must be preserved.'
    Require (-not [IO.File]::Exists("$OutputDirectory.refused.json")) 'A refusal receipt already exists for this output name; use a new run name.'
    $identity=[Security.Principal.WindowsIdentity]::GetCurrent()
    try{$principal=[Security.Principal.WindowsPrincipal]::new($identity)
        Require (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) 'Actual elevated Windows token refused: secure OpenXR environment overrides can be ignored. Use a normal token.'
    }finally{$identity.Dispose()}
    if(-not $LocalConfig){$LocalConfig=Join-Path $repo 'build/toolkit/xr-tools.local.json'}
    $config=@{}
    if([IO.File]::Exists($LocalConfig)){$config=[IO.File]::ReadAllText((Existing-File $LocalConfig))|ConvertFrom-Json -AsHashtable}
    foreach($name in @('HostPath','LoaderPath','RuntimeManifest','TapeManifestDirectory','TapeLayerName','TapeChecker','Python')){
        $value=Get-Variable -Name $name -ValueOnly
        if([string]::IsNullOrWhiteSpace($value) -and $config.ContainsKey($name)){Set-Variable -Name $name -Value ([string]$config[$name]);$value=$config[$name]}
        Require (-not [string]::IsNullOrWhiteSpace($value)) "Supply -$name or its explicit local-config entry."
    }
    $HostPath=Existing-File $HostPath;$LoaderPath=Existing-File $LoaderPath
    $RuntimeManifest=Existing-File $RuntimeManifest;$TapeChecker=Existing-File $TapeChecker;$Python=Existing-File $Python
    Require ([IO.Path]::GetFileName($HostPath) -eq 'BC2XrHost.exe') 'Only BC2XrHost.exe is admitted by this host-only driver.'
    Require ($TapeLayerName -eq 'XR_APILAYER_XRTAPE_recorder') 'TapeLayerName does not match the pinned recorder contract.'
    $TapeManifestDirectory=Full-Path $TapeManifestDirectory
    Require ([IO.Directory]::Exists($TapeManifestDirectory)) 'TapeManifestDirectory is missing.'
    $matches=@(foreach($file in Get-ChildItem -LiteralPath $TapeManifestDirectory -Filter '*.json' -File){
        $manifest=Read-Json $file.FullName
        if($manifest.PSObject.Properties.Name -contains 'api_layer' -and $manifest.api_layer.name -eq $TapeLayerName){$file.FullName}
    })
    Require ($matches.Count -eq 1) 'Exactly one matching tape API-layer manifest is required.'
    $tapeManifest=$matches[0];$tapeLibrary=Manifest-Library $tapeManifest (Read-Json $tapeManifest).api_layer.library_path
    $runtimeLibrary=Manifest-Library $RuntimeManifest (Read-Json $RuntimeManifest).runtime.library_path
    foreach($path in @($HostPath,$LoaderPath,$runtimeLibrary,$tapeLibrary)){
        Require ((Pe-Machine $path) -eq 0x8664) "Host, loader, runtime and tape must all be x64: $path"
    }
    $receipt.identities=@(foreach($path in @($HostPath,$LoaderPath,$RuntimeManifest,$runtimeLibrary,$tapeManifest,$tapeLibrary,$TapeChecker,$Python,$PSCommandPath)){
        @{path=$path;bytes=(Get-Item -LiteralPath $path).Length;sha256=(Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant()}
    })
    # One composition: two 320x240 BGRA eyes and the encoder's mandatory 640x240 SBS copy.
    $storage=Assert-FvrStorageBudget -RepositoryRoot $repo -OutputPath $OutputDirectory -Operation Capture -PlannedBytes 128MB -RawCaptureBytes (320*240*4*4+64KB)
    $admitted=$true
    $null=[IO.Directory]::CreateDirectory($OutputDirectory);$outputReady=$true
    Write-Json (Join-Path $OutputDirectory 'storage.json') $storage
    $simDir=Join-Path $OutputDirectory 'sim';$tapeDir=Join-Path $OutputDirectory 'tape';$scratch=Join-Path $OutputDirectory 'temp'
    foreach($dir in @($simDir,$tapeDir,$scratch)){$null=[IO.Directory]::CreateDirectory($dir)}
    $statePath=Join-Path $simDir 'state.json';$readyPath=Join-Path $OutputDirectory 'ready.json'
    $profilePath=Join-Path $simDir 'headset.json';$systemName='BC2XR host-only 320x240'
    $views=@(
        @{recommendedImageRectWidth=320;recommendedImageRectHeight=240;maxImageRectWidth=320;maxImageRectHeight=240;fov=@{angleLeft=-0.95;angleRight=0.75;angleUp=0.85;angleDown=-0.85}},
        @{recommendedImageRectWidth=320;recommendedImageRectHeight=240;maxImageRectWidth=320;maxImageRectHeight=240;fov=@{angleLeft=-0.75;angleRight=0.95;angleUp=0.85;angleDown=-0.85}}
    )
    Write-Json $profilePath @{schemaVersion=1;systemName=$systemName;views=$views}
    $childEnvironment=@{XR_RUNTIME_JSON=$RuntimeManifest;XR_API_LAYER_PATH=$TapeManifestDirectory;XR_ENABLE_API_LAYERS=$TapeLayerName;XRSIM_DIR=$simDir;XRSIM_HEADSET_CONFIG=$profilePath;XRTAPE_DIR=$tapeDir;XRTAPE_MAX_FRAMES='6000'}
    $argsHost=@('--test-scene','--controllers','--seconds',"$Seconds",'--eye-width','320','--eye-height','240','--ready-file',$readyPath,'--loader',$LoaderPath)
    $receipt.launch=@{executable=$HostPath;arguments=$argsHost;environment=$childEnvironment}
    $clock.Start()
    try{
        $hostRun=Start-Owned $HostPath $argsHost 'host' $childEnvironment
        $receipt.launch.pid=$hostRun.pid;$receipt.launch.start_ticks=$hostRun.start_ticks
        $initial=Wait-Sim {param($s) $s.sessionRunning -and $s.actionsAttached -and $s.sessionState -eq 'FOCUSED' -and $s.endFrames -ge 5} 'a focused host session'
        Require ($initial.graphics -eq 'D3D11' -and $initial.system -eq $systemName -and $initial.headsetConfig -eq $profilePath) 'Simulator graphics/system/profile identity mismatch.'
        Require ([IO.File]::Exists($readyPath)) 'Host readiness report is missing.'
        $ready=Read-Json $readyPath
        Require ($ready.session_created -and $ready.width -eq 320 -and $ready.height -eq 240) 'Host readiness dimensions differ from the requested bounded eyes.'
        $null=Send-Commands 'pose-and-grip' @('pace free 60','capture off','capture size 320 240','compose oncapture','focus norender on','head pose 0.05 1.60 -0.03 12 0 0','hand l follow off','hand r follow off','hand l grip pose -0.20 1.30 -0.35 0 0 0','hand l aim pose -0.20 1.30 -0.35 0 0 0','hand r grip pose 0.20 1.30 -0.35 0 0 0','hand r aim pose 0.20 1.30 -0.35 0 0 0','grip l 1','grip r 1','trigger r 0') {param($s) $s.head.valid -and $s.handL.valid -and $s.handR.valid -and [Math]::Abs($s.head.pos[0]-0.05) -lt 0.001 -and [Math]::Abs($s.head.ypr[0]-12) -lt 0.1 -and $s.controls.gripL -eq 1 -and $s.controls.gripR -eq 1}
        $null=Send-Commands 'trigger-down' @('trigger r 1') {param($s) $s.controls.trigR -eq 1}
        $null=Send-Commands 'trigger-up' @('trigger r 0','grip l 0','grip r 0') {param($s) $s.controls.trigR -eq 0 -and $s.controls.gripL -eq 0 -and $s.controls.gripR -eq 0}
        $null=Send-Commands 'focus-loss' @('focus lose') {param($s) $s.sessionState -eq 'VISIBLE'}
        $null=Send-Commands 'focus-return' @('focus regain') {param($s) $s.sessionState -eq 'FOCUSED' -and $s.layersLastFrame -ge 1 -and $s.projectionViews -eq 2}
        $capture=Send-Commands 'one-composition' @('shot host') {param($s) $s.captureSeq -eq 1}
        $receipt.capture=@{directory=(Join-Path $simDir 'capture');sequence=$capture.captureSeq;frame=$capture.frame}
        # Leave BOTH hands invalid for the remainder. Aggregate host counters then
        # prove hand admission stopped while frame/input processing continued.
        # 30 Hz keeps bounded host diagnostics observable: wait time exceeds its
        # existing 20 ms retained-row threshold. This is not a performance test.
        $invalid=Send-Commands 'invalid-hands' @('pace free 30','hand l valid off','hand r valid off') {param($s) -not $s.handL.valid -and -not $s.handR.valid -and $s.head.valid -and $s.sessionState -eq 'FOCUSED'}
        $receipt.negative_control=@{settled_end_frames=[long]$invalid.endFrames;tracked_upper_bound=([long]$invalid.endFrames+2);required_later_input_frames=30;status='pending'}
        while(-not $hostRun.process.HasExited){
            Check-Bounds
            $state=Read-Json $statePath
            Require ($state.cmdSeq -eq $invalid.cmdSeq -and -not $state.handL.valid -and -not $state.handR.valid) 'Invalid-hand control was changed before shutdown.'
            Start-Sleep -Milliseconds 50
        }
    }catch{$failures.Add($_.Exception.Message)}
    finally{if($null -ne $hostRun){Finish-Owned $hostRun $true}}
    if($null -ne $hostRun){
        Gate (-not $hostRun.forced) 'Host needed forced shutdown.'
        Gate ($hostRun.exit_code -eq 0) "Host exit code: $($hostRun.exit_code)"
    }
    # Analyze preserved evidence even when an earlier stage failed. No trace trimming.
    $hostFailureStart=$failures.Count
    try{
        $report=Read-Json (Join-Path $OutputDirectory 'host.stdout.txt')
        $receipt.host=@{}
        foreach($field in @('state','runtime','system','diagnostic_scene','diagnostic_error','ipc_mode','session_created','controllers_enabled','waited_frames','ended_frames','presented_frames','input_frames','tracked_hand_frames','headset_visuals_verified','error')){$receipt.host[$field]=$report.$field}
        Gate ($report.state -eq 'session_observed' -and $report.diagnostic_scene -and -not $report.ipc_mode -and $report.session_created -and $report.controllers_enabled) 'Host did not confirm the diagnostic controller session.'
        Gate ($report.runtime -eq 'xr-sim' -and $report.system -eq $systemName) 'Host reported a different runtime/system.'
        Gate ([string]::IsNullOrEmpty($report.error) -and [string]::IsNullOrEmpty($report.diagnostic_error)) 'Host or diagnostic scene reported an error.'
        foreach($field in @('waited_frames','ended_frames','presented_frames','input_frames','tracked_hand_frames')){Gate ($report.$field -gt 0) "Host counter did not advance: $field"}
        if($receipt.Contains('negative_control')){
            $boundary=$receipt.negative_control.tracked_upper_bound
            $invalidRows=@($report.frame_diagnostics.rows|Where-Object {$_.frame -gt $boundary -and $_.input_attempted -and $_.input_focused -and $_.input_head_valid -and -not $_.input_hands_tracked})
            $negativeOkay=$report.input_frames -ge $boundary+30 -and $report.tracked_hand_frames -le $boundary -and $invalidRows.Count -gt 0
            $receipt.negative_control.status=if($negativeOkay){'passed'}else{'failed'}
            $receipt.negative_control.host_input_frames=$report.input_frames
            $receipt.negative_control.host_tracked_hand_frames=$report.tracked_hand_frames
            $receipt.negative_control.invalid_focused_host_rows=$invalidRows.Count
            Gate $negativeOkay 'Invalid hands did not produce bounded host hand-admission suppression with continuing focused input.'
        }else{Gate $false 'Invalid-hand control was not completed.'}
        Gate ([IO.File]::Exists((Join-Path $OutputDirectory 'xr-events.jsonl'))) 'Host XR event log is absent.'
    }catch{$failures.Add("Host evidence: $($_.Exception.Message)")}
    $receipt.host_wiring_status=if($failures.Count -eq $hostFailureStart -and $hostFailureStart -eq 0 -and $commands.Count -eq 7){'passed'}else{'failed'}
    $captureFailureStart=$failures.Count
    try{
        $captureDirectory=Join-Path $simDir 'capture'
        $captureJson=Read-Json (Join-Path $captureDirectory 'host.json')
        $pngs=@(Get-ChildItem -LiteralPath $captureDirectory -Filter '*.png' -File)
        Gate ($pngs.Count -eq 3 -and $captureJson.width -eq 320 -and $captureJson.height -eq 240) 'Expected exactly one 320x240 eye pair and its mandatory SBS image.'
        Gate ($captureJson.sessionState -eq 'FOCUSED' -and @($captureJson.layers|Where-Object {$_.type -eq 'projection' -and $_.viewCount -eq 2}).Count -gt 0) 'Capture did not contain a focused stereo projection.'
        Gate ($captureJson.stats.nonBlackPctL -gt 0 -and $captureJson.stats.nonBlackPctR -gt 0) 'Capture has an empty eye.'
        $receipt.capture.files=@(Get-ChildItem -LiteralPath $captureDirectory -File|ForEach-Object {@{path=$_.FullName;bytes=$_.Length;sha256=(Get-FileHash -LiteralPath $_.FullName).Hash.ToLowerInvariant()}})
        $receipt.capture.stats=$captureJson.stats
    }catch{$failures.Add("Capture evidence: $($_.Exception.Message)")}
    $receipt.capture_status=if($failures.Count -eq $captureFailureStart){'passed'}else{'failed'}
    try{
        $traces=@(Get-ChildItem -LiteralPath $tapeDir -Filter '*.ndjson' -File)
        Require ($traces.Count -eq 1) "Expected one new tape trace, found $($traces.Count)."
        $trace=$traces[0].FullName
        $reader=[IO.File]::OpenText($trace)
        try{$header=$reader.ReadLine()|ConvertFrom-Json}finally{$reader.Dispose()}
        Require ($header.r -eq 'header' -and $header.schema -eq 1 -and $header.layer -eq $TapeLayerName -and $header.pid -eq $hostRun.pid -and $header.pointerBits -eq 64 -and $header.exe -eq $HostPath) 'Tape header identity/schema does not match the owned x64 host.'
        $required=@('trace_is_readable','runtime_identified','no_silent_truncation','session_reached_focused','stereo_view_count_is_2','frame_contract','frame_loop_advances','layer_budget','both_eyes_submitted','eye_order','ipd_plausible','no_vertical_disparity','eyes_parallel','submitted_fov_matches_located','submitted_pose_matches_located','eye_subimages_distinct','no_submit_with_invalid_pose','eye_pair_shares_display_time')
        $checkArguments=@('-B',$TapeChecker,$trace,'--expect-runtime','xr-sim','--json')
        foreach($check in $required){$checkArguments+=@('--require',$check)}
        $checker=Start-Owned $Python $checkArguments 'tape-check' @{}
        while(-not $checker.process.HasExited){Check-Bounds;Start-Sleep -Milliseconds 25}
        Finish-Owned $checker $false
        $checkResult=Read-Json (Join-Path $OutputDirectory 'tape-check.stdout.txt')
        $receipt.tape=@{path=$trace;header=$header;checker_exit=$checker.exit_code;checks=$checkResult;recording_header_verified=$true;conformance_status=if($checker.exit_code -eq 0 -and $checkResult.failed -eq 0){'passed'}else{'failed'}}
        Gate ($checker.exit_code -eq 0 -and $checkResult.failed -eq 0 -and $checkResult.runtime -eq 'xr-sim') 'Official tape checker reported failures (full unfiltered trace retained).'
        foreach($check in $required){Gate (@($checkResult.results|Where-Object {$_.name -eq $check -and $_.status -eq 'PASS'}).Count -eq 1) "Required tape check did not pass: $check"}
    }catch{$failures.Add("Tape evidence: $($_.Exception.Message)")}
}catch{$failures.Add($_.Exception.Message)}
finally{
    foreach($entry in $owned){
        try{Finish-Owned $entry $true}catch{$failures.Add("Cleanup: $($_.Exception.Message)")}
        $exited=(-not $entry.started) -or $entry.process.HasExited
        $receipt.cleanup+=@{name=$entry.name;pid=$entry.pid;start_ticks=$entry.start_ticks;exited=$exited;forced=$entry.forced;exit_code=$entry.exit_code}
        if($entry.forced){Gate $false "Forced termination recorded for $($entry.name)."}
        if($exited){$entry.process.Dispose()}
    }
    foreach($name in $savedTemporary.Keys){[Environment]::SetEnvironmentVariable($name,$savedTemporary[$name],'Process')}
    $receipt.finished_utc=[DateTime]::UtcNow.ToString('o');$receipt.elapsed_seconds=$clock.Elapsed.TotalSeconds
    $receipt.status=if($failures.Count -eq 0 -and $admitted){'passed'}else{'failed'}
    if($outputReady){Write-Json (Join-Path $OutputDirectory 'result.json') $receipt}
    elseif($safeRefusalPath){
        # A tiny refusal receipt is not a capture/session. Never overwrite an earlier run.
        $refusal=$safeRefusalPath
        $parent=[IO.Path]::GetDirectoryName($refusal)
        if($refusal.StartsWith((Join-Path $repo 'reports')+'\',[StringComparison]::OrdinalIgnoreCase) -and [IO.Directory]::Exists($parent) -and -not [IO.File]::Exists($refusal)){
            Write-Json $refusal @{schema='fvr.xr-simulation-refusal.v1';status='refused';failures=@($failures);processes_started=$owned.Count}
        }
    }
}
if($receipt.status -ne 'passed'){throw "XR host simulation failed: $($failures -join ' | ')"}
[pscustomobject]@{status=$receipt.status;result=(Join-Path $OutputDirectory 'result.json');evidence_level=$receipt.evidence_level}
