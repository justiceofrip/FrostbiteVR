#requires -Version 7.5
Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'
$path=Join-Path $PSScriptRoot '../tools/Invoke-Bc2XrGameSimulation.ps1'
$tokens=$null;$errors=$null
$ast=[Management.Automation.Language.Parser]::ParseFile($path,[ref]$tokens,[ref]$errors)
if($errors.Count){throw ($errors|Out-String)}
foreach($name in @('Require','AssertObservationLimits')){
    $fn=$ast.Find({param($n) $n -is [Management.Automation.Language.FunctionDefinitionAst] -and $n.Name -ceq $name}.GetNewClosure(),$true)
    if(-not $fn){throw "Missing $name"}
    . ([scriptblock]::Create($fn.Extent.Text))
}
$cases=@(
    @(300,420,$false,$true), @(15,90,$false,$true), @(150,240,$false,$true),
    @(420,540,$false,$false), @(300,540,$false,$false),
    @(420,540,$true,$true), @(420,480,$true,$true),
    @(420,479,$true,$false), @(421,540,$true,$false),
    @(420,541,$true,$false), @(14,90,$true,$false), @(15,89,$true,$false)
)
foreach($case in $cases){
    $passed=$true
    try{AssertObservationLimits $case[0] $case[1] $case[2]}catch{$passed=$false}
    if($passed -ne $case[3]){throw "Wrong duration admission: $($case -join ',')"}
}
Write-Output "XR session duration admission passed: $($cases.Count) cases; no session or process started."
