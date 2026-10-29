#requires -Version 7.5
# Read-only desktop admission diagnostic; never activates or switches desktops.
$ErrorActionPreference = 'Stop'
$repo = 'G:\Unity\VRMMO\FrostbiteVR'
. (Join-Path $repo 'tools/StorageBudget.ps1')
$null = Set-FvrProjectTemporaryStorage -RepositoryRoot $repo
Add-Type @'
using System;
using System.Runtime.InteropServices;
using System.Text;
public static class CoordinatorDesktopRead {
 [DllImport("kernel32.dll")] public static extern uint GetCurrentThreadId();
 [DllImport("kernel32.dll")] public static extern uint WTSGetActiveConsoleSessionId();
 [DllImport("user32.dll")] public static extern IntPtr GetProcessWindowStation();
 [DllImport("user32.dll")] public static extern IntPtr GetThreadDesktop(uint thread);
 [DllImport("user32.dll", SetLastError=true)] public static extern IntPtr OpenInputDesktop(uint flags,bool inherit,uint access);
 [DllImport("user32.dll")] public static extern bool CloseDesktop(IntPtr handle);
 [DllImport("user32.dll",CharSet=CharSet.Unicode,SetLastError=true)] public static extern bool GetUserObjectInformation(IntPtr h,int index,StringBuilder value,int bytes,out int needed);
 [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
 [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr handle,out uint pid);
 public static string Name(IntPtr h) {
  if(h==IntPtr.Zero) return null;
  int n; var b=new StringBuilder(512);
  return GetUserObjectInformation(h,2,b,1024,out n)?b.ToString():"error:"+Marshal.GetLastWin32Error();
 }
}
'@
$inputDesktop = [CoordinatorDesktopRead]::OpenInputDesktop(0,$false,1)
$inputError = [Runtime.InteropServices.Marshal]::GetLastWin32Error()
try {
 $foreground = [CoordinatorDesktopRead]::GetForegroundWindow()
 $foregroundOwner = [uint32]0
 $foregroundThread = [CoordinatorDesktopRead]::GetWindowThreadProcessId($foreground,[ref]$foregroundOwner)
 $threadDesktopName = [CoordinatorDesktopRead]::Name([CoordinatorDesktopRead]::GetThreadDesktop([CoordinatorDesktopRead]::GetCurrentThreadId()))
 $inputDesktopName = [CoordinatorDesktopRead]::Name($inputDesktop)
 [ordered]@{
  schema='fvr.coordinator.desktop-context.v1'; utc=[DateTime]::UtcNow.ToString('o')
  process_session=(Get-Process -Id $PID).SessionId
  console_session=[CoordinatorDesktopRead]::WTSGetActiveConsoleSessionId()
  station=[CoordinatorDesktopRead]::Name([CoordinatorDesktopRead]::GetProcessWindowStation())
  thread_desktop=$threadDesktopName; input_desktop=$inputDesktopName
  same_input_desktop=([bool]($inputDesktopName -and $threadDesktopName -and
   $inputDesktopName -notlike 'error:*' -and $threadDesktopName -notlike 'error:*' -and
   $threadDesktopName -ceq $inputDesktopName))
  input_open_error=$(if($inputDesktop -eq [IntPtr]::Zero){$inputError}else{0})
  foreground_handle=$foreground.ToInt64(); foreground_pid=$foregroundOwner
  foreground_process=$(if($foregroundOwner){(Get-Process -Id $foregroundOwner -ErrorAction SilentlyContinue).ProcessName})
  foreground_thread=$foregroundThread
  observation_only=$true; game_launched=$false; input_sent=$false
 } | ConvertTo-Json
} finally {
 if($inputDesktop -ne [IntPtr]::Zero){$null=[CoordinatorDesktopRead]::CloseDesktop($inputDesktop)}
}
