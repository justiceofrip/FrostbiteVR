[CmdletBinding()]
param([string]$Attempt='01',[string[]]$Suites=@('Bc2AuthoredSupport','SupportGrip','Bc2PhysicalReload','Bc2PhysicalFeed','Bc2FireRouteTelemetry','Bc2ShotgunEquipment','ShellReloadAudio','MechanismAudio','Bc2HeldFireCadence'),[switch]$SkipPython)
$ErrorActionPreference='Stop';$PSNativeCommandUseErrorActionPreference=$false
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../..'))
$run=Join-Path $repo 'reports/coordinator-20261031-feedback60'
$lease=Get-Content -LiteralPath (Join-Path $run 'leases.json') -Raw|ConvertFrom-Json
if((Get-Content -LiteralPath $lease.INTEGRATION.path -Raw|ConvertFrom-Json).owner_token -ne $lease.INTEGRATION.owner_token){throw 'Lease differs'}
if(Get-Process cl,link,cmake,ninja,ctest,MSBuild,BFBC2Game,BC2NativeTrace,BC2NativeIpcProbe,BC2XrHost -ErrorAction SilentlyContinue){throw 'Shared resources not idle'}
$out=Join-Path $run "focused-$Attempt"
if(Test-Path -LiteralPath $out){throw 'Preserve earlier attempt'}
. (Join-Path $repo 'tools/StorageBudget.ps1')
$budget=Assert-FvrStorageBudget -RepositoryRoot $repo -OutputPath $out -Operation Build -PlannedBytes 1GB -AdditionalRoots @('G:/Unity/VRMMO/bc2vr-task-worktrees','G:/Unity/VRMMO/BC2VR-Launcher')
$null=[IO.Directory]::CreateDirectory($out)
function Save($name,$value){[IO.File]::WriteAllText((Join-Path $out $name),($value|ConvertTo-Json -Depth 20),[Text.UTF8Encoding]::new($false))}
Save 'storage.json' $budget
$env:TEMP=Join-Path $repo 'build/scratch';$env:TMP=$env:TEMP;$env:TMPDIR=$env:TEMP
$state=Get-Content -LiteralPath (Join-Path $run 'source-state.json') -Raw|ConvertFrom-Json
$python='G:/Unity/VRMMO/BC2VR-Launcher/versions/0.1.23/runtime/python/python.exe'
& $python -B (Join-Path $repo 'tools/bc2_native_operation_receipts.py') verify-source --root $repo --reviewed-header $state.reviewed_header --receipt $state.source_receipt --expected $state.source_sha256
if($LASTEXITCODE){throw 'Source changed'}
$vs='C:/Program Files/Microsoft Visual Studio/18/Community'
& (Join-Path $vs 'Common7/Tools/Launch-VsDevShell.ps1') -VsInstallationPath $vs -Arch x86 -HostArch amd64 -SkipAutomaticLocation|Out-Null
$build=Join-Path $repo 'build/x86'
& cmake -S $repo -B $build -C (Join-Path $repo 'profiles/weapons43/Enrollment.cmake') "-DBC2_NATIVE_OPERATION_CAPABILITIES_HEADER:FILEPATH=$($state.reviewed_header.Replace('\','/'))" "-DBC2_NATIVE_OPERATION_SOURCE_RECEIPT:FILEPATH=$($state.source_receipt.Replace('\','/'))" "-DBC2_NATIVE_OPERATION_SOURCE_SHA256:STRING=$($state.source_sha256)" *> (Join-Path $out 'configure.log')
if($LASTEXITCODE){Get-Content (Join-Path $out 'configure.log') -Tail 20;throw 'Focused configure failed'}
$targets=@($suites|ForEach-Object {$_+'Tests'})
& cmake --build $build --parallel 4 --target @targets *> (Join-Path $out 'build.log')
if($LASTEXITCODE){Get-Content (Join-Path $out 'build.log') -Tail 35;throw 'Focused build failed'}
$pattern='^('+($suites -join '|')+')$'
& ctest --test-dir $build --output-on-failure --parallel 4 -R $pattern *> (Join-Path $out 'tests.log')
$cpu=$LASTEXITCODE
$pythonExit=0
if(-not $SkipPython){
 & $python -B (Join-Path $repo 'tests/test_bc2_shotgun_equipment.py') *> (Join-Path $out 'generator-tests.log')
 $pythonExit=$LASTEXITCODE
 Get-Content (Join-Path $out 'generator-tests.log') -Tail 6
}
Save 'result.json' @{passed=($cpu -eq 0 -and $pythonExit -eq 0);cpu_exit=$cpu;python_exit=$pythonExit;python_skipped=[bool]$SkipPython;suites=$suites;source_commit=$state.source_commit;source_sha256=$state.source_sha256;architecture='x86';game_started=$false;headset_accepted=$false}
Get-Content (Join-Path $out 'tests.log') -Tail 7
if($cpu -or $pythonExit){throw 'Focused check failed'}
