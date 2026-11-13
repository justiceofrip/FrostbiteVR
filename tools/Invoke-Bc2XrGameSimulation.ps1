#requires -Version 7.5
<# Developer actual-game runner. See xr-sim/GAME.txt. No desktop input is sent. #>
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$RepositoryRoot,
    [Parameter(Mandatory)][string]$InstallRoot,
    [Parameter(Mandatory)][string]$Version,
    [Parameter(Mandatory)][Alias('OutputDirectory')][string]$OutputDir,
    [Parameter(Mandatory)][string]$LocalConfig,
    [Parameter(Mandatory)][ValidatePattern('^[a-f0-9]{64}$')][string]$ExpectedSourceSha256,
    [string]$ProfileDirectory=(Join-Path $env:USERPROFILE 'Documents/BFBC2'),
    [ValidateRange(15,420)][int]$Seconds=300,
    [ValidateRange(90,540)][int]$TimeoutSeconds=420,
    [ValidateRange(1,12)][int]$CaptureCount=4,
    [ValidateRange(64,1024)][int]$CaptureWidth=1024,
    [ValidateRange(64,768)][int]$CaptureHeight=768,
    [ValidateRange(1,[long]::MaxValue)][long]$MaxGeneratedBytes=40GB,
    [string]$GeneratedBudgetReason,
    [switch]$MechanismSnapshots,
    [switch]$PrivateGpuOrderedPresent,
    [switch]$ProfileAttribution,
    [switch]$PrivateSourcePipeline,
    [switch]$PerformanceMode=$false,
    [string]$PreAttachSetupSpec,
    [string]$PreAttachSetupSpecSha256,
    [string]$ContinueCampaignFromRun,
    [string]$CampaignSeedFile,
    [string]$PrivateSessionBinding,
    [string]$PrivateSessionBindingSha256,
    [switch]$ExtendedObservation
)
if($PerformanceMode -and $PrivateSourcePipeline){throw 'PerformanceMode and PrivateSourcePipeline are separate launch options; select only one.'}
Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'
$sourceRoot=Split-Path -Parent $PSScriptRoot
$utf8=[Text.UTF8Encoding]::new($false)
$savedTemporary=@{};foreach($key in 'TEMP','TMP','TMPDIR','PYTHONDONTWRITEBYTECODE'){$savedTemporary[$key]=[Environment]::GetEnvironmentVariable($key,'Process')}
$failures=[Collections.Generic.List[string]]::new()
$owned=@{};$profiles=@();$launcherFiles=@();$entry=$null;$folder=$null;$session=$null
$outputReady=$false;$runningObserved=$false;$simObserved=$false;$restored=$false;$launcherPreserved=$false
$stopReason=$null;$stopWritten=$false;$nativeRetired=$false;$gameClosed=$false;$lastScan=-1000L
$clock=[Diagnostics.Stopwatch]::new();$hostStartedAt=$null;$launchUtc=$null
$privateAdmission=$null;$runtimeVersion=$Version
$startup=$null;$preAttachSpec=$null;$preAttachProvisionRunner=$null;$preparedProfileHashes=@{}
$mechanismMarker=@{requested=[bool]$MechanismSnapshots;status=$(if($MechanismSnapshots){'awaiting_session'}else{'not_requested'});native_enabled_claimed=$false}
$privatePresentation=@{requested=[bool]$PrivateGpuOrderedPresent;environment_variable='FVR_PRIVATE_GPU_ORDERED_PRESENT';environment_value=$(if($PrivateGpuOrderedPresent){'1'}else{'0'});child_environment_verified=$false;other_fvr_bc2_environment_absent=$false;actual_host_enablement_claimed=$false}
$sourcePipeline=@{requested=[bool]$PrivateSourcePipeline;activation_boundary='dedicated_session_after_entry_probe';entry_environment_enabled=$false;session_option_verified=$false;native_enablement_claimed=$false}
$performanceSelection=@{requested=[bool]$PerformanceMode;session_option_verified=$false;session_enabled_observed=$false;native_samples_verified=$false;restoration_verified=$false;restoration_status='not_observed'}
# Finite native attribution starts automatically at first verified infantry, including warmup.
# This does not select a steady Insert window, rearm profiling or establish headset FPS.
$attributionProfile=@{requested=[bool]$ProfileAttribution;environment=@{};
    cpu_window_seconds=12;gpu_maximum_tracked_pairs=8;runtime_rearm=$false;steady_insert_window_guaranteed=$false;
    child_environment_verified=$false;unapproved_fvr_bc2_environment_absent=$false;native_enablement_claimed=$false}
if($ProfileAttribution){$attributionProfile.environment=@{FVR_BC2_PRIVATE_CPU_ATTRIBUTION='12';FVR_PRIVATE_GPU_TIMING436='8'}}
# Two eyes, with the mandatory SBS duplicate as a second image stream. Layers stay off.
$simRawBytes=[long]$CaptureCount*$CaptureWidth*$CaptureHeight*4*2*2+$CaptureCount*64KB
$rawBytes=512MB+$simRawBytes;$plannedBytes=$rawBytes+128MB;$rawLimitBytes=660MB
$capturePlan=@{maximum_compositions=$CaptureCount;width=$CaptureWidth;height=$CaptureHeight;eyes=2;image_streams=2;bytes_per_pixel=4;metadata_bytes_per_composition=64KB;raw_bytes=$simRawBytes;layers=$false;periodic=$false}
$receipt=[ordered]@{schema='fvr.xr-game-simulation.v1';status='failed';evidence_level='actual_game_simulated_runtime';
    actual_game_started=$false;native_running_observed=$false;reload_success_claimed=$false;headset_accepted=$false;
    limits=@{host_seconds=$Seconds;outer_seconds=$TimeoutSeconds;max_generated_bytes=$MaxGeneratedBytes;generated_budget_reason=$GeneratedBudgetReason;planned_bytes=$plannedBytes;raw_budget_bytes=$rawBytes;raw_budget_limit_bytes=$rawLimitBytes;raw_budget_reason='Readable menu/mechanism captures: native eight bounded images plus at most twelve 1024x768 stereo/SBS compositions.';native_maximum_raw_images=8;sim_compositions=$CaptureCount;sim_capture_width=$CaptureWidth;sim_capture_height=$CaptureHeight;render_width=1024;render_height=768;tape_max_frames=30000};
    capture_plan=$capturePlan;mechanism_snapshots=$mechanismMarker;private_gpu_ordered_present=$privatePresentation;profile_attribution=$attributionProfile;private_source_pipeline=$sourcePipeline;performance_mode=$performanceSelection;
    failures=$failures;forced_termination_by_wrapper=$false}
function Require([bool]$Condition,[string]$Message){if(-not $Condition){throw $Message}}
function AssertObservationLimits([int]$HostSeconds,[int]$OuterSeconds,[bool]$Extended){
    Require ($HostSeconds -ge 15 -and $HostSeconds -le 420 -and $OuterSeconds -ge 90 -and $OuterSeconds -le 540) 'Observation duration exceeds the bounded runner limits.'
    Require ($Extended -or ($HostSeconds -le 300 -and $OuterSeconds -le 420)) 'Durations beyond 300/420 seconds require explicit ExtendedObservation.'
    Require ($OuterSeconds -ge $HostSeconds+60) 'Outer deadline must reserve at least 60 seconds beyond host duration.'
}
function Full([string]$Path){
    Require ([IO.Path]::IsPathFullyQualified($Path)) "Absolute path required: $Path"
    $full=[IO.Path]::GetFullPath($Path);Assert-FvrNoLinks $full;return $full
}
function Pin([string]$Path,[long]$Maximum=4MB){
    $Path=Full $Path;$item=Get-Item -LiteralPath $Path
    Require (-not $item.PSIsContainer -and $item.Length -le $Maximum) "Invalid or oversized file: $Path"
    return @{path=$Path;bytes=$item.Length;sha256=(Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()}
}
function ReadSimulatorStateText([string]$Path,[long]$Maximum){
    # xr-sim closes state.json.tmp before MoveFileExW(REPLACE_EXISTING). Admit
    # its DELETE-access owner while reading one complete published file. Do not
    # share writes or assume replacement succeeds while this handle is open.
    $stream=[IO.File]::Open($Path,[IO.FileMode]::Open,[IO.FileAccess]::Read,([IO.FileShare]::Read -bor [IO.FileShare]::Delete))
    try{
        Require ($stream.Length -le $Maximum) "JSON exceeds bound: $Path"
        $reader=[IO.StreamReader]::new($stream)
        try{return $reader.ReadToEnd()}finally{$reader.Dispose()}
    }finally{$stream.Dispose()}
}
function Json([string]$Path,[long]$Maximum=128KB,[double]$SharingDeadlineSeconds=0){
    $attemptLimit=if($SharingDeadlineSeconds -gt 0){25}else{5}
    $readStartedAt=if($SharingDeadlineSeconds -gt 0){$clock.Elapsed.TotalSeconds}else{$null}
    for($attempt=0;$attempt -lt $attemptLimit;$attempt++){
        Require ((Get-Item -LiteralPath $Path).Length -le $Maximum) "JSON exceeds bound: $Path"
        try{
            $text=if($SharingDeadlineSeconds -gt 0){ReadSimulatorStateText $Path $Maximum}else{[IO.File]::ReadAllText($Path)}
            break
        }
        catch{
            $cause=$_.Exception;while($cause.InnerException){$cause=$cause.InnerException}
            $detail=$null
            if($SharingDeadlineSeconds -gt 0){
                $detail=@{path=$Path;attempts=$attempt+1;attempt_limit=$attemptLimit;started_seconds=$readStartedAt;failed_seconds=$clock.Elapsed.TotalSeconds;stopped_seconds=$clock.Elapsed.TotalSeconds;deadline_seconds=$SharingDeadlineSeconds;exception_type=$cause.GetType().FullName;hresult=$cause.HResult;stop_reason=$null}
                $_.Exception.Data['fvr_json_read']=$detail
            }
            # Only live state opts into longer sharing-violation (32) retries.
            # Other reads and lock violations (33) retain their five attempts.
            $retryStop=if($cause -isnot [IO.IOException] -or $cause.HResult -notin @(-2147024864,-2147024863)){'non_retryable_exception'}
                elseif($attempt -eq $attemptLimit-1){'attempt_limit'}
                elseif($attempt -ge 4 -and $cause.HResult -ne -2147024864){'lock_violation_limit'}
                elseif($SharingDeadlineSeconds -gt 0 -and $clock.Elapsed.TotalSeconds+0.01 -ge $SharingDeadlineSeconds){'deadline_before_sleep'}else{$null}
            if($retryStop){if($detail){$detail.stop_reason=$retryStop};throw}
            Start-Sleep -Milliseconds 10
            if($SharingDeadlineSeconds -gt 0 -and $clock.Elapsed.TotalSeconds -ge $SharingDeadlineSeconds){$detail.stopped_seconds=$clock.Elapsed.TotalSeconds;$detail.stop_reason='deadline_after_sleep';throw}
        }
    }
    return $text|ConvertFrom-Json -AsHashtable -DateKind String -Depth 100
}
function Save([string]$Name,$Value){
    if($Name -cnotin @('sim-startup.json','session-link.json')){Write-FvrJson (Join-Path $OutputDir $Name) $Value;return}
    # Only these two handoff files are polled while startup replaces them.
    # Spend the existing bounded publisher wait inside every original clock.
    $deadline=$TimeoutSeconds-60
    if($null -ne $hostStartedAt){$deadline=[Math]::Min($deadline,[Math]::Min($hostStartedAt+$Seconds,$hostStartedAt+20))}
    if($startup){$deadline=[Math]::Min($deadline,$startup.sent_at_seconds+15)}
    Write-FvrJson (Join-Path $OutputDir $Name) $Value -RetrySessionPublication -PublicationDeadlineClock $clock -PublicationDeadlineSeconds $deadline
}
function SameIdentity($Actual,$Expected){
    Require ($Actual.pid -eq $Expected.pid -and $Actual.start_utc -ceq $Expected.start_utc -and $Actual.path -ieq $Expected.path) 'Retained process identity differs.'
}
function AssertPerformanceOptions($Options){
    if($Options.ContainsKey('performance_mode')){
        Require ($Options.performance_mode -is [bool] -and $Options.performance_mode -eq [bool]$PerformanceMode) 'Performance mode session option differs.'
    }else{Require (-not $PerformanceMode) 'Requested performance mode session option is missing.'}
    $performanceSelection.session_option_verified=$true
}
function ObservePerformanceSession($Record){
    if($Record.ContainsKey('performance_mode')){
        $mode=$Record.performance_mode
        Require ($mode -is [hashtable] -and $mode.requested -is [bool] -and $mode.requested -eq [bool]$PerformanceMode -and $mode.enabled -is [bool]) 'Performance mode session receipt differs.'
        Require ($PerformanceMode -or -not $mode.enabled) 'Unrequested performance mode enablement.'
        $performanceSelection.session=$mode
        if($mode.enabled){$performanceSelection.session_enabled_observed=$true}
    }elseif($PerformanceMode -and $Record.state -ceq 'running'){throw 'Running performance mode session receipt is missing.'}
    foreach($key in 'graphics_settings_restoration','graphics_settings_restore_pending'){
        if($Record.ContainsKey($key)){$performanceSelection[$key]=$Record[$key]}
    }
    if($Record.ContainsKey('private_source_pipeline')){
        if($PerformanceMode){
            $pipeline=$Record.private_source_pipeline
            Require ($pipeline.selection -ceq 'performance_mode' -and $pipeline.requested -is [bool] -and $pipeline.requested) 'Performance mode source pipeline receipt differs.'
            $performanceSelection.source_pipeline=$pipeline
        }else{
            Require ([bool]$PrivateSourcePipeline) 'Unrequested private source pipeline session receipt.'
            $sourcePipeline.session=$Record.private_source_pipeline
        }
    }
    if($PerformanceMode -and $Record.state -ceq 'running'){
        Require ($Record.performance_mode.enabled -and $Record.performance_mode.lease.status -ceq 'bound' -and $Record.performance_mode.ContainsKey('watcher') -and $Record.ContainsKey('private_source_pipeline')) 'Running performance mode lacks its bound lease, watcher or pipeline receipt.'
    }
}
function AssertPerformanceRestoration($Journal,$SettingsRow,[string]$CurrentMsaa){
    Require ($Journal.schema -ceq 'bc2vr.graphics-settings-lease.v1' -and $Journal.state -ceq 'restored') 'Performance graphics lease restoration remains unconfirmed.'
    SameIdentity $Journal.supervisor $owned.supervisor.identity
    if($owned.ContainsKey('game')){SameIdentity $Journal.game $owned.game.identity}
    Require ($Journal.settings_path -ieq $SettingsRow.original.path -and $Journal.original_sha256 -ceq $SettingsRow.original.sha256 -and $Journal.game_path -ieq $gameExe) 'Performance graphics lease differs from the original profile/game backup.'
    Require ($CurrentMsaa -ceq $Journal.original_msaa) 'Performance graphics MSAA restoration differs.'
    $performanceSelection.restoration_verified=$true;$performanceSelection.restoration_status='restored'
}
function CompletePerformanceRestoration {
    $performanceSelection.restoration_status='awaiting_watcher_and_lease'
    try{
    foreach($name in $owned.Keys){if($name -cne 'graphics_watcher'){Require ($owned[$name].process.HasExited) 'Performance graphics restoration requires all game/session owners to exit.'}}
    NoOtherProcesses -BeforeLaunch
    if($session.performance_mode.ContainsKey('watcher')){
        Require ($owned.ContainsKey('graphics_watcher')) 'Performance graphics watcher identity was not retained; preserve profiles.'
        $watcher=$owned.graphics_watcher.process
        $remaining=[Math]::Max(0,[Math]::Min(10000,[int](($TimeoutSeconds-$clock.Elapsed.TotalSeconds)*1000)))
        Require ($watcher.HasExited -or $watcher.WaitForExit($remaining)) 'Performance graphics watcher remains live at the original deadline; preserve profiles.'
        Require ($watcher.ExitCode -eq 0) 'Performance graphics watcher failed; preserve profiles and lease diagnostics.'
    }else{Require (-not $performanceSelection.session_enabled_observed) 'Enabled performance mode has no watcher identity; preserve profiles.'}
    # Read the installed lease contract only after its writer exits. Never ask
    # the wrapper's profile restoration to stand in for durable lease recovery.
    . (Join-Path $runtimeRoot 'GraphicsSettings.ps1')
    $leasePaths=Get-Bc2GraphicsLeasePaths $stateRoot
    $lease=Read-Bc2GraphicsLease $leasePaths
    if(-not $lease){Require (-not $performanceSelection.session_enabled_observed) 'Performance graphics lease is missing.';$performanceSelection.restoration_status='not_started';return}
    Require ($owned.ContainsKey('graphics_watcher')) 'Performance graphics watcher was not retained for this lease; preserve profiles.'
    $settingsRows=@($profiles|Where-Object {[IO.Path]::GetFileName($_.original.path) -ceq 'settings.ini'})
    Require ($settingsRows.Count -eq 1) 'Performance mode requires the original graphics profile backup.'
    $current=Convert-Bc2GraphicsMsaa ([IO.File]::ReadAllBytes($settingsRows[0].original.path))
    AssertPerformanceRestoration $lease $settingsRows[0] $current.value
    }finally{
        try{
            $journalPath=Join-Path $stateRoot 'graphics-settings/lease.json'
            if([IO.File]::Exists($journalPath)){Save 'performance-graphics-lease.json' (Json $journalPath 16KB)}
        }catch{$performanceSelection.restoration_evidence_error=$_.Exception.Message}
    }
}
function Bind([string]$Name,$Identity,[string]$ExpectedPath,[int]$ParentId){
    if(-not $Identity){return}
    Require ($Identity.path -ieq $ExpectedPath -and [DateTimeOffset]::Parse($Identity.start_utc).UtcDateTime -ge $launchUtc) "Unexpected $Name executable/lifetime."
    if($owned.ContainsKey($Name)){SameIdentity $owned[$Name].identity $Identity;return}
    $process=Find-Bc2OwnedProcess $Identity
    if(-not $process){return} # Preserve failed/short-lived identities in session.json; never select by name.
    $retained=$false
    try{
        Require ([FvrStorage.NativeSize]::ParentPid($process.Id) -eq $ParentId) "Unexpected $Name process parent."
        try{$observed=Get-Bc2ProcessIdentity $process -RequireLive $true}
        catch{
            $cause=$_.Exception;while($cause.InnerException){$cause=$cause.InnerException}
            # Find already proved this handle's exact birth/path and parentage
            # passed above. Only the brief resolution helpers may finish here.
            # Keep their session receipt; never turn an exited handle into live
            # authority, reopen its PID, or hide any other identity failure.
            $imageQueryFailed=$cause -is [ComponentModel.Win32Exception] -and
                $cause.Message -ceq 'Cannot query retained process image path'
            if($Name -in @('resolution_helper','resolution_release_helper') -and
               (($cause -is [InvalidOperationException] -and $cause.Message -ceq 'Retained process is no longer live') -or $imageQueryFailed) -and
               $process.HasExited){
                # The native query's immediate live check can precede this
                # retained handle's signaled exit. Preserve the query error;
                # only the exit proof permits skipping this finished helper.
                if($imageQueryFailed){Save ($Name+'-binding-exit.json') @{identity=$Identity;query_native_error_code=$cause.NativeErrorCode;exact_handle_exit_observed=$true;live_authority_granted=$false}}
                return
            }
            throw
        }
        SameIdentity $observed $Identity
        $owned[$Name]=@{process=$process;identity=$observed};$retained=$true
        Save ($Name+'-identity.json') $owned[$Name].identity
    }finally{if(-not $retained){$process.Dispose()}}
}
function NoOtherProcesses([switch]$BeforeLaunch,[switch]$RetainedOnly){
    foreach($process in @(Get-Process BFBC2Game,BC2XrHost,BC2NativeTrace,BC2NativeIpcProbe,BC2NativeResolution,ProvisionRunner -ErrorAction SilentlyContinue)){
        try{
            if($process.HasExited){continue}
            $identity=Get-Bc2ProcessIdentity $process -RequireLive $true
            $known=@($owned.Values|Where-Object {$_.identity.pid -eq $identity.pid -and $_.identity.start_utc -ceq $identity.start_utc -and $_.identity.path -ieq $identity.path}).Count -eq 1
            # Session publishes each identity just after Start-Process. Parentage permits that small window.
            if(-not $BeforeLaunch -and -not $RetainedOnly -and -not $known){
                $parentId=[FvrStorage.NativeSize]::ParentPid($process.Id)
                $fromSupervisor=$owned.ContainsKey('supervisor') -and $parentId -eq $owned.supervisor.identity.pid
                $preflight=$entry -and $parentId -eq $entry.Id -and $identity.path -ieq (Join-Path $runtimeRoot 'bin/BC2XrHost.exe')
                $setupChild=$owned.ContainsKey('setup_helper') -and $parentId -eq $owned.setup_helper.identity.pid -and $preAttachProvisionRunner -and $identity.path -ieq $preAttachProvisionRunner
                $expected=@($gameExe)+@($pePaths.Keys)
                if($setupChild){$expected+=,$preAttachProvisionRunner}
                $known=($fromSupervisor -or $preflight -or $setupChild) -and $identity.path -iin $expected -and [DateTimeOffset]::Parse($identity.start_utc).UtcDateTime -ge $launchUtc
            }
            Require $known "Unowned BC2 process detected: $($identity.pid) $($identity.path)."
        }catch{
            # Discovery can overlap the normal supervisor's process exit. A
            # process proven exited cannot be an unowned live participant;
            # failures for a still-live process remain fatal.
            if(-not $process.HasExited){throw}
        }finally{$process.Dispose()}
    }
}
function Backup([string]$Path,[string]$Relative){
    $original=Pin $Path;$destination=Join-Path $OutputDir $Relative
    $null=[IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($destination))
    [IO.File]::Copy($Path,$destination,$false);$backup=Pin $destination
    Require ($backup.sha256 -ceq $original.sha256 -and (Pin $Path).sha256 -ceq $original.sha256) 'Exact backup failed.'
    return @{original=$original;backup=$backup}
}
function SameCampaignPin($Actual,$Expected){
    Require ($Expected.path -is [string] -and $Actual.path -ieq (Full $Expected.path) -and
        ($Expected.bytes -is [int] -or $Expected.bytes -is [long]) -and $Actual.bytes -eq $Expected.bytes -and
        $Expected.sha256 -is [string] -and $Expected.sha256 -cmatch '^[a-f0-9]{64}$' -and $Actual.sha256 -ceq $Expected.sha256) 'Campaign continuation file pin differs.'
}
function GetCampaignContinuation([string]$Run,$Game){
    $Run=Full $Run
    Require ($Run.StartsWith((Join-Path $RepositoryRoot 'reports')+'\',[StringComparison]::OrdinalIgnoreCase) -and $Run -ine $OutputDir) 'Campaign continuation requires a preceding owned runner directory.'
    $proof=@{};$pins=@{}
    foreach($name in 'run-manifest.json','result.json','artifact-binding.json','native-retirement.json','originals.json','profile-after.json'){
        $path=Join-Path $Run $name;$pins[$name]=Pin $path
        $proof[$name]=Json $path 4MB
        SameCampaignPin (Pin $path) $pins[$name]
    }
    $manifest=$proof['run-manifest.json'];$result=$proof['result.json'];$retirement=$proof['native-retirement.json']
    Require ($manifest.schema -ceq 'fvr.xr-game-run.v1' -and (Full $manifest.output_directory) -ieq $Run -and
        (Full $manifest.artifact_binding_path) -ieq (Join-Path $Run 'artifact-binding.json')) 'Campaign continuation runner ownership differs.'
    Require ($result.schema -ceq 'fvr.xr-game-simulation.v1' -and $result.status -ceq 'observed_and_closed' -and
        $result.failures -is [array] -and $result.failures.Count -eq 0 -and
        $result.forced_termination_by_wrapper -is [bool] -and -not $result.forced_termination_by_wrapper) 'Campaign continuation requires a successful closed run.'
    foreach($key in 'actual_game_started','native_running_observed','simulator_host_observed','native_retirement_proven','exact_three_profiles_restored','normal_game_close','launcher_active_settings_preserved'){
        Require ($result[$key] -is [bool] -and $result[$key]) ('Campaign continuation lacks proof: '+$key)
    }
    foreach($key in 'hooks_disabled','reload_flow_drained','native_retirement_proven'){
        Require ($retirement[$key] -is [bool] -and $retirement[$key]) ('Campaign continuation retirement lacks proof: '+$key)
    }
    Require ($retirement.pre_activation_refusal -is [bool] -and -not $retirement.pre_activation_refusal) 'Campaign continuation cannot use a pre-activation refusal.'
    SameCampaignPin $Game $proof['artifact-binding.json'].game
    Require ($result.processes -is [array]) 'Campaign continuation process ownership is missing.'
    foreach($name in 'entry','supervisor','game','host','trace'){
        Require (@($result.processes|Where-Object {$_.name -ceq $name}).Count -eq 1) ('Campaign continuation process ownership differs: '+$name)
    }
    foreach($process in $result.processes){
        Require ($process.exited -is [bool] -and $process.exited -and
            ($process.exit_code -is [int] -or $process.exit_code -is [long]) -and $process.exit_code -eq 0 -and
            ($process.identity.pid -is [int] -or $process.identity.pid -is [long]) -and $process.identity.pid -gt 0) 'Campaign continuation owner has not exited successfully.'
        $null=Full $process.identity.path;$null=[DateTimeOffset]::Parse($process.identity.start_utc)
        $live=Find-Bc2OwnedProcess $process.identity
        if($live){$live.Dispose();throw 'Campaign continuation owner is still live.'}
    }
    $gameOwner=@($result.processes|Where-Object {$_.name -ceq 'game'})[0]
    Require ($gameOwner.identity.path -ieq $Game.path -and $retirement.bootstrap.pid -eq $gameOwner.identity.pid -and
        $retirement.bootstrap.probe_exit -eq 0 -and $retirement.bootstrap.remote_thread_exit_observed -is [bool] -and $retirement.bootstrap.remote_thread_exit_observed) 'Campaign continuation game retirement identity differs.'
    $before=@($proof['originals.json'].profiles);$after=@($proof['profile-after.json'])
    Require ($before.Count -eq 3 -and $after.Count -eq 3) 'Campaign continuation requires exactly three original and retained profiles.'
    $source=$null
    foreach($name in 'GameSettings.bin','GameSettings.ini','settings.ini'){
        $old=@($before|Where-Object {[IO.Path]::GetFileName($_.original.path) -ceq $name})
        $new=@($after|Where-Object {[IO.Path]::GetFileName($_.original.path) -ceq $name})
        Require ($old.Count -eq 1 -and $new.Count -eq 1 -and $old[0].original.path -ieq $new[0].original.path -and
            (Full $old[0].backup.path) -ieq (Join-Path $Run ('profile-before/'+$name)) -and
            (Full $new[0].backup.path) -ieq (Join-Path $Run ('profile-after/'+$name))) 'Campaign continuation profile ownership differs.'
        SameCampaignPin (Pin $old[0].backup.path) $old[0].backup
        Require ($old[0].backup.sha256 -ceq $old[0].original.sha256 -and $old[0].backup.bytes -eq $old[0].original.bytes -and
            $new[0].backup.sha256 -ceq $new[0].original.sha256 -and $new[0].backup.bytes -eq $new[0].original.bytes) 'Campaign continuation profile backup proof differs.'
        if($name -ceq 'GameSettings.bin'){$source=Pin $new[0].backup.path;SameCampaignPin $source $new[0].backup}
    }
    Require ($source.bytes -gt 0) 'Campaign continuation binary is empty.'
    return @{schema='fvr.campaign-continuation.v1';status='admitted';run_directory=$Run;proof=$pins;source=$source;game=$Game;
        exact_original_restoration_required=$true;checkpoint_progress_claimed=$false}
}
function GetKnownCampaignSeedIdentity {
    # Reviewed historical SP03 bytes; the caller supplies an explicit absolute path.
    return @{bytes=117657;sha256='b2a9f98f0c0609b4bf793760634754e8c616f7095d07249c7fe2b4cb74083988'}
}
function GetCampaignSeed([string]$Path,$Game){
    $source=Pin $Path;$known=GetKnownCampaignSeedIdentity
    Require ($source.bytes -eq $known.bytes -and $source.sha256 -ceq $known.sha256) 'Reviewed historical SP03 campaign seed differs.'
    return @{schema='fvr.campaign-seed.v1';status='admitted';seed_label='known_historical_sp03_seed_scene303_start01';source=$source;game=$Game;
        exact_original_restoration_required=$true;checkpoint_progress_claimed=$false;current_seat_claimed=$false}
}
function PrepareCampaignSeed($Seed){
    NoOtherProcesses -BeforeLaunch
    $observed=GetCampaignSeed $Seed.source.path $Seed.game
    SameCampaignPin $observed.source $Seed.source
    PrepareCampaignProfile $Seed 'campaign-seed.json'
}
function PrepareCampaignContinuation($Continuation){
    NoOtherProcesses -BeforeLaunch
    foreach($pin in $Continuation.proof.Values){SameCampaignPin (Pin $pin.path) $pin}
    PrepareCampaignProfile $Continuation 'campaign-continuation.json'
}
function PrepareCampaignProfile($Selection,[string]$ReceiptName){
    NoOtherProcesses -BeforeLaunch
    SameCampaignPin (Pin $Selection.source.path) $Selection.source
    $targets=@($profiles|Where-Object {[IO.Path]::GetFileName($_.original.path) -ceq 'GameSettings.bin'})
    Require ($targets.Count -eq 1) 'Campaign profile requires exactly one current original binary backup.'
    $target=$targets[0]
    SameCampaignPin (Pin $target.original.path) $target.original
    SameCampaignPin (Pin $target.backup.path) $target.backup
    Require ($target.backup.sha256 -ceq $target.original.sha256 -and $target.backup.bytes -eq $target.original.bytes) 'Campaign profile original backup differs.'
    $bytes=[IO.File]::ReadAllBytes($Selection.source.path)
    Require ($bytes.LongLength -eq $Selection.source.bytes -and [Convert]::ToHexString([Security.Cryptography.SHA256]::HashData($bytes)).ToLowerInvariant() -ceq $Selection.source.sha256) 'Campaign profile source changed before staging.'
    [IO.File]::WriteAllBytes($target.original.path,$bytes)
    $staged=Pin $target.original.path
    Require ($staged.sha256 -ceq $Selection.source.sha256 -and $staged.bytes -eq $Selection.source.bytes) 'Campaign profile staging differs.'
    $preparedProfileHashes[$target.original.path]=$staged.sha256
    $Selection.status='staged';$Selection.staged=$staged;$Selection.original=$target.original;$Selection.backup=$target.backup
    Save $ReceiptName $Selection
}
function Pe([string]$Path,[int]$Machine){
    $stream=[IO.File]::OpenRead($Path)
    try{
        $reader=[IO.BinaryReader]::new($stream);Require ($reader.ReadUInt16() -eq 0x5a4d) "Not a PE: $Path"
        $stream.Position=0x3c;$offset=$reader.ReadInt32();Require ($offset -ge 64 -and $offset -lt $stream.Length-6) 'Invalid PE offset.'
        $stream.Position=$offset;Require ($reader.ReadUInt32() -eq 0x4550 -and $reader.ReadUInt16() -eq $Machine) "Wrong PE machine: $Path"
    }finally{$stream.Dispose()}
}
function Library([string]$Manifest,[string]$Path){
    Require (-not [string]::IsNullOrWhiteSpace($Path)) 'Manifest library path missing.'
    if(-not [IO.Path]::IsPathFullyQualified($Path)){$Path=Join-Path ([IO.Path]::GetDirectoryName($Manifest)) $Path}
    return Full $Path
}
function PrepareMechanismMarker {
    if(-not $MechanismSnapshots){return}
    if($privateAdmission){
        Assert-Bc2PrivateMarkers $folder $options.private_campaign.markers
        $mechanismMarker.marker=$options.private_campaign.markers.mechanism
        $mechanismMarker.path=$mechanismMarker.marker.path;$mechanismMarker.status='placed_before_supervisor_start'
        Save 'mechanism-snapshot-marker.json' $mechanismMarker
        return
    }
    $mechanismMarker.status='refused';$mechanismMarker.path=Join-Path $folder 'mechanism-snapshot.enable'
    try{
        $created=(Get-Item -LiteralPath $folder).CreationTimeUtc
        $mechanismMarker.folder_created_utc=$created.ToString('o')
        Require ($created -ge $launchUtc -and ([DateTime]::UtcNow-$created).TotalSeconds -le 5) 'Mechanism marker requires the newly created session directory within five seconds.'
        # These files precede native attach and are retained. Check on both sides
        # of publication; a concurrent stage transition is refusal, never enablement.
        foreach($phase in 'before','after'){
            $record=Join-Path $folder 'session.json'
            $observed=if([IO.File]::Exists($record)){Json $record}else{$null}
            $mechanismMarker[$phase+'_session']=$observed
            if($observed){
                SameIdentity $observed.supervisor $owned.supervisor.identity
                Require ($observed.state -ceq 'starting' -and -not $observed.host -and -not $observed.trace) 'Too late for mechanism marker: host/native startup already observed.'
            }
            $late=@('menu-ready.json','menu-resized.json','BC2NativeProbe.dll','host.stdout.log','native.stdout.log'|Where-Object {Test-Path -LiteralPath (Join-Path $folder $_)})
            $mechanismMarker[$phase+'_startup_files']=$late
            Require ($late.Count -eq 0) 'Too late for mechanism marker: menu readiness or native staging already observed.'
            if($phase -ceq 'before'){
                Assert-FvrNoLinks $mechanismMarker.path
                Require (-not(Test-Path -LiteralPath $mechanismMarker.path)) 'Existing mechanism marker refused.'
                $temporary=Join-Path $folder ('mechanism-snapshot-'+[guid]::NewGuid().ToString('N')+'.tmp')
                [IO.File]::WriteAllBytes($temporary,[byte[]]@(49,10))
                [IO.File]::Move($temporary,$mechanismMarker.path,$false)
                $mechanismMarker.placed_utc=[DateTime]::UtcNow.ToString('o')
                $mechanismMarker.status='placement_unconfirmed'
            }
        }
        $mechanismMarker.marker=Pin $mechanismMarker.path 2
        Require ([Convert]::ToHexString([IO.File]::ReadAllBytes($mechanismMarker.path)) -ceq '310A') 'Mechanism marker bytes differ.'
        $mechanismMarker.status='placed_before_native_staging'
    }catch{$mechanismMarker.error=$_.Exception.Message;throw}
    finally{Save 'mechanism-snapshot-marker.json' $mechanismMarker}
}
function ObserveSession {
    if(-not $folder){
        $activePath=Join-Path $stateRoot 'active-session.json'
        if(-not [IO.File]::Exists($activePath)){return}
        $active=Json $activePath
        if($priorActive -and $active.relative_folder -ceq $priorActive.relative_folder){return}
        Require ($active.version -ceq $runtimeVersion -and $active.relative_folder -cmatch '^sessions/[A-Za-z0-9-]+$') 'Unexpected active-session record.'
        Bind 'supervisor' $active.supervisor $pwsh $entry.Id
        Require ($owned.ContainsKey('supervisor')) 'New supervisor identity could not be retained.'
        $candidate=Get-FvrChild $stateRoot $active.relative_folder;Assert-FvrNoLinks $candidate
        $options=Json (Join-Path $candidate 'options.json')
        Require ($options.version -ceq $runtimeVersion -and $options.source_sha256 -ceq $ExpectedSourceSha256 -and $options.xr_manifest -ieq $runtimeManifest -and $options.game_sha256 -ceq $gamePin.sha256 -and -not $options.activate_window) 'Session options do not match the admitted launch.'
        if($PrivateSourcePipeline){Require ($options.ContainsKey('private_source_pipeline') -and $options.private_source_pipeline -is [bool] -and $options.private_source_pipeline) 'Private source pipeline option differs.'}
        else{Require (-not $options.ContainsKey('private_source_pipeline')) 'Unrequested private source pipeline.'}
        $sourcePipeline.session_option_verified=$true
        AssertPerformanceOptions $options
        if($preAttachSpec){
            Require ($options.ContainsKey('pre_attach_setup') -and $options.pre_attach_setup.path -ieq $PreAttachSetupSpec -and $options.pre_attach_setup.sha256 -ceq $PreAttachSetupSpecSha256) 'Pre-attach setup option differs.'
        }else{Require (-not $options.ContainsKey('pre_attach_setup')) 'Unrequested pre-attach setup.'}
        if($privateAdmission){
            Require ($options.ContainsKey('private_campaign')) 'Missing private session binding.'
            Assert-Bc2PrivateSamePin $options.private_campaign.binding $privateAdmission.binding
            Assert-Bc2PrivateSamePin $options.private_campaign.normal_artifacts_receipt $privateAdmission.specification.normal_artifacts_receipt
            Assert-Bc2PrivateSamePin $options.private_campaign.private_artifacts_receipt $privateAdmission.specification.private_artifacts_receipt
            Assert-Bc2PrivateMarkers $candidate $options.private_campaign.markers
        }else{Require (-not $options.ContainsKey('private_campaign')) 'Unrequested private campaign session.'}
        $script:folder=$candidate;$receipt.session_folder=$candidate
        PrepareMechanismMarker
        Save 'session-link.json' @{folder=$candidate;options=(Join-Path $candidate 'options.json');session=(Join-Path $candidate 'session.json');sim_state=(Join-Path $simDir 'state.json');command=(Join-Path $simDir 'command.txt');startup_receipt=(Join-Path $OutputDir 'sim-startup.json');root_commands_ready=$false;capture_plan=$capturePlan;mechanism_snapshots=$mechanismMarker;stop_request=(Join-Path $candidate 'stop.request');supervisor=$owned.supervisor.identity}
        Write-Output ('SESSION_FOLDER='+$candidate)
        Write-Output ('SIM_DIRECTORY='+$simDir)
    }
    $record=Join-Path $folder 'session.json';if(-not [IO.File]::Exists($record)){return}
    $script:session=Json $record
    Require ($session.schema -ceq 'bc2vr.session.v1' -and $session.source_sha256 -ceq $ExpectedSourceSha256 -and $session.version -ceq $runtimeVersion) 'Session source identity differs.'
    SameIdentity $session.supervisor $owned.supervisor.identity
    ObservePerformanceSession $session
    if($PerformanceMode -and $session.ContainsKey('performance_mode') -and $session.performance_mode.ContainsKey('watcher')){
        Bind 'graphics_watcher' $session.performance_mode.watcher $pwsh $owned.supervisor.identity.pid
    }
    foreach($kind in 'game','host','trace','resolution_helper','resolution_release_helper','setup_helper'){
        if(-not $session.ContainsKey($kind) -or -not $session[$kind]){continue}
        $path=switch($kind){setup_helper {$pwsh} game {$gameExe} host {Join-Path $runtimeRoot 'bin/BC2XrHost.exe'} trace {Join-Path $runtimeRoot 'bin/BC2NativeTrace.exe'} default {Join-Path $runtimeRoot 'bin/BC2NativeResolution.exe'}}
        Bind $kind $session[$kind] $path $owned.supervisor.identity.pid
    }
    if($owned.ContainsKey('game')){$receipt.actual_game_started=$true}
    if($owned.ContainsKey('host') -and $null -eq $hostStartedAt){$script:hostStartedAt=$clock.Elapsed.TotalSeconds}
    if($session.state -eq 'running'){$script:runningObserved=$true}
    if($session.state -eq 'failed'){throw ('Normal launcher failed: '+$session.error)}
}
function RequestStop([string]$Reason){
    if(-not $stopReason){$script:stopReason=$Reason;Save 'stop-reason.json' @{reason=$Reason;elapsed_seconds=$clock.Elapsed.TotalSeconds;utc=[DateTime]::UtcNow.ToString('o')}}
    if($folder -and -not $stopWritten){
        $path=Join-Path $folder 'stop.request';Assert-FvrNoLinks $path
        if(-not [IO.File]::Exists($path)){[IO.File]::WriteAllText($path,'Bounded xr-sim runner: '+$stopReason,$utf8)}
        $script:stopWritten=$true
    }
}
function ObserveSimulatorStartup($Sim){
    if($simObserved -or $stopReason){return}
    Require (-not $owned.host.process.HasExited) 'Actual host exited before simulator startup completed.'
    if(-not $startup){
        if(-not $Sim.sessionRunning -or -not $Sim.actionsAttached){return}
        $script:startup=@{status='preparing';host=$owned.host.identity;before=$Sim;commands=$initialCommands;before_sequence=[long]$Sim.cmdSeq;target_sequence=([long]$Sim.cmdSeq+$initialCommands.Count);sent_at_seconds=$clock.Elapsed.TotalSeconds;ack_state=$null;root_commands_ready=$false}
        Save 'sim-startup.json' $startup
        Require ($Sim.cmdSeq -eq 0 -and $Sim.captureSeq -eq 0) 'Simulator was commanded before the runner startup handshake; preserve the competing episode.'
        $commandPath=Join-Path $simDir 'command.txt'
        Require (-not(Test-Path -LiteralPath $commandPath)) 'Another writer created command.txt before the runner startup handshake.'
        # A pre-launch file is intentionally ignored by xr-sim. Publish only after
        # the retained actual host has initialized, never to the preflight PID.
        Start-Sleep -Milliseconds 25
        $temporary=Join-Path $simDir 'command-startup.tmp'
        [IO.File]::WriteAllText($temporary,($initialCommands -join "`n")+"`n",$utf8)
        [IO.File]::Move($temporary,$commandPath,$false)
        $startup.command_file=Pin $commandPath
        $startup.status='awaiting_ack';Save 'sim-startup.json' $startup
        return
    }
    Require ($clock.Elapsed.TotalSeconds-$startup.sent_at_seconds -le 15) 'Actual-host startup command handshake timed out.'
    Require ((Pin $startup.command_file.path).sha256 -ceq $startup.command_file.sha256) 'Startup command file changed before ownership was handed to root.'
    Require ($Sim.cmdSeq -ge $startup.before_sequence -and $Sim.cmdSeq -le $startup.target_sequence) 'Unexpected startup command sequence; another writer may own the simulator.'
    if($Sim.cmdSeq -ne $startup.target_sequence){return}
    Require ($Sim.lastCmd -ceq $initialCommands[-1]) 'Startup sequence did not acknowledge this runner nonce.'
    if(-not $startup.ack_state){
        $ackPath=Join-Path $simDir 'ack.txt';if(-not [IO.File]::Exists($ackPath)){return}
        Require ((Get-Item -LiteralPath $ackPath).Length -le 4KB) 'Oversized simulator startup acknowledgement.'
        $ackText=[IO.File]::ReadAllText($ackPath)
        if($ackText -cnotmatch '\Aseq=(\d+) frame=(\d+) error=-\r?\nlast=([^\r\n]*)\r?\n\z'){return} # The recorder writes ack.txt in place.
        if([long]$Matches[1] -ne $startup.target_sequence){return}
        Require ($Matches[3] -ceq ($initialCommands -join ' | ')) 'Startup acknowledgement belongs to a different command batch.'
        $startup.ack_frame=[long]$Matches[2];$startup.ack_text=$ackText;$startup.ack_state=$Sim
        [IO.File]::WriteAllText((Join-Path $OutputDir 'sim-startup-ack.txt'),$ackText,$utf8)
        $startup.status='awaiting_committed_frame';Save 'sim-startup.json' $startup
    }
    if(-not $Sim.sessionRunning -or $Sim.frame -lt $startup.ack_frame+2 -or $Sim.endFrames -lt $startup.ack_frame+1){return}
    Require ($Sim.paceMode -ceq 'free' -and $Sim.refreshHz -eq 60 -and $Sim.captureSeq -eq 0 -and -not $Sim.handL.followsHead -and -not $Sim.handR.followsHead) 'Startup pacing/capture/follow state did not commit.'
    foreach($pose in @(@{actual=$Sim.head;expected=@(0,1.60,0)},@{actual=$Sim.handL;expected=@(-0.20,1.30,-0.35)},@{actual=$Sim.handR;expected=@(0.20,1.30,-0.35)})){
        Require ([bool]$pose.actual.valid) 'Startup pose is not valid.'
        for($axis=0;$axis -lt 3;$axis++){Require ([Math]::Abs($pose.actual.pos[$axis]-$pose.expected[$axis]) -lt 0.001 -and [Math]::Abs($pose.actual.ypr[$axis]) -lt 0.001) 'Startup neutral pose did not commit.'}
    }
    foreach($name in 'a','b','x','y','menu','clickL','clickR','restL','restR','trigL','trigR','gripL','gripR','stickL','stickR'){
        foreach($value in @($Sim.controls[$name])){Require ($value -eq 0) 'Startup input clear did not commit.'}
    }
    $startup.settled_state=$Sim;$startup.status='ready';$startup.root_commands_ready=$true
    Save 'sim-startup.json' $startup;Save 'sim-host-initial.json' $Sim
    $link=Json (Join-Path $OutputDir 'session-link.json');$link.root_commands_ready=$true;$link.host=$owned.host.identity;Save 'session-link.json' $link
    $script:simObserved=$true
    Write-Output ('SIM_HOST_PID='+$Sim.pid)
}
function ObserveBounds {
    if($clock.ElapsedMilliseconds-$lastScan -lt 1000){return};$script:lastScan=$clock.ElapsedMilliseconds
    $bytes=0L
    foreach($directory in @($OutputDir,$folder)|Where-Object {$_}){
        foreach($file in Get-ChildItem -LiteralPath $directory -File -Recurse -Force){Assert-FvrNoLinks $file.FullName;$bytes+=$file.Length}
    }
    Require ($bytes -le $plannedBytes) 'Run plus normal-session output exceeded the admitted byte watchdog.'
    if($null -ne $hostStartedAt -and -not $simObserved -and -not $stopReason){Require ($clock.Elapsed.TotalSeconds-$hostStartedAt -le 20) 'Retained actual-host simulator startup did not complete within twenty seconds.'}
    $statePath=Join-Path $simDir 'state.json'
    if([IO.File]::Exists($statePath) -and $owned.ContainsKey('host')){
        # Spend at most 250 ms inside the existing stop/startup clocks. Never
        # cache a prior state or turn a missing/invalid read into an observation.
        $readUntil=[Math]::Min($clock.Elapsed.TotalSeconds+0.25,$TimeoutSeconds-60)
        if($null -ne $hostStartedAt){$readUntil=[Math]::Min($readUntil,$hostStartedAt+$Seconds)}
        if(-not $simObserved -and -not $stopReason){
            if($null -ne $hostStartedAt){$readUntil=[Math]::Min($readUntil,$hostStartedAt+20)}
            if($startup){$readUntil=[Math]::Min($readUntil,$startup.sent_at_seconds+15)}
        }
        $sim=Json $statePath -SharingDeadlineSeconds $readUntil
        # Preflight and actual host can publish different PIDs into the same directory.
        if($sim.pid -eq $owned.host.identity.pid){
            Require ($sim.runtime -ceq 'xr-sim' -and $sim.system -ceq $systemName -and $sim.headsetConfig -ieq $profilePath) 'Actual host simulator identity differs.'
            Require ($sim.errors -eq 0 -and [string]::IsNullOrEmpty($sim.lastCmdError)) 'Simulator command/runtime error; retain state and acknowledgement.'
            Require ($sim.captureSeq -le $CaptureCount) 'Simulator composition count exceeded the admitted capture plan.'
            ObserveSimulatorStartup $sim
        }
    }
}
try{
    Require $IsWindows 'Windows PowerShell 7.5+ is required.'
    . (Join-Path $sourceRoot 'launcher/Launcher.Core.ps1')
    . (Join-Path $sourceRoot 'launcher/runtime/Runtime.Core.ps1')
    . (Join-Path $sourceRoot 'launcher/runtime/Startup.ps1')
    . (Join-Path $sourceRoot 'launcher/runtime/Resolution.ps1')
    . (Join-Path $sourceRoot 'tools/StorageBudget.ps1')
    $RepositoryRoot=Full $RepositoryRoot;$InstallRoot=Full $InstallRoot;$OutputDir=Full $OutputDir;$ProfileDirectory=Full $ProfileDirectory
    if($PerformanceMode){Require ($ProfileDirectory -ieq [IO.Path]::GetFullPath((Join-Path ([Environment]::GetFolderPath('MyDocuments')) 'BFBC2'))) 'Performance mode requires the normal launcher graphics profile directory.'}
    AssertObservationLimits $Seconds $TimeoutSeconds ([bool]$ExtendedObservation)
    Require ($MaxGeneratedBytes -le 40GB -or -not [string]::IsNullOrWhiteSpace($GeneratedBudgetReason)) 'MaxGeneratedBytes above 40 GiB requires an explicit GeneratedBudgetReason.'
    $receipt.limits.extended_observation=[bool]$ExtendedObservation
    Require ([IO.Path]::GetPathRoot($RepositoryRoot) -ieq 'G:\' -and [IO.Path]::GetPathRoot($InstallRoot) -ieq 'G:\') 'Local development repository/install must remain on G:.'
    Require ($OutputDir.StartsWith((Join-Path $RepositoryRoot 'reports')+'\',[StringComparison]::OrdinalIgnoreCase)) 'Use a fresh output under RepositoryRoot/reports.'
    Require (-not(Test-Path -LiteralPath $OutputDir)) 'Existing output refused; preserve all prior episodes.'
    $identity=[Security.Principal.WindowsIdentity]::GetCurrent()
    try{Require (-not [Security.Principal.WindowsPrincipal]::new($identity).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) 'Elevated Windows token refused; OpenXR environment overrides require a normal token.'}finally{$identity.Dispose()}
    $budget=Assert-FvrStorageBudget -RepositoryRoot $RepositoryRoot -OutputPath $OutputDir -Operation Session -PlannedBytes $plannedBytes -RawCaptureBytes $rawBytes -MaxRawCaptureBytes $rawLimitBytes -MaxGeneratedBytes $MaxGeneratedBytes -AdditionalRoots @($InstallRoot,$sourceRoot,(Join-Path (Split-Path -Parent $RepositoryRoot) 'bc2vr-task-worktrees'))
    $budget|Add-Member -NotePropertyName raw_budget_reason -NotePropertyValue $receipt.limits.raw_budget_reason
    $budget|Add-Member -NotePropertyName generated_budget_reason -NotePropertyValue $GeneratedBudgetReason
    $null=[IO.Directory]::CreateDirectory($OutputDir);$outputReady=$true;Save 'storage.json' $budget
    Save 'run-manifest.json' @{schema='fvr.xr-game-run.v1';output_directory=$OutputDir;version=$Version;source_sha256=$ExpectedSourceSha256;artifact_binding_path=(Join-Path $OutputDir 'artifact-binding.json');limits=$receipt.limits;capture_plan=$capturePlan;mechanism_snapshots=$mechanismMarker;profile_attribution=$attributionProfile;private_source_pipeline=$sourcePipeline;performance_mode=$performanceSelection;startup_receipt=(Join-Path $OutputDir 'sim-startup.json');command_authority='Runner owns startup. Root may send acknowledged batches only after sim-startup.json has status ready and exact retained host identity.'}
    NoOtherProcesses -BeforeLaunch
    $null=Assert-FvrVersion $Version
    $packageRoot=Get-FvrChild $InstallRoot ('versions/'+$Version)
    $package=Test-FvrPackage $packageRoot $Version
    Require ($package.runtime_entry -ceq 'runtime/Entry.ps1') 'Expected the normal installed runtime entry.'
    $runtimeRoot=Full (Join-Path $packageRoot 'runtime');$config=Json (Join-Path $runtimeRoot 'runtime.json') 4MB
    $baselineRuntimePin=Pin (Join-Path $runtimeRoot 'runtime.json')
    $baselineSource=$ExpectedSourceSha256
    if($PrivateSessionBinding -or $PrivateSessionBindingSha256){
        Require ($MechanismSnapshots -and -not $PreAttachSetupSpec -and -not $PreAttachSetupSpecSha256) 'Private retained provisioning requires mechanism snapshots and no pre-attach setup.'
        . (Join-Path $sourceRoot 'launcher/runtime/PrivateSession.ps1')
        $PrivateSessionBinding=Full $PrivateSessionBinding
        Require ((Pin $PrivateSessionBinding).sha256 -ceq $PrivateSessionBindingSha256) 'Private binding differs.'
        $privateSpec=Json $PrivateSessionBinding
        $privateAdmission=Get-Bc2PrivateAdmission $PrivateSessionBinding $PrivateSessionBindingSha256 (Full $privateSpec.runtime_root) $InstallRoot
        Require ($privateSpec.baseline_version -ceq $Version -and $privateSpec.source_sha256 -ceq $ExpectedSourceSha256) 'Private baseline/candidate source differs.'
        Assert-Bc2PrivateSamePin $privateSpec.installed_runtime_manifest $baselineRuntimePin
        $baselineSource=$privateAdmission.baseline.source_sha256
    }
    Require ($config.schema -ceq 'bc2vr.runtime.v1' -and $config.version -ceq $Version -and $config.diagnostic_only -eq $false -and $config.source_sha256 -ceq $baselineSource) 'Installed normal runtime/source differs.'
    foreach($file in $config.files){Require ((Pin (Get-FvrChild $runtimeRoot $file.path) 256MB).sha256 -ceq $file.sha256) ('Runtime file differs: '+$file.path)}
    $active=Json (Join-Path $InstallRoot 'active.json');Require ($active.current -ceq $Version) 'Requested version is not the active install.'
    if($privateAdmission){$runtimeRoot=Full $privateSpec.runtime_root;$config=$privateAdmission.config;$runtimeVersion=$config.version}
    $settingsPath=Join-Path $InstallRoot 'data/settings.json';$settings=Json $settingsPath
    $gameExe=Full (Join-Path $settings.game_path 'BFBC2Game.exe');$gamePin=Pin $gameExe 128MB
    Require ($gamePin.sha256 -cin $config.game_sha256) 'Installed normal runtime does not admit this game hash.'
    $stateRoot=Full (Join-Path $InstallRoot 'data/runtime');$priorActive=$null
    if(Test-Path -LiteralPath (Join-Path $stateRoot 'active-session.json')){
        $priorActive=Json (Join-Path $stateRoot 'active-session.json');$priorProcess=Find-Bc2OwnedProcess $priorActive.supervisor
        if($priorProcess){$priorProcess.Dispose();throw 'An earlier launcher supervisor is still running.'}
    }
    $cache=Get-FvrChild (Join-Path $stateRoot 'cache') $config.asset_recipe_sha256
    Require (Test-Bc2AssetCache $cache $config.asset_recipe_sha256 $gamePin.sha256) 'Prepare and verify the normal installed asset cache before this bounded session.'
    $local=Json (Full $LocalConfig)
    $runtimeManifest=Full $local.RuntimeManifest;$tapeManifestDirectory=Full $local.TapeManifestDirectory
    Require ($local.TapeLayerName -ceq 'XR_APILAYER_XRTAPE_recorder') 'Wrong recorder layer.'
    $tapeManifests=@(Get-ChildItem -LiteralPath $tapeManifestDirectory -Filter '*.json' -File|Where-Object {(Json $_.FullName).api_layer.name -ceq $local.TapeLayerName})
    Require ($tapeManifests.Count -eq 1) 'Exactly one selected recorder manifest is required.'
    $tapeManifest=$tapeManifests[0].FullName
    $runtimeLibrary=Library $runtimeManifest (Json $runtimeManifest).runtime.library_path
    $tapeLibrary=Library $tapeManifest (Json $tapeManifest).api_layer.library_path
    $pwsh=Full (Join-Path $PSHOME 'pwsh.exe')
    $pePaths=@{};foreach($name in 'BC2XrHost.exe','openxr_loader.dll'){$pePaths[(Join-Path $runtimeRoot ('bin/'+$name))]=0x8664}
    foreach($name in 'BC2NativeTrace.exe','BC2NativeProbe.dll','BC2NativeResolution.exe','BC2ResolutionBootstrap.dll'){$pePaths[(Join-Path $runtimeRoot ('bin/'+$name))]=0x14c}
    $pePaths[$runtimeLibrary]=0x8664;$pePaths[$tapeLibrary]=0x8664;$pePaths[$gameExe]=0x14c;$pePaths[$pwsh]=0x8664
    if($privateAdmission){$pePaths[(Join-Path $runtimeRoot 'bin/Bc2CampaignProvisioningCommand.exe')]=0x14c}
    $binaryPins=@();foreach($path in $pePaths.Keys){$binaryPins+=Pin $path 256MB;Pe $path $pePaths[$path]}
    Save 'artifact-binding.json' @{version=$Version;source_sha256=$ExpectedSourceSha256;package=(Pin (Join-Path $packageRoot 'package.json'));runtime=(Pin (Join-Path $runtimeRoot 'runtime.json'));local_config=(Pin $LocalConfig);runtime_manifest=(Pin $runtimeManifest);tape_manifest=(Pin $tapeManifest);binaries=$binaryPins;game=$gamePin}
    $manifest=Json (Join-Path $OutputDir 'run-manifest.json')
    if($privateAdmission){
        $privateRecord=@{diagnostic_only=$true;binding=$privateAdmission.binding;runtime_version=$runtimeVersion;baseline_version=$Version;baseline_runtime=$baselineRuntimePin;
            source_commit=$privateSpec.source_commit;normal_artifacts_receipt=$privateSpec.normal_artifacts_receipt;private_artifacts_receipt=$privateSpec.private_artifacts_receipt;shared_build_state=$privateSpec.shared_build_state}
        $manifest.version=$runtimeVersion;$receipt.version=$runtimeVersion
        $manifest.private_campaign=$privateRecord;$receipt.private_campaign=$privateRecord
        $artifacts=Json (Join-Path $OutputDir 'artifact-binding.json');$artifacts.version=$runtimeVersion;$artifacts.private_campaign=$privateRecord;Save 'artifact-binding.json' $artifacts
    }
    $manifest.runtime_manifest=Pin $runtimeManifest;$manifest.runtime_library=Pin $runtimeLibrary 256MB;$manifest.host_executable=Pin (Join-Path $runtimeRoot 'bin/BC2XrHost.exe') 256MB
    Save 'run-manifest.json' $manifest
    if($PreAttachSetupSpec -or $PreAttachSetupSpecSha256){
        $PreAttachSetupSpec=Full $PreAttachSetupSpec
        Require ((Pin $PreAttachSetupSpec).sha256 -ceq $PreAttachSetupSpecSha256) 'Pre-launch setup specification changed.'
        $preAttachSpec=Json $PreAttachSetupSpec
        Require ($preAttachSpec.schema -ceq 'bc2vr.pre-attach-m95-spec.v1' -and $preAttachSpec.output_directory -ieq (Join-Path $OutputDir 'pre-attach-setup')) 'Setup output must belong to this exact new run.'
        Require (($preAttachSpec.maximum_seconds -is [int] -or $preAttachSpec.maximum_seconds -is [long]) -and $preAttachSpec.maximum_seconds -ge 15 -and $preAttachSpec.maximum_seconds -le 90) 'Invalid setup admission bound.'
        foreach($key in 'runner','bench','plan','python','preflight','server_capture','validator','local_config'){
            Require ((Pin $preAttachSpec[$key].path 256MB).sha256 -ceq $preAttachSpec[$key].sha256) ('Setup input changed: '+$key)
        }
        Require ($preAttachSpec.python.path -ieq (Join-Path $runtimeRoot 'python/python.exe')) 'Setup must use the selected installed Python.'
        # Campaign bench plans are bounded to 8 MiB by bc2_campaign_bench.write_new.
        $setupPlan=Json $preAttachSpec.plan.path 8MB
        $preAttachProvisionRunner=Full $setupPlan.provisioning.runner.path
        Require ((Pin $preAttachProvisionRunner 256MB).sha256 -ceq $setupPlan.provisioning.runner.sha256) 'Provisioning runner changed.'
        Save 'pre-attach-specification.json' @{specification=(Pin $PreAttachSetupSpec);admission_seconds=$preAttachSpec.maximum_seconds;outer_deadline_unchanged=$true}
    }
    if($ContinueCampaignFromRun){
        Require (-not $CampaignSeedFile) 'ContinueCampaignFromRun and CampaignSeedFile cannot be combined.'
        Require (-not $preAttachSpec -or -not $preAttachSpec.ContainsKey('campaign_seed')) 'Campaign continuation and pre-attach campaign_seed cannot be combined.'
        $receipt.campaign_continuation=GetCampaignContinuation $ContinueCampaignFromRun $gamePin
        Save 'campaign-continuation.json' $receipt.campaign_continuation
    }
    if($CampaignSeedFile){
        Require (-not $preAttachSpec -or -not $preAttachSpec.ContainsKey('campaign_seed')) 'CampaignSeedFile and pre-attach campaign_seed cannot be combined.'
        $receipt.campaign_seed=GetCampaignSeed $CampaignSeedFile $gamePin
        Save 'campaign-seed.json' $receipt.campaign_seed
    }
    foreach($name in 'GameSettings.bin','GameSettings.ini','settings.ini'){$profiles+=Backup (Join-Path $ProfileDirectory $name) ('profile-before/'+$name)}
    foreach($name in 'active.json','data/settings.json'){$launcherFiles+=Backup (Join-Path $InstallRoot $name) ('launcher-before/'+$name)}
    Save 'originals.json' @{profiles=$profiles;launcher=$launcherFiles}
    if($ContinueCampaignFromRun){PrepareCampaignContinuation $receipt.campaign_continuation}
    if($CampaignSeedFile){PrepareCampaignSeed $receipt.campaign_seed}
    if($preAttachSpec -and $preAttachSpec.ContainsKey('campaign_seed')){
        $seed=$preAttachSpec.campaign_seed;$observedSeed=Pin $seed.path
        Require ($seed.sha256 -ceq 'b2a9f98f0c0609b4bf793760634754e8c616f7095d07249c7fe2b4cb74083988' -and $seed.bytes -eq 117657 -and $observedSeed.sha256 -ceq $seed.sha256 -and $observedSeed.bytes -eq $seed.bytes) 'Reviewed SP03 save seed changed.'
        $targets=@($profiles|Where-Object {[IO.Path]::GetFileName($_.original.path) -ceq 'GameSettings.bin'})
        Require ($targets.Count -eq 1) 'Exactly one original profile backup required for the seed.'
        $target=$targets[0];NoOtherProcesses -BeforeLaunch
        Require ((Pin $target.original.path).sha256 -ceq $target.original.sha256) 'Original profile changed before seed preparation.'
        [IO.File]::WriteAllBytes($target.original.path,[IO.File]::ReadAllBytes($seed.path))
        Require ((Pin $target.original.path).sha256 -ceq $seed.sha256) 'Seed preparation differs.'
        $preparedProfileHashes[$target.original.path]=$seed.sha256
        Save 'pre-attach-profile-preparation.json' @{source=$observedSeed;original=$target.original;backup=$target.backup;prepared=(Pin $target.original.path);exact_original_restoration_required=$true}
    }
    # Entry reuses these small preflight paths; retain the preceding episode first.
    foreach($name in 'active-session.json','xr-selection.json','xr-preflight.stdout.log','xr-preflight.stderr.log','launcher-placement.json'){
        $path=Join-Path $stateRoot $name;if([IO.File]::Exists($path)){$null=Backup $path ('state-before/'+$name)}
    }
    $simDir=Join-Path $OutputDir 'sim';$tapeDir=Join-Path $OutputDir 'tape';$scratch=Join-Path $OutputDir 'temp'
    foreach($directory in @($simDir,$tapeDir,$scratch)){$null=[IO.Directory]::CreateDirectory($directory)}
    $profilePath=Join-Path $simDir 'headset.json';$systemName='BC2XR actual game 1024x768'
    $views=@(
        @{recommendedImageRectWidth=1024;recommendedImageRectHeight=768;maxImageRectWidth=1024;maxImageRectHeight=768;fov=@{angleLeft=-0.95;angleRight=0.75;angleUp=0.85;angleDown=-0.85}},
        @{recommendedImageRectWidth=1024;recommendedImageRectHeight=768;maxImageRectWidth=1024;maxImageRectHeight=768;fov=@{angleLeft=-0.75;angleRight=0.95;angleUp=0.85;angleDown=-0.85}})
    Write-FvrJson $profilePath @{schemaVersion=1;systemName=$systemName;views=$views}
    $initialCommands=@('pace free 60','capture off','capture layers off',"capture size $CaptureWidth $CaptureHeight",'compose oncapture','input clear',
        'head pose 0 1.60 0 0 0 0','hand l follow off','hand r follow off',
        'hand l grip pose -0.20 1.30 -0.35 0 0 0','hand r grip pose 0.20 1.30 -0.35 0 0 0',
        'hand l aimtrim 0 0','hand r aimtrim 0 0',('note startup-'+[guid]::NewGuid().ToString('N')))
    Require (($initialCommands -join ' | ').Length -lt 508) 'Startup batch exceeds simulator acknowledgement text bound.'
    Save 'initial-commands.json' @{commands=$initialCommands;capture_plan=$capturePlan;capture_authority='Root may request individually acknowledged named shots within capture_plan after the exact-host startup handshake; preserve each batch and acknowledgement.'}
    $environment=@{XR_RUNTIME_JSON=$runtimeManifest;XR_API_LAYER_PATH=$tapeManifestDirectory;XR_ENABLE_API_LAYERS=$local.TapeLayerName;XRSIM_DIR=$simDir;XRSIM_HEADSET_CONFIG=$profilePath;XRTAPE_DIR=$tapeDir;XRTAPE_MAX_FRAMES='30000'}
    $psi=[Diagnostics.ProcessStartInfo]::new();$psi.FileName=$pwsh;$psi.WorkingDirectory=$OutputDir
    $psi.UseShellExecute=$false;$psi.CreateNoWindow=$true;$psi.WindowStyle=[Diagnostics.ProcessWindowStyle]::Hidden
    # No caller/global XR mutation: launcher descendants inherit this isolated map.
    foreach($key in @($psi.Environment.Keys)){if($key -match '^(XR_|XRSIM_|XRTAPE_|BVR_XRSIM_|FVR_|BC2_)'){$null=$psi.Environment.Remove($key)}}
    # Admit only explicit private controls after scrubbing every inherited flag.
    $environment[$privatePresentation.environment_variable]=$privatePresentation.environment_value
    $permittedPrivateKeys=@($privatePresentation.environment_variable)
    if($ProfileAttribution){
        foreach($key in $attributionProfile.environment.Keys){$environment[$key]=$attributionProfile.environment[$key]}
        $permittedPrivateKeys+=@('FVR_BC2_PRIVATE_CPU_ATTRIBUTION','FVR_PRIVATE_GPU_TIMING436')
    }
    foreach($key in $environment.Keys){$psi.Environment[$key]=[string]$environment[$key]}
    $privateEnvironmentKeys=@($psi.Environment.Keys|Where-Object {$_ -match '^(FVR_|BC2_)'})
    Require ($privateEnvironmentKeys.Count -eq $permittedPrivateKeys.Count -and @($privateEnvironmentKeys|Where-Object {$_ -cnotin $permittedPrivateKeys}).Count -eq 0 -and $psi.Environment[$privatePresentation.environment_variable] -ceq $privatePresentation.environment_value) 'Private comparison environment is not isolated.'
    if($ProfileAttribution){
        Require ($psi.Environment['FVR_BC2_PRIVATE_CPU_ATTRIBUTION'] -ceq '12' -and $psi.Environment['FVR_PRIVATE_GPU_TIMING436'] -ceq '8') 'Finite attribution environment differs.'
    }
    $privatePresentation.child_environment_verified=$true;$privatePresentation.other_fvr_bc2_environment_absent=(-not [bool]$ProfileAttribution)
    $attributionProfile.child_environment_verified=$true;$attributionProfile.unapproved_fvr_bc2_environment_absent=$true
    foreach($key in 'TEMP','TMP','TMPDIR'){$psi.Environment[$key]=$scratch};$psi.Environment['PYTHONDONTWRITEBYTECODE']='1'
    $launchScript=Join-Path $OutputDir 'launch.ps1'
    # Literal paths are passed as arguments, never interpolated into shell code.
    [IO.File]::WriteAllText($launchScript,"param(`$EntryPath,`$SettingsPath,`$StateRoot,`$Log,`$SetupSpec,`$SetupHash,`$PrivateBinding,`$PrivateHash,`$Performance,`$SourcePipeline)`n`$ErrorActionPreference='Stop'`ntry { `$setup=@{}; if(`$SetupSpec){`$setup.PreAttachSetupSpec=`$SetupSpec;`$setup.PreAttachSetupSpecSha256=`$SetupHash}; if(`$PrivateBinding){`$setup.PrivateSessionBinding=`$PrivateBinding;`$setup.PrivateSessionBindingSha256=`$PrivateHash}; if(`$Performance -ceq '1'){`$setup.PerformanceMode=`$true}elseif(`$Performance -cne '0'){throw 'Invalid performance mode launch selection.'}; if(`$SourcePipeline -ceq '1'){`$setup.PrivateSourcePipeline=`$true}elseif(`$SourcePipeline -cne '0'){throw 'Invalid private source pipeline launch selection.'}; & `$EntryPath -Action Launch -SettingsPath `$SettingsPath -StateRoot `$StateRoot -StartAt MainMenu @setup *>> `$Log } catch { [IO.File]::AppendAllText(`$Log,(`$_ | Out-String),[Text.UTF8Encoding]::new(`$false)); exit 1 }`n",$utf8)
    foreach($arg in @('-NoProfile','-NonInteractive','-ExecutionPolicy','Bypass','-File',$launchScript,(Join-Path $runtimeRoot 'Entry.ps1'),$settingsPath,$stateRoot,(Join-Path $OutputDir 'entry.log'),([string]$PreAttachSetupSpec),([string]$PreAttachSetupSpecSha256),([string]$PrivateSessionBinding),([string]$PrivateSessionBindingSha256),$(if($PerformanceMode){'1'}else{'0'}),$(if($PrivateSourcePipeline){'1'}else{'0'}))){$psi.ArgumentList.Add($arg)}
    Save 'launch.json' @{executable=$pwsh;entry=(Join-Path $runtimeRoot 'Entry.ps1');action='Launch';start_at='MainMenu';activate_window=$false;environment=$environment;state_root=$stateRoot;capture_plan=$capturePlan;mechanism_snapshots_requested=[bool]$MechanismSnapshots;private_gpu_ordered_present=$privatePresentation;profile_attribution=$attributionProfile;private_source_pipeline=$sourcePipeline;performance_mode=$performanceSelection}
    NoOtherProcesses -BeforeLaunch
    foreach($row in $profiles+$launcherFiles){$expected=if($preparedProfileHashes.ContainsKey($row.original.path)){$preparedProfileHashes[$row.original.path]}else{$row.original.sha256};Require ((Pin $row.original.path).sha256 -ceq $expected) 'Prepared/original file changed before launch.'}
    $launchUtc=[DateTime]::UtcNow;$clock.Start();$entry=[Diagnostics.Process]::new();$entry.StartInfo=$psi
    Require ($entry.Start()) 'Could not start owned launcher child.';$null=$entry.Handle
    $owned.entry=@{process=$entry;identity=(Get-Bc2ProcessIdentity $entry -RequireLive $true)};Save 'entry-identity.json' $owned.entry.identity
    Write-Output ('RUN_DIRECTORY='+$OutputDir)
    while($clock.Elapsed.TotalSeconds -lt $TimeoutSeconds-15){
        ObserveSession;NoOtherProcesses;ObserveBounds
        if($entry.HasExited -and $entry.ExitCode -ne 0){throw 'Owned Entry child failed; see entry.log.'}
        if($folder -and [IO.File]::Exists((Join-Path $folder 'stop.request'))){RequestStop 'Root or normal launcher requested stop.'}
        if($null -ne $hostStartedAt -and $clock.Elapsed.TotalSeconds-$hostStartedAt -ge $Seconds){RequestStop 'Host duration reached.'}
        if($clock.Elapsed.TotalSeconds -ge $TimeoutSeconds-60){RequestStop 'Outer deadline cleanup reserve reached.'}
        if($stopReason){RequestStop $stopReason}
        if($owned.ContainsKey('supervisor') -and $owned.supervisor.process.HasExited){break}
        if($entry.HasExited -and -not $folder){throw 'Launcher exited without a new owned session.'}
        Start-Sleep -Milliseconds 100
    }
}catch{
    $failures.Add($_.Exception.Message)
    $receipt.failure_context=@{message=$_.Exception.Message;stack=$_.ScriptStackTrace;position=$_.InvocationInfo.PositionMessage}
    if($_.Exception.Data.Contains('fvr_json_read')){$receipt.failure_context.json_read=$_.Exception.Data['fvr_json_read']}
    try{
        $writeCause=$_.Exception.GetBaseException()
        if($writeCause.Data.Contains('fvr_json_write')){$receipt.failure_context.json_write=$writeCause.Data['fvr_json_write']}
    }catch{} # Diagnostic capture must not replace the original failure.
}
finally{
    if($entry){
        try{
            RequestStop $(if($stopReason){$stopReason}else{'Observation ended or failed.'})
            while($clock.Elapsed.TotalSeconds -lt $TimeoutSeconds-12){
                try{ObserveSession}catch{if(-not $failures.Contains($_.Exception.Message)){$failures.Add($_.Exception.Message)}}
                RequestStop $stopReason
                if($entry.HasExited -and (-not $owned.ContainsKey('supervisor') -or $owned.supervisor.process.HasExited)){break}
                Start-Sleep -Milliseconds 100
            }
            Require ($entry.HasExited) 'Owned Entry is still live at deadline; preserve game/profiles for root cleanup.'
            if($owned.ContainsKey('supervisor')){Require ($owned.supervisor.process.HasExited) 'Supervisor cleanup did not finish at deadline; preserve game/profiles.'}
            NoOtherProcesses
            foreach($kind in 'host','trace','resolution_helper','resolution_release_helper','setup_helper'){if($owned.ContainsKey($kind)){Require ($owned[$kind].process.HasExited) "Owned $kind cleanup remains pending; no game close/restoration."}}
            if($preAttachSpec){Require (-not(Get-Process ProvisionRunner -ErrorAction SilentlyContinue)) 'Provisioning helper is still live; no game close or profile restoration.'}
            if($owned.ContainsKey('game')){
                $game=$owned.game.process
                if(-not $game.HasExited){
                    SameIdentity (Get-Bc2ProcessIdentity $game -RequireLive $true) $owned.game.identity
                    Require ((Pin $gameExe 128MB).sha256 -ceq $gamePin.sha256) 'Original game image changed.'
                    $preActivation=$false;$failedWorkerClose=$false
                    $setupClose=$preAttachSpec -and $session.ContainsKey('pre_attach_setup') -and
                        $session.pre_attach_setup.attempted -eq $true -and $session.pre_attach_setup.complete -eq $false -and
                        $session.pre_attach_setup.normal_close_required -eq $true -and -not $session.host -and -not $session.trace
                    if($setupClose){
                        Require ($session.resolution_cleanup -ceq 'pre_attach_failed_normal_close_required') 'Setup failure cleanup boundary differs.'
                        Save 'pre-attach-close-recovery.json' @{normal_close_only=$true;setup_complete=$false;native_retirement_claimed=$false;forced_termination=$false}
                    }
                    if($session.trace){
                        Require ($owned.ContainsKey('trace') -and $owned.trace.process.HasExited) 'Native trace exit identity was not retained.'
                        $bootstrap=Read-Bc2NativeRunOutcome (Join-Path $folder 'native.stdout.log') $game.Id
                        Require ($bootstrap -and $bootstrap.outcome -ceq 'remote_thread_exited' -and $bootstrap.remote_thread_exit_observed -is [bool] -and $bootstrap.remote_thread_exit_observed -and $bootstrap.report_nonempty -is [bool] -and $bootstrap.report_nonempty) 'Native remote thread exit not proven.'
                        $native=Json (Join-Path $folder 'native-trace.json') 160MB
                        # Actual581/584 retained unresolved hooks; Actual644 disabled
                        # hooks and drained reload, but still failed qualification.
                        # Both permit only normal close, never native retirement.
                        $failedWorkerClose=$owned.trace.process.ExitCode -eq 2 -and $bootstrap.probe_exit -eq 12 -and
                            $bootstrap.module_retained_until_game_exit -is [bool] -and $bootstrap.module_retained_until_game_exit -and
                            $native.state -is [string] -and $native.state -ceq 'observed' -and
                            ($native.pid -is [int] -or $native.pid -is [long]) -and $native.pid -eq $game.Id -and
                            $native.hooks_disabled -is [bool] -and
                            (-not $native.hooks_disabled -or
                                ($native.gameplay.reload_flow.drained -is [bool] -and $native.gameplay.reload_flow.drained -and
                                 ($native.gameplay.reload_flow.in_flight -is [int] -or $native.gameplay.reload_flow.in_flight -is [long]) -and
                                 $native.gameplay.reload_flow.in_flight -eq 0)) -and
                            $native.module_retained_until_exit -is [bool] -and $native.module_retained_until_exit
                        if($failedWorkerClose){
                            $nativePin=Pin (Join-Path $folder 'native-trace.json') 160MB
                            Require ($bootstrap.report_bytes -eq $nativePin.bytes) 'Native worker report length differs from its exit outcome.'
                            Require ($session.state -ceq 'failed') 'Native failed-close requires the original failed session.'
                            if($session.ContainsKey('resolution_lease')){
                                Require ($session.resolution_cleanup -ceq 'unconfirmed' -and
                                    $session.resolution_cleanup_pending -is [bool] -and $session.resolution_cleanup_pending -and
                                    $session.resolution_guard_released -is [bool] -and -not $session.resolution_guard_released) 'Resolution state does not permit failed-worker normal close.'
                            }
                            $nativeRetired=$false
                            $failures.Add('Native worker exited unsuccessfully; native retirement remains unproven with module retained for exact owned game normal close.')
                            Save 'native-close-recovery.json' @{bootstrap=$bootstrap;native_report=$nativePin;hooks_disabled=$native.hooks_disabled;native_retirement_proven=$false;reload_flow_drain_claimed=$false;resolution_release_confirmed=$false;normal_close_only=$true;forced_termination=$false}
                        }else{
                            Require ($native.hooks_disabled -is [bool] -and $native.hooks_disabled) 'Native hooks are not proven disabled.'
                            # These exact setup errors precede gameplay Start. Never use a
                            # prefix match: later failures still require reload drain.
                            $preActivationErrors=@(
                                'Main-menu startup requires native menu capture and input',
                                'Native Present graphics bootstrap unavailable',
                                'Native Present graphics bootstrap unavailable: swap-chain device query or identity failed',
                                'Native Present graphics bootstrap unavailable: no immediate context',
                                'Native Present graphics bootstrap unavailable: missing context method',
                                'Native Present graphics bootstrap unavailable: renderer changed during capture',
                                'Native Present graphics bootstrap unavailable: no Present callback before deadline',
                                'Native Present graphics bootstrap unavailable: only foreign swap chains presented',
                                'Native Present graphics bootstrap unavailable: only test presents on the expected chain',
                                'Native Present graphics bootstrap unavailable: expected chain no longer matches the renderer',
                                'Native Present graphics bootstrap unavailable: capture did not publish before deadline')
                            $preActivation=$owned.trace.process.ExitCode -eq 2 -and ($bootstrap.probe_exit -is [int] -or $bootstrap.probe_exit -is [long]) -and $bootstrap.probe_exit -eq 20 -and $native.state -is [string] -and $native.state -ceq 'failed' -and $native.error -is [string] -and $native.error -cin $preActivationErrors
                            if(-not $preActivation){Require ($owned.trace.process.ExitCode -eq 0 -and $bootstrap.probe_exit -eq 0 -and $native.pid -eq $game.Id -and $native.gameplay.reload_flow.drained -is [bool] -and $native.gameplay.reload_flow.drained -and $native.gameplay.reload_flow.in_flight -eq 0) 'Native clean retirement requires successful trace/probe exits, matching game PID, and reload drain.'}
                            $nativeRetired=-not $preActivation
                            if($preActivation){$failures.Add('Native pre-activation refusal: '+$native.error)}
                            Save 'native-retirement.json' @{bootstrap=$bootstrap;hooks_disabled=$true;pre_activation_refusal=$preActivation;native_retirement_proven=$nativeRetired;reload_flow_drained=(-not $preActivation);native_report=(Pin (Join-Path $folder 'native-trace.json') 160MB)}
                        }
                    }
                    # The supervisor leaves resolution release pending on trace exit 2.
                    # The explicit early refusal or ended failed worker may use normal
                    # process exit; neither establishes guard release or reload retirement.
                    if($session.ContainsKey('resolution_lease') -and -not $preActivation -and -not $setupClose -and -not $failedWorkerClose){
                        $released=Json (Join-Path $folder 'resolution-release.json')
                        Assert-Bc2ResolutionIdentity $released $session.resolution_lease 'release'
                        try{Assert-Bc2ResolutionReleased $released $session.resolution_lease}catch{
                            # Actual514 retired gameplay and restored/drained the resolution
                            # hook, but the inactive guard remained resident until game exit.
                            # Normal close of this exact owned process can finish teardown;
                            # retain the release failure instead of claiming a clean run.
                            Require ($nativeRetired -and $released.passed -is [bool] -and -not $released.passed -and
                                $released.hook_restored -is [bool] -and $released.hook_restored -and
                                $released.callbacks_drained -is [bool] -and $released.callbacks_drained -and
                                $released.guard_active -is [bool] -and -not $released.guard_active -and
                                $released.guard_cleanup_pending -is [bool] -and $released.guard_cleanup_pending -and
                                $released.module_retained_until_game_exit -is [bool] -and $released.module_retained_until_game_exit -and
                                $released.game_exited -is [bool] -and -not $released.game_exited) 'Resolution cleanup does not permit retained-module process-exit recovery.'
                            $failures.Add('Resolution release remained unconfirmed; exact owned game normal close used for process-exit recovery. '+$_.Exception.Message)
                            Save 'resolution-close-recovery.json' @{release=$released;native_retirement_proven=$true;resolution_release_confirmed=$false;normal_close_only=$true;forced_termination=$false}
                        }
                    }
                    NoOtherProcesses -RetainedOnly:$failedWorkerClose;SameIdentity (Get-Bc2ProcessIdentity $game -RequireLive $true) $owned.game.identity
                    if($failedWorkerClose){Require ($clock.Elapsed.TotalSeconds -lt $TimeoutSeconds) 'Failed-worker normal close has no remaining original cleanup time.'}
                    $accepted=$game.CloseMainWindow();Save 'close-request.json' @{game=$owned.game.identity;accepted=$accepted;forced_termination=$false}
                    Require $accepted 'Original game refused normal close; no profile restoration.'
                    $remaining=[Math]::Max(0,[Math]::Min(10000,[int](($TimeoutSeconds-$clock.Elapsed.TotalSeconds)*1000)))
                    Require ($game.WaitForExit($remaining)) 'Original game did not exit before deadline; no force termination or restoration.'
                    $gameClosed=$game.ExitCode -eq 0
                    Save 'game-exit.json' @{game=$owned.game.identity;normal_close_requested=$true;exit_code=$game.ExitCode;normal_close_observed=$gameClosed;forced_termination=$false}
                }else{Save 'game-exit.json' @{game=$owned.game.identity;normal_close_requested=$false;exit_code=$game.ExitCode;normal_close_observed=$false;original_already_absent=$true}}
            }
        }catch{$failures.Add('Cleanup: '+$_.Exception.Message)}
    }
    if($outputReady){
        try{
            if($session -and $session.ContainsKey('performance_mode') -and $session.performance_mode.requested){CompletePerformanceRestoration}
            foreach($value in $owned.Values){Require ($value.process.HasExited) 'A retained owned process is live; profiles remain backed up.'}
            NoOtherProcesses -BeforeLaunch
            foreach($row in $profiles){Require ((Pin $row.backup.path).sha256 -ceq $row.original.sha256) 'Original backup changed.'}
            $after=@();foreach($row in $profiles){$after+=Backup $row.original.path ('profile-after/'+[IO.Path]::GetFileName($row.original.path))};Save 'profile-after.json' $after
            foreach($row in $profiles){
                NoOtherProcesses -BeforeLaunch;Assert-FvrNoLinks $row.original.path
                if((Pin $row.original.path).sha256 -cne $row.original.sha256){[IO.File]::WriteAllBytes($row.original.path,[IO.File]::ReadAllBytes($row.backup.path))}
                Require ((Pin $row.original.path).sha256 -ceq $row.original.sha256) 'Exact profile restoration failed.'
            }
            $restored=$profiles.Count -eq 3
        }catch{$failures.Add('Profiles: '+$_.Exception.Message)}
        try{
            $after=@();foreach($row in $launcherFiles){$pin=Pin $row.original.path;$after+=$pin;Require ($pin.sha256 -ceq $row.original.sha256) 'Launcher active/settings changed; preserve without overwriting another owner.'}
            $launcherPreserved=$launcherFiles.Count -eq 2;Save 'launcher-after.json' @{unchanged=$launcherPreserved;files=$after}
        }catch{$failures.Add('Launcher: '+$_.Exception.Message)}
        try{
            $tapes=@();if(Test-Path -LiteralPath (Join-Path $OutputDir 'tape')){
                foreach($file in Get-ChildItem -LiteralPath (Join-Path $OutputDir 'tape') -Filter '*.ndjson' -File){
                    $row=@{path=$file.FullName;bytes=$file.Length;actual_host=$false;official_check='not_run_by_wrapper'}
                    try{
                        $reader=[IO.File]::OpenText($file.FullName);try{$header=$reader.ReadLine()|ConvertFrom-Json -AsHashtable}finally{$reader.Dispose()}
                        $row.header=$header
                        Require ($header.r -ceq 'header' -and $header.schema -eq 1 -and $header.pointerBits -eq 64 -and $header.layer -ceq 'XR_APILAYER_XRTAPE_recorder') 'Recorder header/schema/bitness differs.'
                        $row.actual_host=$owned.ContainsKey('host') -and $header.pid -eq $owned.host.identity.pid -and $header.exe -ieq $owned.host.identity.path
                        $footer=Get-Content -LiteralPath $file.FullName -Tail 1 -Encoding utf8|ConvertFrom-Json -AsHashtable
                        $row.footer=$footer
                        Require ($footer.r -ceq 'footer' -and $footer.truncated -is [bool] -and -not $footer.truncated -and $footer.recordedFrames -lt 30000) 'Recorder footer missing or frame cap/truncation observed.'
                    }catch{$row.error=$_.Exception.Message;$failures.Add('Tape: '+$file.Name+': '+$_.Exception.Message)}
                    $tapes+=$row
                }
            }
            Save 'tape-index.json' $tapes
            if($owned.ContainsKey('host')){Require (@($tapes|Where-Object {$_.actual_host}).Count -eq 1) 'No unique recorder trace for the retained actual host.'}
            if($folder){[IO.File]::Copy((Join-Path $folder 'session.json'),(Join-Path $OutputDir 'session-final.json'),$false);[IO.File]::Copy((Join-Path $folder 'options.json'),(Join-Path $OutputDir 'options.json'),$false)}
            if($entry -and $entry.HasExited){
                foreach($name in 'active-session.json','xr-selection.json','xr-preflight.stdout.log','xr-preflight.stderr.log','launcher-placement.json'){
                    $path=Join-Path $stateRoot $name;if([IO.File]::Exists($path)){$null=Backup $path ('state-after/'+$name)}
                }
            }
        }catch{$failures.Add('Evidence: '+$_.Exception.Message)}
        if($session -and $session.ContainsKey('host_forced_stop') -and $session.host_forced_stop){$failures.Add('The normal supervisor force-stopped its host; this episode is incomplete.')}
        foreach($kind in 'entry','supervisor','host'){
            if($owned.ContainsKey($kind) -and $owned[$kind].process.HasExited -and $owned[$kind].process.ExitCode -ne 0){$failures.Add("Owned $kind exited nonzero.")}
        }
        $receipt.native_running_observed=$runningObserved;$receipt.simulator_host_observed=$simObserved;$receipt.native_retirement_proven=$nativeRetired
        $receipt.exact_three_profiles_restored=$restored;$receipt.launcher_active_settings_preserved=$launcherPreserved;$receipt.normal_game_close=$gameClosed
        $receipt.elapsed_seconds=$clock.Elapsed.TotalSeconds;$receipt.stop_reason=$stopReason
        $receipt.processes=@(foreach($name in $owned.Keys){$row=$owned[$name];@{name=$name;identity=$row.identity;exited=$row.process.HasExited;exit_code=$(if($row.process.HasExited){$row.process.ExitCode}else{$null})}})
        $receipt.status=if($failures.Count -eq 0 -and $runningObserved -and $simObserved -and $nativeRetired -and $gameClosed -and $restored -and $launcherPreserved){'observed_and_closed'}else{'failed'}
        try{Save 'result.json' $receipt}catch{Write-Warning ('Could not preserve final receipt: '+$_.Exception.Message)}
    }else{
        try{
            $refusal=[IO.Path]::GetFullPath($OutputDir)+'.refused-'+[guid]::NewGuid().ToString('N')+'.json'
            $reportPrefix=Join-Path ([IO.Path]::GetFullPath($RepositoryRoot)) 'reports'
            if($refusal.StartsWith($reportPrefix+'\',[StringComparison]::OrdinalIgnoreCase) -and [IO.Directory]::Exists([IO.Path]::GetDirectoryName($refusal))){
                [IO.File]::WriteAllText($refusal,(@{schema='fvr.xr-game-simulation-refusal.v1';failures=@($failures);game_started=$false;limits=$receipt.limits}|ConvertTo-Json),$utf8)
            }
        }catch{Write-Warning ('Could not preserve refusal receipt: '+$_.Exception.Message)}
    }
    foreach($row in $owned.Values){$row.process.Dispose()}
    foreach($key in $savedTemporary.Keys){[Environment]::SetEnvironmentVariable($key,$savedTemporary[$key],'Process')}
}
if($receipt.status -ne 'observed_and_closed'){throw ('Actual-game simulation incomplete; retain evidence at '+$OutputDir+'. '+($failures -join ' | '))}
Write-Output (Join-Path $OutputDir 'result.json')
