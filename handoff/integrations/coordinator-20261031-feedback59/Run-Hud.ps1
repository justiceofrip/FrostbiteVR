$ErrorActionPreference='Stop';$PSNativeCommandUseErrorActionPreference=$false
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../..'));$run=Join-Path $repo 'reports/coordinator-20261031-feedback59'
$lease=Get-Content -LiteralPath (Join-Path $run 'leases.json') -Raw|ConvertFrom-Json
if((Get-Content -LiteralPath $lease.INTEGRATION.path -Raw|ConvertFrom-Json).owner_token -ne $lease.INTEGRATION.owner_token){throw 'Lease differs'}
if(Get-Process BFBC2Game,BC2NativeTrace,BC2XrHost,cl,link,cmake,ninja,ctest,MSBuild -ErrorAction SilentlyContinue){throw 'Shared resources busy'}
if(Test-Path -LiteralPath (Join-Path $run 'hud-warp.json')){throw 'Preserve existing result'}
. (Join-Path $repo 'tools/StorageBudget.ps1')
$budget=Assert-FvrStorageBudget -RepositoryRoot $repo -OutputPath $run -Operation Build -PlannedBytes 16MB -AdditionalRoots @('G:/Unity/VRMMO/bc2vr-task-worktrees','G:/Unity/VRMMO/BC2VR-Launcher')
$env:TEMP=Join-Path $repo 'build/scratch';$env:TMP=$env:TEMP;$env:TMPDIR=$env:TEMP
$exe=Join-Path $repo 'build/x64/AmmoCounterHudWarpTests.exe'
$hash=(Get-FileHash -LiteralPath $exe).Hash.ToLowerInvariant()
# Real D3D WARP pixel readback with mocked XR calls; no game or runtime launch.
& $exe *> (Join-Path $run 'hud-warp.log');$code=$LASTEXITCODE
$result=@{passed=($code -eq 0);exit_code=$code;binary=$exe;sha256=$hash;storage=$budget;game_started=$false;headset_accepted=$false}
[IO.File]::WriteAllText((Join-Path $run 'hud-warp.json'),($result|ConvertTo-Json -Depth 20),[Text.UTF8Encoding]::new($false))
Get-Content -LiteralPath (Join-Path $run 'hud-warp.log') -Tail 8
if($code){throw 'HUD WARP check failed'}
