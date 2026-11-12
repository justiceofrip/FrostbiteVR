#requires -Version 7.5
param(
    [string]$RunnerPath=(Join-Path $PSScriptRoot '../tools/Invoke-Bc2XrGameSimulation.ps1')
)
Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'
$script:checks=0
function Check([bool]$Good,[string]$Name){if(-not $Good){throw "FAILED: $Name"};$script:checks++}
function Parse([string]$Path){
    $tokens=$null;$errors=$null
    $value=[Management.Automation.Language.Parser]::ParseFile($Path,[ref]$tokens,[ref]$errors)
    Check ($errors.Count -eq 0) "parse $Path"
    return $value
}
function Assignment($Ast,[string]$Name){
    $rows=@($Ast.FindAll({param($n) $n -is [Management.Automation.Language.AssignmentStatementAst] -and $n.Left.Extent.Text -ceq $Name}.GetNewClosure(),$true))
    Check ($rows.Count -eq 1) "one assignment $Name"
    return $rows[0]
}
function SaveCommand($Ast,[string]$Name){
    $rows=@($Ast.FindAll({param($n) $n -is [Management.Automation.Language.CommandAst] -and $n.GetCommandName() -ceq 'Save' -and $n.CommandElements[1].Value -ceq $Name}.GetNewClosure(),$true))
    Check ($rows.Count -eq 1) "one Save $Name"
    return [scriptblock]::Create($rows[0].Extent.Text)
}
function SnapshotEnvironment {
    $result=[Collections.Generic.Dictionary[string,string]]::new([StringComparer]::Ordinal)
    foreach($item in [Environment]::GetEnvironmentVariables('Process').GetEnumerator()){$result.Add($item.Key,$item.Value)}
    return ,$result
}
function AssertEnvironmentUnchanged($Before,[string]$Name){
    $after=SnapshotEnvironment
    Check ($after.Count -eq $Before.Count) "$Name process environment count"
    foreach($key in $Before.Keys){Check ($after.ContainsKey($key) -and $after[$key] -ceq $Before[$key]) "$Name process value $key"}
}
$beforeAll=SnapshotEnvironment
$ast=Parse $RunnerPath
$source=[IO.File]::ReadAllText($RunnerPath)
$parameter=@($ast.ParamBlock.Parameters|Where-Object {$_.Name.VariablePath.UserPath -ceq 'ProfileAttribution'})
Check ($parameter.Count -eq 1 -and $parameter[0].StaticType -eq [switch] -and $null -eq $parameter[0].DefaultValue) 'default-off explicit switch'
$pipelineParameter=@($ast.ParamBlock.Parameters|Where-Object {$_.Name.VariablePath.UserPath -ceq 'PrivateSourcePipeline'})
Check ($pipelineParameter.Count -eq 1 -and $pipelineParameter[0].StaticType -eq [switch] -and $null -eq $pipelineParameter[0].DefaultValue) 'pipeline is an independent default-off switch'
$require=$ast.Find({param($n) $n -is [Management.Automation.Language.FunctionDefinitionAst] -and $n.Name -ceq 'Require'},$true)
. ([scriptblock]::Create($require.Extent.Text))
$presentationInit=[scriptblock]::Create((Assignment $ast '$privatePresentation').Extent.Text)
$pipelineInit=[scriptblock]::Create((Assignment $ast '$sourcePipeline').Extent.Text)
$profileInit=[scriptblock]::Create((Assignment $ast '$attributionProfile').Extent.Text)
$profileIf=@($ast.EndBlock.Statements|Where-Object {$_ -is [Management.Automation.Language.IfStatementAst] -and $_.Extent.Text.StartsWith('if($ProfileAttribution)')})
Check ($profileIf.Count -eq 1) 'one attribution metadata initializer'
$profileOptIn=[scriptblock]::Create($profileIf[0].Extent.Text)
$receiptInit=[scriptblock]::Create((Assignment $ast '$receipt').Extent.Text)
$scrub=@($ast.FindAll({param($n) $n -is [Management.Automation.Language.ForEachStatementAst] -and $n.Extent.Text.Contains('$psi.Environment.Remove($key)')},$true))
Check ($scrub.Count -eq 1) 'one production scrub'
$end=Assignment $ast '$attributionProfile.unapproved_fvr_bc2_environment_absent'
$isolate=[scriptblock]::Create($source.Substring($scrub[0].Extent.StartOffset,$end.Extent.EndOffset-$scrub[0].Extent.StartOffset))
$launchSave=SaveCommand $ast 'launch.json'
# There are two run-manifest saves; inspect and execute only its initial literal manifest.
$manifestSave=$ast.Find({param($n) $n -is [Management.Automation.Language.CommandAst] -and $n.GetCommandName() -ceq 'Save' -and $n.Extent.Text.StartsWith("Save 'run-manifest.json' @{")},$true)
Check ($null -ne $manifestSave) 'initial manifest exists'
$manifestSave=[scriptblock]::Create($manifestSave.Extent.Text)
function Save([string]$Name,$Value){$script:saved[$Name]=$Value|ConvertTo-Json -Depth 12|ConvertFrom-Json -AsHashtable}
function Run([string]$Name,[bool]$OptIn,[bool]$Ordered,[scriptblock]$Change={},[bool]$Allowed=$true,[bool]$Pipeline=$false){
    $before=SnapshotEnvironment
    $ProfileAttribution=[switch]$OptIn;$PrivateGpuOrderedPresent=[switch]$Ordered
    $PrivateSourcePipeline=[switch]$Pipeline
    . $presentationInit
    . $pipelineInit
    . $profileInit
    . $profileOptIn
    Check (-not $attributionProfile.child_environment_verified -and -not $attributionProfile.native_enablement_claimed) "$Name initially unverified"
    $mechanismMarker=@{requested=$false};$Seconds=420;$TimeoutSeconds=540
    $MaxGeneratedBytes=40GB;$GeneratedBudgetReason='fixture only'
    $plannedBytes=1;$rawBytes=1;$rawLimitBytes=1;$CaptureCount=5;$CaptureWidth=640;$CaptureHeight=480
    $capturePlan=@{fixture=$true};$failures=[Collections.Generic.List[string]]::new()
    . $receiptInit
    $OutputDir='G:/fixture/output';$Version='0.1.67';$ExpectedSourceSha256='fixture'
    $script:saved=@{}
    . $manifestSave
    $early=$script:saved['run-manifest.json'].profile_attribution
    Check ($early.requested -eq $OptIn -and -not $early.child_environment_verified) "$Name prelaunch manifest truth"
    $psi=[Diagnostics.ProcessStartInfo]::new()
    # Fixtures affect only this inert child map. No process starts and no parent environment changes.
    foreach($key in @('FVR_BC2_PRIVATE_CPU_ATTRIBUTION','FVR_PRIVATE_GPU_TIMING436','FVR_PRIVATE_GPU_ORDERED_PRESENT','FVR_PRIVATE_GPU_PHASE_C_DELAY_MS','FVR_PRIVATE_TWO_SOURCE_SLOTS','FVR_PRIVATE_THREE_SOURCE_SLOTS','FVR_PRIVATE_SOURCE_LOOKAHEAD','FVR_PRIVATE_EXPORT_OVERLAP','FVR_PRIVATE_HOST_COPY_SERVICE_MS','FVR_UNKNOWN','bc2_unknown','fvr_lowercase','XR_RUNTIME_JSON','XR_UNEXPECTED','XRSIM_UNEXPECTED','XRTAPE_UNEXPECTED','BVR_XRSIM_UNEXPECTED')){$psi.Environment[$key]='inherited-invalid'}
    $psi.Environment['KEEP_PROFILE_FIXTURE']='preserved'
    $psi.Environment['TEMP']='original-temp';$psi.Environment['TMP']='original-tmp';$psi.Environment['TMPDIR']='original-tmpdir'
    $environment=@{XR_RUNTIME_JSON='fixture-runtime';XRSIM_DIR='fixture-sim';XRTAPE_DIR='fixture-tape';XRTAPE_MAX_FRAMES='30000'}
    & $Change $environment $attributionProfile
    $caught=$null
    try{. $isolate}catch{$caught=$_.Exception.Message}
    Check (($null -eq $caught) -eq $Allowed) "$Name admission ($caught)"
    AssertEnvironmentUnchanged $before $Name
    Check ($psi.Environment['KEEP_PROFILE_FIXTURE'] -ceq 'preserved' -and $psi.Environment['TEMP'] -ceq 'original-temp' -and $psi.Environment['TMP'] -ceq 'original-tmp' -and $psi.Environment['TMPDIR'] -ceq 'original-tmpdir') "$Name unrelated child environment preserved"
    if(-not $Allowed){
        Check (-not $attributionProfile.child_environment_verified -and -not $privatePresentation.child_environment_verified) "$Name rejection not marked verified"
        return
    }
    foreach($key in @('FVR_PRIVATE_GPU_PHASE_C_DELAY_MS','FVR_PRIVATE_TWO_SOURCE_SLOTS','FVR_PRIVATE_THREE_SOURCE_SLOTS','FVR_PRIVATE_SOURCE_LOOKAHEAD','FVR_PRIVATE_EXPORT_OVERLAP','FVR_PRIVATE_HOST_COPY_SERVICE_MS','FVR_UNKNOWN','bc2_unknown','fvr_lowercase','XR_UNEXPECTED','XRSIM_UNEXPECTED','XRTAPE_UNEXPECTED','BVR_XRSIM_UNEXPECTED')){Check (-not $psi.Environment.ContainsKey($key)) "$Name strips $key"}
    Check ($psi.Environment['XR_RUNTIME_JSON'] -ceq 'fixture-runtime' -and $psi.Environment['XRTAPE_MAX_FRAMES'] -ceq '30000') "$Name XR overrides retained"
    Check ($psi.Environment['FVR_PRIVATE_GPU_ORDERED_PRESENT'] -ceq $(if($Ordered){'1'}else{'0'})) "$Name presentation preserved"
    $keys=@($psi.Environment.Keys|Where-Object {$_ -match '^(FVR_|BC2_)'})
    Check ($keys.Count -eq $(if($OptIn){3}else{1})) "$Name exact private key count: $($keys -join ',')"
    if($OptIn){
        Check ($psi.Environment['FVR_BC2_PRIVATE_CPU_ATTRIBUTION'] -ceq '12' -and $psi.Environment['FVR_PRIVATE_GPU_TIMING436'] -ceq '8') "$Name exact native finite values"
    }else{
        Check (-not $psi.Environment.ContainsKey('FVR_BC2_PRIVATE_CPU_ATTRIBUTION') -and -not $psi.Environment.ContainsKey('FVR_PRIVATE_GPU_TIMING436')) "$Name inherited profiling disabled"
    }
    Check ($privatePresentation.other_fvr_bc2_environment_absent -eq (-not $OptIn)) "$Name legacy absence field truthful"
    $pwsh='fixture-pwsh';$runtimeRoot='G:/fixture/runtime';$stateRoot='G:/fixture/state';$MechanismSnapshots=$false
    . $launchSave
    $result=$receipt|ConvertTo-Json -Depth 12|ConvertFrom-Json -AsHashtable
    foreach($metadata in @($result.private_source_pipeline,$script:saved['launch.json'].private_source_pipeline,$script:saved['run-manifest.json'].private_source_pipeline)){
        Check ($metadata.requested -eq $Pipeline -and -not $metadata.entry_environment_enabled -and -not $metadata.native_enablement_claimed -and $metadata.activation_boundary -ceq 'dedicated_session_after_entry_probe') "$Name pipeline receipt preserves post-probe activation boundary"
    }
    foreach($metadata in @($result.profile_attribution,$script:saved['launch.json'].profile_attribution)){
        Check ($metadata.requested -eq $OptIn -and $metadata.child_environment_verified -and $metadata.unapproved_fvr_bc2_environment_absent -and -not $metadata.native_enablement_claimed) "$Name final receipt truth"
        Check ($metadata.cpu_window_seconds -eq 12 -and $metadata.gpu_maximum_tracked_pairs -eq 8 -and -not $metadata.runtime_rearm -and -not $metadata.steady_insert_window_guaranteed) "$Name finite limits recorded"
        Check ($metadata.environment.Count -eq $(if($OptIn){2}else{0})) "$Name receipt requested map size"
        if($OptIn){Check ($metadata.environment.FVR_BC2_PRIVATE_CPU_ATTRIBUTION -ceq '12' -and $metadata.environment.FVR_PRIVATE_GPU_TIMING436 -ceq '8') "$Name receipt exact values"}
    }
}
foreach($enabled in @($false,$true)){
    foreach($ordered in @($false,$true)){Run "profile=$enabled ordered=$ordered" $enabled $ordered}
    Run "profile=$enabled pipeline=true" $enabled $false {} $true $true
    foreach($unknown in @('FVR_UNKNOWN','BC2_UNKNOWN','FVR_PRIVATE_GPU_PHASE_C_DELAY_MS')){
        Run "refuse $unknown profile=$enabled" $enabled $false ({param($environment,$profile) $environment[$unknown]='1'}.GetNewClosure()) $false
    }
}
Run 'refuse cpu altered value' $true $false {param($environment,$profile) $profile.environment.FVR_BC2_PRIVATE_CPU_ATTRIBUTION='13'} $false
Run 'refuse gpu altered value' $true $false {param($environment,$profile) $profile.environment.FVR_PRIVATE_GPU_TIMING436='9'} $false
Run 'refuse extra profile key' $true $false {param($environment,$profile) $profile.environment.FVR_UNKNOWN='1'} $false
Run 'refuse missing cpu key' $true $false {param($environment,$profile) $profile.environment.Remove('FVR_BC2_PRIVATE_CPU_ATTRIBUTION')} $false
Run 'refuse profile key without opt-in' $false $false {param($environment,$profile) $environment.FVR_BC2_PRIVATE_CPU_ATTRIBUTION='12'} $false
# Execute the exact generated launch wrapper and its assembled argument list,
# replacing only the Entry invocation boundary with a local fixture function.
$writeLaunch=$ast.Find({param($n) $n -is [Management.Automation.Language.InvokeMemberExpressionAst] -and $n.Extent.Text.StartsWith('[IO.File]::WriteAllText($launchScript,')},$true)
$argumentLoop=$ast.Find({param($n) $n -is [Management.Automation.Language.ForEachStatementAst] -and $n.Extent.Text.Contains('$psi.ArgumentList.Add($arg)')},$true)
Check ($null -ne $writeLaunch -and $null -ne $argumentLoop) 'production launch wrapper and argument assembly found'
. ([scriptblock]::Create('$launchText='+$writeLaunch.Arguments[1].Extent.Text))
$fixture=Join-Path (Split-Path $PSScriptRoot -Parent) ('test-temp/pipeline-forward-'+[guid]::NewGuid().ToString('N'))
[IO.Directory]::CreateDirectory($fixture)|Out-Null
function Invoke-PipelineFixtureEntry {
    param($Action,$SettingsPath,$StateRoot,$StartAt,[switch]$PrivateSourcePipeline)
    $script:forwardedPipeline=[bool]$PrivateSourcePipeline
    Check ($Action -ceq 'Launch' -and $StartAt -ceq 'MainMenu') 'assembled pipeline wrapper preserves launch route'
}
try {
    foreach($selected in @($false,$true)){
        $PrivateSourcePipeline=[switch]$selected;$psi=[Diagnostics.ProcessStartInfo]::new()
        $launchScript=Join-Path $fixture 'launch.ps1';$runtimeRoot=$fixture;$settingsPath='fixture-settings';$stateRoot='fixture-state';$OutputDir=$fixture
        $PreAttachSetupSpec='';$PreAttachSetupSpecSha256='';$PrivateSessionBinding='';$PrivateSessionBindingSha256=''
        . ([scriptblock]::Create($argumentLoop.Extent.Text))
        $assembled=@($psi.ArgumentList);$wrapperArgs=$assembled[6..($assembled.Count-1)]
        Check ($wrapperArgs[0] -ceq (Join-Path $fixture 'Entry.ps1') -and $wrapperArgs[-1] -ceq $(if($selected){'1'}else{'0'})) 'production argument positions preserve pipeline selection'
        $wrapperArgs[0]='Invoke-PipelineFixtureEntry';$script:forwardedPipeline=$null
        & ([scriptblock]::Create($launchText)) @wrapperArgs
        Check ($script:forwardedPipeline -eq $selected) 'generated wrapper forwards exact opt-in/default'
    }
} finally {
    $log=Join-Path $fixture 'entry.log';if(Test-Path -LiteralPath $log){Remove-Item -LiteralPath $log}
    Remove-Item -LiteralPath $fixture
}
AssertEnvironmentUnchanged $beforeAll 'complete suite'
Write-Output "PASS: $script:checks checks; isolated environment cases and assembled launch-wrapper forwarding; no game, global environment or tracked-source mutation."
