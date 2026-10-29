#requires -Version 7.5
# Portable runtime policy. All mutable files live under the chosen data drive.
Set-StrictMode -Version 3
function Get-Bc2RuntimeConfig {
    $config=Read-FvrJson (Join-Path $PSScriptRoot 'runtime.json')
    if($config.schema -ne 'bc2vr.runtime.v1' -or $config.diagnostic_only -or $config.source_sha256 -notmatch '^[a-f0-9]{64}$'){throw 'Unsupported runtime package.'}
    foreach($file in $config.files){
        $path=Get-FvrChild $PSScriptRoot $file.path; Assert-FvrNoLinks $path
        if((Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant() -ne $file.sha256){throw "Runtime file changed: $($file.path)"}
    }
    return $config
}
function Resolve-Bc2Tool([string]$Override,[string[]]$Names,[string]$Label) {
    if($Override){if(-not [IO.Path]::IsPathFullyQualified($Override) -or -not(Test-Path -LiteralPath $Override -PathType Leaf)){throw "$Label override must name an existing executable."};return $Override}
    foreach($name in $Names){$cmd=Get-Command $name -CommandType Application -ErrorAction SilentlyContinue|Select-Object -First 1;if($cmd -and $cmd.Source -notmatch '[\\/]WindowsApps[\\/]'){return $cmd.Source}}
    throw "$Label is required for first-time local asset preparation. Install it on your data drive and add it to PATH (or set FVR_PYTHON/FVR_FFMPEG)."
}
function Initialize-Bc2ProcessIdentityNative {
    if('Bc2Vr.ProcessImageIdentity' -as [type]){return}
    Add-Type -TypeDefinition @'
using System;
using System.ComponentModel;
using System.Runtime.InteropServices;
using System.Text;
namespace Bc2Vr {
    public static class ProcessImageIdentity {
        [DllImport("kernel32.dll", SetLastError=true)]
        static extern uint GetProcessId(IntPtr process);
        [DllImport("kernel32.dll", SetLastError=true)]
        [return: MarshalAs(UnmanagedType.Bool)]
        static extern bool GetProcessTimes(IntPtr process, out long creation, out long exit, out long kernel, out long user);
        [DllImport("kernel32.dll", SetLastError=true)]
        static extern uint WaitForSingleObject(IntPtr handle, uint milliseconds);
        [DllImport("kernel32.dll", EntryPoint="QueryFullProcessImageNameW", CharSet=CharSet.Unicode, ExactSpelling=true, SetLastError=true)]
        [return: MarshalAs(UnmanagedType.Bool)]
        static extern bool QueryFullProcessImageName(IntPtr process, uint flags, StringBuilder path, ref uint length);
        static void RequireLive(IntPtr handle) {
            uint state=WaitForSingleObject(handle,0);
            if(state==0xffffffff)throw new Win32Exception(Marshal.GetLastWin32Error(),"Cannot check retained process lifetime");
            if(state!=258)throw new InvalidOperationException("Retained process is no longer live");
        }
        public static string Resolve(IntPtr handle, uint expectedPid, long expectedCreation, bool requireLive) {
            if(handle==IntPtr.Zero || handle==new IntPtr(-1))throw new InvalidOperationException("Retained process handle is unavailable");
            uint actualPid=GetProcessId(handle);
            if(actualPid==0)throw new Win32Exception(Marshal.GetLastWin32Error(),"Cannot identify retained process handle");
            if(expectedPid==0 || actualPid!=expectedPid)throw new InvalidOperationException("Retained process PID differs");
            long creation,exit,kernel,user;
            if(!GetProcessTimes(handle,out creation,out exit,out kernel,out user))
                throw new Win32Exception(Marshal.GetLastWin32Error(),"Cannot read retained process creation time");
            if(creation!=expectedCreation)throw new InvalidOperationException("Retained process creation time differs");
            if(requireLive)RequireLive(handle);
            var buffer=new StringBuilder(32768);uint length=32768;
            if(!QueryFullProcessImageName(handle,0,buffer,ref length)) {
                int error=Marshal.GetLastWin32Error();
                // An exited helper can lose its queryable image path even
                // while its retained handle still proves PID and birth.
                // This is diagnostic identity only, never action authority.
                if(!requireLive && WaitForSingleObject(handle,0)==0)return null;
                throw new Win32Exception(error,"Cannot query retained process image path");
            }
            if(length==0 || length>=32768)throw new InvalidOperationException("Retained process image path is empty or truncated");
            string path=buffer.ToString();
            if(String.IsNullOrWhiteSpace(path) || !System.IO.Path.IsPathFullyQualified(path))
                throw new InvalidOperationException("Retained process image path is unavailable or invalid");
            if(requireLive)RequireLive(handle);
            return path;
        }
    }
}
'@
}
function Get-Bc2ProcessIdentity($Process,[bool]$RequireLive=$false) {
    # Acquire the real retained handle before reading metadata. Managed Path
    # can be missing or name a loader DLL during startup; query the executable
    # from that handle without reading Path or substituting a configured path.
    $handle=[IntPtr]$Process.Handle;$id=[uint32]$Process.Id
    $start=$Process.StartTime.ToUniversalTime()
    Initialize-Bc2ProcessIdentityNative
    # Recording a just-exited helper must preserve its identity/exit result.
    # Only a proven exited helper may record a missing path. Action lookup opts
    # into liveness and always requires the actual nonempty image path.
    $resolved=[Bc2Vr.ProcessImageIdentity]::Resolve($handle,$id,$start.ToFileTimeUtc(),$RequireLive)
    return @{pid=$id;start_utc=$start.ToString('o');path=$resolved}
}
function Find-Bc2OwnedProcess($Identity) {
    if(-not $Identity){return $null}
    $p=$null;$matched=$false
    try {
        if($Identity.path -isnot [string] -or [string]::IsNullOrWhiteSpace($Identity.path) -or -not [IO.Path]::IsPathFullyQualified($Identity.path)){return $null}
        $p=Get-Process -Id $Identity.pid -ErrorAction SilentlyContinue
        if(-not $p){return $null}
        $observed=Get-Bc2ProcessIdentity $p -RequireLive $true
        if($observed.pid -ne $Identity.pid -or [DateTimeOffset]::Parse($observed.start_utc).UtcDateTime -ne [DateTimeOffset]::Parse($Identity.start_utc).UtcDateTime -or
            -not [string]::Equals($observed.path,$Identity.path,[StringComparison]::OrdinalIgnoreCase)){return $null}
        $matched=$true;return $p
    }catch{return $null}
    finally {if(-not $matched -and $p -is [Diagnostics.Process]){$p.Dispose()}}
}
function ConvertTo-Bc2CommandArgument([string]$Value) {
    if($Value.Contains([char]0)){throw 'NUL in process argument.'}
    # CommandLineToArgvW quoting, including quotes and trailing backslashes.
    return '"'+[regex]::Replace([regex]::Replace($Value,'(\\*)"','$1$1\"'),'(\\+)$','$1$1')+'"'
}
function Start-Bc2Child([string]$File,[string[]]$Arguments,[string]$Folder,[string]$LogName,[hashtable]$Environment=@{}) {
    $options=@{FilePath=$File;WorkingDirectory=$Folder;WindowStyle='Hidden';PassThru=$true;Environment=$Environment
        RedirectStandardOutput=(Join-Path $Folder ($LogName+'.stdout.log'));RedirectStandardError=(Join-Path $Folder ($LogName+'.stderr.log'))}
    if($Arguments.Count){$options.ArgumentList=(@($Arguments|ForEach-Object{ConvertTo-Bc2CommandArgument $_}) -join ' ')}
    $p=Start-Process @options;$null=$p.Handle;return $p
}
function Get-Bc2Session([string]$StateRoot) {
    $record=Join-Path $StateRoot 'active-session.json'
    if(-not(Test-Path -LiteralPath $record)){return $null}
    $active=Read-FvrJson $record
    $folder=Get-FvrChild $StateRoot $active.relative_folder; Assert-FvrNoLinks $folder
    if(-not $active.relative_folder.StartsWith('sessions/')){throw 'Invalid session path.'}
    $active.folder=$folder
    return $active
}
function Get-Bc2ExistingGame([string]$Exe,[bool]$Fresh) {
    $games=@(Get-Process BFBC2Game -ErrorAction SilentlyContinue)
    if($games.Count -gt 1){throw 'Multiple BC2 games are running; no process was selected.'}
    if(-not $games.Count){return $null}
    $game=$games[0]
    $identity=Get-Bc2ProcessIdentity $game -RequireLive $true
    if(-not [string]::Equals($identity.path,$Exe,[StringComparison]::OrdinalIgnoreCase)){throw 'Another BC2 installation is running.'}
    if(-not $Fresh){throw 'BC2 is still open. Enable Restart BC2 before launch, or close BC2 normally and launch again. A fresh game is required for VR and its audio settings; reinstalling BC2VR is unnecessary.'}
    return $game
}
function Get-Bc2SessionRecord($Active) {
    if(-not $Active){return $null}
    $path=Get-FvrChild $Active.folder 'session.json'
    try {
        Assert-FvrNoLinks $path
        if(-not(Test-Path -LiteralPath $path -PathType Leaf) -or (Get-Item -LiteralPath $path).Length -gt 32768){return $null}
        $record=Read-FvrJson $path
        if($record.schema -cne 'bc2vr.session.v1' -or $record.version -cne $Active.version -or
           $record.supervisor.pid -ne $Active.supervisor.pid -or
           $record.supervisor.start_utc -cne $Active.supervisor.start_utc -or
           $record.supervisor.path -ine $Active.supervisor.path){return $null}
        return $record
    } catch {return $null}
}
function Assert-Bc2RuntimeSpace([string]$StateRoot) {
    Assert-FvrNoLinks $StateRoot
    Assert-FvrFreeSpace $StateRoot 768MB
    # No silent deletion of failure evidence. Internal session accumulation is
    # capped; the launcher explains what to remove before starting more work.
    [long]$used=0
    if(Test-Path -LiteralPath $StateRoot){foreach($f in Get-ChildItem -LiteralPath $StateRoot -File -Recurse){if($f.Attributes -band [IO.FileAttributes]::ReparsePoint){throw 'Linked runtime evidence refused.'};$used+=$f.Length}}
    if($used+768MB -gt 4GB){throw 'BC2 VR runtime evidence reached its 4 GiB cap. Keep wanted feedback logs, then remove old folders under data/runtime/sessions before launching again.'}
}
function Get-Bc2AudioEnvironment([string]$Cache) {
    # This map is supplied only to the fresh game process. Keep the validated
    # runtime cloud correction enabled and exclude inherited diagnostic modes.
    return @{BC2_MECHANISM_AUDIO_REPLACE_SPAS='1';BC2_MECHANISM_AUDIO_CACHE=(Join-Path $Cache 'pump');BC2_SHELL_RELOAD_AUDIO_REPLACE='1';BC2_SHELL_RELOAD_AUDIO_CACHE=(Join-Path $Cache 'shell');
        BC2_WEATHER_TRANSLATION='1';BC2_WEATHER_OBSERVER=$null;BC2_WEATHER_TRANSLATION_EXPERIMENT=$null;BC2_WEATHER_OBSERVER_DELAY_MS=$null}
}
function Test-Bc2AssetCache([string]$Cache,[string]$Recipe,[string]$GameHash) {
    $record=Join-Path $Cache 'assets.json'
    if(-not(Test-Path -LiteralPath $record)){return $false}
    try {
        $manifest=Read-FvrJson $record
        if($manifest.schema -ne 'bc2vr.local-assets.v1' -or $manifest.recipe_sha256 -ne $Recipe -or $manifest.game_sha256 -ne $GameHash -or $manifest.files.Count -ne 6){return $false}
        foreach($f in $manifest.files){$path=Get-FvrChild $Cache $f.path;Assert-FvrNoLinks $path;if((Get-FileHash -LiteralPath $path).Hash.ToLowerInvariant() -ne $f.sha256){return $false}}
        return $true
    } catch{return $false}
}
