[CmdletBinding()]
param([ValidateSet('x86','x64')][string]$Architecture='x86',[string]$Attempt='01')
$ErrorActionPreference='Stop';$PSNativeCommandUseErrorActionPreference=$false
$work=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../..'))
$canonical='G:\Unity\VRMMO\FrostbiteVR'
$run=Join-Path $work 'reports/coordinator-20261031-cadence58'
$lease=Get-Content -LiteralPath (Join-Path $run 'coordinator-lease.json') -Raw|ConvertFrom-Json
if((Get-Content -LiteralPath 'G:\Unity\VRMMO\bc2vr-task-worktrees\COORDINATOR.lock' -Raw|ConvertFrom-Json).owner_token -ne $lease.owner_token){throw 'Coordinator lease differs'}
$integration=Get-Content -LiteralPath (Join-Path $run 'integration-lease.json') -Raw|ConvertFrom-Json
if((Get-Content -LiteralPath 'G:\Unity\VRMMO\bc2vr-task-worktrees\INTEGRATION.lock' -Raw|ConvertFrom-Json).owner_token -ne $integration.owner_token){throw 'Integration lease differs'}
if(Get-Process cl,link,cmake,ninja,ctest,MSBuild,BFBC2Game,BC2NativeTrace,BC2NativeIpcProbe,BC2XrHost -ErrorAction SilentlyContinue){throw 'Shared resources not idle'}
$out=Join-Path $run "checks-$Architecture-$Attempt"
if(Test-Path -LiteralPath $out){throw 'Preserve earlier check'}
. (Join-Path $canonical 'tools/StorageBudget.ps1')
$budget=Assert-FvrStorageBudget -RepositoryRoot $canonical -OutputPath $out -Operation Build -PlannedBytes 128MB -AdditionalRoots @('G:/Unity/VRMMO/bc2vr-task-worktrees','G:/Unity/VRMMO/BC2VR-Launcher')
$null=[IO.Directory]::CreateDirectory($out)
function Save($name,$value){[IO.File]::WriteAllText((Join-Path $out $name),($value|ConvertTo-Json -Depth 15),[Text.UTF8Encoding]::new($false))}
function Pin($path){@{path=$path;sha256=(Get-FileHash -LiteralPath $path).Hash.ToLowerInvariant()}}
Save 'storage.json' $budget
$env:TEMP=Join-Path $out 'temp';$env:TMP=$env:TEMP;$env:TMPDIR=$env:TEMP;$null=[IO.Directory]::CreateDirectory($env:TEMP)
$vs='C:\Program Files\Microsoft Visual Studio\18\Community'
$arch=if($Architecture -eq 'x64'){'amd64'}else{'x86'}
& (Join-Path $vs 'Common7/Tools/Launch-VsDevShell.ps1') -VsInstallationPath $vs -Arch $arch -HostArch amd64 -SkipAutomaticLocation|Out-Null
$common=@('/nologo','/std:c++20','/EHsc','/MT','/O2','/Ob1','/DNDEBUG','/DNOMINMAX','/DWIN32_LEAN_AND_MEAN',('/I'+(Join-Path $work 'include')),('/I'+(Join-Path $work 'tests')),('/I'+(Join-Path $work 'src/games/bc2')))
$libs=@('BC2Camera','FvrCore','FrostbiteDiscovery','FrostbiteCamera','BC2Profile')|ForEach-Object {Join-Path $canonical "build/$Architecture/$_.lib"}
$units=@('Bc2MagazineResourceReload','Bc2MagazinePhysicalReload')
$suites=@('Bc2MagazineResource','Bc2MagazinePhysical','Bc2ContextInteract','Bc2HeldFireCadence')
$sources=@($units|ForEach-Object {Join-Path $work "src/games/bc2/$_.cpp"})
$sources+=@($units|ForEach-Object {Join-Path $work "src/games/bc2/$_.h"})
$sources+=@($suites|ForEach-Object {Join-Path $work "tests/${_}Tests.cpp"})
$pins=@(($sources+$libs)|ForEach-Object {Pin $_});Save 'inputs.json' $pins
$results=@();Push-Location $out
try {
 foreach($unit in $units){
  & cl.exe @common /c (Join-Path $work "src/games/bc2/$unit.cpp") "/Fo$unit.obj" *> "$unit-compile.log"
  if($LASTEXITCODE){Get-Content "$unit-compile.log" -Tail 20;throw "$unit compilation failed"}
 }
 $objects=@($units|ForEach-Object {"$_.obj"})
 foreach($suite in $suites){
  & cl.exe @common (Join-Path $work "tests/${suite}Tests.cpp") @objects @libs "/Fe$suite.exe" /link /DEBUG:NONE /INCREMENTAL:NO /MANIFEST:EMBED *> "$suite-compile.log"
  if($LASTEXITCODE){Get-Content "$suite-compile.log" -Tail 20;throw "$suite compilation failed"}
  $start=[Diagnostics.ProcessStartInfo]::new();$start.FileName=Join-Path $out "$suite.exe";$start.WorkingDirectory=$out
  $start.UseShellExecute=$false;$start.CreateNoWindow=$true;$start.RedirectStandardOutput=$true;$start.RedirectStandardError=$true
  $process=[Diagnostics.Process]::new();$process.StartInfo=$start
  try {
   $null=$process.Start();$stdout=$process.StandardOutput.ReadToEndAsync();$stderr=$process.StandardError.ReadToEndAsync()
   if(-not $process.WaitForExit(30000)){throw "$suite exceeded bounded duration"}
   $code=$process.ExitCode
   [IO.File]::WriteAllText((Join-Path $out "$suite-output.log"),($stdout.Result+$stderr.Result),[Text.UTF8Encoding]::new($false))
  }finally{$process.Dispose()}
  $results+=@{suite=$suite;passed=($code -eq 0);exit_code=$code}
  Save 'results.json' @{architecture=$Architecture;checks=$results;game_started=$false;headset_accepted=$false}
  Get-Content "$suite-output.log" -Tail 6
 }
 foreach($pin in $pins){if((Pin $pin.path).sha256 -ne $pin.sha256){throw 'Input changed during checks'}}
 if(@($results|Where-Object {-not $_.passed}).Count){throw 'Focused checks failed'}
}finally{Pop-Location}
