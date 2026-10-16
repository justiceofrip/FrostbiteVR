#requires -Version 7.5
[CmdletBinding()]
param()
$ErrorActionPreference='Stop'
$repository=Split-Path -Parent $PSScriptRoot
$prior=@{}
foreach($name in @('TEMP','TMP','TMPDIR','PYTHONDONTWRITEBYTECODE','FVR_STORAGE_LAUNCH_KEY')){$prior[$name]=[Environment]::GetEnvironmentVariable($name,'Process')}
. (Join-Path $repository 'tools/StorageBudget.ps1')
$null=Set-FvrProjectTemporaryStorage $repository
$fixture=Join-Path $repository ('test-temp/launch-scope-'+[guid]::NewGuid().ToString('N'))
$null=[IO.Directory]::CreateDirectory($fixture)
$checks=[Collections.Generic.List[string]]::new()
function Check([bool]$Condition,[string]$Name){if(-not $Condition){throw "FAIL: $Name"};$checks.Add($Name)}
function Refuses([scriptblock]$Work,[string]$Pattern,[string]$Name){
    $failure=$null;try{& $Work | Out-Null}catch{$failure=$_.Exception.Message}
    Check ($failure -and $failure -match $Pattern) $Name
}
$script:inventories=0;$script:freeBytes=100GB;$script:volumeReads=0
function Get-FvrStorageUsage {param([string[]]$Roots)
    $script:inventories++
    return [pscustomobject]@{allocated_bytes=4GB;logical_bytes=6GB;bytes=4GB;files=25;roots=@($Roots|ForEach-Object{@{path=$_;allocated_bytes=0;logical_bytes=0;files=0}})}
}
function Get-FvrStorageVolume {param([string]$Path)
    $script:volumeReads++
    return @{root=[IO.Path]::GetPathRoot($Path);free_bytes=$script:freeBytes}
}
$arguments=@{RepositoryRoot=$repository;OutputPath=$fixture;Operation='Session';PlannedBytes=640MB;RawCaptureBytes=512MB}
function New-Scope {
    $admission=Assert-FvrStorageBudget @arguments
    return New-FvrLaunchStorageScope -Admission $admission -RepositoryRoot $repository -ReceiptPath (Join-Path $fixture ([guid]::NewGuid().ToString('N')+'.json'))
}
function Rewrite-Scope([string]$Path,[scriptblock]$Change){
    $payload=ConvertFrom-FvrLaunchEnvelope ([IO.File]::ReadAllText($Path))
    & $Change $payload
    [IO.File]::WriteAllText($Path,(ConvertTo-FvrLaunchEnvelope $payload))
}
try{
    $scope=New-Scope;$count=$script:inventories;$volumes=$script:volumeReads
    $verified=Assert-FvrStorageBudget @arguments -LaunchReceiptPath $scope -LaunchStage verified
    Check ($script:inventories -eq $count -and $verified.launch_scope.stage -eq 'verified') 'verified stage consumes original inventory without rescanning'
    Check ($script:volumeReads -gt $volumes -and $verified.volumes.Count -eq 1) 'handoff rechecks current free space on actual G volume'
    Check (-not (Test-FvrLaunchStorageComplete $scope)) 'parent does not report complete before trace admission'
    Refuses {Assert-FvrStorageBudget @arguments -LaunchReceiptPath $scope -LaunchStage verified} 'reused|out of order' 'stage cannot be reused'
    Refuses {Assert-FvrStorageBudget @arguments -LaunchReceiptPath $scope -LaunchStage trace} 'reused|out of order' 'trace cannot skip supervisor admission'
    Refuses {Assert-FvrStorageBudget @arguments -LaunchReceiptPath $scope -LaunchStage session} 'direct child' 'supervisor stage cannot run in original parent'

    $scope=New-Scope
    $script:freeBytes=15GB
    Refuses {Assert-FvrStorageBudget @arguments -LaunchReceiptPath $scope -LaunchStage verified} 'free' 'fresh low free space blocks an otherwise valid receipt'
    $script:freeBytes=100GB
    $scope=New-Scope
    Rewrite-Scope $scope {param($p) $p.expires_utc=[DateTime]::UtcNow.AddSeconds(-1).ToString('o')}
    Refuses {Assert-FvrStorageBudget @arguments -LaunchReceiptPath $scope -LaunchStage verified} 'expired' 'expired launch receipt is rejected'
    $scope=New-Scope
    Rewrite-Scope $scope {param($p) $p.owner.start_utc='2000-01-01T00:00:00.0000000Z'}
    Refuses {Assert-FvrStorageBudget @arguments -LaunchReceiptPath $scope -LaunchStage verified} 'identity changed' 'PID reuse with changed creation time is rejected'
    $scope=New-Scope
    $wire=[IO.File]::ReadAllText($scope) | ConvertFrom-Json -AsHashtable
    $wire.mac='0'*64
    [IO.File]::WriteAllText($scope,($wire | ConvertTo-Json -Compress))
    Refuses {Assert-FvrStorageBudget @arguments -LaunchReceiptPath $scope -LaunchStage verified} 'integrity' 'modified receipt is rejected'
    $scope=New-Scope
    $copied=Join-Path $fixture 'copied-receipt.json'
    [IO.File]::WriteAllText($copied,[IO.File]::ReadAllText($scope))
    Refuses {Assert-FvrStorageBudget @arguments -LaunchReceiptPath $copied -LaunchStage verified} 'different invocation' 'copied receipt cannot masquerade as a new launch'
    $scope=New-Scope
    $changed=$arguments.Clone();$changed.Operation='Build'
    Refuses {Assert-FvrStorageBudget @changed -LaunchReceiptPath $scope -LaunchStage verified} 'only valid for a session' 'builds cannot borrow a session admission'
    $changed=$arguments.Clone();$changed.PlannedBytes=641MB
    Refuses {Assert-FvrStorageBudget @changed -LaunchReceiptPath $scope -LaunchStage verified} 'changes the admitted limits' 'larger output request cannot borrow original plan'
    Refuses {Assert-FvrStorageBudget @arguments -MaxGeneratedBytes 41GB -LaunchReceiptPath $scope -LaunchStage verified} 'changes the admitted limits' 'changed total limit is rejected'
    $changed=$arguments.Clone();$changed.OutputPath=Join-Path (Split-Path $repository -Parent) 'unbudgeted-launch-output'
    Refuses {Assert-FvrStorageBudget @changed -LaunchReceiptPath $scope -LaunchStage verified} 'does not cover' 'unmeasured output roots are rejected'
    $scope=New-Scope;$oldKey=$env:FVR_STORAGE_LAUNCH_KEY
    $null=New-Scope
    Refuses {Assert-FvrStorageBudget @arguments -LaunchReceiptPath $scope -LaunchStage verified} 'integrity' 'next launch key cannot reuse a previous launch receipt'
    $env:FVR_STORAGE_LAUNCH_KEY=$oldKey

    # Exercise the real Windows parent/child identity and inherited in-memory
    # key. The child must not call an inventory; it performs only admission,
    # never launches BC2/SteamVR or writes a native report/capture.
    $scope=New-Scope
    $null=Assert-FvrStorageBudget @arguments -LaunchReceiptPath $scope -LaunchStage verified
    $child=Join-Path $fixture 'child.ps1';$childResult=Join-Path $fixture 'child-result.json'
    @'
param([string]$Repository,[string]$Scope,[string]$Output,[string]$Result)
$ErrorActionPreference='Stop'
. (Join-Path $Repository 'tools/StorageBudget.ps1')
function Get-FvrStorageUsage {throw 'Child unexpectedly rescanned all storage'}
$argsCommon=@{RepositoryRoot=$Repository;OutputPath=$Output;Operation='Session';PlannedBytes=640MB;RawCaptureBytes=512MB;LaunchReceiptPath=$Scope}
$watch=[Diagnostics.Stopwatch]::StartNew()
$one=Assert-FvrStorageBudget @argsCommon -LaunchStage session
$two=Assert-FvrStorageBudget @argsCommon -LaunchStage trace
$reused=$false
try{$null=Assert-FvrStorageBudget @argsCommon -LaunchStage trace}catch{$reused=$_.Exception.Message -match 'reused|out of order'}
@{session=$one.launch_scope.stage;trace=$two.launch_scope.stage;repeat_refused=$reused;seconds=$watch.Elapsed.TotalSeconds;temp=$env:TEMP;parent=$PID}|ConvertTo-Json|Set-Content -LiteralPath $Result
'@ | Set-Content -LiteralPath $child
    $childArguments=@('-NoProfile','-File',('"'+$child+'"'),'-Repository',('"'+$repository+'"'),'-Scope',('"'+$scope+'"'),'-Output',('"'+$fixture+'"'),'-Result',('"'+$childResult+'"'))
    $childProcess=Start-Process -FilePath (Join-Path $PSHOME 'pwsh.exe') -ArgumentList $childArguments -WindowStyle Hidden -PassThru -Wait -RedirectStandardOutput (Join-Path $fixture 'child.stdout') -RedirectStandardError (Join-Path $fixture 'child.stderr')
    Check ($childProcess.ExitCode -eq 0 -and (Test-Path -LiteralPath $childResult)) 'real hidden supervisor and same-process trace admission succeed'
    $result=Get-Content -LiteralPath $childResult -Raw | ConvertFrom-Json
    Check ($result.session -eq 'session' -and $result.trace -eq 'trace' -and $result.repeat_refused) 'real child records both finite stages and rejects a second trace'
    Check ($result.temp.StartsWith('G:') -and (Test-FvrLaunchStorageComplete $scope)) 'G temporary routing inherited and parent sees completed admission'
    $summary=@{passed=$true;checks=$checks.Count;details=$checks.ToArray();child_handoff_seconds=$result.seconds;native_or_headset_test=$false}
    $summary|ConvertTo-Json -Depth 5
    $summary|ConvertTo-Json -Depth 5|Set-Content -LiteralPath (Join-Path $repository 'test-temp/launch-storage-scope412-results.json')
}finally{
    foreach($name in $prior.Keys){[Environment]::SetEnvironmentVariable($name,$prior[$name],'Process')}
    $resolved=[IO.Path]::GetFullPath($fixture)
    $allowed=[IO.Path]::GetFullPath((Join-Path $repository 'test-temp')).TrimEnd('\')+'\'
    if(-not $resolved.StartsWith($allowed,[StringComparison]::OrdinalIgnoreCase) -or (Split-Path -Leaf $resolved) -notmatch '^launch-scope-[0-9a-f]{32}$'){throw 'Invalid fixture cleanup target'}
    Remove-Item -LiteralPath $resolved -Recurse -Force
}
