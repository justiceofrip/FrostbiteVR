# ROOT-only file staging. Functions make no game/native/build calls.
# The accepted PrivateSession.ps1 owns final admission, including current native closure.
function Assert-Stage([bool]$Condition,[string]$Message) {
    if(-not $Condition){throw ('Private staging refused: '+$Message)}
}
function Assert-StageNoLinks([string]$Path) {
    Assert-Stage ([IO.Path]::IsPathFullyQualified($Path)) 'absolute path required'
    $next=[IO.Path]::GetFullPath($Path)
    while($next){
        if(Test-Path -LiteralPath $next){
            Assert-Stage (-not ((Get-Item -LiteralPath $next -Force).Attributes -band [IO.FileAttributes]::ReparsePoint)) ('linked path: '+$next)
        }
        $next=Split-Path -Parent $next
    }
}
function Get-StagePin([string]$Path) {
    Assert-StageNoLinks $Path
    $item=Get-Item -LiteralPath $Path -Force -ErrorAction Stop
    Assert-Stage (-not $item.PSIsContainer) 'file required'
    return @{path=$item.FullName;bytes=$item.Length;sha256=(Get-FileHash -LiteralPath $item.FullName -Algorithm SHA256).Hash.ToLowerInvariant()}
}
function Assert-StagePin($Pin) {
    Assert-Stage ($Pin -is [hashtable] -and $Pin.sha256 -cmatch '^[a-f0-9]{64}$') 'complete pinned input required'
    $actual=Get-StagePin $Pin.path
    Assert-Stage ($actual.path -ieq $Pin.path -and $actual.bytes -eq $Pin.bytes -and $actual.sha256 -ceq $Pin.sha256) ('input changed: '+$Pin.path)
}
function Read-StageJson([string]$Path) {return [IO.File]::ReadAllText($Path)|ConvertFrom-Json -AsHashtable -DateKind String}
function Test-StageSameValue($Left,$Right) {
    if($null -eq $Left -or $null -eq $Right){return $null -eq $Left -and $null -eq $Right}
    if($Left -is [Collections.IDictionary]){
        if($Right -isnot [Collections.IDictionary] -or $Left.Count -ne $Right.Count){return $false}
        foreach($key in $Left.Keys){if(-not $Right.Contains($key) -or -not (Test-StageSameValue $Left[$key] $Right[$key])){return $false}}
        return $true
    }
    if($Left -is [array]){
        if($Right -isnot [array] -or $Left.Count -ne $Right.Count){return $false}
        for($i=0;$i -lt $Left.Count;$i++){if(-not (Test-StageSameValue $Left[$i] $Right[$i])){return $false}}
        return $true
    }
    return ($Left|ConvertTo-Json -Compress) -ceq ($Right|ConvertTo-Json -Compress)
}
function Get-StageLauncherCommit([string]$Repository,[string[]]$Files) {
    # This identity belongs to launcher integration, never to the compiled native batch.
    $head=& git -C $Repository rev-parse --verify HEAD
    Assert-Stage ($LASTEXITCODE -eq 0 -and $head -cmatch '^[a-f0-9]{40}$') 'cannot identify launcher HEAD'
    $dirty=@(& git -C $Repository status --porcelain --untracked-files=all -- @Files)
    Assert-Stage ($LASTEXITCODE -eq 0 -and $dirty.Count -eq 0) 'reviewed launcher files must be committed and unchanged'
    return [string]$head
}
function Assert-StageSameRoots($Actual,$Expected) {
    $a=@($Actual|ForEach-Object {[IO.Path]::GetFullPath($_).TrimEnd('\').ToLowerInvariant()}|Sort-Object -Unique)
    $e=@($Expected|ForEach-Object {[IO.Path]::GetFullPath($_).TrimEnd('\').ToLowerInvariant()}|Sort-Object -Unique)
    Assert-Stage ($a.Count -eq 13 -and $e.Count -eq 13 -and ($a -join "`n") -ceq ($e -join "`n")) 'fresh inventory must contain the exact accepted 13 roots'
}
function New-PrivateStageDecision($ConfigPin,[string]$InstallRoot,[string]$LauncherCommit,$ReviewedPins,$ReviewedPinSetPin) {
    Assert-StagePin $ConfigPin
    Assert-StagePin $ReviewedPinSetPin
    $expectedNames=@('launcher/runtime/Entry.ps1','launcher/runtime/Session.ps1','launcher/runtime/Runtime.Core.ps1','launcher/runtime/PrivateSession.ps1','tools/Invoke-Bc2XrGameSimulation.ps1')
    Assert-Stage ($ReviewedPins.schema -ceq 'fvr.private-stage-reviewed-launcher.v1' -and @($ReviewedPins.files).Count -eq 5 -and @($ReviewedPins.files.path|Select-Object -Unique).Count -eq 5 -and @($ReviewedPins.files.path|Where-Object {$_ -cnotin $expectedNames}).Count -eq 0) 'exact reviewed launcher pinset required'
    $c=Read-StageJson $ConfigPin.path
    Assert-Stage ($c.schema -ceq 'fvr.native-batch.v1' -and $c.root_accepted -is [bool] -and $c.root_accepted) 'accepted native batch required'
    Assert-Stage ($c.source_commit -cmatch '^[a-f0-9]{40}$' -and $c.source_sha256 -cmatch '^[a-f0-9]{64}$') 'final native source identity required'
    Assert-Stage ($LauncherCommit -cmatch '^[a-f0-9]{40}$') 'explicit final launcher commit required'
    foreach($path in @($c.repository,$c.run,$InstallRoot)){Assert-StageNoLinks $path}
    $stage=Join-Path $c.run 'stage'
    Assert-Stage (-not (Test-Path -LiteralPath $stage)) 'stage already exists; preserve it and review before any retry'
    Assert-Stage (-not (Test-Path -LiteralPath (Join-Path $c.run 'private-stage-creation.lock'))) 'prior staging attempt exists; preserve its evidence'
    $scriptPins=@();$runner=$null
    foreach($row in $ReviewedPins.files){
        $pin=Get-StagePin (Get-FvrChild $c.repository $row.path)
        Assert-Stage ($pin.sha256 -ceq $row.sha256 -and (-not $row.ContainsKey('bytes') -or $pin.bytes -eq $row.bytes)) ('canonical launcher differs from reviewed753: '+$row.path)
        if($row.path -ceq 'tools/Invoke-Bc2XrGameSimulation.ps1'){$runner=$pin}
        else{$scriptPins+=@{relative_path=[IO.Path]::GetFileName($row.path);source=$pin}}
    }
    Assert-Stage ($scriptPins.Count -eq 4 -and $null -ne $runner) 'exact reviewed script set required'
    Assert-Stage ((Get-StageLauncherCommit $c.repository @($ReviewedPins.files.path)) -ceq $LauncherCommit) 'launcher commit differs'
    $baseline=Get-FvrChild $InstallRoot 'versions/0.1.77/runtime'
    $manifest=Get-StagePin (Join-Path $baseline 'runtime.json')
    $active=Get-StagePin (Join-Path $InstallRoot 'active.json')
    $m=Read-StageJson $manifest.path;$a=Read-StageJson $active.path
    Assert-Stage ($m.schema -ceq 'bc2vr.runtime.v1' -and $m.version -ceq '0.1.77' -and $m.diagnostic_only -is [bool] -and -not $m.diagnostic_only) 'installed77 normal manifest required'
    Assert-Stage ($a.current -ceq '0.1.77' -and $a.previous -ceq '0.1.76') 'installed77/fallback76 identity differs'
    Assert-Stage ($c.budget.root_authorized -is [bool] -and $c.budget.root_authorized -and $c.budget.freeze_ceiling_bytes -eq 52GB -and $c.budget.floor_bytes -eq 15GB) 'accepted stage budget must be 52 GiB with 15 GiB floor'
    Assert-StageSameRoots $c.budget.roots $c.budget.roots
    $app=@();foreach($name in 'Launcher.Core.ps1','Launcher.Storage.ps1'){$app+=Get-StagePin (Get-FvrChild $InstallRoot ('versions/0.1.77/app/'+$name))}
    $d=[ordered]@{
        schema='fvr.private-stage-decision.v1';root_accepted=$false;reason='';created_utc=[DateTime]::UtcNow.ToString('o')
        repository=[IO.Path]::GetFullPath($c.repository);install_root=[IO.Path]::GetFullPath($InstallRoot);stage_root=$stage
        source_commit=$c.source_commit;source_sha256=$c.source_sha256;launcher_source_commit=$LauncherCommit
        native_batch_config=$ConfigPin;source_receipt=$c.source_receipt;host_source_receipt=$c.host_source_receipt
        normal_artifacts_receipt=(Get-StagePin (Join-Path $c.run 'normal-runtime/receipt.json'))
        private_artifacts_receipt=(Get-StagePin (Join-Path $c.run 'private-x86/receipt.json'))
        shared_build_state=(Get-StagePin (Join-Path $c.run 'private-x86/shared-build-state.json'))
        installed_runtime_manifest=$manifest;installed_active=$active;installed_app_files=$app
        reviewed_runtime_scripts=$scriptPins;reviewed_runner=$runner;reviewed_launcher_pinset=$ReviewedPinSetPin
        budget=@{roots=@($c.budget.roots);max_generated_bytes=52GB;min_free_bytes=15GB;raw_capture_bytes=0}
        estimate=$null
    }
    $plan=Get-PrivateStageCopyPlan $d
    $d.estimate=$plan.estimate
    return $d
}
function Get-PrivateStageCopyPlan($Decision) {
    $d=$Decision
    foreach($name in 'native_batch_config','source_receipt','host_source_receipt','normal_artifacts_receipt','private_artifacts_receipt','shared_build_state','installed_runtime_manifest','installed_active','reviewed_runner'){Assert-StagePin $d[$name]}
    $normal=Read-StageJson $d.normal_artifacts_receipt.path;$private=Read-StageJson $d.private_artifacts_receipt.path
    $manifest=Read-StageJson $d.installed_runtime_manifest.path
    $map=@{};$baseline=Split-Path $d.installed_runtime_manifest.path -Parent
    foreach($file in $manifest.files){
        Assert-Stage (-not $map.ContainsKey($file.path) -and $file.path -ine 'runtime.json') 'duplicate or reserved installed runtime path'
        $pin=Get-StagePin (Get-FvrChild $baseline $file.path)
        Assert-Stage ($pin.sha256 -ceq $file.sha256) ('installed file changed: '+$file.path)
        $map[$file.path]=@{relative_path=('runtime/'+$file.path);source=$pin;origin='retained-installed77'}
    }
    foreach($r in @($normal,$private)){
        Assert-Stage ($r.schema -ceq 'fvr.native-batch-artifacts.v1' -and $r.source_commit -ceq $d.source_commit -and $r.source_sha256 -ceq $d.source_sha256) 'artifact identity differs'
        Assert-Bc2PrivateSamePin $r.config $d.native_batch_config
        foreach($file in $r.files){Assert-StagePin $file}
        foreach($a in $r.artifacts){
            Assert-StagePin $a.binary;Assert-StagePin $a.pdb
            if($r -eq $normal -and $a.key -ceq 'probe'){continue}
            $relative='bin/'+[IO.Path]::GetFileName($a.binary.path)
            $null=Get-FvrChild $d.stage_root $relative
            $map[$relative]=@{relative_path=('runtime/'+$relative);source=$a.binary;origin=$(if($r -eq $normal){'frozen-normal'}else{'frozen-private'})}
        }
    }
    foreach($row in $d.reviewed_runtime_scripts){
        Assert-StagePin $row.source
        $map[$row.relative_path]=@{relative_path=('runtime/'+$row.relative_path);source=$row.source;origin='reviewed-launcher753'}
    }
    $files=@($map.Values|Sort-Object relative_path)
    foreach($pin in $d.installed_app_files){Assert-StagePin $pin;$files+=@{relative_path=('app/'+[IO.Path]::GetFileName($pin.path));source=$pin;origin='retained-installed77-app'}}
    [long]$logical=0;[long]$allocated=0
    foreach($file in $files){
        $null=Get-FvrChild $d.stage_root $file.relative_path
        $logical+=$file.source.bytes
        $allocated+=[long]([Math]::Ceiling($file.source.bytes/65536.0)*65536)
    }
    # Conservative uncompressed copies rounded per file plus metadata/directory allowance.
    return @{files=$files;estimate=@{file_count=$files.Count;logical_copy_bytes=$logical;rounded_copy_bytes=$allocated;allocation_rounding_bytes=65536;metadata_reserve_bytes=16MB;planned_bytes=($allocated+16MB)}}
}
function Write-StageNewJson([string]$Path,$Value) {
    Assert-StageNoLinks $Path
    $bytes=[Text.UTF8Encoding]::new($false).GetBytes(($Value|ConvertTo-Json -Depth 60))
    # At most seven metadata files, each <=1 MiB, fit within the 16 MiB allowance.
    Assert-Stage ($bytes.Length -le 1MB) 'metadata exceeds reserved limit'
    $stream=[IO.File]::Open($Path,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::None)
    try{$stream.Write($bytes,0,$bytes.Length);$stream.Flush($true)}finally{$stream.Dispose()}
    return Get-StagePin $Path
}
function Copy-StageNewFile($Row,[string]$StageRoot) {
    Assert-StagePin $Row.source
    $destination=Get-FvrChild $StageRoot $Row.relative_path
    Assert-StageNoLinks $destination
    $null=[IO.Directory]::CreateDirectory((Split-Path $destination -Parent))
    Assert-StageNoLinks $destination
    $inputStream=[IO.File]::Open($Row.source.path,[IO.FileMode]::Open,[IO.FileAccess]::Read,[IO.FileShare]::Read)
    try{
        $outputStream=[IO.File]::Open($destination,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::None)
        try{$inputStream.CopyTo($outputStream);$outputStream.Flush($true)}finally{$outputStream.Dispose()}
    }finally{$inputStream.Dispose()}
    $actual=Get-StagePin $destination
    Assert-Stage ($actual.bytes -eq $Row.source.bytes -and $actual.sha256 -ceq $Row.source.sha256) ('copied file differs: '+$Row.relative_path)
    return $actual
}
function Invoke-PrivateStage($DecisionPin,$ReviewedPins,[switch]$RootAccept) {
    Assert-Stage $RootAccept.IsPresent 'explicit -RootAccept required'
    Assert-StagePin $DecisionPin
    $d=Read-StageJson $DecisionPin.path
    Assert-Stage ($d.schema -ceq 'fvr.private-stage-decision.v1' -and $d.root_accepted -is [bool] -and $d.root_accepted -and -not [string]::IsNullOrWhiteSpace($d.reason)) 'pinned ROOT-accepted decision and reason required'
    # Recreate the entire plan from live pinned inputs. No final IDs are inferred from draft values.
    $fresh=New-PrivateStageDecision $d.native_batch_config $d.install_root $d.launcher_source_commit $ReviewedPins $d.reviewed_launcher_pinset
    foreach($key in $fresh.Keys|Where-Object {$_ -notin @('root_accepted','reason','created_utc')}){
        Assert-Stage (Test-StageSameValue $fresh[$key] $d[$key]) ('reviewed decision changed: '+$key)
    }
    $plan=Get-PrivateStageCopyPlan $d
    $c=Read-StageJson $d.native_batch_config.path
    Assert-Stage (-not $d.stage_root.StartsWith($d.install_root.TrimEnd('\')+'\',[StringComparison]::OrdinalIgnoreCase)) 'stage must be separate from install'
    # A fresh complete scan, never a previous receipt or launch handoff. Fixed stage limits.
    $admission=Assert-FvrStorageBudget -RepositoryRoot $d.repository -OutputPath $d.stage_root -Operation Build -PlannedBytes $plan.estimate.planned_bytes -RawCaptureBytes 0 -MaxGeneratedBytes 52GB -MinFreeBytes 15GB -AdditionalRoots @($d.budget.roots)
    Assert-StageSameRoots @($admission.generated.roots|ForEach-Object {$_.path}) $d.budget.roots
    Assert-Stage (-not (Test-Path -LiteralPath $d.stage_root)) 'stage appeared during admission'
    Assert-StageNoLinks $d.stage_root
    $lockPath=Join-Path $c.run 'private-stage-creation.lock'
    $lock=[IO.File]::Open($lockPath,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::None)
    try{
        $null=[IO.Directory]::CreateDirectory($d.stage_root)
        $storagePin=Write-StageNewJson (Join-Path $d.stage_root 'storage.json') $admission
        $null=Write-StageNewJson (Join-Path $d.stage_root 'root-decision.json') $d
        $null=Write-StageNewJson (Join-Path $d.stage_root 'copy-plan.json') $plan
        $staged=@{};foreach($row in $plan.files){$staged[$row.relative_path]=Copy-StageNewFile $row $d.stage_root}
        $runtime=Join-Path $d.stage_root 'runtime'
        $m=Read-StageJson $d.installed_runtime_manifest.path
        $m.version='diagnostic-campaign-'+$d.source_commit;$m.source_sha256=$d.source_sha256;$m.diagnostic_only=$true
        $m.files=@($plan.files|Where-Object {$_.relative_path.StartsWith('runtime/')}|ForEach-Object {@{path=$_.relative_path.Substring(8);sha256=$staged[$_.relative_path].sha256}})
        # Requires ROOT's narrow753 provenance correction, specified in guard-metadata-change.txt.
        # Preserve ordinary baseline policy, but never claim baseline77 supplied the loaded probe.
        $m.native_source_commit=$d.source_commit;$m.launcher_source_commit=$d.launcher_source_commit
        $m.native_version=$m.version;$m.frozen_receipt_sha256=$d.private_artifacts_receipt.sha256
        $m.normal_artifacts_receipt_sha256=$d.normal_artifacts_receipt.sha256
        $m.baseline_package_source_commit=$m.package_source_commit;$null=$m.Remove('package_source_commit')
        $manifestPin=Write-StageNewJson (Join-Path $runtime 'runtime.json') $m
        $b=@{schema='fvr.private-campaign-session.v1';root_accepted=$true;diagnostic_only=$true;baseline_version='0.1.77';install_root=$d.install_root;runtime_root=$runtime;source_commit=$d.source_commit;source_sha256=$d.source_sha256;launcher_source_commit=$d.launcher_source_commit;runtime_manifest=$manifestPin;staged_app_files=@($staged['app/Launcher.Core.ps1'],$staged['app/Launcher.Storage.ps1']);staging_decision=$DecisionPin;staging_storage=$storagePin}
        foreach($key in 'native_batch_config','source_receipt','host_source_receipt','normal_artifacts_receipt','private_artifacts_receipt','shared_build_state','installed_runtime_manifest','installed_active','reviewed_runtime_scripts','reviewed_runner','reviewed_launcher_pinset'){$b[$key]=$d[$key]}
        $bindingPin=Write-StageNewJson (Join-Path $d.stage_root 'private-launch.binding.json') $b
        Assert-StagePin $DecisionPin
        Assert-Stage ((Get-StageLauncherCommit $d.repository @($ReviewedPins.files.path)) -ceq $d.launcher_source_commit) 'launcher identity changed during staging'
        # The actual accepted guard is the final validator. No duplicated source/build admission.
        $verified=Get-Bc2PrivateAdmission $bindingPin.path $bindingPin.sha256 $runtime $d.install_root
        $result=@{schema='fvr.private-stage-result.v1';passed=$true;checked_utc=[DateTime]::UtcNow.ToString('o');evidence_level='file identity / deterministic staging; no game or headset claim';source_commit=$d.source_commit;source_sha256=$d.source_sha256;launcher_source_commit=$d.launcher_source_commit;binding=$verified.binding;runtime_root=$runtime;storage=$storagePin;decision=$DecisionPin;estimate=$plan.estimate;game_launched=$false;native_build_started=$false}
        $null=Write-StageNewJson (Join-Path $d.stage_root 'result.json') $result
        return $result
    }catch{
        # Keep partial files and the permanent lock for review; never clean, overwrite or auto-retry.
        try{$null=Write-StageNewJson (Join-Path $d.stage_root 'failure.json') @{schema='fvr.private-stage-failure.v1';error=$_.Exception.Message;checked_utc=[DateTime]::UtcNow.ToString('o');decision=$DecisionPin;stage_validated=$false}}catch{}
        throw
    }finally{$lock.Dispose()}
}
