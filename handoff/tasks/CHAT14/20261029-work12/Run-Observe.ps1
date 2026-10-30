param(
    [Parameter(Mandatory)][ValidateRange(1,2147483647)][int]$ProcessId,
    [Parameter(Mandatory)][string]$Executable,
    [Parameter(Mandatory)][string]$OutputPath
)
$ErrorActionPreference='Stop'
$repository=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../../..'))
$output=[IO.Path]::GetFullPath($OutputPath)
if(Test-Path -LiteralPath $output){throw 'Preserve prior capture; choose a fresh output path.'}
. (Join-Path $repository 'tools/StorageBudget.ps1')
$admission=Assert-FvrStorageBudget -RepositoryRoot $repository -OutputPath $output -Operation Capture -PlannedBytes 8MB -RawCaptureBytes 0
py -3.12 -B (Join-Path $PSScriptRoot 'observe.py') --capture --pid $ProcessId --executable $Executable --output $output
exit $LASTEXITCODE
