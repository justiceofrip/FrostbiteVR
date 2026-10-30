$ErrorActionPreference='Stop';$PSNativeCommandUseErrorActionPreference=$false
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../..'));$run=Join-Path $repo 'reports/coordinator-20261030-integration55'
. (Join-Path $repo 'tools/StorageBudget.ps1')
$lease=Get-Content (Join-Path $run 'leases.json') -Raw|ConvertFrom-Json
foreach($arch in @('x86','x64')){
 if((Get-Content $lease.INTEGRATION.path -Raw|ConvertFrom-Json).owner_token -ne $lease.INTEGRATION.owner_token){throw 'Lease changed'}
 if(Get-Process cl,link,ninja,cmake,ctest,BFBC2Game,BC2XrHost -ErrorAction SilentlyContinue){throw 'Resources active'}
 $budget=Assert-FvrStorageBudget -RepositoryRoot $repo -OutputPath $run -Operation Build -PlannedBytes 256MB -AdditionalRoots @('G:/Unity/VRMMO/bc2vr-task-worktrees','G:/Unity/VRMMO/BC2VR-Launcher')
 $output=Join-Path $run ($arch+'-fixtures-storage.json');if(Test-Path $output){throw 'Preserve earlier attempt'}
 [IO.File]::WriteAllText($output,($budget|ConvertTo-Json -Depth 20),[Text.UTF8Encoding]::new($false))
 $env:TEMP=Join-Path $repo ('build/'+$arch+'/scratch');$env:TMP=$env:TEMP;$env:TMPDIR=$env:TEMP
 $vs='C:/Program Files/Microsoft Visual Studio/18/Community';$target=if($arch -eq 'x64'){'amd64'}else{'x86'}
 & (Join-Path $vs 'Common7/Tools/Launch-VsDevShell.ps1') -VsInstallationPath $vs -Arch $target -HostArch amd64 -SkipAutomaticLocation|Out-Null
 & cmake --build (Join-Path $repo ('build/'+$arch)) --target Bc2PhysicalFeedTests StagedFrameProducerTests --parallel 4 *> (Join-Path $run ($arch+'-fixtures-build.log'))
 if($LASTEXITCODE){Get-Content (Join-Path $run ($arch+'-fixtures-build.log')) -Tail 30;throw 'Fixture build failed'}
 & ctest --test-dir (Join-Path $repo ('build/'+$arch)) -R '^(Bc2PhysicalFeed|StagedFrameProducer)$' --output-on-failure --parallel 2 *> (Join-Path $run ($arch+'-fixtures-tests.log'))
 Get-Content (Join-Path $run ($arch+'-fixtures-tests.log')) -Tail 10
 if($LASTEXITCODE){throw 'Fixture tests failed'}
}
