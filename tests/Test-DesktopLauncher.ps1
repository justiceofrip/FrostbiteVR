#requires -Version 7.5
$ErrorActionPreference='Stop'
$repo=Split-Path $PSScriptRoot -Parent
. (Join-Path $repo 'launcher/Launcher.Core.ps1')
$testRoot=Join-Path $repo ('test-temp/desktop-'+[guid]::NewGuid().ToString('N'))
$null=New-Item -ItemType Directory -Path $testRoot -Force
$script:checks=0
$originalEnvironment=@{}
foreach($key in @('TEMP','TMP','TMPDIR','PYTHONDONTWRITEBYTECODE')){$originalEnvironment[$key]=[Environment]::GetEnvironmentVariable($key,'Process')}
function Check([bool]$Value,[string]$Name){if(-not $Value){throw "FAILED: $Name"};$script:checks++;Write-Host "PASS $Name"}
function Reject([scriptblock]$Work,[string]$Name){$rejected=$false;try{& $Work|Out-Null}catch{$rejected=$true};Check $rejected $Name}
function Fixture([string]$Version,[hashtable]$Extra=@{},[switch]$BadHash){
    $folder=Join-Path $testRoot ('fixture-'+$Version+'-'+[guid]::NewGuid().ToString('N'))
    $null=New-Item -ItemType Directory -Path (Join-Path $folder 'app') -Force
    $files=@(foreach($name in @('Launcher.ps1','Worker.ps1','Launcher.Core.ps1')){
        $path=Join-Path $folder "app/$name";[IO.File]::WriteAllText($path,"# fixture $Version")
        @{path="app/$name";sha256=$(if($BadHash){'0'*64}else{(Get-FileHash -LiteralPath $path).Hash.ToLowerInvariant()})}
    })
    Write-FvrJson (Join-Path $folder 'package.json') @{schema='fvr.desktop.package.v1';version=$Version;runtime_entry=$null;files=$files}
    $zip=$folder+'.zip';[IO.Compression.ZipFile]::CreateFromDirectory($folder,$zip)
    if($Extra.Count){
        $archive=[IO.Compression.ZipFile]::Open($zip,[IO.Compression.ZipArchiveMode]::Update)
        try{foreach($key in $Extra.Keys){$entry=$archive.CreateEntry($key);$writer=[IO.StreamWriter]::new($entry.Open());try{$writer.Write($Extra[$key])}finally{$writer.Dispose()}}}finally{$archive.Dispose()}
    }
    $archive=[IO.Compression.ZipFile]::OpenRead($zip)
    try{$expanded=[long]($archive.Entries|Measure-Object Length -Sum).Sum}finally{$archive.Dispose()}
    return @{zip=$zip;manifest=@{schema='fvr.desktop.release.v1';version=$Version;asset="BC2VR-$Version-win64.zip";bytes=(Get-Item -LiteralPath $zip).Length;unpacked_bytes=$expanded;sha256=(Get-FileHash -LiteralPath $zip).Hash.ToLowerInvariant()}}
}
try{
    $userTemp=[Environment]::GetEnvironmentVariable('TEMP','User')
    $scratch=Set-FvrLauncherStorage $testRoot
    Check ($env:TEMP -eq $scratch -and $env:TMP -eq $scratch -and $env:TMPDIR -eq $scratch) 'launcher scratch uses install drive'
    $childTemp=& (Join-Path $PSHOME 'pwsh.exe') -NoProfile -NonInteractive -Command '[IO.Path]::GetTempFileName()'
    Check ($LASTEXITCODE -eq 0 -and $childTemp.StartsWith($scratch+[IO.Path]::DirectorySeparatorChar) -and (Test-Path -LiteralPath $childTemp)) 'real child inherits scratch and creates its temporary file there'
    Check ([Environment]::GetEnvironmentVariable('TEMP','User') -eq $userTemp) 'Windows user TEMP remains unchanged'
    Reject {Set-FvrLauncherStorage ($env:SystemDrive+'\BC2VR-MustNotCreate')} 'Windows-drive installation refused before writing'
    # Parse every shipped script, without executing launcher/native side effects.
    foreach($script in @(Get-ChildItem (Join-Path $repo 'launcher') -Filter '*.ps1')+@(Get-Item (Join-Path $repo 'tools/Build-DesktopLauncher.ps1'))){
        $errors=$null;$tokens=$null;$null=[Management.Automation.Language.Parser]::ParseFile($script.FullName,[ref]$tokens,[ref]$errors)
        Check ($errors.Count -eq 0) ("parse "+$script.Name)
    }
    foreach($relative in @('../escape','/absolute','a/../../b','a\b','a:b','CON','x./a','a//b')){Reject {Get-FvrChild $testRoot $relative} ("reject path "+$relative)}
    Reject {Assert-FvrAssetUrl 'https://github.com/evil/repo/releases/download/x/a.zip' 'a.zip'} 'foreign repository rejected'
    Reject {Assert-FvrAssetUrl 'http://github.com/justiceofrip/FrostbiteVR/releases/download/x/a.zip' 'a.zip'} 'HTTP rejected'
    $lock=Enter-FvrLock $testRoot
    try{Reject {$second=Enter-FvrLock $testRoot;$second.Dispose()} 'concurrent operation rejected'}finally{$lock.Dispose()}
    $installed=Join-Path $testRoot 'installed';$null=New-Item -ItemType Directory -Path $installed
    $first=Fixture '1.0.0';Install-FvrPackage $installed $first.zip $first.manifest
    Check ((Read-FvrJson (Join-Path $installed 'active.json')).current -eq '1.0.0') 'first version installed'
    $settings=Join-Path $installed 'user-settings.json';[IO.File]::WriteAllText($settings,'keep me')
    $second=Fixture '1.0.1';$corrupt=$second.manifest.Clone();$corrupt.sha256='a'*64
    Reject {Install-FvrPackage $installed $second.zip $corrupt} 'corrupt download rejected'
    Check ((Read-FvrJson (Join-Path $installed 'active.json')).current -eq '1.0.0') 'failed update preserves current version'
    $bad=Fixture '1.0.2' -BadHash
    Reject {Install-FvrPackage $installed $bad.zip $bad.manifest} 'payload hash mismatch rejected'
    $escape=Fixture '1.0.3' @{'../escape.txt'='no'}
    Reject {Install-FvrPackage $installed $escape.zip $escape.manifest} 'ZIP traversal rejected'
    Check (-not(Test-Path -LiteralPath (Join-Path $installed 'escape.txt'))) 'traversal wrote nothing outside staging'
    $duplicate=Fixture '1.0.4' @{'APP/Launcher.ps1'='collision'}
    Reject {Install-FvrPackage $installed $duplicate.zip $duplicate.manifest} 'case-insensitive ZIP collision rejected'
    $unlisted=Fixture '1.0.5' @{'unexpected.exe'='unlisted'}
    Reject {Install-FvrPackage $installed $unlisted.zip $unlisted.manifest} 'unlisted payload rejected'
    $size=$second.manifest.Clone();$size.unpacked_bytes=1
    Reject {Install-FvrPackage $installed $second.zip $size} 'expanded size limit enforced'
    Install-FvrPackage $installed $second.zip $second.manifest
    Switch-FvrPrevious $installed
    Check ((Read-FvrJson (Join-Path $installed 'active.json')).current -eq '1.0.0') 'rollback selects verified previous build'
    Switch-FvrPrevious $installed
    $third=Fixture '1.1.0';Install-FvrPackage $installed $third.zip $third.manifest
    Check (@(Get-ChildItem -LiteralPath (Join-Path $installed 'versions') -Directory).Count -eq 2) 'retention bounded to current and previous'
    Check ([IO.File]::ReadAllText($settings) -eq 'keep me') 'user settings preserved'
    Check (@(Get-ChildItem -LiteralPath $installed -Directory -Filter 'staging-*').Count -eq 0) 'failed and successful staging cleaned'
    # Runtime operations must be rejected BEFORE any install writes when a game is active.
    function Assert-FvrIdle {throw 'fixture game active'}
    $fourth=Fixture '1.2.0';Reject {Install-FvrPackage $installed $fourth.zip $fourth.manifest} 'active game blocks update'
    Check ((Read-FvrJson (Join-Path $installed 'active.json')).current -eq '1.1.0') 'active game leaves current version untouched'
    Write-Host "$script:checks launcher checks passed; no game or VR process launched."
}finally{
    foreach($key in $originalEnvironment.Keys){[Environment]::SetEnvironmentVariable($key,$originalEnvironment[$key],'Process')}
    Remove-FvrOwnedTree (Split-Path $testRoot -Parent) (Split-Path $testRoot -Leaf)
}
