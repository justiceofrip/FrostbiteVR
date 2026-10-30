param([string]$Attempt='01')
$ErrorActionPreference='Stop';$PSNativeCommandUseErrorActionPreference=$false
$repo='G:\Unity\VRMMO\FrostbiteVR';$run=Join-Path $repo ('reports/coordinator-20261030-headset52/storage'+$Attempt)
. (Join-Path $repo 'tools/StorageBudget.ps1')
$lease=Get-Content (Join-Path $repo 'reports/coordinator-20261030-headset52/leases.json') -Raw|ConvertFrom-Json
function Idle {if((Get-Content $lease.INTEGRATION.path -Raw|ConvertFrom-Json).owner_token -ne $lease.INTEGRATION.owner_token){throw 'Lease changed'};if(Get-Process cl,link,ninja,cmake,ctest,BFBC2Game,BC2XrHost,BC2NativeTrace -ErrorAction SilentlyContinue){throw 'Shared resources active'}}
function Sha($p){(Get-FileHash -LiteralPath $p).Hash.ToLowerInvariant()}
Idle
$budget=Assert-FvrStorageBudget -RepositoryRoot $repo -OutputPath $run -Operation Build -PlannedBytes 16MB -AdditionalRoots @('G:/Unity/VRMMO/bc2vr-task-worktrees','G:/Unity/VRMMO/BC2VR-Launcher')
if(Test-Path -LiteralPath $run){throw 'Fresh output required'};$null=[IO.Directory]::CreateDirectory($run)
[IO.File]::WriteAllText((Join-Path $run 'storage.json'),($budget|ConvertTo-Json -Depth 20),[Text.UTF8Encoding]::new($false))
$root='G:\Unity\VRMMO\bc2vr-recovery'
$paths=@(& rg --files --no-ignore $root -g '*.pdb' -g '*.lib' -g '*.exe' -g '*.dll' -g '*.fvrprop' -g '*.fvrmat' -g '*.bin')
if($LASTEXITCODE){throw 'Artifact enumeration failed'}
$files=@($paths|ForEach-Object {Get-Item -LiteralPath $_}|Where-Object {$_.Length -ge 1MB -and $_.Length -le 512MB}|Sort-Object Length -Descending)
$plan=@();$total=0L
$done=[Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
foreach($previous in Get-ChildItem -LiteralPath (Split-Path $run -Parent) -Directory -Filter 'storage*'){
 $receipt=Join-Path $previous.FullName 'result.json'
 if(Test-Path -LiteralPath $receipt){foreach($row in (Get-Content -LiteralPath $receipt -Raw|ConvertFrom-Json).files){$null=$done.Add($row.path)}}
}
foreach($f in $files){
 if($done.Contains($f.FullName)){continue}
 if(-not $f.FullName.StartsWith($root+'\',[StringComparison]::OrdinalIgnoreCase) -or $f.LinkType){throw 'Out of scope'}
 if($total+$f.Length -gt 2GB -or $plan.Count -ge 300){continue}
 $allocated=Get-FvrFileAllocatedBytes -File $f
 if($allocated -lt .9*$f.Length){continue}
 $plan+=@{path=$f.FullName;bytes=$f.Length;allocated_before=$allocated;sha256=Sha $f.FullName};$total+=$f.Length
}
[IO.File]::WriteAllText((Join-Path $run 'plan.json'),(@{files=$plan;input_bytes=$total}|ConvertTo-Json -Depth 6),[Text.UTF8Encoding]::new($false))
$result=@{files=@();reclaimed_bytes=0L;deleted_files=0;content_changes=0}
foreach($row in $plan){
 Idle;if((Sha $row.path) -ne $row.sha256){throw 'Input changed'}
 & "$env:SystemRoot\System32\compact.exe" /C /EXE:LZX /Q $row.path *> (Join-Path $run 'compact-last.log')
 if($LASTEXITCODE){throw 'Compression failed'}
 $f=Get-Item -LiteralPath $row.path;$after=Get-FvrFileAllocatedBytes -File $f
 if($f.Length -ne $row.bytes -or (Sha $row.path) -ne $row.sha256){throw 'Content verification failed'}
 $result.files+=@{path=$row.path;sha256=$row.sha256;before=$row.allocated_before;after=$after};$result.reclaimed_bytes+=$row.allocated_before-$after
 [IO.File]::WriteAllText((Join-Path $run 'result.json'),($result|ConvertTo-Json -Depth 6),[Text.UTF8Encoding]::new($false))
}
Write-Output ('Compressed {0} retained artifacts; reclaimed {1:N2} GiB; hashes unchanged.' -f $result.files.Count,($result.reclaimed_bytes/1GB))
