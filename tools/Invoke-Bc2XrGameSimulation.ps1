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
    [ValidateRange(15,300)][int]$Seconds=300,
    [ValidateRange(90,420)][int]$TimeoutSeconds=420
)
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
$receipt=[ordered]@{schema='fvr.xr-game-simulation.v1';status='failed';evidence_level='actual_game_simulated_runtime';
    actual_game_started=$false;native_running_observed=$false;reload_success_claimed=$false;headset_accepted=$false;
    limits=@{host_seconds=$Seconds;outer_seconds=$TimeoutSeconds;planned_bytes=640MB;raw_budget_bytes=(512MB+4*320*240*4*4+4*64KB);raw_budget_limit_bytes=520MB;native_maximum_raw_images=8;sim_compositions=4;sim_capture_width=320;sim_capture_height=240;render_width=1024;render_height=768;tape_max_frames=30000};
    failures=$failures;forced_termination_by_wrapper=$false}
function Require([bool]$Condition,[string]$Message){if(-not $Condition){throw $Message}}
function Full([string]$Path){
    Require ([IO.Path]::IsPathFullyQualified($Path)) "Absolute path required: $Path"
    $full=[IO.Path]::GetFullPath($Path);Assert-FvrNoLinks $full;return $full
}
function Pin([string]$Path,[long]$Maximum=4MB){
    $Path=Full $Path;$item=Get-Item -LiteralPath $Path
    Require (-not $item.PSIsContainer -and $item.Length -le $Maximum) "Invalid or oversized file: $Path"
    return @{path=$Path;bytes=$item.Length;sha256=(Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()}
}
function Json([string]$Path,[long]$Maximum=128KB){
    Require ((Get-Item -LiteralPath $Path).Length -le $Maximum) "JSON exceeds bound: $Path"
    return [IO.File]::ReadAllText($Path)|ConvertFrom-Json -AsHashtable -DateKind String -Depth 100
}
function Save([string]$Name,$Value){Write-FvrJson (Join-Path $OutputDir $Name) $Value}
function SameIdentity($Actual,$Expected){
    Require ($Actual.pid -eq $Expected.pid -and $Actual.start_utc -ceq $Expected.start_utc -and $Actual.path -ieq $Expected.path) 'Retained process identity differs.'
}
function Bind([string]$Name,$Identity,[string]$ExpectedPath,[int]$ParentId){
    if(-not $Identity){return}
    Require ($Identity.path -ieq $ExpectedPath -and [DateTimeOffset]::Parse($Identity.start_utc).UtcDateTime -ge $launchUtc) "Unexpected $Name executable/lifetime."
    if($owned.ContainsKey($Name)){SameIdentity $owned[$Name].identity $Identity;return}
    $process=Find-Bc2OwnedProcess $Identity
    if(-not $process){return} # Preserve failed/short-lived identities in session.json; never select by name.
    Require ([FvrStorage.NativeSize]::ParentPid($process.Id) -eq $ParentId) "Unexpected $Name process parent."
    $owned[$Name]=@{process=$process;identity=(Get-Bc2ProcessIdentity $process -RequireLive $true)}
    Save ($Name+'-identity.json') $owned[$Name].identity
}
function NoOtherProcesses([switch]$BeforeLaunch){
    foreach($process in @(Get-Process BFBC2Game,BC2XrHost,BC2NativeTrace,BC2NativeIpcProbe,BC2NativeResolution -ErrorAction SilentlyContinue)){
        try{
            $identity=Get-Bc2ProcessIdentity $process -RequireLive $true
            $known=@($owned.Values|Where-Object {$_.identity.pid -eq $identity.pid -and $_.identity.start_utc -ceq $identity.start_utc -and $_.identity.path -ieq $identity.path}).Count -eq 1
            # Session publishes each identity just after Start-Process. Parentage permits that small window.
            if(-not $BeforeLaunch -and -not $known){
                $parentId=[FvrStorage.NativeSize]::ParentPid($process.Id)
                $fromSupervisor=$owned.ContainsKey('supervisor') -and $parentId -eq $owned.supervisor.identity.pid
                $preflight=$entry -and $parentId -eq $entry.Id -and $identity.path -ieq (Join-Path $runtimeRoot 'bin/BC2XrHost.exe')
                $expected=@($gameExe)+@($pePaths.Keys)
                $known=($fromSupervisor -or $preflight) -and $identity.path -iin $expected -and [DateTimeOffset]::Parse($identity.start_utc).UtcDateTime -ge $launchUtc
            }
            Require $known "Unowned BC2 process detected: $($identity.pid) $($identity.path)."
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
function ObserveSession {
    if(-not $folder){
        $activePath=Join-Path $stateRoot 'active-session.json'
        if(-not [IO.File]::Exists($activePath)){return}
        $active=Json $activePath
        if($priorActive -and $active.relative_folder -ceq $priorActive.relative_folder){return}
        Require ($active.version -ceq $Version -and $active.relative_folder -cmatch '^sessions/[A-Za-z0-9-]+$') 'Unexpected active-session record.'
        Bind 'supervisor' $active.supervisor $pwsh $entry.Id
        Require ($owned.ContainsKey('supervisor')) 'New supervisor identity could not be retained.'
        $candidate=Get-FvrChild $stateRoot $active.relative_folder;Assert-FvrNoLinks $candidate
        $options=Json (Join-Path $candidate 'options.json')
        Require ($options.version -ceq $Version -and $options.source_sha256 -ceq $ExpectedSourceSha256 -and $options.xr_manifest -ieq $runtimeManifest -and $options.game_sha256 -ceq $gamePin.sha256 -and -not $options.activate_window) 'Session options do not match the admitted launch.'
        $script:folder=$candidate;$receipt.session_folder=$candidate
        Save 'session-link.json' @{folder=$candidate;options=(Join-Path $candidate 'options.json');session=(Join-Path $candidate 'session.json');sim_state=(Join-Path $simDir 'state.json');command=(Join-Path $simDir 'command.txt');stop_request=(Join-Path $candidate 'stop.request');supervisor=$owned.supervisor.identity}
        Write-Output ('SESSION_FOLDER='+$candidate)
        Write-Output ('SIM_DIRECTORY='+$simDir)
    }
    $record=Join-Path $folder 'session.json';if(-not [IO.File]::Exists($record)){return}
    $script:session=Json $record
    Require ($session.schema -ceq 'bc2vr.session.v1' -and $session.source_sha256 -ceq $ExpectedSourceSha256 -and $session.version -ceq $Version) 'Session source identity differs.'
    SameIdentity $session.supervisor $owned.supervisor.identity
    foreach($kind in 'game','host','trace','resolution_helper','resolution_release_helper'){
        if(-not $session.ContainsKey($kind) -or -not $session[$kind]){continue}
        $path=switch($kind){game {$gameExe} host {Join-Path $runtimeRoot 'bin/BC2XrHost.exe'} trace {Join-Path $runtimeRoot 'bin/BC2NativeTrace.exe'} default {Join-Path $runtimeRoot 'bin/BC2NativeResolution.exe'}}
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
function ObserveBounds {
    if($clock.ElapsedMilliseconds-$lastScan -lt 1000){return};$script:lastScan=$clock.ElapsedMilliseconds
    $bytes=0L
    foreach($directory in @($OutputDir,$folder)|Where-Object {$_}){
        foreach($file in Get-ChildItem -LiteralPath $directory -File -Recurse -Force){Assert-FvrNoLinks $file.FullName;$bytes+=$file.Length}
    }
    Require ($bytes -le 640MB) 'Run plus normal-session output exceeded the 640 MiB watchdog.'
    $statePath=Join-Path $simDir 'state.json'
    if([IO.File]::Exists($statePath) -and $owned.ContainsKey('host')){
        $sim=Json $statePath
        # Preflight and actual host can publish different PIDs into the same directory.
        if($sim.pid -eq $owned.host.identity.pid){
            Require ($sim.runtime -ceq 'xr-sim' -and $sim.system -ceq $systemName -and $sim.headsetConfig -ieq $profilePath) 'Actual host simulator identity differs.'
            Require ($sim.errors -eq 0 -and [string]::IsNullOrEmpty($sim.lastCmdError)) 'Simulator command/runtime error; retain state and acknowledgement.'
            Require ($sim.captureSeq -le 4) 'More than four simulator compositions were requested.'
            if(-not $simObserved -and $sim.cmdSeq -ge $initialCommands.Count -and $sim.sessionRunning){
                Save 'sim-host-initial.json' $sim;$script:simObserved=$true
                Write-Output ('SIM_HOST_PID='+$sim.pid)
            }
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
    Require ($TimeoutSeconds -ge $Seconds+60) 'Outer deadline must reserve at least 60 seconds beyond host duration.'
    Require ([IO.Path]::GetPathRoot($RepositoryRoot) -ieq 'G:\' -and [IO.Path]::GetPathRoot($InstallRoot) -ieq 'G:\') 'Local development repository/install must remain on G:.'
    Require ($OutputDir.StartsWith((Join-Path $RepositoryRoot 'reports')+'\',[StringComparison]::OrdinalIgnoreCase)) 'Use a fresh output under RepositoryRoot/reports.'
    Require (-not(Test-Path -LiteralPath $OutputDir)) 'Existing output refused; preserve all prior episodes.'
    $identity=[Security.Principal.WindowsIdentity]::GetCurrent()
    try{Require (-not [Security.Principal.WindowsPrincipal]::new($identity).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) 'Elevated Windows token refused; OpenXR environment overrides require a normal token.'}finally{$identity.Dispose()}
    $budget=Assert-FvrStorageBudget -RepositoryRoot $RepositoryRoot -OutputPath $OutputDir -Operation Session -PlannedBytes 640MB -RawCaptureBytes (512MB+4*320*240*4*4+4*64KB) -MaxRawCaptureBytes 520MB -AdditionalRoots @($InstallRoot,$sourceRoot,(Join-Path (Split-Path -Parent $RepositoryRoot) 'bc2vr-task-worktrees'))
    $null=[IO.Directory]::CreateDirectory($OutputDir);$outputReady=$true;Save 'storage.json' $budget
    NoOtherProcesses -BeforeLaunch
    $null=Assert-FvrVersion $Version
    $packageRoot=Get-FvrChild $InstallRoot ('versions/'+$Version)
    $package=Test-FvrPackage $packageRoot $Version
    Require ($package.runtime_entry -ceq 'runtime/Entry.ps1') 'Expected the normal installed runtime entry.'
    $runtimeRoot=Full (Join-Path $packageRoot 'runtime');$config=Json (Join-Path $runtimeRoot 'runtime.json') 4MB
    Require ($config.schema -ceq 'bc2vr.runtime.v1' -and $config.version -ceq $Version -and $config.diagnostic_only -eq $false -and $config.source_sha256 -ceq $ExpectedSourceSha256) 'Installed normal runtime/source differs.'
    foreach($file in $config.files){Require ((Pin (Get-FvrChild $runtimeRoot $file.path) 256MB).sha256 -ceq $file.sha256) ('Runtime file differs: '+$file.path)}
    $active=Json (Join-Path $InstallRoot 'active.json');Require ($active.current -ceq $Version) 'Requested version is not the active install.'
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
    $binaryPins=@();foreach($path in $pePaths.Keys){$binaryPins+=Pin $path 256MB;Pe $path $pePaths[$path]}
    Save 'artifact-binding.json' @{version=$Version;source_sha256=$ExpectedSourceSha256;package=(Pin (Join-Path $packageRoot 'package.json'));runtime=(Pin (Join-Path $runtimeRoot 'runtime.json'));local_config=(Pin $LocalConfig);runtime_manifest=(Pin $runtimeManifest);tape_manifest=(Pin $tapeManifest);binaries=$binaryPins;game=$gamePin}
    foreach($name in 'GameSettings.bin','GameSettings.ini','settings.ini'){$profiles+=Backup (Join-Path $ProfileDirectory $name) ('profile-before/'+$name)}
    foreach($name in 'active.json','data/settings.json'){$launcherFiles+=Backup (Join-Path $InstallRoot $name) ('launcher-before/'+$name)}
    Save 'originals.json' @{profiles=$profiles;launcher=$launcherFiles}
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
    $initialCommands=@('pace free 60','capture off','capture size 320 240','compose oncapture','input clear',
        'head pose 0 1.60 0 0 0 0','hand l follow off','hand r follow off',
        'hand l grip pose -0.20 1.30 -0.35 0 0 0','hand r grip pose 0.20 1.30 -0.35 0 0 0',
        'hand l aimtrim 0 0','hand r aimtrim 0 0')
    [IO.File]::WriteAllText((Join-Path $simDir 'command.txt'),($initialCommands -join "`n")+"`n",$utf8)
    Save 'initial-commands.json' @{commands=$initialCommands;capture_authority='Root may request at most four acknowledged shot commands; preserve each batch and acknowledgement.'}
    $environment=@{XR_RUNTIME_JSON=$runtimeManifest;XR_API_LAYER_PATH=$tapeManifestDirectory;XR_ENABLE_API_LAYERS=$local.TapeLayerName;XRSIM_DIR=$simDir;XRSIM_HEADSET_CONFIG=$profilePath;XRTAPE_DIR=$tapeDir;XRTAPE_MAX_FRAMES='30000'}
    $psi=[Diagnostics.ProcessStartInfo]::new();$psi.FileName=$pwsh;$psi.WorkingDirectory=$OutputDir
    $psi.UseShellExecute=$false;$psi.CreateNoWindow=$true;$psi.WindowStyle=[Diagnostics.ProcessWindowStyle]::Hidden
    # No caller/global XR mutation: launcher descendants inherit this isolated map.
    foreach($key in @($psi.Environment.Keys)){if($key -match '^(XR_|XRSIM_|XRTAPE_|BVR_XRSIM_|FVR_|BC2_)'){$null=$psi.Environment.Remove($key)}}
    foreach($key in $environment.Keys){$psi.Environment[$key]=[string]$environment[$key]}
    foreach($key in 'TEMP','TMP','TMPDIR'){$psi.Environment[$key]=$scratch};$psi.Environment['PYTHONDONTWRITEBYTECODE']='1'
    $launchScript=Join-Path $OutputDir 'launch.ps1'
    # Literal paths are passed as arguments, never interpolated into shell code.
    [IO.File]::WriteAllText($launchScript,"param(`$EntryPath,`$SettingsPath,`$StateRoot,`$Log)`n`$ErrorActionPreference='Stop'`ntry { & `$EntryPath -Action Launch -SettingsPath `$SettingsPath -StateRoot `$StateRoot -StartAt MainMenu *>> `$Log } catch { [IO.File]::AppendAllText(`$Log,(`$_ | Out-String),[Text.UTF8Encoding]::new(`$false)); exit 1 }`n",$utf8)
    foreach($arg in @('-NoProfile','-NonInteractive','-ExecutionPolicy','Bypass','-File',$launchScript,(Join-Path $runtimeRoot 'Entry.ps1'),$settingsPath,$stateRoot,(Join-Path $OutputDir 'entry.log'))){$psi.ArgumentList.Add($arg)}
    Save 'launch.json' @{executable=$pwsh;entry=(Join-Path $runtimeRoot 'Entry.ps1');action='Launch';start_at='MainMenu';activate_window=$false;environment=$environment;state_root=$stateRoot}
    NoOtherProcesses -BeforeLaunch
    foreach($row in $profiles+$launcherFiles){Require ((Pin $row.original.path).sha256 -ceq $row.original.sha256) 'Original changed before launch.'}
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
}catch{$failures.Add($_.Exception.Message)}
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
            foreach($kind in 'host','trace','resolution_helper','resolution_release_helper'){if($owned.ContainsKey($kind)){Require ($owned[$kind].process.HasExited) "Owned $kind cleanup remains pending; no game close/restoration."}}
            if($owned.ContainsKey('game')){
                $game=$owned.game.process
                if(-not $game.HasExited){
                    SameIdentity (Get-Bc2ProcessIdentity $game -RequireLive $true) $owned.game.identity
                    Require ((Pin $gameExe 128MB).sha256 -ceq $gamePin.sha256) 'Original game image changed.'
                    if($session.trace){
                        Require ($owned.ContainsKey('trace') -and $owned.trace.process.HasExited) 'Native trace exit identity was not retained.'
                        $bootstrap=Read-Bc2NativeRunOutcome (Join-Path $folder 'native.stdout.log') $game.Id
                        Require ($bootstrap -and $bootstrap.outcome -ceq 'remote_thread_exited' -and $bootstrap.remote_thread_exit_observed -and $bootstrap.report_nonempty) 'Native remote thread exit not proven.'
                        $native=Json (Join-Path $folder 'native-trace.json') 160MB
                        Require ($native.hooks_disabled -is [bool] -and $native.hooks_disabled) 'Native hooks are not proven disabled.'
                        $preActivation=$bootstrap.probe_exit -eq 20 -and $native.state -ceq 'failed' -and $native.error -ceq 'Main-menu startup requires native menu capture and input'
                        if(-not $preActivation){Require ($owned.trace.process.ExitCode -eq 0 -and $bootstrap.probe_exit -eq 0 -and $native.pid -eq $game.Id -and $native.gameplay.reload_flow.drained -is [bool] -and $native.gameplay.reload_flow.drained -and $native.gameplay.reload_flow.in_flight -eq 0) 'Native reload hooks did not prove successful drain.'}
                        $nativeRetired=$true;Save 'native-retirement.json' @{bootstrap=$bootstrap;hooks_disabled=$true;pre_activation_refusal=$preActivation;reload_flow_drained=(-not $preActivation);native_report=(Pin (Join-Path $folder 'native-trace.json') 160MB)}
                    }
                    if($session.ContainsKey('resolution_lease')){
                        $released=Json (Join-Path $folder 'resolution-release.json');Assert-Bc2ResolutionIdentity $released $session.resolution_lease 'release';Assert-Bc2ResolutionReleased $released $session.resolution_lease
                    }
                    NoOtherProcesses;SameIdentity (Get-Bc2ProcessIdentity $game -RequireLive $true) $owned.game.identity
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
                [IO.File]::WriteAllText($refusal,(@{schema='fvr.xr-game-simulation-refusal.v1';failures=@($failures);game_started=$false}|ConvertTo-Json),$utf8)
            }
        }catch{Write-Warning ('Could not preserve refusal receipt: '+$_.Exception.Message)}
    }
    foreach($row in $owned.Values){$row.process.Dispose()}
    foreach($key in $savedTemporary.Keys){[Environment]::SetEnvironmentVariable($key,$savedTemporary[$key],'Process')}
}
if($receipt.status -ne 'observed_and_closed'){throw ('Actual-game simulation incomplete; retain evidence at '+$OutputDir+'. '+($failures -join ' | '))}
Write-Output (Join-Path $OutputDir 'result.json')
