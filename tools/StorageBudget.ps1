# Storage admission only: never deletes files, starts a process, or changes native state.
function Initialize-FvrStorageNative {
    if('FvrStorage.NativeSize' -as [type]){return}
    Add-Type -TypeDefinition @'
using System;
using System.ComponentModel;
using System.IO;
using System.Runtime.InteropServices;
using Microsoft.Win32.SafeHandles;
namespace FvrStorage {
    public static class NativeSize {
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
        [string[]]$AdditionalRoots=@()
    )
    # All checks precede caller-created directories, copies, builds and launches.
    if($RawCaptureBytes -gt $MaxRawCaptureBytes){throw "Storage budget refused: raw capture needs $RawCaptureBytes bytes; per-run limit is $MaxRawCaptureBytes. Reduce frame count/resolution or retain only the required event window."}
    if($RawCaptureBytes -gt $PlannedBytes){throw 'Storage budget refused: planned output does not include all raw capture bytes.'}
    if($PlannedBytes -gt $MaxGeneratedBytes){throw 'Storage budget refused: planned output exceeds the complete generated-artifact budget.'}
    $output=[IO.Path]::GetFullPath($OutputPath)
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
    $usage=Get-FvrStorageUsage -Roots $roots
    if($usage.allocated_bytes -gt $MaxGeneratedBytes -or ($MaxGeneratedBytes-$usage.allocated_bytes) -lt $PlannedBytes){
        throw ('Storage budget refused: generated outputs occupy {0:N2} GiB on disk ({3:N2} GiB logical); {1:N2} GiB planned exceeds the {2:N2} GiB total limit. Review and prune superseded generated data before another run; the guard never deletes evidence.' -f ($usage.allocated_bytes/1GB),($PlannedBytes/1GB),($MaxGeneratedBytes/1GB),($usage.logical_bytes/1GB))
    }
    return [pscustomobject]@{schema='fvr.storage-admission.v1';operation=$Operation;checked_utc=[DateTime]::UtcNow.ToString('o');output_path=$output;planned_bytes=$PlannedBytes;raw_capture_bytes=$RawCaptureBytes;max_raw_capture_bytes=$MaxRawCaptureBytes;max_generated_bytes=$MaxGeneratedBytes;min_free_bytes=$MinFreeBytes;generated=$usage;volumes=$volumes.ToArray()}
}
