param([ValidateSet('x86','x64')][string]$Architecture='x86',[string]$Attempt='01',[string]$ReuseRuntimeFrom='',
 [ValidateSet('Bc2RigReadBatch','Bc2Rig','Bc2MagazineRawContactPipeline','Bc2MagazineAssembly','Bc2HeldFireCadence','Bc2ReloadFamilyDispatch','Bc2MagazineEmptyControl','Bc2ManualEmptyNative','Bc2ManualEmptyFamily','Bc2EmptyBoundaryDiagnostic')]
 [string[]]$Suites=@('Bc2MagazineRawContactPipeline','Bc2HeldFireCadence','Bc2ReloadFamilyDispatch'))
$ErrorActionPreference='Stop';$PSNativeCommandUseErrorActionPreference=$false
$work=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../..'))
$canonical='G:\Unity\VRMMO\FrostbiteVR'
$out=Join-Path $work ("reports/coordinator-20261030-offline57/checks-$Architecture-$Attempt")
if(Test-Path $out){throw 'Fresh output required'}
. (Join-Path $work 'tools/StorageBudget.ps1')
$budget=Assert-FvrStorageBudget -RepositoryRoot $work -OutputPath $out -Operation Build -PlannedBytes 128MB -AdditionalRoots @((Join-Path $canonical 'build'),'G:\Unity\VRMMO\bc2vr-recovery')
$null=[IO.Directory]::CreateDirectory($out)
function Save($name,$value){[IO.File]::WriteAllText((Join-Path $out $name),($value|ConvertTo-Json -Depth 12),[Text.UTF8Encoding]::new($false))}
function Pin($p){@{path=$p;sha256=(Get-FileHash -LiteralPath $p).Hash.ToLowerInvariant()}}
Save 'storage.json' $budget
$env:TEMP=Join-Path $out 'temp';$env:TMP=$env:TEMP;$env:TMPDIR=$env:TEMP;$null=[IO.Directory]::CreateDirectory($env:TEMP)
$vs='C:\Program Files\Microsoft Visual Studio\18\Community'
$arch=if($Architecture -eq 'x64'){'amd64'}else{'x86'}
& (Join-Path $vs 'Common7/Tools/Launch-VsDevShell.ps1') -VsInstallationPath $vs -Arch $arch -HostArch amd64 -SkipAutomaticLocation|Out-Null
$common=@('/nologo','/std:c++20','/EHsc','/MT','/O2','/Ob1','/DNDEBUG','/DNOMINMAX','/DWIN32_LEAN_AND_MEAN',('/I'+(Join-Path $work 'include')),('/I'+(Join-Path $work 'tests')),('/I'+(Join-Path $work 'src/games/bc2')),('/I'+(Join-Path $work 'third_party/minhook-1.3.4/include')),('/I'+(Join-Path $canonical "build/$Architecture/generated/bc2-operation")))
$libs=@('BC2Camera','FvrCore','FrostbiteDiscovery','FrostbiteCamera','BC2Profile')|ForEach-Object {Join-Path $canonical "build/$Architecture/$_.lib"}
$paths=@()
$paths+=@('src/games/bc2/Bc2MagazineEmptyControl.h','src/games/bc2/Bc2MagazineEmptyDiagnostic.h')
$needRig=@($Suites|Where-Object {$_ -in @('Bc2RigReadBatch','Bc2Rig','Bc2MagazineRawContactPipeline','Bc2MagazineAssembly')}).Count -gt 0
if($needRig){$paths+=@('src/games/bc2/Bc2Rig.cpp','src/games/bc2/Bc2Rig.h')}
if(($Suites -contains 'Bc2MagazineRawContactPipeline') -or ($Suites -contains 'Bc2MagazineAssembly')){$paths+=@('src/games/bc2/Bc2MagazinePresentation.cpp','src/games/bc2/Bc2MagazinePresentation.h','src/games/bc2/Bc2RigPublication.h','tests/Bc2MagazineRawContactPipelineFixture.generated.h')}
if($Suites -contains 'Bc2HeldFireCadence'){$paths+='src/games/bc2/Bc2HeldFireCadence.h'}
if($Suites -contains 'Bc2ReloadFamilyDispatch'){
 $paths+=@('src/games/bc2/Bc2ReloadFlowRuntime.cpp','src/games/bc2/Bc2ReloadFlowRuntime.h')
 if($Architecture -eq 'x86'){$libs+=Join-Path $canonical 'build/x86/FvrMinHook.lib'}
}
$paths+=@($Suites|ForEach-Object {"tests/${_}Tests.cpp"})
$sources=$paths|ForEach-Object {Join-Path $work $_}
$runtimeObject='ReloadRuntime.obj'
if($ReuseRuntimeFrom){
 if($Suites -notcontains 'Bc2ReloadFamilyDispatch'){throw 'Runtime reuse requires the dispatch suite'}
 $reuse=[IO.Path]::GetFullPath($ReuseRuntimeFrom,$work)
 if((Split-Path $reuse -Leaf) -notlike "checks-$Architecture-*"){throw 'Runtime object architecture differs'}
 $oldPins=Get-Content -LiteralPath (Join-Path $reuse 'inputs.json') -Raw|ConvertFrom-Json
 foreach($file in @('src/games/bc2/Bc2ReloadFlowRuntime.cpp','src/games/bc2/Bc2ReloadFlowRuntime.h')){
  $full=Join-Path $work $file;$old=@($oldPins|Where-Object {$_.path -eq $full})
  if($old.Count -ne 1 -or (Pin $full).sha256 -ne $old[0].sha256){throw 'Runtime source changed; compile a fresh object'}
 }
 $runtimeObject=Join-Path $reuse 'ReloadRuntime.obj';$sources+=$runtimeObject
}
$pins=@(($sources+$libs)|ForEach-Object {Pin $_});Save 'inputs.json' $pins
$results=@();Push-Location $out
try {
 if($needRig){
  & cl.exe @common /c (Join-Path $work 'src/games/bc2/Bc2Rig.cpp') /FoRig.obj *> 'rig-compile.log'
  if($LASTEXITCODE){Get-Content 'rig-compile.log' -Tail 24;throw 'Rig compilation failed'}
 }
 if(($Suites -contains 'Bc2MagazineRawContactPipeline') -or ($Suites -contains 'Bc2MagazineAssembly')){
  & cl.exe @common /c (Join-Path $work 'src/games/bc2/Bc2MagazinePresentation.cpp') /FoPresentation.obj *> 'presentation-compile.log'
  if($LASTEXITCODE){Get-Content 'presentation-compile.log' -Tail 24;throw 'Presentation compilation failed'}
 }
 if(($Suites -contains 'Bc2ReloadFamilyDispatch') -and -not $ReuseRuntimeFrom){
  & cl.exe @common /c (Join-Path $work 'src/games/bc2/Bc2ReloadFlowRuntime.cpp') /FoReloadRuntime.obj *> 'reload-runtime-compile.log'
  if($LASTEXITCODE){Get-Content 'reload-runtime-compile.log' -Tail 24;throw 'Reload runtime compilation failed'}
 }
 foreach($name in $Suites){
  $objects=@();if($needRig){$objects+='Rig.obj'}
  if($name -in @('Bc2MagazineRawContactPipeline','Bc2MagazineAssembly')){$objects+='Presentation.obj'}
  if($name -eq 'Bc2ReloadFamilyDispatch'){$objects+=$runtimeObject}
  & cl.exe @common (Join-Path $work "tests/${name}Tests.cpp") @objects @libs "/Fe$name.exe" /link /DEBUG:NONE /INCREMENTAL:NO /MANIFEST:EMBED *> "$name-compile.log"
  if($LASTEXITCODE){Get-Content "$name-compile.log" -Tail 24;throw "$name compilation failed"}
  $start=[Diagnostics.ProcessStartInfo]::new()
  $start.FileName=Join-Path $out "$name.exe";$start.WorkingDirectory=$out
  $start.UseShellExecute=$false;$start.CreateNoWindow=$true
  $start.RedirectStandardOutput=$true;$start.RedirectStandardError=$true
  $process=[Diagnostics.Process]::new();$process.StartInfo=$start
  try {
   $null=$process.Start();$stdout=$process.StandardOutput.ReadToEndAsync();$stderr=$process.StandardError.ReadToEndAsync()
   if(-not $process.WaitForExit(30000)){throw "$name exceeded its bounded test duration"}
   $code=$process.ExitCode
   [IO.File]::WriteAllText((Join-Path $out "$name-output.log"),($stdout.Result+$stderr.Result),[Text.UTF8Encoding]::new($false))
  }finally{$process.Dispose()}
  $results+=@{suite=$name;passed=($code -eq 0);exit_code=$code}
  Save 'results.json' @{architecture=$Architecture;checks=$results;game_started=$false;headset_accepted=$false}
  Get-Content "$name-output.log" -Tail 8
 }
 foreach($p in $pins){if((Pin $p.path).sha256 -ne $p.sha256){throw 'Input changed during checks'}}
 Save 'results.json' @{architecture=$Architecture;checks=$results;game_started=$false;headset_accepted=$false}
 if(@($results|Where-Object {-not $_.passed}).Count){throw 'Focused check failed'}
}finally{Pop-Location}
