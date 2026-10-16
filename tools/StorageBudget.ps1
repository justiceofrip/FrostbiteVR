# Storage admission and process-local scratch routing: never deletes files,
# starts a process, changes Windows/user environment settings or native state.
function Set-FvrProjectTemporaryStorage {
    param([Parameter(Mandatory)][string]$RepositoryRoot)
    $scratch=[IO.Path]::GetFullPath((Join-Path $RepositoryRoot 'build/scratch'))
    $ancestor=$scratch
    while($ancestor){
        if((Test-Path -LiteralPath $ancestor) -and ((Get-Item -LiteralPath $ancestor -Force).Attributes -band [IO.FileAttributes]::ReparsePoint)){
            throw "Project scratch refuses a linked path: $ancestor"
        }
        $ancestor=Split-Path -Parent $ancestor
    }
    $null=[IO.Directory]::CreateDirectory($scratch)
    $env:TEMP=$scratch;$env:TMP=$scratch;$env:TMPDIR=$scratch
    $env:PYTHONDONTWRITEBYTECODE='1'
    return $scratch
}
function Initialize-FvrStorageNative {
    if('FvrStorage.NativeSize' -as [type]){return}
    Add-Type -TypeDefinition @'
using System;
using System.ComponentModel;
using System.Collections.Generic;
using System.IO;
using System.Runtime.InteropServices;
using System.Threading.Tasks;
using Microsoft.Win32.SafeHandles;
namespace FvrStorage {
    public sealed class RootUsage {
        public string path { get; set; }
        public long bytes { get; set; }
        public long allocated_bytes { get; set; }
        public long logical_bytes { get; set; }
        public long files { get; set; }
    }
    public sealed class Usage {
        public long bytes { get; set; }
        public long allocated_bytes { get; set; }
        public long logical_bytes { get; set; }
        public long files { get; set; }
        public string accounting { get { return "GetCompressedFileSizeW"; } }
        public RootUsage[] roots { get; set; }
    }
    public static class NativeSize {
        struct FileEntry { public FileInfo file; public bool reparse; }
        [StructLayout(LayoutKind.Sequential, CharSet=CharSet.Unicode)] struct ProcessEntry {
            public uint Size, Usage, Pid;
            public UIntPtr Heap;
            public uint Module, Threads, ParentPid;
            public int Priority;
            public uint Flags;
            [MarshalAs(UnmanagedType.ByValTStr, SizeConst=260)] public string Name;
        }
        [DllImport("kernel32.dll", SetLastError=true)] static extern SafeFileHandle CreateToolhelp32Snapshot(uint flags, uint pid);
        [DllImport("kernel32.dll", CharSet=CharSet.Unicode, SetLastError=true)] static extern bool Process32FirstW(SafeFileHandle snapshot, ref ProcessEntry entry);
        [DllImport("kernel32.dll", CharSet=CharSet.Unicode, SetLastError=true)] static extern bool Process32NextW(SafeFileHandle snapshot, ref ProcessEntry entry);
        public static int ParentPid(int pid) {
            using(var snapshot=CreateToolhelp32Snapshot(2,0)) {
                if(snapshot.IsInvalid) throw new Win32Exception(Marshal.GetLastWin32Error(), "Cannot verify launch process parent");
                var entry=new ProcessEntry { Size=(uint)Marshal.SizeOf<ProcessEntry>() };
                if(!Process32FirstW(snapshot,ref entry)) throw new Win32Exception(Marshal.GetLastWin32Error(), "Cannot enumerate launch process parent");
                do { if(entry.Pid==pid) return checked((int)entry.ParentPid); } while(Process32NextW(snapshot,ref entry));
                throw new IOException("Launch process was absent from the process snapshot");
            }
        }
        [StructLayout(LayoutKind.Sequential)] struct TagInfo { public uint Attributes, Tag; }
        [DllImport("kernel32.dll", CharSet=CharSet.Unicode, SetLastError=true)]
        static extern uint GetCompressedFileSizeW(string path, out uint high);
        [DllImport("kernel32.dll")] static extern void SetLastError(uint error);
        [DllImport("kernel32.dll", CharSet=CharSet.Unicode, SetLastError=true)]
        static extern SafeFileHandle CreateFileW(string path, uint access, uint share, IntPtr security, uint disposition, uint flags, IntPtr template);
        [DllImport("kernel32.dll", SetLastError=true)]
        static extern bool GetFileInformationByHandleEx(SafeFileHandle handle, int kind, out TagInfo info, uint bytes);
        public static bool AllowedFileTag(uint tag) { return tag == 0x80000017; } // IO_REPARSE_TAG_WOF; never a symlink/junction.
        public static long Get(string path, bool reparse) {
            string full=Path.GetFullPath(path);
            if(!full.StartsWith(@"\\?\")) full=full.StartsWith(@"\\") ? @"\\?\UNC\"+full.Substring(2) : @"\\?\"+full;
            if(reparse) {
                // OPEN_REPARSE_POINT: inspect the file's own tag without
                // resolving a link. WOF is compressed backing, not traversal.
                using(var handle=CreateFileW(full, 0, 7, IntPtr.Zero, 3, 0x00200000, IntPtr.Zero)) {
                    if(handle.IsInvalid) throw new Win32Exception(Marshal.GetLastWin32Error(), "Cannot inspect storage file: "+path);
                    TagInfo info;
                    if(!GetFileInformationByHandleEx(handle, 9, out info, 8)) throw new Win32Exception(Marshal.GetLastWin32Error(), "Cannot read storage file tag: "+path);
                    if((info.Attributes & 0x10)!=0 || !AllowedFileTag(info.Tag)) throw new IOException("Storage accounting refuses a linked file: "+path);
                }
            }
            SetLastError(0);
            uint high; uint low=GetCompressedFileSizeW(full, out high);
            int error=Marshal.GetLastWin32Error();
            if(low==UInt32.MaxValue && error!=0) throw new Win32Exception(error, "Cannot read allocated storage: "+path);
            ulong bytes=((ulong)high << 32) | low;
            if(bytes>Int64.MaxValue) throw new IOException("Allocated storage exceeds supported size: "+path);
            return (long)bytes;
        }
        // Keep the complete traversal in managed code. Calling a PowerShell
        // function (and resolving this type) once per file made every launch
        // inventory take tens of seconds across retained diagnostic trees.
        // This is a fresh inventory on every call, never a cached admission.
        static IEnumerable<FileEntry> EnumerateFiles(string root) {
            var pending = new Stack<DirectoryInfo>();
            pending.Push(new DirectoryInfo(root));
            while (pending.Count != 0) {
                var directory = pending.Pop();
                if ((directory.Attributes & FileAttributes.ReparsePoint) != 0)
                    throw new IOException("Storage accounting refuses a linked directory: " + directory.FullName);
                IEnumerator<FileSystemInfo> entries;
                try { entries = directory.EnumerateFileSystemInfos().GetEnumerator(); }
                catch (Exception error) { throw new IOException("Storage accounting could not completely read '" + directory.FullName + "': " + error.Message, error); }
                using (entries) {
                    while (true) {
                        FileSystemInfo child;
                        try {
                            if (!entries.MoveNext()) break;
                            child = entries.Current;
                        } catch (Exception error) { throw new IOException("Storage accounting could not completely read '" + directory.FullName + "': " + error.Message, error); }
                        if ((child.Attributes & FileAttributes.Directory) != 0) pending.Push((DirectoryInfo)child);
                        else yield return new FileEntry { file = (FileInfo)child, reparse = (child.Attributes & FileAttributes.ReparsePoint) != 0 };
                    }
                }
            }
        }
        public static Usage Scan(string[] roots) {
            var result = new Usage();
            var details = new List<RootUsage>();
            foreach (string root in roots) {
                var detail = new RootUsage { path = root };
                details.Add(detail);
                FileAttributes attributes;
                try { attributes = File.GetAttributes(root); }
                catch (DirectoryNotFoundException) { continue; }
                catch (FileNotFoundException) { continue; }
                if ((attributes & FileAttributes.Directory) == 0)
                    throw new IOException("Storage root is not a directory: " + root);
                // Bounded parallel allocation reads, not four independent
                // tree scans. Local sums avoid one lock per file; failed
                // enumeration/query rejects the whole result, never a subtotal.
                Parallel.ForEach(EnumerateFiles(root), new ParallelOptions { MaxDegreeOfParallelism = 4 },
                    () => new RootUsage(),
                    (entry, loop, subtotal) => {
                        long allocated = Get(entry.file.FullName, entry.reparse);
                        long logical = entry.file.Length;
                        checked {
                            subtotal.allocated_bytes += allocated;
                            subtotal.logical_bytes += logical;
                            subtotal.files++;
                        }
                        return subtotal;
                    },
                    subtotal => { lock (detail) { checked {
                        detail.allocated_bytes += subtotal.allocated_bytes;
                        detail.logical_bytes += subtotal.logical_bytes;
                        detail.files += subtotal.files;
                    } } });
                detail.bytes = detail.allocated_bytes;
                checked {
                    result.allocated_bytes += detail.allocated_bytes;
                    result.logical_bytes += detail.logical_bytes;
                    result.files += detail.files;
                }
            }
            result.bytes = result.allocated_bytes;
            result.roots = details.ToArray();
            return result;
        }
    }
}
'@
}

function Get-FvrFileAllocatedBytes {
    param([Parameter(Mandatory)][IO.FileInfo]$File)
    $reparse=[bool]($File.Attributes -band [IO.FileAttributes]::ReparsePoint)
    if([Environment]::OSVersion.Platform -eq [PlatformID]::Win32NT){
        Initialize-FvrStorageNative
        return [FvrStorage.NativeSize]::Get($File.FullName,$reparse)
    }
    if($reparse){throw "Storage accounting refuses a linked file: $($File.FullName)"}
    # No Windows allocation API on other platforms: conservatively retain the
    # logical estimate rather than assuming compression or sparse savings.
    return [long]$File.Length
}

function Get-FvrStorageRoots {
    param([Parameter(Mandatory)][string]$RepositoryRoot, [string[]]$AdditionalRoots=@())
    $repository=[IO.Path]::GetFullPath($RepositoryRoot)
    $candidates=@('build','reports','dist','release','test-temp') | ForEach-Object { Join-Path $repository $_ }
    $parent=Split-Path -Parent $repository
    $candidates+=Join-Path $parent 'bc2vr-recovery'
    if(Test-Path -LiteralPath $parent -PathType Container){
        # Historical preview workspaces are generated siblings, not free
        # storage outside the budget merely because they predate this repo.
        $candidates+=@(Get-ChildItem -LiteralPath $parent -Directory -Filter 'bc2vr-preview-*' -ErrorAction Stop | ForEach-Object {$_.FullName})
    }
    # Legacy development output on this installation; absent on ordinary clones.
    $legacy=Join-Path ([IO.Path]::GetPathRoot($repository)) 'BC2VR'
    if(Test-Path -LiteralPath $legacy -PathType Container){$candidates+=$legacy}
    $candidates+=$AdditionalRoots
    $roots=[Collections.Generic.List[string]]::new()
    foreach($candidate in ($candidates | ForEach-Object {[IO.Path]::GetFullPath($_)} | Sort-Object -Unique)){
        $nested=$false
        foreach($root in $roots){
            if($candidate.Equals($root,[StringComparison]::OrdinalIgnoreCase) -or $candidate.StartsWith($root.TrimEnd('\','/')+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase)){$nested=$true;break}
        }
        if(-not $nested){$roots.Add($candidate)}
    }
    return $roots.ToArray()
}

function Get-FvrStorageUsage {
    param([Parameter(Mandatory)][string[]]$Roots)
    # A .NET enumeration error is otherwise nonterminating when this helper is
    # called from an interactive shell with ErrorActionPreference=Continue.
    # Never return a partial disk count as an admitted complete inventory.
    $ErrorActionPreference='Stop'
    if([Environment]::OSVersion.Platform -eq [PlatformID]::Win32NT){
        Initialize-FvrStorageNative
        return [FvrStorage.NativeSize]::Scan($Roots)
    }
    [long]$total=0;[long]$logicalTotal=0;[long]$files=0
    $details=[Collections.Generic.List[object]]::new()
    foreach($root in $Roots){
        [long]$rootBytes=0;[long]$rootLogicalBytes=0;[long]$rootFiles=0
        if(Test-Path -LiteralPath $root){
            $entry=Get-Item -LiteralPath $root -Force -ErrorAction Stop
            if(-not $entry.PSIsContainer){throw "Storage root is not a directory: $root"}
            $pending=[Collections.Generic.Stack[IO.DirectoryInfo]]::new();$pending.Push($entry)
            while($pending.Count){
                $directory=$pending.Pop()
                if($directory.Attributes -band [IO.FileAttributes]::ReparsePoint){throw "Storage accounting refuses a linked directory: $($directory.FullName)"}
                try{
                    foreach($child in $directory.EnumerateFileSystemInfos()){
                        if($child.Attributes -band [IO.FileAttributes]::Directory){$pending.Push([IO.DirectoryInfo]$child)}
                        else{$rootBytes+=Get-FvrFileAllocatedBytes -File $child;$rootLogicalBytes+=$child.Length;$rootFiles++}
                    }
                }catch{throw "Storage accounting could not completely read '$($directory.FullName)': $($_.Exception.Message)"}
            }
        }
        $total+=$rootBytes;$logicalTotal+=$rootLogicalBytes;$files+=$rootFiles
        $details.Add([pscustomobject]@{path=$root;bytes=$rootBytes;allocated_bytes=$rootBytes;logical_bytes=$rootLogicalBytes;files=$rootFiles})
    }
    $method=if([Environment]::OSVersion.Platform -eq [PlatformID]::Win32NT){'GetCompressedFileSizeW'}else{'conservative_logical_fallback'}
    return [pscustomobject]@{bytes=$total;allocated_bytes=$total;logical_bytes=$logicalTotal;accounting=$method;files=$files;roots=$details.ToArray()}
}

function Get-FvrStorageVolume {
    param([Parameter(Mandatory)][string]$Path)
    $root=[IO.Path]::GetPathRoot([IO.Path]::GetFullPath($Path))
    $drive=[IO.DriveInfo]::new($root)
    if(-not $drive.IsReady){throw "Storage volume is not ready: $root"}
    return [pscustomobject]@{root=$root;free_bytes=[long]$drive.AvailableFreeSpace}
}

function Get-FvrLaunchProcessIdentity {
    param([int]$ProcessId=$PID)
    $process=Get-Process -Id $ProcessId -ErrorAction Stop
    return @{pid=$process.Id;start_utc=$process.StartTime.ToUniversalTime().ToString('o')}
}

function Get-FvrLaunchParentProcessId {
    # Called once by the new supervisor, not per file or per frame.
    Initialize-FvrStorageNative
    return [FvrStorage.NativeSize]::ParentPid($PID)
}

function ConvertTo-FvrLaunchEnvelope {
    param([Parameter(Mandatory)]$Payload)
    $key=[Convert]::FromBase64String($env:FVR_STORAGE_LAUNCH_KEY)
    if($key.Length -ne 32){throw 'Launch storage handoff key is unavailable'}
    $bytes=[Text.Encoding]::UTF8.GetBytes(($Payload | ConvertTo-Json -Depth 16 -Compress))
    $mac=[Security.Cryptography.HMACSHA256]::HashData($key,$bytes)
    return @{payload=[Convert]::ToBase64String($bytes);mac=[Convert]::ToHexString($mac)} | ConvertTo-Json -Compress
}

function ConvertFrom-FvrLaunchEnvelope {
    param([Parameter(Mandatory)][string]$Envelope)
    $record=$Envelope | ConvertFrom-Json
    $key=[Convert]::FromBase64String($env:FVR_STORAGE_LAUNCH_KEY)
    if($key.Length -ne 32){throw 'Launch storage handoff key is unavailable'}
    $bytes=[Convert]::FromBase64String($record.payload)
    $expected=[Security.Cryptography.HMACSHA256]::HashData($key,$bytes)
    $actual=[Convert]::FromHexString($record.mac)
    if(-not [Security.Cryptography.CryptographicOperations]::FixedTimeEquals($expected,$actual)){throw 'Launch storage handoff integrity check failed'}
    return [Text.Encoding]::UTF8.GetString($bytes) | ConvertFrom-Json -AsHashtable -DateKind String
}

function New-FvrLaunchStorageScope {
    param([Parameter(Mandatory)]$Admission,[Parameter(Mandatory)][string]$RepositoryRoot,
          [Parameter(Mandatory)][string]$ReceiptPath)
    if($Admission.schema -ne 'fvr.storage-admission.v1' -or $Admission.operation -ne 'Session' -or
       $Admission.PSObject.Properties.Name -contains 'launch_scope') {throw 'Launch handoff requires an original session inventory'}
    $age=([DateTime]::UtcNow-[DateTimeOffset]::Parse($Admission.checked_utc).UtcDateTime).TotalSeconds
    if($age -lt 0 -or $age -gt 10){throw 'Launch handoff requires the just-completed inventory'}
    $receipt=[IO.Path]::GetFullPath($ReceiptPath)
    if(-not $receipt.StartsWith(([IO.Path]::GetFullPath($Admission.output_path).TrimEnd('\')+'\'),[StringComparison]::OrdinalIgnoreCase)){
        throw 'Launch receipt must be within the admitted output directory'
    }
    $key=[byte[]]::new(32);[Security.Cryptography.RandomNumberGenerator]::Fill($key)
    $env:FVR_STORAGE_LAUNCH_KEY=[Convert]::ToBase64String($key)
    $payload=@{schema='fvr.storage-launch-scope.v1';nonce=[guid]::NewGuid().ToString('N');
        repository=[IO.Path]::GetFullPath($RepositoryRoot);receipt_path=$receipt;owner=(Get-FvrLaunchProcessIdentity);
        expires_utc=[DateTime]::UtcNow.AddSeconds(180).ToString('o');
        admission=$Admission;next_stage=0;consumed=@();stages=@('verified','session','trace')}
    $envelope=ConvertTo-FvrLaunchEnvelope $payload
    $stream=[IO.File]::Open($receipt,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::None)
    try{$bytes=[Text.Encoding]::UTF8.GetBytes($envelope);$stream.Write($bytes,0,$bytes.Length)}finally{$stream.Dispose()}
    return $receipt
}

function Use-FvrLaunchStorageScope {
    param([Parameter(Mandatory)][string]$ReceiptPath,[Parameter(Mandatory)][string]$Stage,
          [Parameter(Mandatory)][string]$RepositoryRoot,[Parameter(Mandatory)][string]$Operation,
          [Parameter(Mandatory)][string[]]$Roots,[long]$PlannedBytes,[long]$RawCaptureBytes,
          [long]$MaxRawCaptureBytes,[long]$MaxGeneratedBytes,[long]$MinFreeBytes)
    # Explicit finite chain: quick launcher -> verified helper (same process)
    # -> one child supervisor -> its trace. No ambient cache for other launches,
    # builds, captures or unrelated processes; each stage can consume once.
    if($Operation -ne 'Session'){throw 'Launch storage handoff is only valid for a session'}
    $receipt=[IO.Path]::GetFullPath($ReceiptPath)
    $stream=$null;$lockUntil=[DateTime]::UtcNow.AddSeconds(2)
    while(-not $stream){
        try{$stream=[IO.File]::Open($receipt,[IO.FileMode]::Open,[IO.FileAccess]::ReadWrite,[IO.FileShare]::None)}
        catch [IO.IOException]{
            # The launcher's read-only completion poll can briefly hold the
            # file. Retry only sharing/lock contention, never missing/denied IO.
            $errorCode=$_.Exception.HResult -band 0xffff
            if($errorCode -notin @(32,33) -or [DateTime]::UtcNow -ge $lockUntil){throw}
            Start-Sleep -Milliseconds 20
        }
    }
    try{
        if($stream.Length -gt 128KB){throw 'Launch storage handoff is oversized'}
        $reader=[IO.StreamReader]::new($stream,[Text.Encoding]::UTF8,$false,4096,$true)
        try{$scope=ConvertFrom-FvrLaunchEnvelope ($reader.ReadToEnd())}finally{$reader.Dispose()}
        if($scope.schema -ne 'fvr.storage-launch-scope.v1' -or $scope.receipt_path -ne $receipt -or
           $scope.repository -ne [IO.Path]::GetFullPath($RepositoryRoot)){throw 'Launch storage handoff belongs to a different invocation or repository'}
        if([DateTime]::UtcNow -gt [DateTimeOffset]::Parse($scope.expires_utc).UtcDateTime){throw 'Launch storage handoff expired; retry with a fresh launch inventory'}
        if($scope.next_stage -ge $scope.stages.Count -or $scope.stages[$scope.next_stage] -ne $Stage){throw 'Launch storage handoff stage was reused or is out of order'}
        $current=Get-FvrLaunchProcessIdentity
        $owner=Get-FvrLaunchProcessIdentity -ProcessId $scope.owner.pid
        if($owner.start_utc -ne $scope.owner.start_utc){throw 'Launch storage owner identity changed'}
        if($Stage -eq 'verified' -and ($current.pid -ne $owner.pid -or $current.start_utc -ne $owner.start_utc)){throw 'Launch storage helper is not its owning process'}
        if($Stage -eq 'session' -and ($current.pid -eq $owner.pid -or (Get-FvrLaunchParentProcessId) -ne $owner.pid)){throw 'Launch storage supervisor is not a direct child of its owner'}
        if($Stage -eq 'trace' -and ($scope.consumed.Count -ne 2 -or $current.pid -ne $scope.consumed[1].pid -or $current.start_utc -ne $scope.consumed[1].start_utc)){throw 'Launch storage trace is not in its admitted supervisor'}
        $original=$scope.admission
        if($PlannedBytes -gt $original.planned_bytes -or $RawCaptureBytes -gt $original.raw_capture_bytes -or
           $MaxRawCaptureBytes -ne $original.max_raw_capture_bytes -or $MaxGeneratedBytes -ne $original.max_generated_bytes -or $MinFreeBytes -ne $original.min_free_bytes){throw 'Launch storage request exceeds or changes the admitted limits'}
        foreach($root in $Roots){
            $covered=$false
            foreach($known in $original.generated.roots){
                $path=[IO.Path]::GetFullPath($known.path)
                if($root.Equals($path,[StringComparison]::OrdinalIgnoreCase) -or $root.StartsWith($path.TrimEnd('\')+'\',[StringComparison]::OrdinalIgnoreCase)){$covered=$true;break}
            }
            if(-not $covered){throw "Launch storage inventory does not cover output root: $root"}
        }
        $scope.consumed+=@(@{stage=$Stage;pid=$current.pid;start_utc=$current.start_utc;utc=[DateTime]::UtcNow.ToString('o')})
        $scope.next_stage++
        $bytes=[Text.Encoding]::UTF8.GetBytes((ConvertTo-FvrLaunchEnvelope $scope))
        $stream.Position=0;$stream.SetLength(0);$stream.Write($bytes,0,$bytes.Length);$stream.Flush()
        return @{generated=$original.generated;nonce=$scope.nonce;inventory_checked_utc=$original.checked_utc;stage=$Stage;expires_utc=$scope.expires_utc}
    }finally{$stream.Dispose()}
}

function Test-FvrLaunchStorageComplete {
    param([Parameter(Mandatory)][string]$ReceiptPath)
    # Parent stays alive until trace admission consumes its final stage, so a
    # supervisor cannot outlive the identity that granted the startup handoff.
    try{
        $scope=ConvertFrom-FvrLaunchEnvelope ([IO.File]::ReadAllText($ReceiptPath))
        return $scope.next_stage -eq 3 -and $scope.consumed.Count -eq 3
    }catch [IO.IOException]{return $false}
}

function Assert-FvrStorageBudget {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)][string]$RepositoryRoot,
        [Parameter(Mandatory)][string]$OutputPath,
        [ValidateSet('Build','Session','Capture')][string]$Operation='Session',
        [ValidateRange(0,[long]::MaxValue)][long]$PlannedBytes=512MB,
        [ValidateRange(0,[long]::MaxValue)][long]$RawCaptureBytes=0,
        [ValidateRange(1,[long]::MaxValue)][long]$MaxRawCaptureBytes=512MB,
        [ValidateRange(1,[long]::MaxValue)][long]$MaxGeneratedBytes=40GB,
        [ValidateRange(1,[long]::MaxValue)][long]$MinFreeBytes=15GB,
        [string[]]$AdditionalRoots=@(),
        [string]$LaunchReceiptPath,
        [ValidateSet('verified','session','trace')][string]$LaunchStage
    )
    # All checks precede caller-created directories, copies, builds and launches.
    if($RawCaptureBytes -gt $MaxRawCaptureBytes){throw "Storage budget refused: raw capture needs $RawCaptureBytes bytes; per-run limit is $MaxRawCaptureBytes. Reduce frame count/resolution or retain only the required event window."}
    if($RawCaptureBytes -gt $PlannedBytes){throw 'Storage budget refused: planned output does not include all raw capture bytes.'}
    if($PlannedBytes -gt $MaxGeneratedBytes){throw 'Storage budget refused: planned output exceeds the complete generated-artifact budget.'}
    $output=[IO.Path]::GetFullPath($OutputPath)
    # User policy: route actual temporary writes to the project/data drive
    # BEFORE admission. Do not reserve the unrelated Windows TEMP volume.
    # Only this tiny bootstrap directory precedes the full output admission.
    $temporary=Set-FvrProjectTemporaryStorage -RepositoryRoot $RepositoryRoot
    $volumePaths=@($output,[IO.Path]::GetTempPath())
    foreach($temporary in @($env:TEMP,$env:TMP)){if($temporary){$volumePaths+=$temporary}}
    $volumes=[Collections.Generic.List[object]]::new();$seen=[Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    foreach($path in $volumePaths){
        $volume=Get-FvrStorageVolume -Path $path
        if(-not $seen.Add($volume.root)){continue}
        # Reserve planned headroom on TEMP as well: compiler/linker scratch may
        # live on C: even when every persistent output is on another volume.
        if($volume.free_bytes -lt $MinFreeBytes -or ($volume.free_bytes-$MinFreeBytes) -lt $PlannedBytes){
            throw ('Storage budget refused: {0} has {1:N2} GiB free; retain {2:N2} GiB plus {3:N2} GiB planned output/scratch. No build or game process was started by this guard.' -f $volume.root,($volume.free_bytes/1GB),($MinFreeBytes/1GB),($PlannedBytes/1GB))
        }
        $volumes.Add($volume)
    }
    $roots=@(Get-FvrStorageRoots -RepositoryRoot $RepositoryRoot -AdditionalRoots (@($output)+$AdditionalRoots))
    if($LaunchReceiptPath){
        if(-not $LaunchStage){throw 'Explicit launch storage stage is required'}
        $handoff=Use-FvrLaunchStorageScope -ReceiptPath $LaunchReceiptPath -Stage $LaunchStage -RepositoryRoot $RepositoryRoot -Operation $Operation -Roots $roots -PlannedBytes $PlannedBytes -RawCaptureBytes $RawCaptureBytes -MaxRawCaptureBytes $MaxRawCaptureBytes -MaxGeneratedBytes $MaxGeneratedBytes -MinFreeBytes $MinFreeBytes
        return [pscustomobject]@{schema='fvr.storage-admission.v1';operation=$Operation;checked_utc=[DateTime]::UtcNow.ToString('o');output_path=$output;planned_bytes=$PlannedBytes;raw_capture_bytes=$RawCaptureBytes;max_raw_capture_bytes=$MaxRawCaptureBytes;max_generated_bytes=$MaxGeneratedBytes;min_free_bytes=$MinFreeBytes;generated=$handoff.generated;volumes=$volumes.ToArray();launch_scope=$handoff}
    }
    $usage=Get-FvrStorageUsage -Roots $roots
    if($usage.allocated_bytes -gt $MaxGeneratedBytes -or ($MaxGeneratedBytes-$usage.allocated_bytes) -lt $PlannedBytes){
        throw ('Storage budget refused: generated outputs occupy {0:N2} GiB on disk ({3:N2} GiB logical); {1:N2} GiB planned exceeds the {2:N2} GiB total limit. Review and prune superseded generated data before another run; the guard never deletes evidence.' -f ($usage.allocated_bytes/1GB),($PlannedBytes/1GB),($MaxGeneratedBytes/1GB),($usage.logical_bytes/1GB))
    }
    return [pscustomobject]@{schema='fvr.storage-admission.v1';operation=$Operation;checked_utc=[DateTime]::UtcNow.ToString('o');output_path=$output;planned_bytes=$PlannedBytes;raw_capture_bytes=$RawCaptureBytes;max_raw_capture_bytes=$MaxRawCaptureBytes;max_generated_bytes=$MaxGeneratedBytes;min_free_bytes=$MinFreeBytes;generated=$usage;volumes=$volumes.ToArray()}
}
