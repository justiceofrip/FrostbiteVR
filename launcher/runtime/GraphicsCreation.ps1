#requires -Version 7.5
# Creation policy lasts until game exit. Evidence collection has a separate lifetime.
$script:bc2GraphicsCreation=$null
function Initialize-Bc2GraphicsCreationAbi {
    if('Bc2Vr.GraphicsCreation.Bridge' -as [type]){return}
    Add-Type -TypeDefinition @'
using System;
using System.IO;
using System.Text;
using System.ComponentModel;
using System.Runtime.InteropServices;
using Microsoft.Win32.SafeHandles;
namespace Bc2Vr.GraphicsCreation {
    [StructLayout(LayoutKind.Sequential, Pack=8)]
    public struct Config {
        public uint bytes, version, collectEvidence, ownerPid;
        public ulong ownerCreated;
        public uint helperTimeoutMs, reserved;
        [MarshalAs(UnmanagedType.ByValArray, SizeConst=16)] public byte[] token;
    }
    [StructLayout(LayoutKind.Sequential, Pack=8)]
    public struct Ticket {
        public uint bytes, version, targetPid, ownerPid;
        public ulong targetCreated, ownerCreated;
        [MarshalAs(UnmanagedType.ByValArray, SizeConst=16)] public byte[] token;
    }
    [StructLayout(LayoutKind.Sequential, Pack=8)]
    public struct Record {
        public int published;
        public uint kind, sequence, pid, tid, exportId, depth, proof;
        public ulong processCreated, callId, parentCallId, module, entry;
        public ulong device, context, swapchain, armedSequence;
        public uint originalFlags, forwardedFlags, returnedFlags;
        public int hresult;
        public ulong qpc;
    }
    [StructLayout(LayoutKind.Sequential, CharSet=CharSet.Unicode)]
    public struct StartupInfo {
        public uint cb;
        public IntPtr reserved, desktop, title;
        public uint x, y, xSize, ySize, xCountChars, yCountChars, fillAttribute, flags;
        public ushort showWindow, reserved2Bytes;
        public IntPtr reserved2, stdin, stdout, stderr;
    }
    [StructLayout(LayoutKind.Sequential)]
    public struct ProcessInfo { public IntPtr process, thread; public uint pid, tid; }
    public sealed class Bridge {
        [DllImport("kernel32.dll", CharSet=CharSet.Unicode, ExactSpelling=true, SetLastError=true)]
        static extern IntPtr LoadLibraryExW(string name, IntPtr file, uint flags);
        [DllImport("kernel32.dll", CharSet=CharSet.Ansi, ExactSpelling=true, SetLastError=true)]
        static extern IntPtr GetProcAddress(IntPtr module, string name);
        [DllImport("kernel32.dll", SetLastError=true)]
        public static extern bool CloseHandle(IntPtr handle);
        [DllImport("kernel32.dll", SetLastError=true)]
        public static extern uint WaitForSingleObject(IntPtr handle, uint milliseconds);
        [DllImport("kernel32.dll", SetLastError=true)]
        public static extern bool GetProcessTimes(IntPtr handle, out long creation, out long exit, out long kernel, out long user);
        [DllImport("kernel32.dll")] public static extern IntPtr GetCurrentProcess();
        [StructLayout(LayoutKind.Sequential, CharSet=CharSet.Unicode)] struct ProcessEntry {
            public uint Size, Usage, Pid;
            public UIntPtr Heap;
            public uint Module, Threads, ParentPid;
            public int Priority;
            public uint Flags;
            [MarshalAs(UnmanagedType.ByValTStr, SizeConst=260)] public string Name;
        }
        [DllImport("kernel32.dll", SetLastError=true)] static extern SafeFileHandle CreateToolhelp32Snapshot(uint flags, uint pid);
        [DllImport("kernel32.dll", CharSet=CharSet.Unicode, ExactSpelling=true, SetLastError=true)]
        static extern bool Process32FirstW(SafeFileHandle snapshot, ref ProcessEntry entry);
        [DllImport("kernel32.dll", CharSet=CharSet.Unicode, ExactSpelling=true, SetLastError=true)]
        static extern bool Process32NextW(SafeFileHandle snapshot, ref ProcessEntry entry);
        public static uint ParentPid(uint pid) {
            using(var snapshot=CreateToolhelp32Snapshot(2,0)) {
                if(snapshot.IsInvalid) throw new Win32Exception(Marshal.GetLastWin32Error(),"Cannot verify game parent");
                var entry=new ProcessEntry { Size=(uint)Marshal.SizeOf<ProcessEntry>() };
                if(!Process32FirstW(snapshot,ref entry)) throw new Win32Exception(Marshal.GetLastWin32Error());
                do { if(entry.Pid==pid) return entry.ParentPid; } while(Process32NextW(snapshot,ref entry));
                throw new IOException("Game was absent from the process snapshot");
            }
        }
        [UnmanagedFunctionPointer(CallingConvention.Winapi, CharSet=CharSet.Unicode, SetLastError=true)]
        [return: MarshalAs(UnmanagedType.Bool)]
        public delegate bool CreateFn(string app, StringBuilder command, IntPtr processSecurity, IntPtr threadSecurity,
            [MarshalAs(UnmanagedType.Bool)] bool inherit, uint flags, IntPtr environment, string cwd,
            ref StartupInfo startup, out ProcessInfo process,
            [MarshalAs(UnmanagedType.LPStr)] string dll, ref Config config, out Ticket ticket);
        [UnmanagedFunctionPointer(CallingConvention.Winapi, SetLastError=true)]
        [return: MarshalAs(UnmanagedType.Bool)] public delegate bool ResumeFn(ref Ticket ticket, IntPtr process, IntPtr thread);
        [UnmanagedFunctionPointer(CallingConvention.Winapi, SetLastError=true)]
        [return: MarshalAs(UnmanagedType.Bool)] public delegate bool TicketFn(ref Ticket ticket);
        [UnmanagedFunctionPointer(CallingConvention.Winapi, SetLastError=true)]
        [return: MarshalAs(UnmanagedType.Bool)] public delegate bool ReadFn(ref Ticket ticket, uint index, out Record record, uint bytes);
        [UnmanagedFunctionPointer(CallingConvention.Winapi, SetLastError=true)]
        [return: MarshalAs(UnmanagedType.Bool)] public delegate bool StatusFn(ref Ticket ticket, [Out] int[] status, uint count);
        public readonly CreateFn CreateSuspended;
        public readonly ResumeFn Resume;
        public readonly TicketFn StopCollection, AbortSuspended;
        public readonly ReadFn ReadRecord;
        public readonly StatusFn ReadStatus;
        public readonly string DllPath;
        readonly IntPtr module; // Retained for supervisor lifetime; deliberately no FreeLibrary.
        T Bind<T>(string name) where T : class {
            IntPtr address=GetProcAddress(module,name);
            if(address==IntPtr.Zero) throw new Win32Exception(Marshal.GetLastWin32Error(),name);
            return (T)(object)Marshal.GetDelegateForFunctionPointer(address,typeof(T));
        }
        public Bridge(string absolutePath) {
            if(IntPtr.Size!=8 || Marshal.SizeOf<StartupInfo>()!=104 || Marshal.SizeOf<ProcessInfo>()!=24 ||
                Marshal.SizeOf<Config>()!=48 || Marshal.SizeOf<Ticket>()!=48 || Marshal.SizeOf<Record>()!=128)
                throw new InvalidOperationException("Graphics creation ABI requires the x64 launcher");
            if(!Path.IsPathFullyQualified(absolutePath)) throw new ArgumentException("Absolute graphics bridge path required");
            DllPath=Path.GetFullPath(absolutePath);
            if(!String.Equals(Path.GetFileName(DllPath),"Bc2GraphicsCreation64.dll",StringComparison.Ordinal))
                throw new ArgumentException("Expected graphics creation DLL sibling pair");
            if(DllPath.Length>=260) throw new ArgumentException("Graphics creation DLL path exceeds the supported length");
            foreach(char c in DllPath) if(c>127) throw new ArgumentException("BC2VR requires an installation path containing ASCII characters for graphics startup");
            module=LoadLibraryExW(DllPath,IntPtr.Zero,0x100u|0x1000u);
            if(module==IntPtr.Zero) throw new Win32Exception(Marshal.GetLastWin32Error());
            CreateSuspended=Bind<CreateFn>("Bc2GraphicsCreateSuspendedW"); Resume=Bind<ResumeFn>("Bc2GraphicsResume");
            StopCollection=Bind<TicketFn>("Bc2GraphicsStopCollection"); AbortSuspended=Bind<TicketFn>("Bc2GraphicsAbortSuspended");
            ReadRecord=Bind<ReadFn>("Bc2GraphicsReadRecord"); ReadStatus=Bind<StatusFn>("Bc2GraphicsReadStatus");
        }
    }
}
'@
}
function Assert-Bc2GraphicsPackage($Config) {
    foreach($relative in 'GraphicsCreation.ps1','bin/Bc2GraphicsCreation64.dll','bin/Bc2GraphicsCreation32.dll'){
        $pins=@($Config.files|Where-Object {$_.path -ceq $relative})
        if($pins.Count -ne 1 -or $pins[0].sha256 -cnotmatch '^[a-f0-9]{64}$'){throw 'The runtime package is missing its graphics creation identity.'}
        $path=Get-FvrChild $PSScriptRoot $relative;Assert-FvrNoLinks $path
        if((Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant() -cne $pins[0].sha256){throw "Graphics creation runtime changed: $relative"}
    }
}
function Get-Bc2GraphicsEnvironmentBlock([hashtable]$Audio) {
    # Inherit every process variable; apply the same fresh-game audio removals/overrides.
    $environment=@{}
    foreach($entry in [Environment]::GetEnvironmentVariables('Process').GetEnumerator()){$environment[[string]$entry.Key]=[string]$entry.Value}
    foreach($key in $Audio.Keys){if($null -eq $Audio[$key]){$environment.Remove($key)}else{$environment[$key]=[string]$Audio[$key]}}
    [string[]]$keys=@($environment.Keys);[Array]::Sort($keys,[StringComparer]::OrdinalIgnoreCase)
    return (($keys|ForEach-Object {$_+'='+$environment[$_]}) -join [char]0)+[char]0+[char]0
}
function Assert-Bc2GraphicsReceipt($Saved,$Identity,$Ticket,$OwnerCreated) {
    if($Saved.schema -cne 'bc2vr.session.v1' -or $Saved.version -cne $options.version -or $Saved.source_sha256 -cne $options.source_sha256){throw 'Persisted graphics runtime identity differs.'}
    foreach($name in 'pid','start_utc','path'){
        if($Saved.game[$name] -cne $Identity[$name] -or $Saved.supervisor[$name] -cne $state.supervisor[$name]){throw 'Persisted graphics process ownership differs.'}
    }
    foreach($name in 'bytes','version','targetPid','ownerPid','targetCreated','ownerCreated'){
        if($Saved.graphics_creation.ticket[$name] -ne $Ticket.$name){throw 'Persisted graphics creation ticket differs.'}
    }
    if($Saved.graphics_creation.resumed -isnot [bool] -or $Saved.graphics_creation.resumed -or
       [Convert]::ToHexString([byte[]]$Saved.graphics_creation.ticket.token) -cne [Convert]::ToHexString($Ticket.token) -or
       [DateTimeOffset]::Parse($Saved.supervisor.start_utc).UtcDateTime.ToFileTimeUtc() -ne $OwnerCreated){throw 'Persisted graphics token or supervisor birth differs.'}
}
function Get-Bc2GraphicsStatus($Creation) {
    $ticket=$Creation.ticket;$status=[int[]]::new(8)
    if(-not $Creation.bridge.ReadStatus.Invoke([ref]$ticket,$status,8)){throw 'Cannot read the owned graphics creation status.'}
    if($status[0] -notin 0,1 -or $status[1] -lt 0 -or $status[1] -gt 4 -or $status[2] -notin 0,1 -or
       $status[3] -lt 0 -or $status[3] -gt 128 -or $status[4] -lt 0 -or $status[5] -lt 0 -or $status[6] -lt 0){throw 'Invalid graphics creation status.'}
    return @{armed=$status[0];collection_reason=$status[1];collection_stopped=$status[2];reserved_records=$status[3];overflow=$status[4];in_flight=$status[5];calls=$status[6];attach_error=$status[7]}
}
function Assert-Bc2GraphicsArmed {
    $d=$script:bc2GraphicsCreation
    if(-not $d -or -not $d.created -or -not $d.resumed){throw 'A fresh graphics creation launch is required before VR can connect.'}
    $status=Get-Bc2GraphicsStatus $d;$state.graphics_creation.status=$status
    if($status.armed -ne 1 -or $status.attach_error -ne 0){throw 'BC2 graphics startup could not be verified. Close BC2 normally and launch again.'}
    # Armed describes the wrappers only. The native adapter must verify actual
    # returned device flags separately for every admitted renderer generation.
}
function Start-Bc2GraphicsGame([string]$Exe,$RuntimeConfig) {
    if($script:bc2GraphicsCreation){throw 'Only one graphics creation launch is allowed per session supervisor.'}
    Assert-Bc2GraphicsPackage $RuntimeConfig
    Initialize-Bc2GraphicsCreationAbi
    $bridge=[Bc2Vr.GraphicsCreation.Bridge]::new((Join-Path $PSScriptRoot 'bin/Bc2GraphicsCreation64.dll'))
    $config=[Bc2Vr.GraphicsCreation.Config]::new();$config.bytes=48;$config.version=1;$config.ownerPid=[uint32]$PID;$config.helperTimeoutMs=10000
    # Reuse the existing explicit graphics diagnostic opt-in; no player setting.
    $config.collectEvidence=[uint32]($env:FVR_BC2_GRAPHICS_PROVENANCE -ceq '1')
    [long]$created=0;[long]$ended=0;[long]$kernel=0;[long]$user=0
    if(-not [Bc2Vr.GraphicsCreation.Bridge]::GetProcessTimes([Bc2Vr.GraphicsCreation.Bridge]::GetCurrentProcess(),[ref]$created,[ref]$ended,[ref]$kernel,[ref]$user)){throw 'Cannot verify the graphics supervisor creation time.'}
    $config.ownerCreated=[ulong]$created;$config.token=[byte[]]::new(16)
    $rng=[Security.Cryptography.RandomNumberGenerator]::Create();try{$rng.GetBytes($config.token)}finally{$rng.Dispose()}
    $script:bc2GraphicsCreation=@{bridge=$bridge;ticket=[Bc2Vr.GraphicsCreation.Ticket]::new();process=[Bc2Vr.GraphicsCreation.ProcessInfo]::new();created=$false;resumed=$false;collect=($config.collectEvidence -eq 1)}
    $d=$script:bc2GraphicsCreation;$ticket=$d.ticket;$pi=$d.process
    $block=Get-Bc2GraphicsEnvironmentBlock (Get-Bc2AudioEnvironment $options.cache)
    $memory=[Runtime.InteropServices.Marshal]::StringToHGlobalUni($block)
    $startup=[Bc2Vr.GraphicsCreation.StartupInfo]::new();$startup.cb=104
    $command=[Text.StringBuilder]::new((ConvertTo-Bc2CommandArgument $Exe))
    try {
        # CREATE_SUSPENDED | CREATE_UNICODE_ENVIRONMENT; no inherited handles.
        $d.created=$bridge.CreateSuspended.Invoke($Exe,$command,[IntPtr]::Zero,[IntPtr]::Zero,$false,1028,$memory,$options.game_path,[ref]$startup,[ref]$pi,$bridge.DllPath,[ref]$config,[ref]$ticket)
        $createError=[Runtime.InteropServices.Marshal]::GetLastWin32Error()
    } finally {$d.process=$pi;$d.ticket=$ticket;[Runtime.InteropServices.Marshal]::FreeHGlobal($memory)}
    if(-not $d.created){throw "BC2 suspended graphics startup failed ($createError)."}
    if($ticket.bytes -ne 48 -or $ticket.version -ne 1 -or $ticket.targetPid -ne $pi.pid -or $ticket.targetPid -eq 0 -or
       $ticket.targetCreated -eq 0 -or $ticket.ownerPid -ne $PID -or $ticket.ownerCreated -ne $config.ownerCreated -or
       [Convert]::ToHexString($ticket.token) -cne [Convert]::ToHexString($config.token)){throw 'Suspended graphics ownership ticket differs.'}
    $script:game=[Diagnostics.Process]::GetProcessById([int]$pi.pid);$null=$script:game.Handle
    $identity=Get-Bc2ProcessIdentity $script:game -RequireLive $true
    if($identity.path -ine $Exe -or [DateTimeOffset]::Parse($identity.start_utc).UtcDateTime.ToFileTimeUtc() -ne $ticket.targetCreated -or
       [Bc2Vr.GraphicsCreation.Bridge]::ParentPid([uint32]$pi.pid) -ne $PID){throw 'The game is not the graphics supervisor owned child.'}
    $state.game=$identity;$state.audio_environment_inherited=$true
    $state.graphics_creation=@{schema='bc2vr.graphics-creation.v1';policy='clear_singlethreaded';lifetime='game_process';ticket=$ticket;primary_tid=$pi.tid;resumed=$false;collection_requested=$d.collect;device_flags_verification='native_adapter_required'}
    Save-State
    Assert-Bc2GraphicsReceipt (Read-FvrJson $stateFile) $identity $ticket $config.ownerCreated
    $checked=Get-Bc2ProcessIdentity $script:game -RequireLive $true
    foreach($name in 'pid','start_utc','path'){if($checked[$name] -cne $identity[$name]){throw 'Game identity changed before graphics resume.'}}
    Assert-Bc2GraphicsPackage $RuntimeConfig
    if((Get-FileHash -LiteralPath $Exe -Algorithm SHA256).Hash.ToLowerInvariant() -cne $options.game_sha256 -or
       [Bc2Vr.GraphicsCreation.Bridge]::ParentPid([uint32]$pi.pid) -ne $PID -or (Test-Path -LiteralPath $stopFile)){throw 'Pre-resume game, parent or stop check failed.'}
    $d.resumed=$bridge.Resume.Invoke([ref]$ticket,$pi.process,$pi.thread)
    if(-not $d.resumed){throw ('BC2 graphics resume failed ('+[Runtime.InteropServices.Marshal]::GetLastWin32Error()+').')}
    $state.graphics_creation.resumed=$true;Save-State
    if(-not [Bc2Vr.GraphicsCreation.Bridge]::CloseHandle($pi.thread)){throw 'Primary game thread handle close failed.'};$d.process.thread=[IntPtr]::Zero
    $wait=[Diagnostics.Stopwatch]::StartNew()
    do {
        if($script:game.HasExited){throw 'BC2 exited before graphics startup was verified.'}
        if(Test-Path -LiteralPath $stopFile){throw 'BC2 graphics startup was stopped before VR connected.'}
        $status=Get-Bc2GraphicsStatus $d;$state.graphics_creation.status=$status
        if($status.attach_error -ne 0){throw "BC2 graphics startup failed ($($status.attach_error)). Close BC2 normally before retrying."}
        if($status.armed -eq 1){Assert-Bc2GraphicsArmed;Save-State;return $script:game}
        Start-Sleep -Milliseconds 25
    } while($wait.ElapsedMilliseconds -lt 10000)
    throw 'BC2 graphics startup did not become ready. Close BC2 normally before retrying.'
}
function Complete-Bc2GraphicsCreation {
    $d=$script:bc2GraphicsCreation
    if(-not $d -or -not $d.created){return}
    $ticket=$d.ticket
    try {
        if(-not $d.resumed){
            # A failed native Resume can already have terminated its exact target.
            # Allow that exit to finish before deciding that suspension needs abort.
            $lifetime=[Bc2Vr.GraphicsCreation.Bridge]::WaitForSingleObject($d.process.process,1000)
            if($lifetime -eq 258){
                if(-not $d.bridge.AbortSuspended.Invoke([ref]$ticket)){throw 'Could not abort the owned suspended game.'}
                if([Bc2Vr.GraphicsCreation.Bridge]::WaitForSingleObject($d.process.process,5000) -ne 0){throw 'Owned suspended game exit remains unconfirmed.'}
            }elseif($lifetime -ne 0){throw 'Cannot verify the owned suspended game lifetime.'}
        }
        if(-not $d.bridge.StopCollection.Invoke([ref]$ticket)){throw 'Could not stop graphics evidence collection.'}
        $status=Get-Bc2GraphicsStatus $d
        if($state.ContainsKey('graphics_creation')){$state.graphics_creation.status=$status;$state.graphics_creation.cleanup='collection_stopped'}
        if($d.collect){
            $rows=[Collections.Generic.List[object]]::new();$missing=[Collections.Generic.List[int]]::new()
            for($i=0;$i -lt [Math]::Min($status.reserved_records,128);$i++){
                $row=[Bc2Vr.GraphicsCreation.Record]::new()
                if(-not $d.bridge.ReadRecord.Invoke([ref]$ticket,[uint32]$i,[ref]$row,128)){$missing.Add($i);continue}
                if($row.published -ne 2 -or $row.pid -ne $ticket.targetPid -or $row.processCreated -ne $ticket.targetCreated){throw 'Graphics evidence record ownership differs.'}
                $rows.Add($row)
            }
            $after=Get-Bc2GraphicsStatus $d
            Write-FvrJson (Join-Path $folder 'graphics-creation.json') @{schema='bc2vr.graphics-creation.records.v1';ticket=$ticket;status=$status;status_after=$after;records=@($rows.ToArray());incomplete_indices=@($missing.ToArray());limit='Bounded intercepted exports only; arming does not prove returned renderer flags. Stopping collection leaves creation correction active until game exit.'}
        }
    } finally {
        # These handles belong to this supervisor. Never unload either bridge or
        # bootstrap, and never terminate a game whose sole resume succeeded.
        $closeErrors=[Collections.Generic.List[string]]::new()
        foreach($name in 'thread','process'){
            if($d.process.$name -ne [IntPtr]::Zero){
                if(-not [Bc2Vr.GraphicsCreation.Bridge]::CloseHandle($d.process.$name)){$closeErrors.Add("Graphics $name handle close failed.")}
                else{$d.process.$name=[IntPtr]::Zero}
            }
        }
        if($closeErrors.Count){throw ($closeErrors -join ' ')}
    }
}
