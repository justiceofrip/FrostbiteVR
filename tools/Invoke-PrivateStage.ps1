#requires -Version 7.5
[CmdletBinding(DefaultParameterSetName='Plan')]
param(
    [Parameter(Mandatory,ParameterSetName='Plan')][switch]$Plan,
    [Parameter(Mandatory,ParameterSetName='Plan')][string]$BatchConfig,
    [Parameter(Mandatory,ParameterSetName='Plan')][string]$BatchConfigSha256,
    [Parameter(Mandatory,ParameterSetName='Plan')][string]$InstallRoot,
    [Parameter(Mandatory,ParameterSetName='Plan')][string]$LauncherCommit,
    [Parameter(Mandatory,ParameterSetName='Plan')][string]$ReviewedLauncherPins,
    [Parameter(Mandatory,ParameterSetName='Plan')][string]$ReviewedLauncherPinsSha256,
    [Parameter(ParameterSetName='Plan')][long]$MaxGeneratedBytes=52GB,
    [Parameter(Mandatory,ParameterSetName='Stage')][string]$Decision,
    [Parameter(Mandatory,ParameterSetName='Stage')][string]$DecisionSha256,
    [Parameter(Mandatory,ParameterSetName='Stage')][switch]$RootAccept
)
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
. (Join-Path $PSScriptRoot 'PrivateStage.Core.ps1')
if($Plan){
    $configPin=Get-StagePin $BatchConfig
    Assert-Stage ($BatchConfigSha256 -cmatch '^[a-f0-9]{64}$' -and $configPin.sha256 -ceq $BatchConfigSha256) 'explicit batch config hash differs'
    $reviewPin=Get-StagePin $ReviewedLauncherPins
    Assert-Stage ($ReviewedLauncherPinsSha256 -cmatch '^[a-f0-9]{64}$' -and $reviewPin.sha256 -ceq $ReviewedLauncherPinsSha256) 'explicit reviewed launcher pinset hash differs'
}else{
    $decisionPin=Get-StagePin $Decision
    Assert-Stage ($DecisionSha256 -cmatch '^[a-f0-9]{64}$' -and $decisionPin.sha256 -ceq $DecisionSha256) 'explicit decision hash differs'
    $d=Read-StageJson $Decision;$configPin=$d.native_batch_config;Assert-StagePin $configPin
    $reviewPin=$d.reviewed_launcher_pinset;Assert-StagePin $reviewPin
}
$review=Read-StageJson $reviewPin.path
$config=Read-StageJson $configPin.path
# Load only helpers pinned by the accepted native batch and the accepted753 guard bytes.
foreach($relative in 'launcher/Launcher.Core.ps1','tools/StorageBudget.ps1'){
    $path=[IO.Path]::GetFullPath((Join-Path $config.repository $relative))
    $pins=@($config.tool_pins|Where-Object {$_.path -ieq $path})
    Assert-Stage ($pins.Count -eq 1) ('missing accepted helper pin: '+$relative)
    Assert-StagePin $pins[0]
    . $path
}
$guardPath=Get-FvrChild $config.repository 'launcher/runtime/PrivateSession.ps1'
$guardPin=Get-StagePin $guardPath
$reviewedGuard=@($review.files|Where-Object path -CEQ 'launcher/runtime/PrivateSession.ps1')[0]
Assert-Stage ($guardPin.sha256 -ceq $reviewedGuard.sha256 -and $guardPin.bytes -eq $reviewedGuard.bytes) 'accepted753 guard has not been integrated exactly'
. $guardPath
if($Plan){
    New-PrivateStageDecision $configPin $InstallRoot $LauncherCommit $review $reviewPin -MaxGeneratedBytes $MaxGeneratedBytes|ConvertTo-Json -Depth 60
}else{
    Invoke-PrivateStage $decisionPin $review -RootAccept:$RootAccept|ConvertTo-Json -Depth 60
}
