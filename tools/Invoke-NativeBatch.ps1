[CmdletBinding()]
param(
    [Parameter(Mandatory)][ValidateSet('Check','NormalX86','NormalX64','Freeze','PrivateX86')][string]$Stage,
    [Parameter(Mandatory)][string]$Config,
    [Parameter(Mandatory)][ValidatePattern('^[0-9a-f]{64}$')][string]$ConfigSha256,
    [Parameter(Mandatory)][string]$Python
)
$ErrorActionPreference='Stop'
$PSNativeCommandUseErrorActionPreference=$false
$checker=Join-Path $PSScriptRoot 'native_batch.py'
$binding=@('--config',$Config,'--config-sha256',$ConfigSha256)
& $Python -B $checker check @binding
if($LASTEXITCODE){throw 'Configuration/source/owner review refused'}
if($Stage -eq 'Check'){return}
$c=Get-Content -LiteralPath $Config -Raw | ConvertFrom-Json
$repo=$c.repository
$run=$c.run
$private=$Stage -eq 'PrivateX86'
$arch=if($Stage -eq 'NormalX64'){'x64'}else{'x86'}
$leaf=if($private){'private-x86'}elseif($Stage -eq 'Freeze'){'normal-runtime'}else{'normal-'+$arch}
$out=Join-Path $run $leaf
if(Test-Path -LiteralPath $out){throw 'Preserve attempted stage; review a fresh run for a failed attempt'}
. (Join-Path $repo 'launcher/Launcher.Core.ps1')
Assert-FvrIdle
if(Get-Process cl,link,cmake,ninja,ctest,MSBuild -ErrorAction SilentlyContinue){throw 'Shared build resources busy'}
. (Join-Path $repo 'tools/StorageBudget.ps1')
$ceiling=[long]$c.budget.build_ceiling_bytes
$planned=[long]$c.budget.build_reservation_bytes
if($Stage -eq 'Freeze'){
    $size=& $Python -B $checker freeze-size @binding
    if($LASTEXITCODE){throw 'Freeze sizing refused'}
    $planned=[long]$size
    $ceiling=[long]$c.budget.freeze_ceiling_bytes
}
if($private){
    & $Python -B $checker check-frozen @binding
    if($LASTEXITCODE){throw 'Both normal builds must be frozen and still pinned before private reconfiguration'}
}
$admission=Assert-FvrStorageBudget -RepositoryRoot $repo -OutputPath $out -Operation Build -PlannedBytes $planned -MaxGeneratedBytes $ceiling -MinFreeBytes 15GB -AdditionalRoots @($c.budget.roots)
if(@($admission.generated.roots).Count -ne 13){throw 'Complete thirteen-root admission required'}
New-Item -ItemType Directory -Path $out | Out-Null
function Save-Json([string]$Name,$Value){
    $target=Join-Path $out $Name
    if(Test-Path -LiteralPath $target){throw 'Preserve existing evidence'}
    [IO.File]::WriteAllText($target,($Value | ConvertTo-Json -Depth 20),[Text.UTF8Encoding]::new($false))
}
Save-Json 'storage.json' $admission
if($Stage -eq 'Freeze'){
    & $Python -B $checker freeze @binding --directory $out
    if($LASTEXITCODE){throw 'Frozen normal artifacts refused'}
    return
}
$PSDefaultParameterValues=@{}+$PSDefaultParameterValues
$PSDefaultParameterValues['Assert-FvrStorageBudget:MaxGeneratedBytes']=$ceiling
$PSDefaultParameterValues['Assert-FvrStorageBudget:MinFreeBytes']=15GB
$PSDefaultParameterValues['Assert-FvrStorageBudget:AdditionalRoots']=@($c.budget.roots)
$env:TEMP=Join-Path $repo 'build/scratch'
$env:TMP=$env:TEMP
$env:TMPDIR=$env:TEMP
$vswhere=Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$vs=& $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if(-not $vs){throw 'Visual Studio C++ tools missing'}
$vsArch=if($arch -eq 'x64'){'amd64'}else{'x86'}
& (Join-Path $vs 'Common7/Tools/Launch-VsDevShell.ps1') -VsInstallationPath $vs -Arch $vsArch -HostArch amd64 -SkipAutomaticLocation | Out-Null
$build=Join-Path $repo ('build/'+$arch)
function Step([string]$Name,[scriptblock]$Action){
    $log=Join-Path $out ($Name+'.log')
    & $Action *> $log
    if($LASTEXITCODE){Get-Content -LiteralPath $log -Tail 30;throw ($Name+' failed; preserve this attempt')}
}
$flagArgs=@('flags')+$binding+@('--architecture',$arch)
if($private){$flagArgs+='--private'}
$flagText=& $Python -B $checker @flagArgs
if($LASTEXITCODE){throw 'Reviewed flags refused'}
$flags=@($flagText | ConvertFrom-Json)
try {
    Step 'configure-binding' {& cmake -S $repo -B $build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DBUILD_TESTING=ON "-DCMAKE_PDB_OUTPUT_DIRECTORY=$build/symbols" "-DCMAKE_PDB_OUTPUT_DIRECTORY_RELWITHDEBINFO=$build/symbols" @flags}
    if($private){
        # Full normal CTest has already run exactly once per architecture.
        Step 'private-build' {& cmake --build $build --parallel $c.jobs --target @($c.private_targets)}
        Copy-Item -LiteralPath (Join-Path $build 'CMakeCache.txt') -Destination (Join-Path $out 'CMakeCache.txt')
        & ctest --test-dir $build --show-only=json-v1 *> (Join-Path $out 'test-list.json')
        if($LASTEXITCODE){throw 'Private CTest registration inventory failed'}
        & $Python -B $checker verify-private @binding --directory $out
        if($LASTEXITCODE){throw 'Private artifact/source/symbol verification refused'}
    }else{
        & (Join-Path $repo 'Build.ps1') -Architecture $arch -Jobs $c.jobs -MaxGeneratedBytes $ceiling *> (Join-Path $out 'build-console.log')
        if(-not $?){throw 'Normal Build.ps1 failed'}
        foreach($name in @('build.json','configure.log','build.log','tests.log')){
            Copy-Item -LiteralPath (Join-Path $repo ('reports/'+$arch+'-'+$name)) -Destination (Join-Path $out $name)
        }
        Copy-Item -LiteralPath (Join-Path $build 'Testing/Temporary/LastTest.log') -Destination (Join-Path $out 'LastTest.log')
        Copy-Item -LiteralPath (Join-Path $build 'CMakeCache.txt') -Destination (Join-Path $out 'CMakeCache.txt')
        Copy-Item -LiteralPath (Join-Path $build 'generated/bc2-operation/Bc2NativeOperationBuild.generated.h') -Destination (Join-Path $out 'Bc2NativeOperationBuild.generated.h')
        & ctest --test-dir $build --show-only=json-v1 *> (Join-Path $out 'test-list.json')
        if($LASTEXITCODE){throw 'CTest inventory failed'}
        Step 'chest-mesh' {& (Join-Path $build 'Bc2BodyDisplayBatchTests.exe') $c.chest_mesh.path}
        Step 'chest-attack' {& (Join-Path $build 'KnifeMotionBoundaryTests.exe') --chest-attack-transaction; if($LASTEXITCODE -eq 0){Write-Output 'chest-attack-transaction exited 0'}}
        & $Python -B $checker verify-normal @binding --architecture $arch --directory $out
        if($LASTEXITCODE){throw 'Normal schedule/source/feature verification refused'}
    }
}catch{
    if(-not $private){
        foreach($kind in @('configure','build','tests')){
            $failed=Join-Path $repo ('reports/'+$arch+'-'+$kind+'.log')
            if(Test-Path -LiteralPath $failed){Copy-Item -LiteralPath $failed -Destination (Join-Path $out ($kind+'-at-failure.log'))}
        }
        $failed=Join-Path $build 'Testing/Temporary/LastTest.log'
        if(Test-Path -LiteralPath $failed){Copy-Item -LiteralPath $failed -Destination (Join-Path $out 'LastTest-at-failure.log')}
    }
    throw
}finally{
    if($private){
        # This resets configuration only. Shared x86 outputs remain PRIVATE;
        # normal consumers must use frozen normal-runtime artifacts by receipt.
        & cmake -S $repo -B $build -DFVR_BC2_CAMPAIGN_PROVISIONING:BOOL=OFF *> (Join-Path $out 'restore-default-config.log')
        if($LASTEXITCODE){throw 'Private build ended; resetting campaign option OFF failed'}
        Save-Json 'shared-build-state.json' @{campaign_cache='OFF';binary_kind='private';normal_artifacts=(Join-Path $run 'normal-runtime/receipt.json');normal_rebuild_performed=$false}
    }
}
