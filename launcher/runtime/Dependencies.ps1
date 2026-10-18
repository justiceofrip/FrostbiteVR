#requires -Version 7.5
# FFmpeg is obtained directly from its upstream provider, never from our package.
Set-StrictMode -Version 3

function Get-Bc2FfmpegRecipe([string]$Path) {
    Assert-FvrNoLinks $Path
    $r=Read-FvrJson $Path
    if($r.schema -ne 'bc2vr.ffmpeg-download.v1' -or $r.sha256 -notmatch '^[a-f0-9]{64}$' -or
       $r.release -notmatch '^autobuild-\d{4}-\d{2}-\d{2}-\d{2}-\d{2}$' -or
       $r.archive_root -notmatch '^ffmpeg-n[0-9.]+-\d+-g[a-f0-9]+-win64-lgpl-shared-[0-9.]+$' -or
       $r.url -cne ('https://github.com/BtbN/FFmpeg-Builds/releases/download/'+$r.release+'/'+$r.archive_root+'.zip') -or
       [long]$r.bytes -le 0 -or [long]$r.bytes -gt 80MB -or
       [long]$r.unpacked_bytes -le 0 -or [long]$r.unpacked_bytes -gt 200MB -or
       [long]$r.selected_bytes -le 0 -or [long]$r.selected_bytes -gt [long]$r.unpacked_bytes -or
       [int]$r.entries -le 0 -or [int]$r.entries -gt 512 -or $r.files.Count -ne 9){throw 'Invalid pinned FFmpeg download recipe.'}
    $required=@('LICENSE.txt','ffmpeg.exe','avcodec-63.dll','avdevice-63.dll','avfilter-12.dll','avformat-63.dll','avutil-61.dll','swresample-7.dll','swscale-10.dll')
    $seen=[Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
    [long]$sum=0
    foreach($f in $r.files){
        if($f.path -cnotin $required -or -not $seen.Add($f.path) -or $f.sha256 -notmatch '^[a-f0-9]{64}$' -or [long]$f.bytes -le 0){throw 'Invalid FFmpeg file pin.'}
        $expected=$r.archive_root+'/'+$(if($f.path -ceq 'LICENSE.txt'){''}else{'bin/'})+$f.path
        if($f.archive_path -cne $expected){throw 'Unexpected FFmpeg archive member.'}
        $sum+=[long]$f.bytes
    }
    if($sum -ne [long]$r.selected_bytes){throw 'FFmpeg expanded file sizes differ from recipe.'}
    $r.recipe_sha256=(Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()
    return $r
}

function Test-Bc2FfmpegCache([string]$Folder,$Recipe) {
    try {
        Assert-FvrNoLinks $Folder
        $receiptPath=Join-Path $Folder 'receipt.json';Assert-FvrNoLinks $receiptPath
        $receipt=Read-FvrJson $receiptPath
        if($receipt.schema -ne 'bc2vr.ffmpeg-cache.v1' -or $receipt.recipe_sha256 -ne $Recipe.recipe_sha256 -or
           $receipt.archive_sha256 -ne $Recipe.sha256 -or $receipt.url -cne $Recipe.url){return $false}
        # Reject additional DLLs and nested paths as well as changed pinned files.
        $actual=@(Get-ChildItem -LiteralPath $Folder -Force)
        if($actual.Count -ne ($Recipe.files.Count+1)){return $false}
        foreach($item in $actual){if($item.PSIsContainer -or ($item.Attributes -band [IO.FileAttributes]::ReparsePoint)){return $false}}
        foreach($f in $Recipe.files){
            $path=Get-FvrChild $Folder $f.path;Assert-FvrNoLinks $path
            if((Get-Item -LiteralPath $path).Length -ne [long]$f.bytes -or (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant() -ne $f.sha256){return $false}
        }
        return $true
    } catch{return $false}
}

function Expand-Bc2Ffmpeg([string]$Archive,[string]$Destination,$Recipe) {
    Assert-FvrNoLinks $Archive;Assert-FvrNoLinks $Destination
    if(Test-Path -LiteralPath $Destination){throw 'FFmpeg extraction requires a new staging directory.'}
    if((Get-Item -LiteralPath $Archive).Length -ne [long]$Recipe.bytes -or (Get-FileHash -LiteralPath $Archive -Algorithm SHA256).Hash.ToLowerInvariant() -ne $Recipe.sha256){throw 'FFmpeg archive size or SHA-256 mismatch.'}
    $zip=[IO.Compression.ZipFile]::OpenRead($Archive)
    try {
        if($zip.Entries.Count -ne [int]$Recipe.entries){throw 'FFmpeg archive entry count changed.'}
        $seen=[Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
        $selected=[Collections.Generic.List[object]]::new();[long]$total=0
        foreach($entry in $zip.Entries){
            $name=$entry.FullName.TrimEnd('/')
            $null=Get-FvrChild $Destination $name
            $type=($entry.ExternalAttributes -shr 16) -band 0xf000
            if(-not $seen.Add($name) -or $type -notin @(0,0x8000,0x4000) -or ($entry.ExternalAttributes -band 0x400) -or
               ($name -cne $Recipe.archive_root -and -not $name.StartsWith($Recipe.archive_root+'/',[StringComparison]::Ordinal))){throw 'Unsafe FFmpeg archive path or link.'}
            $total+=$entry.Length
            if($total -gt [long]$Recipe.unpacked_bytes){throw 'FFmpeg archive exceeds expanded size.'}
            $pin=@($Recipe.files|Where-Object {$_.archive_path -ceq $entry.FullName})
            if($pin.Count){
                if($pin.Count -ne 1 -or $entry.Length -ne [long]$pin[0].bytes -or $type -eq 0x4000){throw 'FFmpeg member size or type mismatch.'}
                $selected.Add(@{entry=$entry;pin=$pin[0]})
            }
        }
        if($total -ne [long]$Recipe.unpacked_bytes -or $selected.Count -ne $Recipe.files.Count){throw 'FFmpeg archive inventory differs from recipe.'}
        $null=[IO.Directory]::CreateDirectory($Destination)
        $buffer=[byte[]]::new(65536)
        foreach($item in $selected){
            $target=Get-FvrChild $Destination $item.pin.path
            $memberStream=$item.entry.Open();$output=$null
            try {
                $output=[IO.File]::Open($target,'CreateNew','Write','None');[long]$count=0
                while(($n=$memberStream.Read($buffer,0,$buffer.Length)) -gt 0){
                    $count+=$n;if($count -gt [long]$item.pin.bytes){throw 'FFmpeg member exceeded pinned size.'};$output.Write($buffer,0,$n)
                }
                if($count -ne [long]$item.pin.bytes){throw 'Truncated FFmpeg member.'}
            } finally {if($output){$output.Dispose()};$memberStream.Dispose()}
            if((Get-FileHash -LiteralPath $target -Algorithm SHA256).Hash.ToLowerInvariant() -ne $item.pin.sha256){throw 'FFmpeg member SHA-256 mismatch.'}
        }
    } finally {$zip.Dispose()}
    Write-FvrJson (Join-Path $Destination 'receipt.json') @{schema='bc2vr.ffmpeg-cache.v1';recipe_sha256=$Recipe.recipe_sha256;archive_sha256=$Recipe.sha256;url=$Recipe.url;fetched_utc=[DateTime]::UtcNow.ToString('o')}
    if(-not(Test-Bc2FfmpegCache $Destination $Recipe)){throw 'Extracted FFmpeg verification failed.'}
}

function Get-Bc2PreparationFfmpeg([string]$StateRoot,[string]$RecipePath,[bool]$AssetsReady,[ValidateSet('Validate','Launch')][string]$Action) {
    $recipe=Get-Bc2FfmpegRecipe $RecipePath
    # Validation and a warm asset cache never acquire or execute a decoder.
    if($Action -eq 'Validate' -or $AssetsReady){return ''}
    if(-not [IO.Path]::IsPathFullyQualified($StateRoot) -or $StateRoot -notmatch '^[A-Za-z]:[\\/]' -or
       [IO.Path]::GetPathRoot($StateRoot).TrimEnd('\','/') -eq $env:SystemDrive){throw 'FFmpeg cache must use the chosen data drive.'}
    Assert-FvrNoLinks $StateRoot;Assert-Bc2RuntimeSpace $StateRoot
    $base=Get-FvrChild $StateRoot 'tools/ffmpeg';$lock=Enter-FvrLock $base
    $workName='download-'+[guid]::NewGuid().ToString('N');$work=Get-FvrChild $base $workName
    try {
        $cache=Get-FvrChild $base $recipe.sha256
        if(Test-Path -LiteralPath $cache){
            if(-not(Test-Bc2FfmpegCache $cache $recipe)){throw "Cached FFmpeg files changed. Remove this generated folder and retry: $cache"}
            return (Join-Path $cache 'ffmpeg.exe')
        }
        $null=[IO.Directory]::CreateDirectory($work)
        $archive=Get-FvrChild $work 'upstream.zip';$ready=Get-FvrChild $work 'ready'
        Write-Host 'Downloading the pinned audio decoder from GitHub for first-time asset preparation…'
        Receive-FvrFile $recipe.url $archive ([long]$recipe.bytes)
        Expand-Bc2Ffmpeg $archive $ready $recipe
        [IO.Directory]::Move($ready,$cache)
        return (Join-Path $cache 'ffmpeg.exe')
    } finally {try{Remove-FvrOwnedTree $base $workName}finally{$lock.Dispose()}}
}
