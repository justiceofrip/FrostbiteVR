#requires -Version 7.5
[CmdletBinding()]
param([Parameter(Mandatory)][string]$Plan,
      [Parameter(Mandatory)][ValidatePattern('^[a-f0-9]{64}$')][string]$PlanSha256,
      [Parameter(Mandatory)][ValidatePattern('^batch[0-9]+$')][string]$Batch)
$ErrorActionPreference='Stop';$PSNativeCommandUseErrorActionPreference=$false
$repo='G:\Unity\VRMMO\FrostbiteVR'
$output=Join-Path $repo "reports/integration-chat25-chat26-20261028-01/storage/$Batch"
function Sha($path){(Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant()}
function Idle {
    $lease=Get-Content -LiteralPath 'G:\Unity\VRMMO\bc2vr-task-worktrees\INTEGRATION.lock' -Raw|ConvertFrom-Json
    if($lease.owner_token -cne 'chat25-chat26-20261028-integration01-12ad90001f4a4986aee8b08654985b14'){throw 'Integration lease not owned'}
    if(Get-Process BFBC2Game,BC2XrHost,BC2NativeTrace,BC2NativeIpcProbe,cl,link,cmake,ninja,ctest,MSBuild -ErrorAction SilentlyContinue){throw 'Shared resources active'}
}
function ExactFile($path,[bool]$compressed=$false){
    $full=[IO.Path]::GetFullPath($path)
    if($full -cne $path -or -not $full.StartsWith('G:\Unity\VRMMO\',[StringComparison]::OrdinalIgnoreCase)){throw 'Noncanonical/non-workspace path'}
    if((Resolve-Path -LiteralPath $full).ProviderPath -ine $full){throw 'Resolved path changed'}
    $file=Get-Item -LiteralPath $full -Force
    if($file.PSIsContainer -or $file.LinkType){throw 'Expected ordinary exact file'}
    for($parent=$file.Directory;$parent;$parent=$parent.Parent){
        if($parent.Attributes -band [IO.FileAttributes]::ReparsePoint){throw 'Linked parent refused'}
    }
    if($file.Attributes -band [IO.FileAttributes]::ReparsePoint){
        if(-not $compressed){throw 'Initial reparse file refused'}
        $null=Get-FvrFileAllocatedBytes -File $file # Only verified WOF accepted.
    }
    return $file
}
if((Test-Path -LiteralPath $output) -or (Get-Item -LiteralPath $Plan).Length -gt 1MB -or (Sha $Plan) -cne $PlanSha256){throw 'Fresh output and exact bounded plan required'}
$planData=Get-Content -LiteralPath $Plan -Raw|ConvertFrom-Json
if($planData.schema -cne 'fvr.lossless-generated-compression-plan.v1' -or $planData.files.Count -gt 1000 -or
   $planData.maximum_input_bytes -ne 3GB -or $planData.target_reclaimed_bytes -ne 1GB){throw 'Unexpected plan limits'}
$roots=@('G:\Unity\VRMMO\FrostbiteVR\build','G:\Unity\VRMMO\FrostbiteVR\reports\integration-chat04-20261028-01',
    'G:\Unity\VRMMO\bc2vr-recovery')
$seen=[Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
$total=0L;Idle
foreach($row in $planData.files){
    $file=ExactFile $row.path
    if(-not $seen.Add($file.FullName) -or -not @($roots|Where-Object {$file.FullName.StartsWith($_+'\',[StringComparison]::OrdinalIgnoreCase)}).Count){throw 'Duplicate/out-of-scope file'}
    if($file.Extension.ToLowerInvariant() -notin @('.pdb','.obj','.lib','.ilk','.exe','.dll','.json','.jsonl','.log')){throw 'Not an approved generated file type'}
    if($file.Length -lt 4096 -or $file.Length -gt 512MB -or $file.Length -ne $row.bytes -or
       [string]$file.LastWriteTimeUtc.ToFileTimeUtc() -cne [string]$row.mtime_filetime -or
       $row.sha256 -notmatch '^[a-f0-9]{64}$'){throw 'Changed or unpinned input'}
    $total+=$file.Length
}
if($total -ne $planData.input_bytes -or $total -gt 3GB){throw 'Input exceeds exact plan bound'}
. (Join-Path $repo 'tools/StorageBudget.ps1')
$admission=Assert-FvrStorageBudget -RepositoryRoot $repo -OutputPath $output -Operation Capture -PlannedBytes 16MB -RawCaptureBytes 0 -AdditionalRoots 'G:\Unity\VRMMO\bc2vr-task-worktrees'
$null=[IO.Directory]::CreateDirectory($output)
[IO.File]::WriteAllText((Join-Path $output 'storage.json'),($admission|ConvertTo-Json -Depth 20),[Text.UTF8Encoding]::new($false))
$receipt=[ordered]@{schema='fvr.lossless-generated-compression.v1';plan=$Plan;plan_sha256=$PlanSha256;recipe_sha256=(Sha $PSCommandPath);
    started_utc=[DateTime]::UtcNow.ToString('o');completed=$false;failure=$null;rows=@();input_bytes=0L;reclaimed_bytes=0L;
    deleted_files=0;content_changes=0;storage_limits_changed=$false}
function SaveReceipt {[IO.File]::WriteAllText((Join-Path $output 'compression.json'),($receipt|ConvertTo-Json -Depth 12),[Text.UTF8Encoding]::new($false))}
SaveReceipt
try {
    foreach($item in $planData.files){
        if($receipt.reclaimed_bytes -ge 1GB){break}
        Idle;$file=ExactFile $item.path
        if($file.Length -ne $item.bytes -or [string]$file.LastWriteTimeUtc.ToFileTimeUtc() -cne [string]$item.mtime_filetime){throw 'Input changed after admission'}
        $before=Get-FvrFileAllocatedBytes -File $file
        if($before -lt .95*$file.Length){continue}
        $hash=Sha $file.FullName
        if($hash -cne $item.sha256){throw 'Content differs from reviewed plan'}
        $row=[ordered]@{path=$file.FullName;bytes=$file.Length;sha256_before=$hash;sha256_after=$null;
            allocated_before=$before;allocated_after=$null;compact_exit=$null;content_equal=$false}
        $receipt.rows+=,$row;$receipt.input_bytes+=$file.Length;SaveReceipt
        & (Join-Path $env:SystemRoot 'System32/compact.exe') /C /EXE:LZX /Q $file.FullName *> (Join-Path $output 'compact-last.log')
        $row.compact_exit=$LASTEXITCODE
        $after=ExactFile $file.FullName $true
        $row.sha256_after=Sha $after.FullName;$row.allocated_after=Get-FvrFileAllocatedBytes -File $after
        $row.content_equal=($after.Length -eq $row.bytes -and $row.sha256_after -ceq $hash)
        if(-not $row.content_equal){$receipt.content_changes++;throw 'Lossless comparison failed'}
        $receipt.reclaimed_bytes+=$before-$row.allocated_after;SaveReceipt
        if($row.compact_exit -ne 0){throw 'Compact failed; verified partial outcome retained'}
    }
    $receipt.completed=$true
}catch{$receipt.failure=$_.Exception.Message;throw}finally{
    $receipt.finished_utc=[DateTime]::UtcNow.ToString('o');SaveReceipt
    [pscustomobject]@{output=$output;completed=$receipt.completed;files=$receipt.rows.Count;reclaimed_bytes=$receipt.reclaimed_bytes}|ConvertTo-Json -Compress
}
