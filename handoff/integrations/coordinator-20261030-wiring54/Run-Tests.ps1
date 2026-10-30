[CmdletBinding()]
param(
 [Parameter(Mandatory)][string]$ComponentReport,
 [Parameter(Mandatory)][string]$OutputPath,
 [string[]]$Suites=@(),
 [ValidateSet('x86','x64')][string]$Architecture='x86',
 [string[]]$Recompile=@()
)
# Defaults to the failed suites in ComponentReport/results.json. Explicit Suites
# selects affected tests after a source fix. Recompile accepts candidate component
# basenames, including D3D11SceneDepthPair and RigPose, and writes only fresh objects.
# Header/profile changes require a full Run-Checks pass. No syntax receipt is reused
# as a new claim. Old reports, archives and canonical build trees remain untouched.
$ErrorActionPreference='Stop'
& (Join-Path $PSScriptRoot 'Run-Checks.ps1') -Architecture $Architecture -OutputPath $OutputPath `
 -ComponentReport $ComponentReport -OnlySuites $Suites -Recompile $Recompile
