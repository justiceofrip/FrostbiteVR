# Explicit developer-only admission. Never changes installed package metadata.
function Assert-Bc2Private([bool]$Condition,[string]$Message) {
    if(-not $Condition){throw ('Private campaign runtime refused: '+$Message)}
}
function Get-Bc2PrivatePin([string]$Path) {
    Assert-Bc2Private ([IO.Path]::IsPathFullyQualified($Path)) 'absolute file path required'
    $Path=[IO.Path]::GetFullPath($Path);Assert-FvrNoLinks $Path
    Assert-Bc2Private (Test-Path -LiteralPath $Path -PathType Leaf) ('pinned file missing: '+$Path)
    $item=Get-Item -LiteralPath $Path
    Assert-Bc2Private (-not $item.PSIsContainer) 'file required'
    return @{path=$Path;bytes=$item.Length;sha256=(Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()}
}
function Assert-Bc2PrivatePin($Pin,[string]$ExpectedPath='') {
    Assert-Bc2Private ($Pin -is [hashtable] -and $Pin.sha256 -cmatch '^[a-f0-9]{64}$') 'complete pin required'
    $actual=Get-Bc2PrivatePin $Pin.path
    Assert-Bc2Private ($actual.path -ieq $Pin.path -and $actual.bytes -eq $Pin.bytes -and $actual.sha256 -ceq $Pin.sha256) ('file changed: '+$Pin.path)
    if($ExpectedPath){Assert-Bc2Private ($actual.path -ieq [IO.Path]::GetFullPath($ExpectedPath)) 'pin path differs'}
    return $actual
}
function Read-Bc2PrivatePinnedJson($Pin,[string]$ExpectedPath='') {
    $null=Assert-Bc2PrivatePin $Pin $ExpectedPath
    return Read-FvrJson $Pin.path
}
function Assert-Bc2PrivateSamePin($Actual,$Expected) {
    Assert-Bc2Private ($Actual.path -ieq $Expected.path -and $Actual.bytes -eq $Expected.bytes -and $Actual.sha256 -ceq $Expected.sha256) 'receipt/file binding differs'
}
function Test-Bc2PrivateBoolean($Value,[bool]$Expected) {return ($Value -is [bool] -and $Value -eq $Expected)}
function Get-Bc2PrivateAdmission([string]$BindingPath,[string]$BindingSha256,[string]$RuntimeRoot,[string]$InstallRoot) {
    Assert-Bc2Private ($BindingSha256 -cmatch '^[a-f0-9]{64}$') 'explicit binding SHA256 required'
    $bindingPin=Get-Bc2PrivatePin $BindingPath
    Assert-Bc2Private ($bindingPin.sha256 -ceq $BindingSha256) 'binding changed'
    $b=Read-FvrJson $BindingPath
    Assert-Bc2Private ($b.schema -ceq 'fvr.private-campaign-session.v1' -and (Test-Bc2PrivateBoolean $b.root_accepted $true) -and (Test-Bc2PrivateBoolean $b.diagnostic_only $true)) 'ROOT-accepted diagnostic binding required'
    Assert-Bc2Private ([IO.Path]::IsPathFullyQualified($RuntimeRoot) -and [IO.Path]::IsPathFullyQualified($InstallRoot)) 'absolute runtime/install roots required'
    $RuntimeRoot=[IO.Path]::GetFullPath($RuntimeRoot);$InstallRoot=[IO.Path]::GetFullPath($InstallRoot)
    Assert-FvrNoLinks $RuntimeRoot;Assert-FvrNoLinks $InstallRoot
    Assert-Bc2Private ($b.runtime_root -ieq $RuntimeRoot -and $b.install_root -ieq $InstallRoot -and -not $RuntimeRoot.StartsWith($InstallRoot+'\',[StringComparison]::OrdinalIgnoreCase)) 'separate staging runtime required'
    $c=Read-Bc2PrivatePinnedJson $b.native_batch_config
    Assert-Bc2Private ($c.schema -ceq 'fvr.native-batch.v1' -and $c.root_accepted -ceq $true -and $c.source_commit -cmatch '^[a-f0-9]{40}$' -and $c.source_sha256 -cmatch '^[a-f0-9]{64}$') 'final accepted batch required'
    Assert-Bc2Private ($b.source_commit -ceq $c.source_commit -and $b.source_sha256 -ceq $c.source_sha256) 'candidate source differs'
    Assert-Bc2Private ($RuntimeRoot.StartsWith(([IO.Path]::GetFullPath($c.run)+'\'),[StringComparison]::OrdinalIgnoreCase)) 'stage outside exact batch run'
    Assert-Bc2PrivateSamePin $b.source_receipt $c.source_receipt
    Assert-Bc2PrivateSamePin $b.host_source_receipt $c.host_source_receipt
    foreach($sourcePin in @($b.source_receipt,$b.host_source_receipt)){
        $closure=Read-Bc2PrivatePinnedJson $sourcePin
        Assert-Bc2Private ($closure.schema -ceq 'fvr.bc2.native-operation-source.v1' -and @($closure.files).Count -gt 0) 'source closure missing'
        foreach($row in @($closure.files)+@($closure.excluded_data_sha256)){
            Assert-Bc2Private ((Get-Bc2PrivatePin (Get-FvrChild $c.repository $row.path)).sha256 -ceq $row.sha256) ('current source closure changed: '+$row.path)
        }
        Assert-Bc2Private ($closure.recipe_sha256 -ceq (Get-Bc2PrivatePin (Join-Path $c.repository 'tools/bc2_native_operation_receipts.py')).sha256 -and $closure.build_wiring_sha256 -ceq (Get-Bc2PrivatePin (Join-Path $c.repository 'cmake/Bc2NativeOperationCapability.cmake')).sha256) 'source closure recipe/wiring differs'
    }
    $source=Read-FvrJson $b.source_receipt.path
    $null=Assert-Bc2PrivatePin $c.reviewed_header
    Assert-Bc2Private ($source.source_sha256 -ceq $c.source_sha256 -and $source.reviewed_header_sha256 -ceq $c.reviewed_header.sha256) 'source/header identity differs'
    $normal=Read-Bc2PrivatePinnedJson $b.normal_artifacts_receipt (Join-Path $c.run 'normal-runtime/receipt.json')
    $private=Read-Bc2PrivatePinnedJson $b.private_artifacts_receipt (Join-Path $c.run 'private-x86/receipt.json')
    foreach($r in @($normal,$private)){
        Assert-Bc2Private ($r.schema -ceq 'fvr.native-batch-artifacts.v1' -and $r.source_commit -ceq $c.source_commit -and $r.source_sha256 -ceq $c.source_sha256 -and $r.runtime_default_enabled -ceq $false) 'artifact source/default differs'
        Assert-Bc2PrivateSamePin $r.config $b.native_batch_config
        foreach($field in 'normal','private_campaign_provisioning','runtime_default_enabled'){Assert-Bc2Private ($r[$field] -is [bool]) 'artifact flags must be booleans'}
        foreach($file in $r.files){$null=Assert-Bc2PrivatePin $file}
    }
    Assert-Bc2Private ($normal.normal -ceq $true -and $normal.private_campaign_provisioning -ceq $false -and $private.normal -ceq $false -and $private.private_campaign_provisioning -ceq $true) 'normal/private artifacts confused'
    Assert-Bc2Private (@($private.prerequisite_receipts).Count -eq 1 -and @($normal.prerequisite_receipts).Count -eq 2) 'exact prerequisites required'
    Assert-Bc2PrivateSamePin $private.prerequisite_receipts[0] $b.normal_artifacts_receipt
    foreach($arch in 'x86','x64'){
        $rows=@($normal.prerequisite_receipts|Where-Object {$_.path -ieq (Join-Path $c.run ('normal-'+$arch+'/receipt.json'))})
        Assert-Bc2Private ($rows.Count -eq 1) 'normal test receipt missing'
        $tested=Read-Bc2PrivatePinnedJson $rows[0]
        Assert-Bc2Private ($tested.schema -ceq 'fvr.native-batch-normal.v1' -and $tested.normal -ceq $true -and $tested.architecture -ceq $arch -and $tested.source_sha256 -ceq $c.source_sha256 -and $tested.source_commit -ceq $c.source_commit) 'normal test receipt differs'
        Assert-Bc2PrivateSamePin $tested.config $b.native_batch_config
        # Historical tested build/x86 outputs are intentionally superseded. Do not traverse tested.files.
    }
    $reset=Read-Bc2PrivatePinnedJson $b.shared_build_state (Join-Path $c.run 'private-x86/shared-build-state.json')
    Assert-Bc2Private ($reset.campaign_cache -ceq 'OFF' -and $reset.binary_kind -ceq 'private' -and $reset.normal_rebuild_performed -ceq $false -and $reset.normal_artifacts -ieq $b.normal_artifacts_receipt.path) 'shared OFF reset missing'
    $cache=Join-Path $c.repository 'build/x86/CMakeCache.txt';Assert-FvrNoLinks $cache
    $flag=@([IO.File]::ReadAllLines($cache)|Where-Object {$_ -cmatch '^FVR_BC2_CAMPAIGN_PROVISIONING:'})
    Assert-Bc2Private ($flag.Count -eq 1 -and $flag[0] -ceq 'FVR_BC2_CAMPAIGN_PROVISIONING:BOOL=OFF') 'shared cache must still be OFF; shared binaries remain private'
    $baselineRoot=Get-FvrChild $InstallRoot ('versions/'+$b.baseline_version+'/runtime')
    $baseline=Read-Bc2PrivatePinnedJson $b.installed_runtime_manifest (Join-Path $baselineRoot 'runtime.json')
    Assert-Bc2Private ($baseline.schema -ceq 'bc2vr.runtime.v1' -and $baseline.diagnostic_only -ceq $false -and $baseline.version -ceq $b.baseline_version) 'installed normal baseline differs'
    $active=Read-Bc2PrivatePinnedJson $b.installed_active (Join-Path $InstallRoot 'active.json')
    Assert-Bc2Private ($active.current -ceq $b.baseline_version) 'active installed baseline differs'
    $config=Read-Bc2PrivatePinnedJson $b.runtime_manifest (Join-Path $RuntimeRoot 'runtime.json')
    Assert-Bc2Private ($config.schema -ceq 'bc2vr.runtime.v1' -and $config.diagnostic_only -ceq $true -and $config.version -ceq ('diagnostic-campaign-'+$c.source_commit) -and $config.source_sha256 -ceq $c.source_sha256) 'distinct diagnostic manifest required'
    # Native/launcher identities describe this diagnostic composition, not its installed dependency baseline.
    Assert-Bc2Private ($config['native_source_commit'] -ceq $c.source_commit) 'native source commit differs'
    Assert-Bc2Private ($b['launcher_source_commit'] -is [string] -and $b['launcher_source_commit'] -cmatch '^[a-f0-9]{40}$' -and $config['launcher_source_commit'] -ceq $b['launcher_source_commit']) 'launcher source commit differs'
    Assert-Bc2Private ($config['native_version'] -ceq $config.version) 'native version differs'
    Assert-Bc2Private ($config['frozen_receipt_sha256'] -ceq $b.private_artifacts_receipt.sha256) 'private artifact receipt identity differs'
    Assert-Bc2Private ($config['normal_artifacts_receipt_sha256'] -ceq $b.normal_artifacts_receipt.sha256) 'normal artifact receipt identity differs'
    Assert-Bc2Private (-not $config.ContainsKey('package_source_commit')) 'diagnostic composition cannot claim package source commit'
    Assert-Bc2Private ($config['baseline_package_source_commit'] -ceq $baseline.package_source_commit) 'baseline package source commit differs'
    foreach($key in $baseline.Keys|Where-Object {$_ -cnotin @('version','source_sha256','diagnostic_only','files','native_source_commit','launcher_source_commit','package_source_commit','frozen_receipt_sha256','native_version')}){
        Assert-Bc2Private (($config[$key]|ConvertTo-Json -Depth 30 -Compress) -ceq ($baseline[$key]|ConvertTo-Json -Depth 30 -Compress)) ('baseline policy differs: '+$key)
    }
    $old=@{};foreach($file in $baseline.files){
        Assert-Bc2Private (-not $old.ContainsKey($file.path)) 'duplicate installed runtime file'
        $old[$file.path]=$file.sha256
        Assert-Bc2Private ((Get-Bc2PrivatePin (Get-FvrChild $baselineRoot $file.path)).sha256 -ceq $file.sha256) 'installed runtime file changed'
    }
    $expected=@{};foreach($key in $old.Keys){$expected[$key]=$old[$key]}
    $artifactNames=@{probe=@('x86','BC2NativeProbe.dll');trace=@('x86','BC2NativeTrace.exe');receiver=@('x64','BC2NativeIpcProbe.exe');host=@('x64','BC2XrHost.exe');resolution=@('x86','BC2NativeResolution.exe');resolution_bootstrap=@('x86','BC2ResolutionBootstrap.dll');graphics_creation32=@('x86','Bc2GraphicsCreation32.dll');graphics_creation64=@('x64','Bc2GraphicsCreation64.dll');provision_command=@('x86','Bc2CampaignProvisioningCommand.exe')}
    foreach($r in @($normal,$private)){
        $keys=@($r.artifacts|ForEach-Object {$_.key});$wanted=if($r.normal){@($artifactNames.Keys|Where-Object {$_ -ne 'provision_command'})}else{@('probe','provision_command')}
        Assert-Bc2Private ($keys.Count -eq $wanted.Count -and @($keys|Select-Object -Unique).Count -eq $keys.Count -and @($wanted|Where-Object {$_ -cnotin $keys}).Count -eq 0) 'artifact set differs'
        foreach($a in $r.artifacts){
            $spec=$artifactNames[$a.key]
            Assert-Bc2Private ($a.architecture -ceq $spec[0] -and [IO.Path]::GetFileName($a.binary.path) -ceq $spec[1] -and $a.pe.guid -ceq $a.symbols.guid -and $a.pe.age -eq $a.symbols.age) 'artifact architecture/symbol identity differs'
            $frozenDirectory=Join-Path $c.run $(if($r.normal){'normal-runtime'}else{'private-x86'})
            foreach($pin in @($a.binary,$a.pdb)){
                $null=Assert-Bc2PrivatePin $pin
                Assert-Bc2Private ((Split-Path $pin.path -Parent) -ieq $frozenDirectory -and @($r.files|Where-Object {$_.path -ieq $pin.path -and $_.sha256 -ceq $pin.sha256}).Count -eq 1) 'artifact outside frozen receipt files'
            }
            if(-not $r.normal -or $a.key -ne 'probe'){$expected['bin/'+$spec[1]]=$a.binary.sha256}
        }
    }
    Assert-Bc2Private ((@($private.artifacts|Where-Object key -eq 'probe')[0]).binary.sha256 -cne (@($normal.artifacts|Where-Object key -eq 'probe')[0]).binary.sha256) 'private probe is normal probe'
    # ROOT reviews only these launcher changes; dependencies remain exact installed bytes.
    $scriptNames=@('Entry.ps1','Session.ps1','Runtime.Core.ps1','PrivateSession.ps1')
    Assert-Bc2Private (@($b.reviewed_runtime_scripts).Count -eq $scriptNames.Count) 'exact reviewed launcher scripts required'
    foreach($name in $scriptNames){
        $rows=@($b.reviewed_runtime_scripts|Where-Object {$_.relative_path -ceq $name})
        Assert-Bc2Private ($rows.Count -eq 1) 'reviewed script missing/duplicated'
        $null=Assert-Bc2PrivatePin $rows[0].source (Join-Path $c.repository ('launcher/runtime/'+$name))
        $expected[$name]=$rows[0].source.sha256
    }
    $null=Assert-Bc2PrivatePin $b.reviewed_runner (Join-Path $c.repository 'tools/Invoke-Bc2XrGameSimulation.ps1')
    $seen=@{}
    Assert-Bc2Private (@($b.staged_app_files).Count -eq 2) 'exact app dependencies required'
    foreach($name in 'Launcher.Core.ps1','Launcher.Storage.ps1'){
        $app=Join-Path (Split-Path $RuntimeRoot -Parent) ('app/'+$name)
        $pins=@($b.staged_app_files|Where-Object {$_.path -ieq $app})
        Assert-Bc2Private ($pins.Count -eq 1) 'staged app dependency missing'
        $null=Assert-Bc2PrivatePin $pins[0] $app
        $baselineApp=Join-Path $b.install_root ('versions/'+$b.baseline_version+'/app/'+$name)
        Assert-Bc2Private ($pins[0].sha256 -ceq (Get-Bc2PrivatePin $baselineApp).sha256) 'staged app dependency differs from installed baseline'
    }
    foreach($file in $config.files){
        Assert-Bc2Private (-not $seen.ContainsKey($file.path) -and $expected.ContainsKey($file.path) -and $file.sha256 -ceq $expected[$file.path]) ('unreviewed runtime file: '+$file.path)
        $seen[$file.path]=$true
        Assert-Bc2Private ((Get-Bc2PrivatePin (Get-FvrChild $RuntimeRoot $file.path)).sha256 -ceq $file.sha256) ('staged file changed: '+$file.path)
    }
    Assert-Bc2Private ($seen.Count -eq $expected.Count) 'runtime file omitted'
    return @{binding=$bindingPin;specification=$b;config=$config;baseline=$baseline}
}
function Assert-Bc2PrivateMarkerBytes([string]$Path,[string]$Text) {
    Assert-FvrNoLinks $Path
    Assert-Bc2Private ([Convert]::ToHexString([IO.File]::ReadAllBytes($Path)) -ceq [Convert]::ToHexString([Text.Encoding]::ASCII.GetBytes($Text))) 'marker bytes differ'
}
function Assert-Bc2PrivateMarkers([string]$Folder,$Markers,[switch]$BeforeAttach) {
    foreach($item in @(@('campaign','campaign-provision.enable',"campaign-provision-v1`n"),@('mechanism','mechanism-snapshot.enable',"1`n"))){
        $null=Assert-Bc2PrivatePin $Markers[$item[0]] (Join-Path $Folder $item[1])
        Assert-Bc2PrivateMarkerBytes (Join-Path $Folder $item[1]) $item[2]
    }
    if($BeforeAttach){foreach($name in 'campaign-provision-request.bin','campaign-provision-source.bin','campaign-provision-receipt.bin','BC2NativeProbe.dll','host.stdout.log','native.stdout.log'){
        Assert-Bc2Private (-not(Test-Path -LiteralPath (Join-Path $Folder $name))) ('startup transport already exists: '+$name)
    }}
}
function New-Bc2PrivateMarkers([string]$Folder) {
    Assert-FvrNoLinks $Folder
    Assert-Bc2Private (@(Get-ChildItem -LiteralPath $Folder -Force).Count -eq 0) 'new empty owned session folder required'
    $markers=@{}
    foreach($item in @(@('campaign','campaign-provision.enable',"campaign-provision-v1`n"),@('mechanism','mechanism-snapshot.enable',"1`n"))){
        $path=Join-Path $Folder $item[1]
        $stream=[IO.File]::Open($path,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::None)
        try{$bytes=[Text.Encoding]::ASCII.GetBytes($item[2]);$stream.Write($bytes,0,$bytes.Length);$stream.Flush($true)}finally{$stream.Dispose()}
        $markers[$item[0]]=Get-Bc2PrivatePin $path
    }
    Assert-Bc2PrivateMarkers $Folder $markers -BeforeAttach
    return $markers
}
