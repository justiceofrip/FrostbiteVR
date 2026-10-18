#requires -Version 7.5
$ErrorActionPreference='Stop'
$repo=Split-Path $PSScriptRoot -Parent
. (Join-Path $repo 'launcher/Launcher.Core.ps1')
. (Join-Path $repo 'launcher/runtime/Runtime.Core.ps1')
. (Join-Path $repo 'launcher/runtime/Dependencies.ps1')
$root=Join-Path $repo ('test-temp/dependencies-'+[guid]::NewGuid().ToString('N'))
$saved=@{};foreach($key in @('TEMP','TMP','TMPDIR','PYTHONDONTWRITEBYTECODE')){$saved[$key]=[Environment]::GetEnvironmentVariable($key,'Process')}
$script:checks=0;$script:downloads=0;$script:downloadMode='valid'
function Check([bool]$Value,[string]$Name){if(-not $Value){throw "FAILED: $Name"};$script:checks++;Write-Host "PASS $Name"}
function Reject([scriptblock]$Work,[string]$Name){$bad=$false;try{& $Work|Out-Null}catch{$bad=$true};Check $bad $Name}
function Save-FixtureRecipe($Value){$p=Join-Path $root ([guid]::NewGuid().ToString('N')+'.json');Write-FvrJson $p $Value;return $p}
function Copy-Recipe($Value){return ($Value|ConvertTo-Json -Depth 10|ConvertFrom-Json -AsHashtable)}
function New-FixtureZip([string]$Path,$Recipe,[string]$Extra='', [int]$ExtraType=0){
    $zip=[IO.Compression.ZipFile]::Open($Path,'Create')
    try {
        foreach($f in $Recipe.files){$e=$zip.CreateEntry($f.archive_path);$s=$e.Open();try{$data=[Text.Encoding]::UTF8.GetBytes($f.path);$s.Write($data)}finally{$s.Dispose()}}
        if($Extra){$e=$zip.CreateEntry($Extra);$e.ExternalAttributes=$ExtraType;$s=$e.Open();try{$s.WriteByte(1)}finally{$s.Dispose()}}
    } finally {$zip.Dispose()}
}
# Replace only the transport for deterministic offline tests. The acquisition,
# recipe validation, extraction, cache checks and cleanup are production code.
function Receive-FvrFile([string]$Url,[string]$Destination,[long]$MaxBytes){
    $script:downloads++
    if($script:downloadMode -eq 'failure'){[IO.File]::WriteAllText($Destination,'partial');throw 'Offline injected network failure'}
    if($script:downloadMode -eq 'corrupt'){[IO.File]::WriteAllText($Destination,'corrupt');return}
    if($Url -cne $script:fixtureRecipe.url -or $MaxBytes -ne $script:fixtureRecipe.bytes){throw 'Transport did not receive exact recipe URL and byte bound'}
    [IO.File]::Copy($script:fixtureArchive,$Destination)
}
try {
    $null=Set-FvrLauncherStorage $root
    $actual=Get-Bc2FfmpegRecipe (Join-Path $repo 'config/ffmpeg-download.json')
    Check ($actual.bytes -eq 76972461 -and $actual.selected_bytes -eq 156721635 -and $actual.files.Count -eq 9) 'published monthly recipe validates with exact byte bounds'
    $r=Copy-Recipe $actual
    foreach($f in $r.files){$data=[Text.Encoding]::UTF8.GetBytes($f.path);$f.bytes=$data.Length;$f.sha256=[Convert]::ToHexString([Security.Cryptography.SHA256]::HashData($data)).ToLowerInvariant()}
    $r.selected_bytes=[long](($r.files|Measure-Object bytes -Sum).Sum);$r.unpacked_bytes=$r.selected_bytes;$r.entries=9
    $script:fixtureArchive=Join-Path $root 'fixture.zip';New-FixtureZip $script:fixtureArchive $r
    $r.bytes=(Get-Item -LiteralPath $script:fixtureArchive).Length;$r.sha256=(Get-FileHash -LiteralPath $script:fixtureArchive).Hash.ToLowerInvariant()
    $recipePath=Save-FixtureRecipe $r;$script:fixtureRecipe=Get-Bc2FfmpegRecipe $recipePath
    $state=Join-Path $root 'data/runtime';$null=[IO.Directory]::CreateDirectory($state)
    Check ((Get-Bc2PreparationFfmpeg $state $recipePath $false Validate) -eq '' -and $script:downloads -eq 0) 'setup validation makes no download'
    Check ((Get-Bc2PreparationFfmpeg $state $recipePath $true Launch) -eq '' -and $script:downloads -eq 0) 'warm asset cache makes no download'
    Check (-not(Test-Path -LiteralPath (Join-Path $state 'tools'))) 'offline paths do not create dependency cache'
    foreach($url in @($r.url.Replace('github.com','example.com'),$r.url.Replace('https:','http:'),($r.url+'?redirect=1'),$r.url.Replace('BtbN','Other'))){
        $bad=Copy-Recipe $r;$bad.url=$url;$p=Save-FixtureRecipe $bad;Reject {Get-Bc2FfmpegRecipe $p} 'foreign or altered upstream URL rejected'
    }
    foreach($change in @('size','path','hash','duplicate')){
        $bad=Copy-Recipe $r
        switch($change){size{$bad.bytes=81MB} path{$bad.files[0].path='../ffmpeg.exe'} hash{$bad.sha256='bad'} duplicate{$bad.files[1]=Copy-Recipe $bad.files[0]}}
        $p=Save-FixtureRecipe $bad;Reject {Get-Bc2FfmpegRecipe $p} ('invalid recipe '+$change+' rejected')
    }
    Reject {Get-Bc2PreparationFfmpeg '\\server\share\decoder-test' $recipePath $false Launch} 'network volume rejected before download'
    $tool=Get-Bc2PreparationFfmpeg $state $recipePath $false Launch
    Check ($script:downloads -eq 1 -and (Test-Path -LiteralPath $tool)) 'first preparation downloads bounded exact archive and commits complete cache'
    $folder=Split-Path $tool -Parent
    Check (Test-Bc2FfmpegCache $folder $script:fixtureRecipe) 'complete extracted file hashes and receipt validate'
    Check (@(Get-ChildItem (Split-Path $folder -Parent) -Directory -Filter 'download-*').Count -eq 0) 'successful acquisition removes archive and staging'
    Check ((Get-Bc2PreparationFfmpeg $state $recipePath $false Launch) -eq $tool -and $script:downloads -eq 1) 'decoder cache reused offline after full verification'
    [IO.File]::WriteAllText((Join-Path $folder 'extra.dll'),'extra')
    Check (-not(Test-Bc2FfmpegCache $folder $script:fixtureRecipe)) 'unlisted DLL invalidates cache'
    Reject {Get-Bc2PreparationFfmpeg $state $recipePath $false Launch} 'changed cache fails without download or execution'
    Remove-Item -LiteralPath (Join-Path $folder 'extra.dll')
    [IO.File]::WriteAllText($tool,'changed')
    Check (-not(Test-Bc2FfmpegCache $folder $script:fixtureRecipe)) 'changed executable invalidates cache'
    foreach($mode in @('failure','corrupt')){
        $script:downloadMode=$mode;$other=Join-Path $root ('state-'+$mode);$null=[IO.Directory]::CreateDirectory($other)
        Reject {Get-Bc2PreparationFfmpeg $other $recipePath $false Launch} ($mode+' download rejected')
        $base=Join-Path $other 'tools/ffmpeg'
        Check (@(Get-ChildItem $base -Directory).Count -eq 0) ($mode+' leaves no committed or partial decoder')
    }
    foreach($kind in @('traversal','duplicate','symlink','member-hash','expanded-size')){
        $bad=Copy-Recipe $r;$archive=Join-Path $root ($kind+'.zip');$dest=Join-Path $root ('extract-'+$kind)
        if($kind -eq 'traversal'){New-FixtureZip $archive $bad ($bad.archive_root+'/../outside');$bad.entries++;$bad.unpacked_bytes++}
        elseif($kind -eq 'duplicate'){New-FixtureZip $archive $bad $bad.files[0].archive_path;$bad.entries++;$bad.unpacked_bytes++}
        elseif($kind -eq 'symlink'){New-FixtureZip $archive $bad ($bad.archive_root+'/link') ([int]0xa0000000);$bad.entries++;$bad.unpacked_bytes++}
        else{New-FixtureZip $archive $bad;if($kind -eq 'member-hash'){$bad.files[0].sha256='0'*64}else{$bad.unpacked_bytes--}}
        $bad.bytes=(Get-Item -LiteralPath $archive).Length;$bad.sha256=(Get-FileHash -LiteralPath $archive).Hash.ToLowerInvariant()
        Reject {Expand-Bc2Ffmpeg $archive $dest $bad} ($kind+' archive rejected')
        Check (-not(Test-Path -LiteralPath (Join-Path $dest 'receipt.json'))) ($kind+' cannot commit verified receipt')
    }
    Write-Host "Portable dependency policy passed $script:checks checks; all downloads were offline fixtures."
} finally {
    foreach($key in $saved.Keys){[Environment]::SetEnvironmentVariable($key,$saved[$key],'Process')}
    if(Test-Path -LiteralPath $root){Remove-FvrOwnedTree (Split-Path $root -Parent) (Split-Path $root -Leaf)}
}
