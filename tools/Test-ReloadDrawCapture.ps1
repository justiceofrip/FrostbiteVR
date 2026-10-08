param([ValidateSet('x86','x64')][string]$Architecture='x64')
$ErrorActionPreference='Stop'
$vswhere=Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$vsInstall=& $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
& (Join-Path $vsInstall 'Common7\Tools\Launch-VsDevShell.ps1') -VsInstallationPath $vsInstall -Arch $Architecture -HostArch amd64 -SkipAutomaticLocation | Out-Null
$root=Split-Path -Parent $PSScriptRoot
$outDir=Join-Path $root "build/reload-draw-$Architecture"
New-Item -ItemType Directory -Path $outDir -Force | Out-Null
Push-Location -LiteralPath $outDir
try {
 & cl.exe /nologo /std:c++20 /EHsc /MT /W4 /DWIN32_LEAN_AND_MEAN /DNOMINMAX /I"$root/src/games/bc2" /I"$root/tests" "$root/tests/Bc2ReloadDrawGeometryTests.cpp" /Fe:Bc2ReloadDrawGeometryTests.exe
 if($LASTEXITCODE){throw 'geometry compile failed'}
 & .\Bc2ReloadDrawGeometryTests.exe
 if($LASTEXITCODE){throw 'geometry tests failed'}
 & cl.exe /nologo /std:c++20 /EHsc /MT /W4 /DWIN32_LEAN_AND_MEAN /DNOMINMAX /I"$root/src/games/bc2" /I"$root/tests" "$root/tests/Bc2ReloadDrawCaptureGpuTests.cpp" "$root/src/games/bc2/Bc2ReloadDrawCapture.cpp" /Fe:Bc2ReloadDrawCaptureGpuTests.exe /link d3d11.lib
 if($LASTEXITCODE){throw 'GPU capture compile failed'}
 & .\Bc2ReloadDrawCaptureGpuTests.exe
 if($LASTEXITCODE){throw 'GPU capture tests failed'}
}finally {Pop-Location}
