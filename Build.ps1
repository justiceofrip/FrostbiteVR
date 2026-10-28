[CmdletBinding()]
param([ValidateSet('x86','x64')][string]$Architecture='x86',[ValidateRange(1,16)][int]$Jobs=4,
      [string]$BuildDirectory='',
      [ValidateRange(1,[long]::MaxValue)][long]$MaxGeneratedBytes=40GB)
$ErrorActionPreference='Stop'
$PSNativeCommandUseErrorActionPreference=$false
$build=if($BuildDirectory){[IO.Path]::GetFullPath($BuildDirectory,$PSScriptRoot)}else{Join-Path $PSScriptRoot "build\$Architecture"}
. (Join-Path $PSScriptRoot 'tools/StorageBudget.ps1')
$storageAdmission=Assert-FvrStorageBudget -RepositoryRoot $PSScriptRoot -OutputPath $build -Operation Build -PlannedBytes 3GB -MaxGeneratedBytes $MaxGeneratedBytes
$symbols=Join-Path $build 'symbols'
$scratch=Join-Path $build 'scratch'
$previousTemp=$env:TEMP
$previousTmp=$env:TMP
try {
New-Item -ItemType Directory -Path $scratch -Force | Out-Null
$env:TEMP=$scratch
$env:TMP=$scratch
$vswhere=Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$vsInstall=& $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if(-not $vsInstall){throw 'Visual Studio C++ tools are required.'}
# VS 2022's dev shell accepts amd64 but not x64 for the target architecture.
$vsArch=if($Architecture -eq 'x64'){'amd64'}else{$Architecture}
& (Join-Path $vsInstall 'Common7\Tools\Launch-VsDevShell.ps1') -VsInstallationPath $vsInstall -Arch $vsArch -HostArch amd64 -SkipAutomaticLocation | Out-Null
$cmake=(Get-Command cmake -ErrorAction Stop).Source
$ctest=Join-Path (Split-Path -Parent $cmake) 'ctest.exe'
$logs=Join-Path $PSScriptRoot 'reports'
New-Item -ItemType Directory -Path $logs -Force | Out-Null
function Step([string]$Name,[string]$Exe,[string[]]$Arguments){
    $log=Join-Path $logs "$Architecture-$Name.log"
    & $Exe @Arguments *> $log
    if($LASTEXITCODE -ne 0){Get-Content -LiteralPath $log -Tail 80;throw "$Name failed ($LASTEXITCODE)"}
    Get-Content -LiteralPath $log -Tail 4
}
Step 'configure' $cmake @('-S',$PSScriptRoot,'-B',$build,'-G','Ninja','-DCMAKE_BUILD_TYPE=RelWithDebInfo','-DBUILD_TESTING=ON',"-DCMAKE_PDB_OUTPUT_DIRECTORY=$symbols","-DCMAKE_PDB_OUTPUT_DIRECTORY_RELWITHDEBINFO=$symbols")
Step 'build' $cmake @('--build',$build,'--parallel',"$Jobs")
Step 'tests' $ctest @('--test-dir',$build,'--output-on-failure','--parallel',"$Jobs")
[ordered]@{architecture=$Architecture;build_directory=$build;completed_utc=[DateTime]::UtcNow.ToString('o');built=$true;tests_passed=$true;native_bc2_verified=$false;headset_tested=$false;storage=$storageAdmission} | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath (Join-Path $logs "$Architecture-build.json")
} finally {
    $env:TEMP=$previousTemp
    $env:TMP=$previousTmp
}
