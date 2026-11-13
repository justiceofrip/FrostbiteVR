#requires -Version 7.5
Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'
$path=Join-Path $PSScriptRoot '../tools/Invoke-Bc2XrGameSimulation.ps1'
$tokens=$null;$errors=$null
$ast=[Management.Automation.Language.Parser]::ParseFile($path,[ref]$tokens,[ref]$errors)
if($errors.Count){throw ($errors|Out-String)}
foreach($name in @('Require','GetObservationProfile','AssertObservationLimits','NewCapturePlan','AssertTapeFooter')){
    $fn=$ast.Find({param($n) $n -is [Management.Automation.Language.FunctionDefinitionAst] -and $n.Name -ceq $name}.GetNewClosure(),$true)
    if(-not $fn){throw "Missing $name"}
    . ([scriptblock]::Create($fn.Extent.Text))
}
$cases=@(
    @(300,420,$false,$false,$true), @(15,90,$false,$false,$true), @(150,240,$false,$false,$true),
    @(420,540,$false,$false,$false), @(300,540,$false,$false,$false),
    @(420,540,$true,$false,$true), @(420,480,$true,$false,$true),
    @(420,479,$true,$false,$false), @(421,540,$true,$false,$false),
    @(420,541,$true,$false,$false), @(14,90,$true,$false,$false), @(15,89,$true,$false,$false),
    @(1200,1320,$false,$true,$true), @(1200,1319,$false,$true,$false),
    @(1201,1320,$false,$true,$false), @(1200,1321,$false,$true,$false),
    @(300,420,$false,$true,$true), @(300,419,$false,$true,$false),
    @(15,135,$false,$true,$true), @(15,134,$false,$true,$false),
    @(1200,1320,$true,$false,$false), @(1200,1320,$false,$false,$false),
    @(300,420,$true,$true,$false), @(14,135,$false,$true,$false)
)
foreach($case in $cases){
    $passed=$true
    try{AssertObservationLimits $case[0] $case[1] $case[2] $case[3]}catch{$passed=$false}
    if($passed -ne $case[4]){throw "Wrong duration admission: $($case -join ',')"}
}
foreach($row in @(@('Seconds',300),@('TimeoutSeconds',420),@('CaptureCount',4))){
    $parameter=@($ast.ParamBlock.Parameters|Where-Object {$_.Name.VariablePath.UserPath -ceq $row[0]})
    if($parameter.Count -ne 1 -or $parameter[0].DefaultValue.SafeGetValue() -ne $row[1]){throw "Default changed: $($row[0])"}
}
$campaignParameter=@($ast.ParamBlock.Parameters|Where-Object {$_.Name.VariablePath.UserPath -ceq 'CampaignObservation'})
if($campaignParameter.Count -ne 1 -or $campaignParameter[0].StaticType -ne [switch] -or $null -ne $campaignParameter[0].DefaultValue){throw 'Campaign opt-in must default off.'}
function Assignment([string]$Name){
    $rows=@($ast.FindAll({param($n) $n -is [Management.Automation.Language.AssignmentStatementAst] -and $n.Left.Extent.Text -ceq $Name}.GetNewClosure(),$true))
    if($rows.Count -ne 1){throw "Expected one production assignment: $Name"}
    return [scriptblock]::Create($rows[0].Extent.Text)
}
$profileInit=Assignment '$observationProfile';$reserveInit=Assignment '$cleanupReserveSeconds'
$tapeInit=Assignment '$tapeMaxFrames';$environmentInit=Assignment '$environment'
$footerCall=$ast.Find({param($n) $n -is [Management.Automation.Language.CommandAst] -and $n.GetCommandName() -ceq 'AssertTapeFooter'},$true)
if(-not $footerCall){throw 'Missing production footer admission.'}
$footerCall=[scriptblock]::Create($footerCall.Extent.Text)
$stopIf=$ast.Find({param($n) $n -is [Management.Automation.Language.IfStatementAst] -and $n.Extent.Text.StartsWith('if($clock.Elapsed.TotalSeconds -ge ') -and $n.Extent.Text.Contains("RequestStop 'Outer deadline cleanup reserve reached.'")},$true)
if(-not $stopIf){throw 'Missing production cleanup trigger.'}
$stopIf=[scriptblock]::Create($stopIf.Extent.Text)
function RequestStop([string]$Reason){$script:stopRequested=$Reason}
$runtimeManifest='fixture-runtime';$tapeManifestDirectory='fixture-tape-layer';$local=@{TapeLayerName='fixture-layer'}
$simDir='fixture-sim';$profilePath='fixture-profile';$tapeDir='fixture-tape'
foreach($row in @(@($false,$false,'standard',300,420,60,12,30000),@($true,$false,'extended',420,540,60,12,30000),@($false,$true,'campaign',1200,1320,120,30,90000))){
    $ExtendedObservation=$row[0];$CampaignObservation=$row[1]
    . $profileInit;. $reserveInit;. $tapeInit;. $environmentInit
    $actual=@($observationProfile.mode,$observationProfile.maximum_host_seconds,$observationProfile.maximum_outer_seconds,$cleanupReserveSeconds,$observationProfile.maximum_compositions,$tapeMaxFrames)
    if(($actual -join '|') -cne ($row[2..7] -join '|')){throw "Wrong production observation profile: $($row[2])"}
    if($environment.XRTAPE_MAX_FRAMES -isnot [string] -or $environment.XRTAPE_MAX_FRAMES -cne [string]$row[7]){throw 'Recorder environment cap differs from profile.'}
    $TimeoutSeconds=$row[4]
    foreach($delta in @(-0.01,0)){
        $clock=@{Elapsed=@{TotalSeconds=$TimeoutSeconds-$cleanupReserveSeconds+$delta}};$script:stopRequested=$null
        . $stopIf
        if(($null -ne $script:stopRequested) -ne ($delta -eq 0)){throw 'Cleanup did not start at the exact profile reserve.'}
    }
    foreach($frames in @(0,($row[7]-1),$row[7],($row[7]+1),-1)){
        $footer=@{r='footer';truncated=$false;recordedFrames=$frames;maxFrames=$tapeMaxFrames};$passed=$true
        try{. $footerCall}catch{$passed=$false}
        if($passed -ne ($frames -ge 0 -and $frames -lt $row[7])){throw "Wrong tape cap admission: $($row[2]), $frames"}
    }
    foreach($footer in @(@{r='footer';truncated=$true;recordedFrames=1;maxFrames=$tapeMaxFrames},@{r='frame';truncated=$false;recordedFrames=1;maxFrames=$tapeMaxFrames},@{r='footer';truncated=$false;recordedFrames='1';maxFrames=$tapeMaxFrames},@{r='footer';truncated=$false;maxFrames=$tapeMaxFrames},@{r='footer';truncated=$false;recordedFrames=1;maxFrames=($tapeMaxFrames+1)},@{r='footer';truncated=$false;recordedFrames=1})){
        $passed=$true;try{. $footerCall}catch{$passed=$false}
        if($passed){throw 'Incomplete or invalid recorder footer admitted.'}
    }
}
Write-Output "XR observation admission passed: $($cases.Count) duration cases, three production profiles, recorder caps and cleanup triggers; no session or process started."
