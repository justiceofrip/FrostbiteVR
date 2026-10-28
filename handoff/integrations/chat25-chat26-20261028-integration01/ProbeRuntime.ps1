#requires -Version 7.5
[CmdletBinding()]
param(
    [Parameter(Mandatory)][ValidatePattern('^[0-9a-fA-F]{64}$')][string]$ReceiptSha256
)
$ErrorActionPreference='Stop'
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../..'))
$integration=Join-Path $repo 'reports/integration-chat25-chat26-20261028-01'
$run=Join-Path $integration 'xr-preflight01'
$runtime=Join-Path $integration 'normal-runtime'
$receiptPath=Join-Path $runtime 'build-receipt.json'
$launcher='G:\Unity\VRMMO\BC2VR-Launcher'
$leasePath='G:\Unity\VRMMO\bc2vr-task-worktrees\INTEGRATION.lock'
$leaseToken='chat25-chat26-20261028-integration01-12ad90001f4a4986aee8b08654985b14'
$settingsPath=Join-Path $launcher 'data/settings.json'
$activePath=Join-Path $launcher 'active.json'
$loaderPath=Join-Path $repo 'build/x64/runtime/openxr/win64/openxr_loader.dll'
$loaderSha='a69729a3348bc4dcb9be126a49d594074e477be7312f65180b0d082bf100fa5f'
$builtCommit='7a5d8aa7281de90e877b35b8ac73d145191161a6'
$settingsSha='be6f984f6bd44dcc62aab0a2ac1a63acaa2d3221d2919f2a44a4aeeef2b1f7cf'
$activeSha='9b60ff5ce16e3c146a56d5410edbd43b9f91e393ab7851af41d9c6f3b800a8b2'
$fallbackRoot=Join-Path $repo 'reports/integration-chat04-20261028-01/normal-runtime'
$fallbackReceiptSha='f4fdcb64ef2fc1e539db12bf97dee6f338bc66719b92ae7d04e1f50ea0556950'
$binaryNames=@{probe='BC2NativeProbe.dll';trace='BC2NativeTrace.exe';receiver='BC2NativeIpcProbe.exe';host='BC2XrHost.exe';resolution='BC2NativeResolution.exe';resolution_bootstrap='BC2ResolutionBootstrap.dll'}

function Assert-NoLinks([string]$Path,[bool]$AllowCompressedFile=$false) {
    $next=[IO.Path]::GetFullPath($Path)
    $first=$true
    while($next){
        if((Test-Path -LiteralPath $next) -and ((Get-Item -LiteralPath $next -Force).Attributes -band [IO.FileAttributes]::ReparsePoint)){
            $file=Get-Item -LiteralPath $next -Force
            if($first -and $AllowCompressedFile -and -not $file.PSIsContainer -and
               (Get-Command Get-FvrFileAllocatedBytes -ErrorAction SilentlyContinue)){
                # Only the storage guard's verified WOF file tag is accepted.
                # Compressed fallback symbols retain their original content.
                $null=Get-FvrFileAllocatedBytes -File $file
            } else {throw "Linked path refused: $next"}
        }
        $first=$false
        $next=Split-Path -Parent $next
    }
}
function Read-Json([string]$Path) {
    Assert-NoLinks $Path $true
    if((Get-Item -LiteralPath $Path).Length -gt 4MB){throw "Oversized JSON refused: $Path"}
    return Get-Content -LiteralPath $Path -Raw | ConvertFrom-Json -AsHashtable
}
function Pin([string]$Path) {
    Assert-NoLinks $Path $true
    $file=Get-Item -LiteralPath $Path
    if($file.PSIsContainer){throw "Expected file: $Path"}
    return @{path=$file.FullName;bytes=$file.Length;sha256=(Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()}
}
function Check-Pin($Expected) {
    $actual=Pin $Expected.path
    if($actual.sha256 -cne $Expected.sha256.ToLowerInvariant() -or
       ($Expected.ContainsKey('bytes') -and $actual.bytes -ne $Expected.bytes)){throw "Pinned file changed: $($Expected.path)"}
    return $actual
}
function Save([string]$Path,$Value) {
    if(Test-Path -LiteralPath $Path){throw "Preserve existing evidence: $Path"}
    [IO.File]::WriteAllText($Path,($Value | ConvertTo-Json -Depth 40),[Text.UTF8Encoding]::new($false))
}
function Assert-Lease {
    if((Read-Json $leasePath).owner_token -cne $leaseToken){throw 'Integration lease not owned'}
}
function Assert-Idle {
    $busy=@(Get-Process cl,link,cmake,ninja,ctest,MSBuild,BFBC2Game,BC2NativeTrace,BC2NativeIpcProbe,BC2XrHost,BC2NativeResolution -ErrorAction SilentlyContinue)
    if($busy.Count){throw ('Shared build/session resources not idle: '+(($busy | ForEach-Object {$_.ProcessName+':'+$_.Id}) -join ', '))}
    # Catch an installed session supervisor before it has started native helpers.
    $sessionPath=Join-Path $launcher 'data/runtime/active-session.json'
    if(Test-Path -LiteralPath $sessionPath){
        $session=Read-Json $sessionPath
        if($session.supervisor){
            $process=Get-Process -Id ([int]$session.supervisor.pid) -ErrorAction SilentlyContinue
            if($process){
                try {
                    if($process.StartTime.ToUniversalTime() -eq [DateTime]::Parse($session.supervisor.start_utc).ToUniversalTime() -and
                       $process.Path -ieq $session.supervisor.path){throw 'Installed session supervisor is still running'}
                } finally {$process.Dispose()}
            }
        }
    }
}
function Snapshot-Launcher([string]$Phase) {
    $pins=@{}
    foreach($entry in @(@{name='settings';path=$settingsPath},@{name='active';path=$activePath})){
        $pin=Pin $entry.path
        $destination=Join-Path $run ($entry.name+'-'+$Phase+'.json')
        if(Test-Path -LiteralPath $destination){throw "Preserve snapshot: $destination"}
        [IO.File]::WriteAllBytes($destination,[IO.File]::ReadAllBytes($entry.path))
        if((Pin $destination).sha256 -cne $pin.sha256){throw 'Launcher snapshot changed while reading'}
        $null=Check-Pin $pin
        $pins[$entry.name]=$pin
    }
    return $pins
}
function Check-RuntimePairs($Receipt,[string]$Root) {
    if($Receipt.binaries.Count -ne 6 -or $Receipt.symbols_by_binary.Count -ne 6){throw 'Expected six complete binary/PDB pairs'}
    $pins=@()
    foreach($key in $binaryNames.Keys){
        foreach($kind in @('binaries','symbols_by_binary')){
            $name=if($kind -ceq 'binaries'){$binaryNames[$key]}else{[IO.Path]::ChangeExtension($binaryNames[$key],'.pdb')}
            $expected=[IO.Path]::GetFullPath((Join-Path $Root $name))
            $entry=$Receipt[$kind][$key]
            if(-not $entry.path -or [IO.Path]::GetFullPath($entry.path) -ine $expected){throw "Runtime file outside exact frozen location: $key/$kind"}
            $pins+=Check-Pin $entry
        }
    }
    return $pins
}
function Pin-InstalledPayloads {
    # Preserve the actual active and previous install's package and executable
    # content. This check does not stage, update, or switch either version.
    $pins=@()
    foreach($version in @('0.1.12','0.1.11')){
        $root=Join-Path $launcher "versions/$version"
        $expected=if($version -ceq '0.1.12'){'e03ec23b6ee6e0ae1b56ce930cfdc6b7e7dee4921a131d2c0ccf9d5eb1c3ec3b'}else{'a45c27aedd3e29f9a488f00bd0b3ff9f2cd3b1c2c11d7e2752854a8f82b5c3cb'}
        $packagePath=Join-Path $root 'package.json'
        $pins+=Check-Pin @{path=$packagePath;sha256=$expected}
        $package=Read-Json $packagePath
        if($package.runtime_version -cne $version -or $package.files.Count -gt 512){throw 'Installed package identity changed'}
        foreach($name in @('BC2NativeProbe.dll','BC2NativeTrace.exe','BC2XrHost.exe','openxr_loader.dll')){
            $relative='runtime/bin/'+$name
            $entries=@($package.files | Where-Object {$_.path -ceq $relative})
            if($entries.Count -ne 1){throw "Installed executable manifest entry missing: $version/$name"}
            $pins+=Check-Pin @{path=(Join-Path $root $relative);sha256=$entries[0].sha256}
        }
    }
    return $pins
}
function Select-OpenXr {
    # Launcher settings currently specify the game and optional local backend.
    # Match launcher/runtime/Startup.ps1; do not invent an OpenXR settings key.
    $manifest=$env:XR_RUNTIME_JSON
    $source='XR_RUNTIME_JSON override'
    if(-not $manifest){
        $source='Windows active OpenXR runtime (HKLM64)'
        $base=[Microsoft.Win32.RegistryKey]::OpenBaseKey([Microsoft.Win32.RegistryHive]::LocalMachine,[Microsoft.Win32.RegistryView]::Registry64)
        try {
            $key=$base.OpenSubKey('SOFTWARE\Khronos\OpenXR\1')
            try {if($key){$manifest=[string]$key.GetValue('ActiveRuntime')}} finally {if($key){$key.Dispose()}}
        } finally {$base.Dispose()}
    }
    if(-not $manifest -or -not [IO.Path]::IsPathFullyQualified($manifest)){throw 'No absolute active OpenXR runtime manifest is configured'}
    $manifestPin=Pin $manifest
    $description=Read-Json $manifest
    $library=[string]$description.runtime.library_path
    if(-not $library){throw 'Selected OpenXR runtime has no library_path'}
    if(-not [IO.Path]::IsPathFullyQualified($library)){$library=[IO.Path]::GetFullPath((Join-Path (Split-Path $manifest -Parent) $library))}
    return @{source=$source;manifest=$manifestPin;library=(Pin $library);label=$description.runtime.name}
}
function Quote-Argument([string]$Value) {
    if($Value.Contains([char]0)){throw 'NUL in argument'}
    return '"'+[regex]::Replace([regex]::Replace($Value,'(\\*)"','$1$1\"'),'(\\+)$','$1$1')+'"'
}

Assert-Lease
Assert-Idle
Assert-NoLinks $run
if(Test-Path -LiteralPath $run){throw 'Preserve existing xr-preflight01 evidence'}
. (Join-Path $repo 'tools/StorageBudget.ps1')
$admission=Assert-FvrStorageBudget -RepositoryRoot $repo -OutputPath $run -Operation Session -PlannedBytes 640MB -RawCaptureBytes 0 -MaxGeneratedBytes 40GB -MinFreeBytes 15GB -AdditionalRoots @('G:\Unity\VRMMO\bc2vr-task-worktrees')
[void][IO.Directory]::CreateDirectory($run)
Save (Join-Path $run 'storage-admission.json') $admission
$env:TEMP=Join-Path $repo 'build/scratch';$env:TMP=$env:TEMP;$env:TMPDIR=$env:TEMP
$report=@{
    schema='fvr.chat25-chat26.xr-preflight.v1';started_utc=[DateTime]::UtcNow.ToString('o');completed_utc=$null
    lease_token=$leaseToken;expected_receipt_sha256=$ReceiptSha256.ToLowerInvariant();timeout_ms=45000
    available=$false;exit_code=$null;timed_out=$false;owned_process=$null;owned_host_stopped=$false
    error=$null;launcher_before=$null;launcher_after=$null;launcher_unchanged=$false
    build_receipt=$null;host=$null;loader=$null;selection=$null;probe_result=$null
    built_source_commit=$builtCommit;candidate_files=@();fallback_files=@();installed_files=@()
    candidate_payload_verified=$false;fallback_payload_verified=$false;installed_payloads_unchanged=$false
    no_game=$true;no_install=$true;game_started=$false;installed=$false;native_bc2_verified=$false
    native_acceptance=$false;headset_verified=$false;equipment_smoke_completed=$false
    limitation='OpenXR instance/system availability only; no game, native attachment, XR session or equipment smoke'
    storage=(Pin (Join-Path $run 'storage-admission.json'))
}
$owned=$null
try {
    $report.launcher_before=Snapshot-Launcher 'before'
    $null=Check-Pin @{path=$settingsPath;sha256=$settingsSha}
    $null=Check-Pin @{path=$activePath;sha256=$activeSha}
    $settings=Read-Json $settingsPath
    $active=Read-Json $activePath
    if(-not $settings.game_path -or -not [IO.Path]::IsPathFullyQualified($settings.game_path)){throw 'Launcher has no absolute game_path'}
    if($active.current -cne '0.1.12' -or $active.previous -cne '0.1.11'){throw 'Installed active/fallback version changed'}
    $report.launcher_version=$active.current
    $report.launcher_previous_version=$active.previous
    $report.installed_files=@(Pin-InstalledPayloads)
    $report.configured_game_path=$settings.game_path
    $report.build_receipt=Check-Pin @{path=$receiptPath;sha256=$ReceiptSha256}
    $receipt=Read-Json $receiptPath
    if($receipt.schema -cne 'fvr.checkpoint436.normal-build.v1' -or $receipt.normal -ne $true -or
       $receipt.full_normal_builds -ne $true -or $receipt.diagnostic_only -ne $false -or
       $receipt.diagnostic_kind -cne 'normal' -or $receipt.operation_records_changed -ne $false -or
       $receipt.built_source_commit -cne $builtCommit){throw 'Expected exact frozen normal candidate receipt'}
    $report.candidate_files=@(Check-RuntimePairs $receipt $runtime)
    $report.candidate_payload_verified=$true
    $report.source_freeze=Check-Pin $receipt.source_freeze
    $sourceFreeze=Read-Json $receipt.source_freeze.path
    if($sourceFreeze.source_commit -cne $builtCommit -or $sourceFreeze.source_sha256 -cne $receipt.source_sha256){throw 'Candidate source freeze mismatch'}
    $report.symbol_audit=Check-Pin $receipt.symbol_audit
    $symbolAudit=Read-Json $receipt.symbol_audit.path
    if($symbolAudit.passed -ne $true -or $symbolAudit.pairs.Count -ne 6){throw 'Candidate symbol audit incomplete'}
    $report.fallback_receipt=Check-Pin @{path=(Join-Path $fallbackRoot 'build-receipt.json');sha256=$fallbackReceiptSha}
    $fallbackReceipt=Read-Json $report.fallback_receipt.path
    $report.fallback_files=@(Check-RuntimePairs $fallbackReceipt $fallbackRoot)
    $report.fallback_payload_verified=$true
    $expectedHost=[IO.Path]::GetFullPath((Join-Path $runtime 'BC2XrHost.exe'))
    if([IO.Path]::GetFullPath($receipt.binaries.host.path) -ine $expectedHost){throw 'Host is outside the frozen candidate runtime'}
    $report.host=Check-Pin $receipt.binaries.host
    $report.loader=Check-Pin @{path=$loaderPath;sha256=$loaderSha}
    $report.selection=Select-OpenXr
    Assert-Lease
    Assert-Idle
    foreach($pin in @($report.build_receipt,$report.host,$report.loader,$report.selection.manifest,$report.selection.library,$report.launcher_before.settings,$report.launcher_before.active)){$null=Check-Pin $pin}
    $arguments=@('--probe','--loader',$loaderPath) | ForEach-Object {Quote-Argument $_}
    $owned=Start-Process -FilePath $expectedHost -ArgumentList ($arguments -join ' ') -WorkingDirectory $run -WindowStyle Hidden -PassThru -Environment @{XR_RUNTIME_JSON=$report.selection.manifest.path;TEMP=$env:TEMP;TMP=$env:TMP;TMPDIR=$env:TMPDIR} -RedirectStandardOutput (Join-Path $run 'xr-preflight.stdout.log') -RedirectStandardError (Join-Path $run 'xr-preflight.stderr.log')
    $null=$owned.Handle
    $report.owned_process=@{pid=$owned.Id;start_utc=$owned.StartTime.ToUniversalTime().ToString('o');path=$expectedHost}
    if(-not $owned.WaitForExit(45000)){
        $report.timed_out=$true
        # The retained Process handle identifies only this invocation, never a
        # name search, PID replacement, runtime service, or descendant tree.
        $owned.Kill()
        $report.owned_host_stopped=$owned.WaitForExit(3000)
        throw 'OpenXR host probe timed out after 45 seconds'
    }
    $report.owned_host_stopped=$true
    $report.exit_code=$owned.ExitCode
    $report.probe_result=Read-Json (Join-Path $run 'xr-preflight.stdout.log')
    if($report.exit_code -ne 0 -or $report.probe_result.instance_created -ne $true -or $report.probe_result.system_available -ne $true){throw ('OpenXR unavailable: '+$report.probe_result.error)}
    if($report.probe_result.session_created -ne $false){throw 'Probe unexpectedly created an XR session'}
    $report.available=$true
} catch {
    $report.error=$_.Exception.Message
    $report.available=$false
} finally {
    if($owned){
        try {
            if(-not $owned.HasExited){$owned.Kill();$report.owned_host_stopped=$owned.WaitForExit(3000)}
            else {$report.owned_host_stopped=$true}
        } catch {$report.error=($report.error+'; Owned host cleanup: '+$_.Exception.Message);$report.available=$false}
        finally {$owned.Dispose()}
    }
    try {
        $report.launcher_after=Snapshot-Launcher 'after'
        if($report.launcher_before){
            $report.launcher_unchanged=$report.launcher_before.settings.sha256 -ceq $report.launcher_after.settings.sha256 -and $report.launcher_before.active.sha256 -ceq $report.launcher_after.active.sha256
        }
        if(-not $report.launcher_unchanged){throw 'Launcher settings or active version changed during preflight'}
        foreach($pin in @($report.candidate_files)+@($report.fallback_files)+@($report.installed_files)){$null=Check-Pin $pin}
        $report.installed_payloads_unchanged=$report.installed_files.Count -eq 10
        foreach($pin in @($report.source_freeze,$report.symbol_audit,$report.fallback_receipt)){if($pin){$null=Check-Pin $pin}}
        foreach($pin in @($report.build_receipt,$report.host,$report.loader,$report.selection.manifest,$report.selection.library)){
            if($pin){$null=Check-Pin $pin}
        }
        Assert-Lease
        Assert-Idle
    } catch {$report.error=($report.error+'; Postflight: '+$_.Exception.Message);$report.available=$false}
    $report.completed_utc=[DateTime]::UtcNow.ToString('o')
    $report.logs=@(foreach($name in @('xr-preflight.stdout.log','xr-preflight.stderr.log')){if(Test-Path -LiteralPath (Join-Path $run $name)){Pin (Join-Path $run $name)}})
    Save (Join-Path $run 'preflight-receipt.json') $report
}
Write-Output ($report | ConvertTo-Json -Depth 8)
if(-not $report.available){exit 1}
