param(
    [Parameter(Mandatory)][int]$GameProcessId,
    [Parameter(Mandatory)][string]$Python,
    [Parameter(Mandatory)][string]$OutputPrefix,
    [ValidateRange(0.1,15)][double]$Seconds=12,
    [switch]$InspectOnly
)
$ErrorActionPreference='Stop'
$repo=Split-Path -Parent $PSScriptRoot
. (Join-Path $PSScriptRoot 'StorageBudget.ps1')
$null=Set-FvrProjectTemporaryStorage -RepositoryRoot $repo
$output=[IO.Path]::GetFullPath($OutputPrefix)
if([IO.Path]::GetPathRoot($output) -ne 'G:\'){throw 'Audio output must be on G:'}
$argsList=@('-B',(Join-Path $PSScriptRoot 'bc2_audio_loopback.py'),'--pid',"$GameProcessId",'--seconds',"$Seconds",'--output',$output)
if($InspectOnly){$argsList+='--inspect'}
else {
    $directory=Split-Path -Parent $output
    $admission=Assert-FvrStorageBudget -RepositoryRoot $repo -OutputPath $directory -Operation Capture -PlannedBytes 3MB -RawCaptureBytes 2880044
    $null=[IO.Directory]::CreateDirectory($directory)
    $receipt=$output+'.storage.json'
    if(Test-Path -LiteralPath $receipt){throw 'Storage receipt already exists'}
    [IO.File]::WriteAllText($receipt,($admission|ConvertTo-Json -Depth 12),[Text.UTF8Encoding]::new($false))
    $argsList+=@('--admission',$receipt)
}
& $Python @argsList
if($LASTEXITCODE -ne 0){throw "Audio measurement failed: $LASTEXITCODE"}
