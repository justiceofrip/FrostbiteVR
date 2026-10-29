#requires -Version 7.5
[CmdletBinding()]
param([Parameter(Mandatory)][ValidateSet('Start','Activate','Capture','Resume','ExitSeat','Finish')][string]$Operation,
      [ValidatePattern('^scene[0-9]{2}$')][string]$SceneName='scene01',
      [ValidatePattern('^[a-z][a-z0-9-]{0,31}$')][string]$CaptureName='menu',
      [ValidateRange(0,45)][int]$WaitForegroundSeconds=0,
      [ValidatePattern('^[a-f0-9]{64}$')][string]$ReviewedImageSha256,
      [ValidateRange(0,1)][double]$U,[ValidateRange(0,1)][double]$V,
      [switch]$ReviewedExitPrompt)
$ErrorActionPreference='Stop'
$repo='G:\Unity\VRMMO\FrostbiteVR'
$run=Join-Path $repo ('reports/coordinator-20261028-runtime02/'+$SceneName)
$profile='C:\Users\outca\Documents\BFBC2'
$exe='G:\New folder (2)\Battlefield Bad Company 2\BFBC2Game.exe'
$exeHash='3911fcc8914b0158c434cee36f19295316e715d4c9a0e70c857e8c39e3d54258'
$seed='G:\Unity\VRMMO\bc2vr-recovery\performance436\vanilla-r43\run02\profile-before\GameSettings.bin'
$seedHash='775166d2f605518b1c30d63b834fcf68a064d9d71357e10e1b3f79d2b3c819de'
$windowHelper='G:\Unity\VRMMO\bc2vr-recovery\performance436\WindowPlacementR4.ps1'
function Sha($path){(Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant()}
function Load($path){Get-Content -LiteralPath $path -Raw|ConvertFrom-Json -DateKind String}
function Save($name,$value){
 $path=Join-Path $run $name
 if(Test-Path -LiteralPath $path){throw "Preserve existing evidence: $name"}
 [IO.File]::WriteAllText($path,($value|ConvertTo-Json -Depth 16),[Text.UTF8Encoding]::new($false))
}
function Lease {
 $lease=Load 'G:\Unity\VRMMO\bc2vr-task-worktrees\INTEGRATION.lock'
 if($lease.owner_token -cne 'campaign-coordinator-20261028-r1-runtime02-91aa4c54d34a41219b8483ccdd7629cb'){throw 'This run does not own integration'}
}
function IdleHelpers {
 if(Get-Process BC2XrHost,BC2NativeTrace,BC2NativeIpcProbe,BC2NativeMenuProbe,SceneMenuSidecar421,SceneMenuSidecar431,SceneMenuSidecarR46,ExceptionWatch422,ProvisionRunner,cl,link,cmake,ninja,ctest,MSBuild -ErrorAction SilentlyContinue){throw 'Shared helper remains active'}
}
function CurrentGame($identity){
 $games=@(Get-Process BFBC2Game -ErrorAction SilentlyContinue)
 if($games.Count -ne 1){throw 'Expected exactly one original game'}
 $game=$games[0]
 if($game.Id -ne $identity.pid -or $game.Path -cne $exe -or [string]$game.StartTime.ToUniversalTime().ToFileTimeUtc() -cne [string]$identity.creation_filetime){throw 'Original game lifetime differs'}
 return $game
}
Lease;IdleHelpers
. (Join-Path $repo 'tools/StorageBudget.ps1')
$null=Set-FvrProjectTemporaryStorage -RepositoryRoot $repo
$desktop=(& (Join-Path $PSScriptRoot 'DesktopContext.ps1')|ConvertFrom-Json)
$interactive=$desktop.same_input_desktop -and $desktop.station -ceq 'WinSta0' -and $desktop.process_session -eq $desktop.console_session
if($Operation -ne 'Finish' -and -not $interactive){throw 'Caller is not on the active interactive desktop; no launch/input/capture'}
if((Sha $windowHelper) -cne 'd166488c41d7df6b86c7782ea6178496683e67651dc3726571edb7270485111a'){throw 'Reviewed window helper changed'}
. $windowHelper
Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class CoordinatorSceneUi {
 [StructLayout(LayoutKind.Sequential)] public struct Last {public uint size,tick;}
 [StructLayout(LayoutKind.Sequential)] public struct Point {public int x,y;}
 [DllImport("user32.dll")] public static extern bool GetLastInputInfo(ref Last value);
 [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
 [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h,out uint pid);
 [DllImport("user32.dll")] public static extern bool ClientToScreen(IntPtr h,ref Point p);
 [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr h,IntPtr after,int x,int y,int w,int z,uint flags);
 [DllImport("user32.dll")] public static extern IntPtr SetThreadDpiAwarenessContext(IntPtr context);
 [DllImport("user32.dll")] public static extern bool SetCursorPos(int x,int y);
 [DllImport("user32.dll")] public static extern bool GetCursorPos(out Point p);
 [DllImport("user32.dll")] public static extern IntPtr WindowFromPoint(Point p);
 [DllImport("user32.dll")] public static extern short GetAsyncKeyState(int key);
 [DllImport("user32.dll")] public static extern void mouse_event(uint flags,uint x,uint y,uint data,UIntPtr extra);
 [DllImport("user32.dll")] public static extern void keybd_event(byte vk,byte scan,uint flags,UIntPtr extra);
 public static uint Idle(uint last){return unchecked((uint)Environment.TickCount-last);}
}
'@
function IdleInput {
 param([switch]$ReturnTick)
 $last=[CoordinatorSceneUi+Last]::new();$last.size=8
 if(-not [CoordinatorSceneUi]::GetLastInputInfo([ref]$last) -or [CoordinatorSceneUi]::Idle($last.tick) -lt 3000){throw 'Recent user input; no launch, activation or navigation'}
 if($ReturnTick){return $last.tick}
}
function Observe($identity){
 $game=CurrentGame $identity
 $window=[FvrWindow436R4.Native]::Observe($game.MainWindowHandle,[uint]$game.Id)
 $foreground=[CoordinatorSceneUi]::GetForegroundWindow();$owner=0u
 $thread=[CoordinatorSceneUi]::GetWindowThreadProcessId($foreground,[ref]$owner)
 $window['foreground']=$foreground.ToInt64()
 $window.Add('foreground_owner',$owner);$window.Add('foreground_thread',$thread)
 $window.Add('foreground_matches',($foreground.ToInt64() -eq $window.hwnd -and $owner -eq $identity.pid -and $thread -ne 0))
 $window.Add('creation_filetime',$identity.creation_filetime)
 return $window
}
function ReviewedWindow($identity,$capture,[long]$maximumAge){
 $age=[Environment]::TickCount64-[long]$capture.observed_tick_ms
 if($age -lt 0 -or $age -gt $maximumAge){throw 'Reviewed capture is stale; capture and review again'}
 $current=Observe $identity
 if(-not $current.foreground_matches){throw 'Exact reviewed game must own foreground; no navigation'}
 foreach($key in 'hwnd','pid','creation_filetime','style','visible','minimized'){
  if($current[$key] -ne $capture.window.$key){throw 'Reviewed window identity or state changed'}
 }
 foreach($key in 'rect','client_rect'){
  if(($current[$key]|ConvertTo-Json -Compress) -cne ($capture.window.$key|ConvertTo-Json -Compress)){throw 'Reviewed window geometry changed'}
 }
 return $current
}
function UnchangedInput([uint]$tick){
 $last=[CoordinatorSceneUi+Last]::new();$last.size=8
 if(-not [CoordinatorSceneUi]::GetLastInputInfo([ref]$last) -or $last.tick -ne $tick){throw 'User input changed during admission; no navigation'}
}
if($Operation -eq 'Start'){
 IdleInput
 if((Test-Path -LiteralPath $run) -or (Get-Process BFBC2Game -ErrorAction SilentlyContinue)){throw 'Fresh scene and no existing game required'}
 if((Sha $exe) -cne $exeHash -or (Sha $seed) -cne $seedHash -or (Get-Item -LiteralPath $seed).Length -ne 168868){throw 'Reviewed game/seed differs'}
 if([Text.Encoding]::ASCII.GetString([IO.File]::ReadAllBytes($seed)) -notmatch '(?i)levels/SP_01'){throw 'Seed map marker differs'}
 $install='G:\Unity\VRMMO\BC2VR-Launcher';$active=Load (Join-Path $install 'active.json')
 if($active.current -cne '0.1.12'){throw 'Installed baseline changed'}
 . (Join-Path $install 'versions/0.1.12/app/Launcher.Core.ps1')
 . (Join-Path $install 'versions/0.1.12/runtime/Runtime.Core.ps1')
 $config=Get-Bc2RuntimeConfig;$cache=Join-Path $install ('data/runtime/cache/'+$config.asset_recipe_sha256)
 if(-not (Test-Bc2AssetCache $cache $config.asset_recipe_sha256 $exeHash)){throw 'Existing asset cache failed verification'}
 $unreviewed=@(Get-ChildItem Env:|Where-Object {$_.Name -match '^(FVR_PRIVATE_|FVR_BC2_|BC2_WEATHER_)' -and $_.Value})
 if($unreviewed.Count){throw 'Unreviewed inherited diagnostic environment'}
 $budget=Assert-FvrStorageBudget -RepositoryRoot $repo -OutputPath $run -Operation Session -PlannedBytes 32MB -RawCaptureBytes 0 -AdditionalRoots @('G:\Unity\VRMMO\BC2VR-Hotfix018','G:\Unity\VRMMO\bc2vr-task-worktrees')
 [IO.Directory]::CreateDirectory((Join-Path $run 'profile-before'))|Out-Null
 Save 'storage-start.json' $budget;Save 'desktop-start.json' $desktop
 $files=@(foreach($name in 'GameSettings.bin','GameSettings.ini','settings.ini'){
  $path=Join-Path $profile $name;$item=Get-Item -LiteralPath $path
  if($item.Length -gt 4MB -or ($item.Attributes -band [IO.FileAttributes]::ReparsePoint)){throw 'Unexpected profile'}
  $hash=Sha $path;Copy-Item -LiteralPath $path -Destination (Join-Path $run ('profile-before/'+$name))
  if((Sha $path) -cne $hash -or (Sha (Join-Path $run ('profile-before/'+$name))) -cne $hash){throw 'Original profile changed during preservation'}
  @{name=$name;bytes=$item.Length;sha256=$hash}
 })
 Save 'profile-before.json' @{profile_path=$profile;files=$files}
 Lease
 [IO.File]::WriteAllBytes((Join-Path $profile 'GameSettings.bin'),[IO.File]::ReadAllBytes($seed))
 if((Sha (Join-Path $profile 'GameSettings.bin')) -cne $seedHash){throw 'Installed checkpoint differs'}
 $settings=Join-Path $profile 'settings.ini';$content=[IO.File]::ReadAllText($settings)
 foreach($pair in @(@('Width','1920'),@('Height','1080'),@('Fullscreen','false'),@('MSAA','0'))){
  $pattern='(?m)^'+$pair[0]+'=[^\r\n]*'
  if([regex]::Matches($content,$pattern).Count -ne 1){throw 'One existing graphics setting required'}
  $content=[regex]::Replace($content,$pattern,$pair[0]+'='+$pair[1])
 }
 [IO.File]::WriteAllText($settings,$content,[Text.UTF8Encoding]::new($false))
 Save 'temporary-profile.json' @{seed=$seed;seed_sha256=$seedHash;installed_seed_sha256=(Sha (Join-Path $profile 'GameSettings.bin'));settings_sha256=(Sha $settings);width=1920;height=1080;msaa=0;fullscreen=$false;loaded_map_verified=$false}
 $childEnvironment=Get-Bc2AudioEnvironment $cache
 $childEnvironment['FVR_PRIVATE_PRESENT_PROGRESS_MS']='0';$childEnvironment['FVR_PRIVATE_RENDER_QUEUE436']='1';$childEnvironment['FVR_PRIVATE_EXPORT_OVERLAP']='0'
 Lease;IdleHelpers;IdleInput
 $game=Start-Process -FilePath $exe -WorkingDirectory (Split-Path $exe) -WindowStyle Normal -Environment $childEnvironment -PassThru
 $identity=[ordered]@{pid=$game.Id;creation_filetime=[string]$game.StartTime.ToUniversalTime().ToFileTimeUtc();path=$exe;sha256=$exeHash;root_launched=$true;mod_injected=$false;asset_cache=$cache;recipe=$config.asset_recipe_sha256;environment=$childEnvironment;utc=[DateTime]::UtcNow.ToString('o')}
 Save 'process.json' $identity
 $clock=[Diagnostics.Stopwatch]::StartNew()
 do{$game=CurrentGame $identity;if($game.MainWindowHandle -ne [IntPtr]::Zero){break};Start-Sleep -Milliseconds 200}while($clock.ElapsedMilliseconds -lt 15000)
 if($game.MainWindowHandle -eq [IntPtr]::Zero){throw 'No exact visible game window within15s; preserve process and cleanup normally'}
 Save 'window-before.json' (Observe $identity)
 $null=CurrentGame $identity
 Save 'placement.json' ([FvrWindow436R4.Native]::PlaceLeftOnly($game.MainWindowHandle,[uint]$game.Id,1920))
  Lease
  Save 'foreground-observation.json' @{activation_requested=$false;input_sent=$false;desktop=$desktop;window=(Observe $identity)}
 Write-Output (Join-Path $run 'process.json')
 return
}
if(Test-Path -LiteralPath (Join-Path $run 'finish.json')){throw 'Scene already finished'}
if($Operation -eq 'Activate'){
 $identity=Load (Join-Path $run 'process.json');$window=Observe $identity
 $helper=Join-Path $repo 'launcher/runtime/activate_game_once.py'
 if((Sha $helper) -cne 'e247b25cd917cc49f127873cabd2e7109959dd13866792f5521d1e3568959dec'){throw 'Reviewed activation helper changed'}
 if(Test-Path -LiteralPath (Join-Path $run 'activation.json')){throw 'Only one established-helper activation attempt permitted'}
 $budget=Assert-FvrStorageBudget -RepositoryRoot $repo -OutputPath $run -Operation Session -PlannedBytes 16MB -RawCaptureBytes 0 -AdditionalRoots @('G:\Unity\VRMMO\BC2VR-Hotfix018','G:\Unity\VRMMO\bc2vr-task-worktrees')
 Save 'storage-activation.json' $budget
 Lease;IdleInput;$null=CurrentGame $identity
 $arguments=@('--pid',[string]$identity.pid,'--creation-filetime',[string]$identity.creation_filetime,'--hwnd',[string]$window.hwnd,'--exe',$exe,'--expected-sha256',$exeHash,'--output',(Join-Path $run 'activation.json'),'--allow-initial-activation')
 & 'G:\BF2142VR\tools\python-3.13.15\python.exe' -B -c 'import sys,runpy; sys.path.insert(0,r"G:\Unity\VRMMO\FrostbiteVR\tools"); sys.path.insert(0,r"G:\Unity\VRMMO\FrostbiteVR\launcher\runtime"); runpy.run_path(r"G:\Unity\VRMMO\FrostbiteVR\launcher\runtime\activate_game_once.py",run_name="__main__")' @arguments *> (Join-Path $run 'activation.log')
 $exit=$LASTEXITCODE
 Get-Content -LiteralPath (Join-Path $run 'activation.json')
 if($exit){throw 'Established activation helper refused; no automatic retry or gameplay input'}
 return
}
if($Operation -eq 'Capture'){
 $path=Join-Path $run ('capture-'+$CaptureName+'.png')
 foreach($name in @(('capture-'+$CaptureName+'.png'),('capture-'+$CaptureName+'.json'),('refused-'+$CaptureName+'.json'),('foreground-wait-'+$CaptureName+'.json'),('storage-'+$CaptureName+'.json'),('client-placement-'+$CaptureName+'.json'))){
  if(Test-Path -LiteralPath (Join-Path $run $name)){throw 'Capture name already used; preserve evidence and choose a fresh name'}
 }
 $identity=$null;$captureBefore=$null;$window=$null;$again=$null;$stage='identity-and-window'
 try{
 $identity=Load (Join-Path $run 'process.json');$window=Observe $identity
  $captureBefore=$window
  $stage='foreground-admission'
 if($WaitForegroundSeconds){
  $clock=[Diagnostics.Stopwatch]::StartNew();$observations=@();$ready=$false
  do{
   Lease;$window=Observe $identity;$idle=$false
   try{IdleInput;$idle=$true}catch{}
   $observations+=@{elapsed_ms=$clock.ElapsedMilliseconds;foreground=$window.foreground;owner=$window.foreground_owner;matches=$window.foreground_matches;input_idle=$idle}
   if($window.foreground_matches -and $idle){$ready=$true;break}
   Start-Sleep -Milliseconds 500
  }while($clock.ElapsedMilliseconds -lt $WaitForegroundSeconds*1000 -and $observations.Count -lt 90)
  Save ('foreground-wait-'+$CaptureName+'.json') @{ready=$ready;maximum_seconds=$WaitForegroundSeconds;observations=$observations;input_sent=$false;activation_requested=$false}
 }
  if(-not $window.foreground_matches){throw 'Exact game foreground required; no image/input'}
  $stage='client-bounds'
 $width=$window.client_rect.right;$height=$window.client_rect.bottom
 if($width -lt 800 -or $height -lt 500 -or $width -gt 1920 -or $height -gt 1080 -or [long]$width*$height*4 -gt 8MB){throw 'Observed client exceeds bounded menu image limits'}
  $stage='storage-admission'
 $budget=Assert-FvrStorageBudget -RepositoryRoot $repo -OutputPath $run -Operation Capture -PlannedBytes 16MB -RawCaptureBytes 8MB -AdditionalRoots @('G:\Unity\VRMMO\BC2VR-Hotfix018','G:\Unity\VRMMO\bc2vr-task-worktrees')
 Save ('storage-'+$CaptureName+'.json') $budget
 Add-Type -AssemblyName System.Drawing
 $dpi=[CoordinatorSceneUi]::SetThreadDpiAwarenessContext([IntPtr](-4))
 if($dpi -eq [IntPtr]::Zero){throw 'Physical client coordinates unavailable'}
 try{
   $stage='client-alignment'
  $point=[CoordinatorSceneUi+Point]::new()
  if(-not [CoordinatorSceneUi]::ClientToScreen([IntPtr][long]$window.hwnd,[ref]$point)){throw 'Client origin unavailable'}
  # Startup can shrink the client after its initial reservation. Align its
  # current physical client rectangle to the left monitor without resize,
  # style changes, Z-order changes or activation.
  $left=$window.left_monitor
  if($width -gt $left.right-$left.left -or $height -gt $left.bottom-$left.top){throw 'Client does not fit the left monitor'}
  if($point.x -ne $left.left -or $point.y -ne $left.top){
   Lease;IdleInput;$beforePlacement=Observe $identity
   if(-not $beforePlacement.foreground_matches -or $beforePlacement.hwnd -ne $window.hwnd){throw 'Foreground changed before position-only alignment'}
   $x=$window.rect.left+$left.left-$point.x;$y=$window.rect.top+$left.top-$point.y
   if(-not [CoordinatorSceneUi]::SetWindowPos([IntPtr][long]$window.hwnd,[IntPtr]::Zero,$x,$y,0,0,0x15)){throw 'Position-only client alignment failed'}
   $aligned=Observe $identity
   if(-not $aligned.foreground_matches -or $aligned.hwnd -ne $window.hwnd -or $aligned.style -ne $window.style -or ($aligned.client_rect|ConvertTo-Json -Compress) -cne ($window.client_rect|ConvertTo-Json -Compress)){throw 'Placement changed owner/foreground/style/client dimensions'}
   Save ('client-placement-'+$CaptureName+'.json') @{before=$window;after=$aligned;flags=0x15;input_sent=$false;activation_requested=$false}
   $window=$aligned
   $point=[CoordinatorSceneUi+Point]::new()
   if(-not [CoordinatorSceneUi]::ClientToScreen([IntPtr][long]$window.hwnd,[ref]$point) -or $point.x -ne $left.left -or $point.y -ne $left.top){throw 'Client alignment differs'}
   Start-Sleep -Milliseconds 250
  }
   $stage='capture-before'
   $again=Observe $identity
   if(-not $again.foreground_matches -or $again.hwnd -ne $window.hwnd -or ($again.rect|ConvertTo-Json -Compress) -cne ($window.rect|ConvertTo-Json -Compress) -or ($again.client_rect|ConvertTo-Json -Compress) -cne ($window.client_rect|ConvertTo-Json -Compress)){throw 'Window changed before capture'}
   $bitmap=[Drawing.Bitmap]::new($width,$height);$graphics=[Drawing.Graphics]::FromImage($bitmap)
  try{
    $stage='capture-copy'
   $graphics.CopyFromScreen($point.x,$point.y,0,0,$bitmap.Size,[Drawing.CopyPixelOperation]::SourceCopy)
    $stage='capture-after'
   $again=Observe $identity
    if(-not $again.foreground_matches -or $again.hwnd -ne $window.hwnd -or $again.style -ne $window.style -or ($again.rect|ConvertTo-Json -Compress) -cne ($window.rect|ConvertTo-Json -Compress) -or ($again.client_rect|ConvertTo-Json -Compress) -cne ($window.client_rect|ConvertTo-Json -Compress)){throw 'Window changed during capture'}
    $observedTick=[Environment]::TickCount64
   $bitmap.Save($path,[Drawing.Imaging.ImageFormat]::Png)
  }finally{$graphics.Dispose();$bitmap.Dispose()}
 }finally{$null=[CoordinatorSceneUi]::SetThreadDpiAwarenessContext($dpi)}
  $stage='capture-receipt'
  Save ('capture-'+$CaptureName+'.json') @{schema='fvr.coordinator.scene-capture.v1';scene_name=$SceneName;path=$path;sha256=(Sha $path);pid=$identity.pid;creation_filetime=$identity.creation_filetime;observed_tick_ms=$observedTick;client_origin=@{x=$point.x;y=$point.y};window=$window;after=$again;desktop=$desktop;input_sent=$false;native_attached=$false}
 Write-Output $path;return
 }catch{
  $failure=$_;$afterFailure=$null;$afterError=$null
  if($identity){try{$afterFailure=Observe $identity}catch{$afterError=$_.Exception.Message}}
  $foreground=[CoordinatorSceneUi]::GetForegroundWindow();$owner=0u
  $thread=[CoordinatorSceneUi]::GetWindowThreadProcessId($foreground,[ref]$owner)
  Save ('refused-'+$CaptureName+'.json') @{schema='fvr.coordinator.capture-refusal.v1';scene_name=$SceneName;stage=$stage;failure=$failure.Exception.Message;desktop=$desktop;before=$captureBefore;expected=$window;last_observed=$again;after=$afterFailure;after_observation_error=$afterError;foreground=@{hwnd=$foreground.ToInt64();pid=$owner;thread=$thread};image_written=(Test-Path -LiteralPath $path);input_sent=$false;activation_requested=$false}
  throw $failure
 }
}
if($Operation -in @('Resume','ExitSeat')){
 if(-not $PSBoundParameters.ContainsKey('CaptureName') -or -not $ReviewedImageSha256){throw 'Navigation requires an explicit root-reviewed capture name and image hash'}
 if($Operation -eq 'Resume'){
  if(-not $PSBoundParameters.ContainsKey('U') -or -not $PSBoundParameters.ContainsKey('V') -or [double]::IsNaN($U) -or [double]::IsNaN($V) -or [double]::IsInfinity($U) -or [double]::IsInfinity($V)){throw 'Resume requires explicit finite normalized coordinates from root image review'}
  $action='resume';$maximumAge=120000;$virtualKey=1
 }else{
  if(-not $ReviewedExitPrompt){throw 'ExitSeat requires the actual captured exit prompt explicitly reviewed by root'}
  if($PSBoundParameters.ContainsKey('U') -or $PSBoundParameters.ContainsKey('V')){throw 'ExitSeat accepts no click coordinates'}
  $action='exit-seat';$maximumAge=180000;$virtualKey=0x45
 }
 if(Test-Path -LiteralPath (Join-Path $run ($action+'.json'))){throw 'Only one navigation attempt of this kind is permitted per fresh scene'}
 $identity=Load (Join-Path $run 'process.json')
 $capturePath=Join-Path $run ('capture-'+$CaptureName+'.json');$imagePath=Join-Path $run ('capture-'+$CaptureName+'.png')
 foreach($entry in @(@($capturePath,1MB),@($imagePath,8MB))){
  $item=Get-Item -LiteralPath $entry[0]
  if($item.Length -gt $entry[1] -or ($item.Attributes -band [IO.FileAttributes]::ReparsePoint)){throw 'Unexpected reviewed capture file'}
 }
 $capture=Load $capturePath
 if($capture.schema -cne 'fvr.coordinator.scene-capture.v1' -or $capture.scene_name -cne $SceneName -or
    $capture.pid -ne $identity.pid -or [string]$capture.creation_filetime -cne [string]$identity.creation_filetime -or
    -not $capture.path -or [IO.Path]::GetFullPath($capture.path) -ine [IO.Path]::GetFullPath($imagePath) -or
    $capture.sha256 -cne $ReviewedImageSha256 -or (Sha $imagePath) -cne $ReviewedImageSha256 -or
    ($capture.observed_tick_ms -isnot [long] -and $capture.observed_tick_ms -isnot [int]) -or
    $capture.input_sent -isnot [bool] -or $capture.input_sent -or $capture.native_attached -isnot [bool] -or $capture.native_attached -or
    -not $capture.window.foreground_matches -or -not $capture.after.foreground_matches){throw 'Reviewed capture changed or belongs to another scene/game lifetime'}
 $game=CurrentGame $identity;$null=$game.Handle
 $modules=@($game.Modules|Select-Object ModuleName,FileName)
 if(@($modules|Where-Object {$_.ModuleName -like 'BC2*.dll' -or $_.ModuleName -like '*NativeProbe*' -or $_.ModuleName -like '*TestRange*' -or $_.ModuleName -like '*PreWorldMsaa*'}).Count){throw 'Navigation requires stock game without native helpers'}
 $window=ReviewedWindow $identity $capture $maximumAge
 $budget=Assert-FvrStorageBudget -RepositoryRoot $repo -OutputPath $run -Operation Session -PlannedBytes 1MB -RawCaptureBytes 0 -AdditionalRoots @('G:\Unity\VRMMO\BC2VR-Hotfix018','G:\Unity\VRMMO\bc2vr-task-worktrees')
 Save ('storage-'+$action+'.json') $budget;Save ('modules-before-'+$action+'.json') $modules
 $dpi=[CoordinatorSceneUi]::SetThreadDpiAwarenessContext([IntPtr](-4))
 if($dpi -eq [IntPtr]::Zero){throw 'Physical reviewed coordinates unavailable'}
 try{
  $point=[CoordinatorSceneUi+Point]::new()
  if(-not [CoordinatorSceneUi]::ClientToScreen([IntPtr][long]$window.hwnd,[ref]$point) -or $null -eq $capture.client_origin.x -or $null -eq $capture.client_origin.y -or $point.x -ne $capture.client_origin.x -or $point.y -ne $capture.client_origin.y){throw 'Reviewed client origin changed'}
  Lease;IdleHelpers;$admittedTick=IdleInput -ReturnTick
  $window=ReviewedWindow $identity $capture $maximumAge
  if([CoordinatorSceneUi]::GetAsyncKeyState($virtualKey) -band 0x8000){throw 'Physical navigation key/button already held; no input'}
  $record=@{scene_name=$SceneName;pid=$identity.pid;creation_filetime=$identity.creation_filetime;action=$Operation;reviewed_capture=$capturePath;reviewed_capture_sha256=(Sha $capturePath);reviewed_image=$imagePath;reviewed_image_sha256=$ReviewedImageSha256;reviewed_exit_prompt=[bool]$ReviewedExitPrompt;count=1;input_attempt_reserved=$true;activation_requested=$false;native_attached=$false;world_ready_claimed=$false;window=$window}
  if($Operation -eq 'Resume'){
   $x=$point.x+[int]($U*($window.client_rect.right-1));$y=$point.y+[int]($V*($window.client_rect.bottom-1))
   $record.u=$U;$record.v=$V;$record.x=$x;$record.y=$y;$record.hold_ms=80
  }else{$record.virtual_key=0x45;$record.scan=0x12;$record.hold_ms=150}
  Save ($action+'.json') $record
  Lease;IdleHelpers;$null=ReviewedWindow $identity $capture $maximumAge
  $game.Refresh();if($game.HasExited){throw 'Original game exited before navigation'}
  UnchangedInput $admittedTick
  if([CoordinatorSceneUi]::GetAsyncKeyState($virtualKey) -band 0x8000){throw 'Physical navigation key/button changed before input'}
  if($Operation -eq 'Resume'){
   if(-not [CoordinatorSceneUi]::SetCursorPos($x,$y)){throw 'Reviewed cursor movement failed'}
   $positioned=[CoordinatorSceneUi+Point]::new();$positionInput=[CoordinatorSceneUi+Last]::new();$positionInput.size=8
   if(-not [CoordinatorSceneUi]::GetCursorPos([ref]$positioned) -or $positioned.x -ne $x -or $positioned.y -ne $y -or -not [CoordinatorSceneUi]::GetLastInputInfo([ref]$positionInput)){throw 'Reviewed cursor position could not be retained'}
   Start-Sleep -Milliseconds 120
   Lease;IdleHelpers;$null=ReviewedWindow $identity $capture $maximumAge
   UnchangedInput $positionInput.tick
   $beforeClick=[CoordinatorSceneUi+Point]::new()
   if(-not [CoordinatorSceneUi]::GetCursorPos([ref]$beforeClick) -or $beforeClick.x -ne $x -or $beforeClick.y -ne $y -or [CoordinatorSceneUi]::WindowFromPoint($beforeClick) -ne [IntPtr][long]$window.hwnd -or ([CoordinatorSceneUi]::GetAsyncKeyState(1) -band 0x8000)){throw 'Reviewed game no longer owns the click position/button'}
   try{[CoordinatorSceneUi]::mouse_event(2,0,0,0,[UIntPtr]::Zero);Start-Sleep -Milliseconds 80}
   finally{[CoordinatorSceneUi]::mouse_event(4,0,0,0,[UIntPtr]::Zero)}
   $waitMs=20000
  }else{
   try{[CoordinatorSceneUi]::keybd_event(0x45,0x12,0,[UIntPtr]::Zero);Start-Sleep -Milliseconds 150}
   finally{[CoordinatorSceneUi]::keybd_event(0x45,0x12,2,[UIntPtr]::Zero)}
   $waitMs=400
  }
  $exited=$game.WaitForExit($waitMs)
  Save ($action+'-outcome.json') @{pid=$identity.pid;creation_filetime=$identity.creation_filetime;input_sent=$true;maximum_count=1;process_exited=$exited;wait_limit_ms=$waitMs;exit_code=$(if($exited){$game.ExitCode}else{$null});world_ready_claimed=$false;seat_exit_verified=$false;forced_termination=$false}
  if($exited){throw 'Original game exited after navigation; preserve evidence and close scene normally'}
 }finally{$null=[CoordinatorSceneUi]::SetThreadDpiAwarenessContext($dpi)}
 Write-Output (Join-Path $run ($action+'-outcome.json'));return
}
# This initial scene recipe permits stock-only cleanup. Native attachment must
# gain its own reviewed retirement binding before this guard can ever change.
$before=Load (Join-Path $run 'profile-before.json');$names=@('GameSettings.bin','GameSettings.ini','settings.ini')
if($before.profile_path -cne $profile -or @($before.files).Count -ne 3 -or ((@($before.files.name|Sort-Object)-join '|') -cne (($names|Sort-Object)-join '|'))){throw 'Exact profile set required'}
foreach($file in $before.files){if((Sha (Join-Path $run ('profile-before/'+$file.name))) -cne $file.sha256){throw 'Original backup changed'}}
if(Get-Process BFBC2Game -ErrorAction SilentlyContinue){
 if(-not $interactive){throw 'Use the active input desktop to close the original game; no profiles restored'}
 $identity=Load (Join-Path $run 'process.json');$game=CurrentGame $identity
 if(@($game.Modules|Where-Object {$_.ModuleName -like 'BC2*.dll' -or $_.ModuleName -like '*NativeProbe*' -or $_.ModuleName -like '*PreWorldMsaa*' -or $_.ModuleName -like '*TestRange*'}).Count){throw 'Native module observed; exact native retirement proof required before close'}
 Lease;$null=$game.CloseMainWindow()
 if(-not $game.WaitForExit(10000)){throw 'Original game did not close normally; no force termination/restoration'}
}
IdleHelpers
if(Get-Process BFBC2Game -ErrorAction SilentlyContinue){throw 'Game still owns profiles'}
$budget=Assert-FvrStorageBudget -RepositoryRoot $repo -OutputPath $run -Operation Session -PlannedBytes 16MB -RawCaptureBytes 0 -AdditionalRoots @('G:\Unity\VRMMO\BC2VR-Hotfix018','G:\Unity\VRMMO\bc2vr-task-worktrees')
$finishAttempt=1+@(Get-ChildItem -LiteralPath $run -Filter 'storage-finish-*.json').Count
if($finishAttempt -gt 3){throw 'Three cleanup attempts retained; inspect failures before further action'}
Save ('storage-finish-'+$finishAttempt+'.json') $budget
Lease
$after=Join-Path $run 'profile-after';[IO.Directory]::CreateDirectory($after)|Out-Null
$afterManifestExists=Test-Path -LiteralPath (Join-Path $run 'profile-after.json')
foreach($file in $before.files){
 $target=Join-Path $profile $file.name;$item=Get-Item -LiteralPath $target
 if($item.Length -gt 4MB -or ($item.Attributes -band [IO.FileAttributes]::ReparsePoint)){throw 'Unexpected profile before restoration'}
 $saved=Join-Path $after $file.name
 if(-not (Test-Path -LiteralPath $saved)){
  $hash=Sha $target;Copy-Item -LiteralPath $target -Destination $saved
  if((Sha $target) -cne $hash -or (Sha $saved) -cne $hash){throw 'After-profile preservation changed'}
 }
 if(-not $afterManifestExists -and (Sha $saved) -cne (Sha $target)){throw 'Incomplete prior after-image; preserve evidence before any restoration'}
}
if(-not (Test-Path -LiteralPath (Join-Path $run 'profile-after.json'))){
 Save 'profile-after.json' @{files=@(foreach($file in $before.files){@{name=$file.name;sha256=(Sha (Join-Path $after $file.name))}})}
}
$afterProof=Load (Join-Path $run 'profile-after.json')
foreach($file in $afterProof.files){if((Sha (Join-Path $after $file.name)) -cne $file.sha256){throw 'Preserved after-profile differs'}}
foreach($file in $before.files){
 Lease;IdleHelpers
 if(Get-Process BFBC2Game -ErrorAction SilentlyContinue){throw 'Game acquired profile during cleanup; stop restoration'}
 $target=Join-Path $profile $file.name
 [IO.File]::WriteAllBytes($target,[IO.File]::ReadAllBytes((Join-Path $run ('profile-before/'+$file.name))))
 if((Sha $target) -cne $file.sha256){throw 'Exact profile restoration failed'}
}
Save 'finish.json' @{closed_normally=$true;forced=$false;exact_three_profiles_restored=$true;native_attached=$false;headset_verified=$false}
