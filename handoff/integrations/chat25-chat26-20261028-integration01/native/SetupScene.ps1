#requires -Version 7.5
[CmdletBinding()]
param(
 [Parameter(Mandatory)][ValidateSet('Start','Capture','Resume')][string]$Operation,
 [ValidatePattern('^game[0-9]{2}$')][string]$OutputName,
 [ValidatePattern('^[a-z][a-z0-9-]{0,31}$')][string]$CaptureName='menu',
 [ValidateRange(0,1)][double]$U,[ValidateRange(0,1)][double]$V,
 [ValidatePattern('^[a-f0-9]{64}$')][string]$ReviewedImageSha256
)
$ErrorActionPreference='Stop'
$repo='G:\Unity\VRMMO\FrostbiteVR'
$run=Join-Path $repo 'reports/integration-chat25-chat26-20261028-01/native/scene01'
$profile='C:\Users\outca\Documents\BFBC2'
$exe='G:\New folder (2)\Battlefield Bad Company 2\BFBC2Game.exe'
function Sha($p){(Get-FileHash -LiteralPath $p -Algorithm SHA256).Hash.ToLowerInvariant()}
function Load($p){Get-Content -LiteralPath $p -Raw|ConvertFrom-Json -DateKind String}
function Save($name,$value){$path=Join-Path $run $name;if(Test-Path -LiteralPath $path){throw 'Preserve prior scene evidence'};[IO.File]::WriteAllText($path,($value|ConvertTo-Json -Depth 16),[Text.UTF8Encoding]::new($false))}
function AssertLease {
 $lease=Load 'G:\Unity\VRMMO\bc2vr-task-worktrees\INTEGRATION.lock'
 if($lease.owner_token -cne 'chat25-chat26-20261028-integration01-12ad90001f4a4986aee8b08654985b14'){throw 'Another integration owns the lease'}
}
function AssertHelpersIdle {
 if(Get-Process BC2XrHost,BC2NativeTrace,BC2NativeIpcProbe,SceneMenuSidecar421,SceneMenuSidecar431,SceneMenuSidecarR46,ExceptionWatch422,ProvisionRunner -ErrorAction SilentlyContinue){throw 'A native/session helper is active'}
}
function Game($id){
 $p=Get-Process -Id $id.pid -ErrorAction Stop
 if($id.path -cne $exe -or $id.root_launched -isnot [bool] -or -not $id.root_launched -or
    $p.Path -cne $exe -or [string]$p.StartTime.ToUniversalTime().ToFileTimeUtc() -cne [string]$id.creation_filetime){throw 'Original game lifetime differs'}
 return $p
}
AssertLease
. (Join-Path $PSScriptRoot 'Dependencies.ps1');Assert-KnifeIntegrationDependencies
. (Join-Path $repo 'tools/StorageBudget.ps1');$null=Set-FvrProjectTemporaryStorage -RepositoryRoot $repo
Add-Type -AssemblyName System.Drawing
Add-Type -TypeDefinition @'
using System; using System.Runtime.InteropServices;
public static class KnifeSceneUi {
 [StructLayout(LayoutKind.Sequential)] public struct Rect { public int left,top,right,bottom; }
 [StructLayout(LayoutKind.Sequential)] public struct Point { public int x,y; }
 [StructLayout(LayoutKind.Sequential)] public struct Last { public uint size,tick; }
 public static uint Idle(uint last) { return unchecked((uint)Environment.TickCount - last); }
 [DllImport("user32.dll")] public static extern IntPtr SetThreadDpiAwarenessContext(IntPtr v);
 [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr h,out Rect r);
 [DllImport("user32.dll")] public static extern bool ClientToScreen(IntPtr h,ref Point p);
 [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
 [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
 [DllImport("user32.dll")] public static extern bool GetLastInputInfo(ref Last l);
 [DllImport("user32.dll")] public static extern bool SetCursorPos(int x,int y);
 [DllImport("user32.dll")] public static extern bool GetCursorPos(out Point p);
 [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
 [DllImport("user32.dll")] public static extern bool IsIconic(IntPtr h);
 [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h,out uint p);
 [DllImport("user32.dll")] public static extern void mouse_event(uint f,uint x,uint y,uint d,UIntPtr e);
 [DllImport("kernel32.dll")] public static extern ulong GetTickCount64();
}
'@
$null=[KnifeSceneUi]::SetThreadDpiAwarenessContext([IntPtr](-4))
function Idle {
 $last=[KnifeSceneUi+Last]::new();$last.size=8
 if(-not [KnifeSceneUi]::GetLastInputInfo([ref]$last) -or [KnifeSceneUi]::Idle($last.tick) -lt 3000){throw 'User input recently active; no input or foreground request'}
 return $last.tick
}
function Window($p){
 $h=$p.MainWindowHandle;$owner=0u
 $null=[KnifeSceneUi]::GetWindowThreadProcessId($h,[ref]$owner)
 if($h -eq [IntPtr]::Zero -or $owner -ne $p.Id -or [KnifeSceneUi]::GetForegroundWindow() -ne $h -or
    -not [KnifeSceneUi]::IsWindowVisible($h) -or [KnifeSceneUi]::IsIconic($h)){throw 'Exact visible game window must own foreground'}
 $r=[KnifeSceneUi+Rect]::new();$point=[KnifeSceneUi+Point]::new()
 if(-not [KnifeSceneUi]::GetClientRect($h,[ref]$r) -or -not [KnifeSceneUi]::ClientToScreen($h,[ref]$point) -or
    $r.right -ne 1920 -or $r.bottom -ne 1080 -or [long]$r.right*$r.bottom*4 -gt 8MB){throw 'Expected bounded1920x1080 game client'}
 return @{hwnd=[long]$h;pid=$p.Id;x=$point.x;y=$point.y;width=$r.right;height=$r.bottom}
}
if($Operation -eq 'Start'){
 if(-not $OutputName){throw 'Start requires one unused gameNN output name'}
 $null=Idle;AssertHelpersIdle
 if((Test-Path -LiteralPath $run) -or (Test-Path -LiteralPath (Join-Path 'G:\Unity\VRMMO\bc2vr-recovery\performance436' $OutputName))){throw 'Fresh scene01 and gameNN required'}
 if(Get-Process BFBC2Game -ErrorAction SilentlyContinue){throw 'An existing game owns the profile'}
 $budget=Assert-FvrStorageBudget -RepositoryRoot $repo -OutputPath $run -Operation Session -PlannedBytes 32MB -RawCaptureBytes 0 -AdditionalRoots @('G:\Unity\VRMMO\BC2VR-Hotfix018','G:\Unity\VRMMO\bc2vr-task-worktrees')
 [IO.Directory]::CreateDirectory((Join-Path $run 'profile-before'))|Out-Null
 Save 'storage-start.json' $budget
 $files=@(foreach($name in 'GameSettings.bin','GameSettings.ini','settings.ini'){
  $path=Join-Path $profile $name;$file=Get-Item -LiteralPath $path
  if($file.Length -gt 4MB -or ($file.Attributes -band [IO.FileAttributes]::ReparsePoint)){throw 'Unexpected original profile'}
  $hash=Sha $path;Copy-Item -LiteralPath $path -Destination (Join-Path $run ('profile-before/'+$name))
  if((Sha (Join-Path $run ('profile-before/'+$name))) -cne $hash -or (Sha $path) -cne $hash){throw 'Original profile changed during preservation'}
  @{name=$name;sha256=$hash;bytes=$file.Length}
 })
 Save 'profile-before.json' @{profile_path=$profile;files=$files}
 $save=Join-Path $profile 'GameSettings.bin'
 if([Text.Encoding]::ASCII.GetString([IO.File]::ReadAllBytes($save)) -notmatch '(?i)levels/SP_01'){throw 'Current saved campaign is not marked SP01; no seed is substituted'}
 Save 'checkpoint-preserved.json' @{path=$save;sha256=(Sha $save);seed_written=$false;level_marker='Levels/SP_01';actual_loaded_scene_verified=$false}
 $settings=Join-Path $profile 'settings.ini';$text=[IO.File]::ReadAllText($settings)
 foreach($pair in @(@('Width','1920'),@('Height','1080'),@('Fullscreen','false'),@('MSAA','0'))){
  $pattern='(?m)^'+$pair[0]+'=[^\r\n]*'
  if([regex]::Matches($text,$pattern).Count -ne 1){throw 'One existing graphics setting required'}
  $text=[regex]::Replace($text,$pattern,$pair[0]+'='+$pair[1])
 }
 [IO.File]::WriteAllText($settings,$text,[Text.UTF8Encoding]::new($false))
 Save 'temporary-settings.json' @{path=$settings;sha256=(Sha $settings);width=1920;height=1080;fullscreen=$false;msaa=0;actual_render_target_samples_verified=$false}
 $launch=Join-Path 'G:\Unity\VRMMO\bc2vr-recovery\performance436' $OutputName
 Save 'launch-request.json' @{output_name=$OutputName;original_process=(Join-Path $launch 'process.json');observed_process=(Join-Path $launch 'process-knife-observation.json')}
 try {
  & (Join-Path $PSScriptRoot 'StartObservedGame.ps1') -OutputName $OutputName -ProfileBackupReceipt (Join-Path $run 'profile-before.json') -ProfileBackupReceiptSha256 (Sha (Join-Path $run 'profile-before.json'))
 } finally {
  if(Test-Path -LiteralPath (Join-Path $launch 'process.json')){Save 'process.json' (Load (Join-Path $launch 'process.json'))}
 }
 $observed=Join-Path $launch 'process-knife-observation.json';Save 'observed-process.json' @{path=$observed;sha256=(Sha $observed)}
 $id=Load (Join-Path $run 'process.json');$p=Game $id;$null=Idle
 $focused=[KnifeSceneUi]::SetForegroundWindow($p.MainWindowHandle)
 Save 'focus-request.json' @{pid=$id.pid;creation_filetime=$id.creation_filetime;requested_once=$true;result=$focused;foreground_matches=([KnifeSceneUi]::GetForegroundWindow() -eq $p.MainWindowHandle)}
 Write-Output $observed
 return
}
if(Test-Path -LiteralPath (Join-Path $run 'finish.json')){throw 'Scene already finished'}
$id=Load (Join-Path $run 'process.json');$p=Game $id;AssertHelpersIdle
$window=Window $p
if($Operation -eq 'Capture'){
 $path=Join-Path $run ('capture-'+$CaptureName+'.png')
 if((Test-Path -LiteralPath $path) -or (Test-Path -LiteralPath (Join-Path $run ('capture-'+$CaptureName+'.json')))){throw 'Preserve previous capture'}
 $budget=Assert-FvrStorageBudget -RepositoryRoot $repo -OutputPath $run -Operation Capture -PlannedBytes 16MB -RawCaptureBytes 8MB -AdditionalRoots @('G:\Unity\VRMMO\BC2VR-Hotfix018','G:\Unity\VRMMO\bc2vr-task-worktrees')
 Save ('storage-capture-'+$CaptureName+'.json') $budget
 $bitmap=[Drawing.Bitmap]::new($window.width,$window.height);$graphics=[Drawing.Graphics]::FromImage($bitmap)
 try {
  $graphics.CopyFromScreen($window.x,$window.y,0,0,$bitmap.Size,[Drawing.CopyPixelOperation]::SourceCopy)
  $again=Window (Game $id)
  foreach($key in $window.Keys){if($window[$key] -ne $again[$key]){throw 'Game window changed during capture'}}
  $bitmap.Save($path,[Drawing.Imaging.ImageFormat]::Png)
 } finally {$graphics.Dispose();$bitmap.Dispose()}
 Save ('capture-'+$CaptureName+'.json') @{path=$path;sha256=(Sha $path);pid=$id.pid;creation_filetime=$id.creation_filetime;window=$window;observed_tick_ms=[KnifeSceneUi]::GetTickCount64();input_sent=$false;world_ready_claimed=$false}
 Write-Output $path
 return
}
if(-not $PSBoundParameters.ContainsKey('U') -or -not $PSBoundParameters.ContainsKey('V') -or -not $ReviewedImageSha256){throw 'Resume requires reviewed image hash and explicit coordinates'}
if(Test-Path -LiteralPath (Join-Path $run 'resume.json')){throw 'Only one ordinary Resume click is allowed'}
$capture=Load (Join-Path $run ('capture-'+$CaptureName+'.json'))
if($capture.sha256 -cne $ReviewedImageSha256 -or (Sha $capture.path) -cne $ReviewedImageSha256 -or $capture.pid -ne $id.pid -or
   [string]$capture.creation_filetime -cne [string]$id.creation_filetime){throw 'Reviewed image belongs to another game or changed'}
foreach($key in $window.Keys){if($window[$key] -ne $capture.window.$key){throw 'Reviewed window geometry changed; capture and review again'} }
$age=[KnifeSceneUi]::GetTickCount64()-[ulong]$capture.observed_tick_ms
if($age -gt 120000){throw 'Menu review is older than two minutes; take a fresh named capture'}
$budget=Assert-FvrStorageBudget -RepositoryRoot $repo -OutputPath $run -Operation Session -PlannedBytes 1MB -RawCaptureBytes 0 -AdditionalRoots @('G:\Unity\VRMMO\BC2VR-Hotfix018','G:\Unity\VRMMO\bc2vr-task-worktrees')
Save 'storage-resume.json' $budget
$tick=Idle;$null=$p.Handle
$modules=@($p.Modules|Select-Object ModuleName,FileName,BaseAddress,ModuleMemorySize)
if(@($modules|Where-Object {$_.ModuleName -like 'BC2*.dll' -or $_.ModuleName -like '*NativeProbe*' -or $_.ModuleName -like '*TestRange*' -or $_.ModuleName -like '*PreWorldMsaa*'}).Count){throw 'Resume requires stock game without attached helpers'}
Save 'modules-before-resume.json' $modules
$null=Game $id;AssertLease;$null=Window $p
$last=[KnifeSceneUi+Last]::new();$last.size=8
if(-not [KnifeSceneUi]::GetLastInputInfo([ref]$last) -or $last.tick -ne $tick){throw 'User input changed during admission'}
$x=$window.x+[int]($U*($window.width-1));$y=$window.y+[int]($V*($window.height-1))
Save 'resume.json' @{pid=$id.pid;creation_filetime=$id.creation_filetime;action='ordinary Resume';reviewed_image=$capture.path;reviewed_sha256=$ReviewedImageSha256;u=$U;v=$V;x=$x;y=$y;maximum_clicks=1;click_attempt_reserved=$true;native_helper_injected=$false}
if(-not [KnifeSceneUi]::SetCursorPos($x,$y)){throw 'Cursor movement failed'}
$positioned=[KnifeSceneUi+Point]::new();$positionInput=[KnifeSceneUi+Last]::new();$positionInput.size=8
if(-not [KnifeSceneUi]::GetCursorPos([ref]$positioned) -or $positioned.x -ne $x -or $positioned.y -ne $y -or
   -not [KnifeSceneUi]::GetLastInputInfo([ref]$positionInput)){throw 'Reviewed cursor position could not be retained'}
Start-Sleep -Milliseconds 120
$null=Window (Game $id)
$beforeClick=[KnifeSceneUi+Point]::new();$beforeClickInput=[KnifeSceneUi+Last]::new();$beforeClickInput.size=8
if(-not [KnifeSceneUi]::GetLastInputInfo([ref]$beforeClickInput) -or $beforeClickInput.tick -ne $positionInput.tick -or
   -not [KnifeSceneUi]::GetCursorPos([ref]$beforeClick) -or $beforeClick.x -ne $x -or $beforeClick.y -ne $y){throw 'Cursor or user input changed before mouse-down; no click'}
[KnifeSceneUi]::mouse_event(2,0,0,0,[UIntPtr]::Zero)
try {Start-Sleep -Milliseconds 80} finally {[KnifeSceneUi]::mouse_event(4,0,0,0,[UIntPtr]::Zero)}
$exited=$p.WaitForExit(20000)
Save 'resume-outcome.json' @{pid=$id.pid;creation_filetime=$id.creation_filetime;original_process_handle=$true;wait_limit_ms=20000;process_exited=$exited;exit_code=$(if($exited){$p.ExitCode}else{$null});world_ready_claimed=$false;forced_termination=$false}
if($exited){throw 'Original game exited after Resume; preserve failure evidence'}
