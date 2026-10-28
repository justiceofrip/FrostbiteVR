[CmdletBinding()]
param([Parameter(Mandatory)][string]$Plan,[Parameter(Mandatory)][string]$PlanSha256)
$ErrorActionPreference='Stop';$PSNativeCommandUseErrorActionPreference=$false
$repo='G:\Unity\VRMMO\FrostbiteVR';$run=Join-Path $repo 'reports/coordinator-20261028-batch01/storage01'
$token='campaign-coordinator-20261028-r1-batch01-70571f7dcb734b658814f1d2a17765c1'
function Sha($p){(Get-FileHash -LiteralPath $p -Algorithm SHA256).Hash.ToLowerInvariant()}
function Idle {
 if((Get-Content 'G:\Unity\VRMMO\bc2vr-task-worktrees\INTEGRATION.lock' -Raw|ConvertFrom-Json).owner_token -ne $token){throw 'Lease not owned'}
 if(Get-Process BFBC2Game,BC2XrHost,BC2NativeTrace,BC2NativeIpcProbe,cl,link,cmake,ninja,ctest,MSBuild -ErrorAction SilentlyContinue){throw 'Shared resources active'}
}
function Exact($p){
 $full=[IO.Path]::GetFullPath($p)
 if($full -cne $p -or (Resolve-Path -LiteralPath $full).ProviderPath -ine $full){throw 'Noncanonical input'}
 $allowed=@((Join-Path $repo 'build\x86\'),(Join-Path $repo 'build\x64\'),(Join-Path $repo 'reports\integration-chat25-chat26-20261028-01\normal-runtime\'))
 if(-not @($allowed|Where-Object {$full.StartsWith($_,[StringComparison]::OrdinalIgnoreCase)}).Count){throw 'Out-of-scope input'}
 $file=Get-Item -LiteralPath $full -Force
 if($file.PSIsContainer -or $file.LinkType -or $file.Extension -notin @('.pdb','.obj','.lib','.exe','.dll','.ilk')){throw 'Not a reviewed generated file'}
 for($parent=$file.Directory;$parent;$parent=$parent.Parent){if($parent.Attributes -band [IO.FileAttributes]::ReparsePoint){throw 'Linked parent'}}
 $null=Get-FvrFileAllocatedBytes -File $file
 return $file
}
$env:TEMP=Join-Path $repo 'build/scratch';$env:TMP=$env:TEMP;$env:TMPDIR=$env:TEMP
. (Join-Path $repo 'tools/StorageBudget.ps1')
Idle
if((Test-Path -LiteralPath $run) -or (Get-Item -LiteralPath $Plan).Length -gt 1MB -or (Sha $Plan) -cne $PlanSha256){throw 'Fresh output and pinned plan required'}
$planData=Get-Content -LiteralPath $Plan -Raw|ConvertFrom-Json
if($planData.files.Count -gt 80 -or $planData.input_bytes -gt 2GB){throw 'Plan bound exceeded'}
$seen=[Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase);$total=0L
foreach($row in $planData.files){$f=Exact $row.path;if(-not $seen.Add($f.FullName) -or $f.Length -ne $row.bytes -or $f.Length -gt 512MB -or (Sha $f.FullName) -cne $row.sha256){throw 'Input changed'};$total+=$f.Length}
if($total -ne $planData.input_bytes){throw 'Plan size mismatch'}
$admission=Assert-FvrStorageBudget -RepositoryRoot $repo -OutputPath $run -Operation Capture -RawCaptureBytes 0 -PlannedBytes 16MB -AdditionalRoots 'G:\Unity\VRMMO\bc2vr-task-worktrees'
[void][IO.Directory]::CreateDirectory($run)
[IO.File]::WriteAllText((Join-Path $run 'storage.json'),($admission|ConvertTo-Json -Depth 30),[Text.UTF8Encoding]::new($false))
$receipt=[ordered]@{schema='fvr.lossless-generated-compression.v1';plan=$Plan;plan_sha256=$PlanSha256;completed=$false;failure=$null;rows=@();reclaimed_bytes=0L;deleted_files=0;content_changes=0;storage_limits_changed=$false}
function Save {[IO.File]::WriteAllText((Join-Path $run 'compression.json'),($receipt|ConvertTo-Json -Depth 15),[Text.UTF8Encoding]::new($false))}
try {foreach($item in $planData.files){
 Idle;$file=Exact $item.path;$before=Get-FvrFileAllocatedBytes -File $file
 if($before -lt .9*$file.Length){continue}
 if($file.Length -ne $item.bytes -or (Sha $file.FullName) -cne $item.sha256){throw 'Input changed before compression'}
 $row=[ordered]@{path=$file.FullName;bytes=$file.Length;sha256_before=$item.sha256;sha256_after=$null;allocated_before=$before;allocated_after=$null;content_equal=$false;compact_exit=$null}
 $receipt.rows+=,$row;Save
 & (Join-Path $env:SystemRoot 'System32/compact.exe') /C /EXE:LZX /Q $file.FullName *> (Join-Path $run 'compact-last.log')
 $row.compact_exit=$LASTEXITCODE;$after=Exact $file.FullName;$row.sha256_after=Sha $after.FullName;$row.allocated_after=Get-FvrFileAllocatedBytes -File $after
 $row.content_equal=($after.Length -eq $row.bytes -and $row.sha256_after -ceq $row.sha256_before)
 if(-not $row.content_equal){$receipt.content_changes++;throw 'Lossless verification failed'}
 $receipt.reclaimed_bytes+=$before-$row.allocated_after;Save
 if($row.compact_exit -ne 0){throw 'Compression failed; partial verified receipt retained'}
};$receipt.completed=$true}catch{$receipt.failure=$_.Exception.Message;throw}finally{Save;[pscustomobject]@{files=$receipt.rows.Count;reclaimed_bytes=$receipt.reclaimed_bytes;completed=$receipt.completed}|ConvertTo-Json -Compress}
