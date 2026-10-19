#requires -Version 7.5
# Shared installer/updater policy. No game or desktop input is sent here.
Set-StrictMode -Version 3
. (Join-Path $PSScriptRoot 'Launcher.Storage.ps1')
$script:FvrRepository = 'justiceofrip/BC2VR'
$script:FvrFeedbackRepository = 'justiceofrip/BC2VR'

function Assert-FvrRepository([string]$Repository) {
    if ($Repository -notmatch '^[A-Za-z0-9](?:[A-Za-z0-9-]{0,38})/[A-Za-z0-9][A-Za-z0-9_.-]{0,99}$' -or $Repository.Contains('..')) {
        throw 'Invalid GitHub repository in the launcher package.'
    }
    return $Repository
}
function Set-FvrPackageRepositories($Package) {
    # Package metadata selects this product's release and feedback destinations.
    # Legacy packages without these fields use the BC2 VR product repository.
    $release='justiceofrip/BC2VR'; $feedback=$release
    if ($Package.ContainsKey('release_repository')) { $release=Assert-FvrRepository $Package.release_repository }
    if ($Package.ContainsKey('feedback_repository')) { $feedback=Assert-FvrRepository $Package.feedback_repository }
    else { $feedback=$release }
    $script:FvrRepository=$release; $script:FvrFeedbackRepository=$feedback
}

function Set-FvrLauncherStorage([string]$Root) {
    return (Set-FvrProcessStorage $Root)
}

function Read-FvrJson([string]$Path) {
    Get-Content -LiteralPath $Path -Raw | ConvertFrom-Json -AsHashtable -DateKind String
}
function Get-FvrLocalLaunchRoute($Backend, [ValidateSet('MainMenu','Campaign')][string]$StartAt='MainMenu', [switch]$ActivateGameWindow) {
    if (-not $Backend -or -not $Backend.launch) { throw 'The local VR launch profile is missing.' }
    $arguments=@{}
    if ($ActivateGameWindow) {
        if (-not $Backend.ContainsKey('launch_activation') -or $Backend.launch_activation -ne 'user_click_once') {
            throw 'This runtime profile does not support launching the game window from the desktop button. Install the updated local profile.'
        }
        $arguments.ActivateGameWindow=$true
    }
    if ($StartAt -eq 'MainMenu' -and $Backend.ContainsKey('main_menu_launch') -and $Backend.main_menu_launch) {
        return @{entry=$Backend.main_menu_launch;arguments=$arguments;start_at=$StartAt}
    }
    if ($Backend.ContainsKey('launch_modes') -and $StartAt -in $Backend.launch_modes) {
        $arguments.StartAt=$StartAt
        return @{entry=$Backend.launch;arguments=$arguments;start_at=$StartAt}
    }
    # Legacy quick-launch profiles are retained for explicit development runs.
    # An ordinary Launch must never silently force Continue Campaign.
    if ($StartAt -eq 'Campaign') { return @{entry=$Backend.launch;arguments=$arguments;start_at=$StartAt} }
    throw 'This runtime profile only supports the old campaign test launch. Install the main-menu VR profile before using Launch BC2 in VR.'
}
function Write-FvrJson([string]$Path, $Value) {
    $temp = $Path + '.' + [guid]::NewGuid().ToString('N') + '.tmp'
    try {
        [IO.File]::WriteAllText($temp, ($Value | ConvertTo-Json -Depth 20), [Text.UTF8Encoding]::new($false))
        [IO.File]::Move($temp, $Path, $true)
    } finally { if (Test-Path -LiteralPath $temp) { Remove-Item -LiteralPath $temp -Force } }
}
function Assert-FvrVersion([string]$Version) {
    if ($Version -notmatch '^\d{1,5}\.\d{1,5}\.\d{1,5}$') { throw 'Invalid release version.' }
    return [version]$Version
}
function Get-FvrChild([string]$Root, [string]$Relative) {
    if (-not $Relative -or $Relative -match '[\\:\x00-\x1f]' -or $Relative.StartsWith('/') -or
        @($Relative.Split('/') | Where-Object { $_ -in @('', '.', '..') -or $_ -match '[. ]$' -or $_ -match '^(CON|PRN|AUX|NUL|COM[0-9]|LPT[0-9])(?:\.|$)' }).Count) {
        throw "Unsafe package path: $Relative"
    }
    $base = [IO.Path]::GetFullPath($Root).TrimEnd('\','/') + [IO.Path]::DirectorySeparatorChar
    $path = [IO.Path]::GetFullPath((Join-Path $Root $Relative))
    if (-not $path.StartsWith($base, [StringComparison]::OrdinalIgnoreCase)) { throw 'Path escapes package root.' }
    return $path
}
function Assert-FvrNoLinks([string]$Path) {
    $next = [IO.Path]::GetFullPath($Path)
    while ($next) {
        if ((Test-Path -LiteralPath $next) -and ((Get-Item -LiteralPath $next -Force).Attributes -band [IO.FileAttributes]::ReparsePoint)) { throw "Linked install path refused: $next" }
        $next = Split-Path -Parent $next
    }
}
function Remove-FvrOwnedTree([string]$Root, [string]$Relative) {
    $path = Get-FvrChild $Root $Relative
    Assert-FvrNoLinks $path
    if (Test-Path -LiteralPath $path) {
        foreach ($entry in Get-ChildItem -LiteralPath $path -Recurse -Force) {
            if ($entry.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw 'Refusing cleanup through a linked file or directory.' }
        }
        Remove-Item -LiteralPath $path -Recurse -Force
    }
}
function Assert-FvrIdle {
    if (Get-Process BFBC2Game,BC2XrHost,BC2NativeTrace,BC2NativeIpcProbe -ErrorAction SilentlyContinue) {
        throw 'Close BC2 and its VR session before installing or rolling back an update. SteamVR can stay open.'
    }
}
function Enter-FvrLock([string]$Root) {
    Assert-FvrNoLinks $Root
    $null = New-Item -ItemType Directory -Path $Root -Force
    try { return [IO.File]::Open((Join-Path $Root 'operation.lock'), 'OpenOrCreate', 'ReadWrite', 'None') }
    catch { throw 'Another launcher operation is running. Let it finish first.' }
}
function Receive-FvrFile([string]$Url, [string]$Destination, [long]$MaxBytes) {
    # Restrict the original URL AND every redirect. GitHub release blobs use
    # signed release-assets.githubusercontent.com URLs; never log those URLs.
    $handler = [Net.Http.HttpClientHandler]::new(); $handler.AllowAutoRedirect = $false
    $client = [Net.Http.HttpClient]::new($handler); $client.Timeout = [TimeSpan]::FromMinutes(5)
    $client.DefaultRequestHeaders.UserAgent.ParseAdd('FrostbiteVR-Desktop/0.1')
    $cts = [Threading.CancellationTokenSource]::new([TimeSpan]::FromMinutes(5))
    $response = $null; $stream = $null; $file = $null
    try {
        $uri = [uri]$Url
        for ($redirect = 0; $redirect -le 5; $redirect++) {
            if ($uri.Scheme -ne 'https' -or -not $uri.IsDefaultPort -or $uri.UserInfo -or $uri.Host -notin @('api.github.com','github.com','release-assets.githubusercontent.com','objects.githubusercontent.com')) { throw 'Update URL is outside GitHub release hosting.' }
            $response = $client.GetAsync($uri, [Net.Http.HttpCompletionOption]::ResponseHeadersRead, $cts.Token).GetAwaiter().GetResult()
            if ([int]$response.StatusCode -in @(301,302,303,307,308)) {
                if (-not $response.Headers.Location) { throw 'Invalid update redirect.' }
                $uri = [uri]::new($uri, $response.Headers.Location); $response.Dispose(); $response = $null
                continue
            }
            break
        }
        if (-not $response) { throw 'Too many update redirects.' }
        $null = $response.EnsureSuccessStatusCode()
        if ($response.Content.Headers.ContentLength -gt $MaxBytes) { throw 'Update download exceeds its size limit.' }
        $stream = $response.Content.ReadAsStreamAsync($cts.Token).GetAwaiter().GetResult()
        $file = [IO.File]::Open($Destination, 'CreateNew', 'Write', 'None')
        $buffer = [byte[]]::new(65536); [long]$total = 0
        while (($count = $stream.ReadAsync($buffer, 0, $buffer.Length, $cts.Token).GetAwaiter().GetResult()) -gt 0) {
            $total += $count
            if ($total -gt $MaxBytes) { throw 'Update download exceeded its size limit.' }
            $file.Write($buffer, 0, $count)
        }
    } finally {
        if ($file) { $file.Dispose() }; if ($stream) { $stream.Dispose() }; if ($response) { $response.Dispose() }
        $cts.Dispose(); $client.Dispose(); $handler.Dispose()
    }
}
function Assert-FvrAssetUrl([string]$Url, [string]$Name) {
    $uri = [uri]$Url
    if ($uri.Scheme -ne 'https' -or $uri.Host -ne 'github.com' -or -not $uri.IsDefaultPort -or $uri.UserInfo -or $uri.Query -or $uri.Fragment -or
        -not $uri.AbsolutePath.StartsWith("/$script:FvrRepository/releases/download/", [StringComparison]::Ordinal) -or
        [uri]::UnescapeDataString(($uri.AbsolutePath.Split('/') | Select-Object -Last 1)) -ne $Name) { throw 'Release asset is not from the configured repository.' }
}
function Assert-FvrManifest($Manifest) {
    if ($Manifest.schema -ne 'fvr.desktop.release.v1') { throw 'Unsupported update format.' }
    $null = Assert-FvrVersion $Manifest.version
    if ($Manifest.asset -ne "BC2VR-$($Manifest.version)-win64.zip" -or $Manifest.sha256 -notmatch '^[0-9a-f]{64}$' -or
        [long]$Manifest.bytes -le 0 -or [long]$Manifest.bytes -gt 512MB -or
        [long]$Manifest.unpacked_bytes -le 0 -or [long]$Manifest.unpacked_bytes -gt 1GB) { throw 'Invalid update sizes, asset name, or SHA-256.' }
}
function Find-FvrUpdate([string]$Root, [switch]$IncludePrerelease) {
    $scratch = Join-Path $Root ('query-' + [guid]::NewGuid().ToString('N'))
    $null = New-Item -ItemType Directory -Path $scratch
    try {
        Receive-FvrFile "https://api.github.com/repos/$script:FvrRepository/releases?per_page=30" (Join-Path $scratch 'releases.json') 4MB
        $releases = Read-FvrJson (Join-Path $scratch 'releases.json')
        $best = $null
        foreach ($release in $releases) {
            if ($release.draft -or ($release.prerelease -and -not $IncludePrerelease)) { continue }
            $assets = @($release.assets | Where-Object { $_.name -eq 'bc2vr-update.json' })
            if ($assets.Count -ne 1) { continue } # Source-only checkpoints are not installable hotfixes.
            $asset = $assets[0]; Assert-FvrAssetUrl $asset.browser_download_url 'bc2vr-update.json'
            $path = Join-Path $scratch ([guid]::NewGuid().ToString('N') + '.json')
            Receive-FvrFile $asset.browser_download_url $path 64KB
            $manifest = Read-FvrJson $path; Assert-FvrManifest $manifest
            $payloads = @($release.assets | Where-Object { $_.name -eq $manifest.asset })
            if ($payloads.Count -ne 1 -or [long]$payloads[0].size -ne [long]$manifest.bytes) { throw 'Release package is missing or has the wrong size.' }
            Assert-FvrAssetUrl $payloads[0].browser_download_url $manifest.asset
            if (-not $best -or ([version]$manifest.version -gt [version]$best.manifest.version)) {
                $best = @{manifest=$manifest;url=$payloads[0].browser_download_url;tag=$release.tag_name}
            }
        }
        return $best
    } finally { Remove-FvrOwnedTree $Root (Split-Path -Leaf $scratch) }
}
function Expand-FvrPackage([string]$Zip, [string]$Destination, [long]$ExpectedBytes) {
    Add-Type -AssemblyName System.IO.Compression
    $archive = [IO.Compression.ZipFile]::OpenRead($Zip)
    try {
        $seen = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
        if ($archive.Entries.Count -gt 12000) { throw 'Too many files in update.' }
        [long]$sum = 0
        foreach ($entry in $archive.Entries) {
            $name = $entry.FullName.TrimEnd('/')
            $target = Get-FvrChild $Destination $name
            if (-not $seen.Add($name)) { throw 'Duplicate package path.' }
            $unixType = ($entry.ExternalAttributes -shr 16) -band 0xf000
            if ($unixType -notin @(0,0x8000,0x4000) -or ($entry.ExternalAttributes -band 0x400)) { throw 'Linked package entry refused.' }
            $sum += $entry.Length
            if ($sum -gt $ExpectedBytes) { throw 'Expanded package exceeds declared size.' }
        }
        if ($sum -ne $ExpectedBytes) { throw 'Expanded package size differs from manifest.' }
        $null = New-Item -ItemType Directory -Path $Destination
        foreach ($entry in $archive.Entries) {
            $target = Get-FvrChild $Destination $entry.FullName.TrimEnd('/')
            if ($entry.FullName.EndsWith('/')) { $null = New-Item -ItemType Directory -Path $target -Force; continue }
            $null = New-Item -ItemType Directory -Path (Split-Path $target -Parent) -Force
            [IO.Compression.ZipFileExtensions]::ExtractToFile($entry, $target, $false)
        }
    } finally { $archive.Dispose() }
}
function Test-FvrPackage([string]$Folder, [string]$Version) {
    $null=Assert-FvrVersion $Version
    Assert-FvrNoLinks $Folder
    $metadata=Join-Path $Folder 'package.json';Assert-FvrNoLinks $metadata
    if((Get-Item -LiteralPath $metadata).Length -gt 4MB){throw 'Package metadata exceeds its size limit. Run the latest Setup in this launcher folder.'}
    $package = Read-FvrJson $metadata
    $schemaLabel='<missing>';$versionLabel='<missing>'
    if($package -is [Collections.IDictionary]){
        if($package.Contains('schema')){$schemaLabel=([string]$package.schema -replace '[\x00-\x1f]','?');$schemaLabel=$schemaLabel.Substring(0,[Math]::Min(96,$schemaLabel.Length))}
        if($package.Contains('version')){$versionLabel=([string]$package.version -replace '[\x00-\x1f]','?');$versionLabel=$versionLabel.Substring(0,[Math]::Min(96,$versionLabel.Length))}
    }
    if($package -isnot [Collections.IDictionary] -or $schemaLabel -cne 'fvr.desktop.package.v1' -or $versionLabel -cne $Version){
        throw "Package identity mismatch. Expected schema 'fvr.desktop.package.v1' and version '$Version'; found schema '$schemaLabel' and version '$versionLabel'. Use Install update to repair, or run the latest Setup in this launcher folder."
    }
    $listed = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    foreach ($entry in $package.files) {
        $path = Get-FvrChild $Folder $entry.path
        Assert-FvrNoLinks $path
        if (-not $listed.Add($entry.path) -or $entry.sha256 -notmatch '^[0-9a-f]{64}$' -or
            (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant() -ne $entry.sha256) { throw "Package file verification failed: $($entry.path)" }
    }
    foreach ($required in @('app/Launcher.ps1','app/Worker.ps1','app/Launcher.Core.ps1')) {
        if (-not $listed.Contains($required)) { throw "Package is missing $required" }
    }
    if ($package.runtime_entry -and (-not $package.runtime_entry.StartsWith('runtime/') -or -not $listed.Contains($package.runtime_entry))) { throw 'Runtime entry must be a verified package file.' }
    foreach ($file in Get-ChildItem -LiteralPath $Folder -File -Recurse) {
        $relative = [IO.Path]::GetRelativePath($Folder, $file.FullName).Replace('\','/')
        if ($relative -ne 'package.json' -and -not $listed.Contains($relative)) { throw "Unlisted package file: $relative" }
    }
    return $package
}
function Get-FvrInstalledPackageState([string]$Folder,[string]$Version) {
    $null=Assert-FvrVersion $Version
    try{
        $package=Test-FvrPackage $Folder $Version
        Set-FvrPackageRepositories $package
        return @{verified=$true;package=$package;error=$null}
    }catch{
        # Recovery metadata is compiled into this launcher, not taken from a
        # failed manifest. Runtime entry points remain unavailable.
        $fallback=@{schema='fvr.desktop.package-recovery.v1';version=$Version;runtime_entry=$null;release_repository='justiceofrip/BC2VR';feedback_repository='justiceofrip/BC2VR';files=@()}
        Set-FvrPackageRepositories $fallback
        return @{verified=$false;package=$fallback;error=$_.Exception.Message}
    }
}
function Get-FvrVerifiedPrevious([string]$Root,$Active,[string]$NewVersion) {
    foreach($candidate in @($Active.current,$Active.previous)){
        if(-not $candidate -or $candidate -ceq $NewVersion){continue}
        try{
            $null=Assert-FvrVersion $candidate
            $null=Test-FvrPackage (Get-FvrChild $Root ('versions/'+$candidate)) $candidate
            return $candidate
        }catch{} # Never advertise an invalid package as the rollback destination.
    }
    return $null
}
function Restore-FvrPackageRepair([string]$Root) {
    # Called under the installer/worker operation lock. A crash between the
    # directory renames leaves this bounded journal for the next trusted setup.
    $journalPath=Get-FvrChild $Root 'package-repair.json';Assert-FvrNoLinks $journalPath
    if(-not(Test-Path -LiteralPath $journalPath)){return}
    Assert-FvrIdle
    if((Get-Item -LiteralPath $journalPath).Length -gt 32768){throw 'Package repair record is invalid; original package was kept.'}
    $journal=Read-FvrJson $journalPath
    if($journal.schema -cne 'fvr.desktop.package-repair.v1' -or $journal.id -cnotmatch '^[a-f0-9]{32}$' -or $journal.phase -notin @('prepared','committed')){throw 'Package repair record is invalid; original package was kept.'}
    $null=Assert-FvrVersion $journal.version
    $stageName='staging-'+$journal.id;$backupName='repair-backup-'+$journal.id
    $stage=Get-FvrChild $Root $stageName;$backup=Get-FvrChild $Root $backupName
    $target=Get-FvrChild $Root ('versions/'+$journal.version)
    foreach($path in @($stage,$backup,$target)){Assert-FvrNoLinks $path}
    $activePath=Get-FvrChild $Root 'active.json';Assert-FvrNoLinks $activePath
    if($journal.phase -eq 'committed'){
        $null=Test-FvrPackage $target $journal.version
        if((Read-FvrJson $activePath).current -cne $journal.version){throw 'Committed repair pointer changed; original backup was kept.'}
    }else{
        $bytes=[Convert]::FromBase64String($journal.active_before)
        if($bytes.Length -gt 16384){throw 'Package repair pointer backup is invalid.'}
        if($bytes.Length){
            $old=[Text.Encoding]::UTF8.GetString($bytes).TrimStart([char]0xfeff)|ConvertFrom-Json -AsHashtable
            $null=Assert-FvrVersion $old.current
        }
        if(Test-Path -LiteralPath $backup){
            if(Test-Path -LiteralPath $target){
                if(Test-Path -LiteralPath $stage){throw 'Package repair has conflicting directories; original backup was kept.'}
                [IO.Directory]::Move($target,$stage)
            }
            [IO.Directory]::Move($backup,$target)
        }elseif(-not(Test-Path -LiteralPath $target)){throw 'Package repair original is unavailable; remaining recovery files were kept.'}
        if($bytes.Length){
            $temporary=$activePath+'.'+[guid]::NewGuid().ToString('N')+'.tmp'
            try{[IO.File]::WriteAllBytes($temporary,$bytes);[IO.File]::Move($temporary,$activePath,$true)}
            finally{if(Test-Path -LiteralPath $temporary){Remove-Item -LiteralPath $temporary -Force}}
        }elseif(Test-Path -LiteralPath $activePath){Remove-Item -LiteralPath $activePath -Force}
    }
    Remove-FvrOwnedTree $Root $stageName
    Remove-FvrOwnedTree $Root $backupName
    Remove-Item -LiteralPath $journalPath -Force
}
function Install-FvrPackage([string]$Root, [string]$Zip, $Manifest) {
    Assert-FvrIdle; Assert-FvrManifest $Manifest; Assert-FvrNoLinks $Root
    if ((Get-Item -LiteralPath $Zip).Length -ne [long]$Manifest.bytes -or
        (Get-FileHash -LiteralPath $Zip -Algorithm SHA256).Hash.ToLowerInvariant() -ne $Manifest.sha256) { throw 'Update checksum or download size mismatch; current version unchanged.' }
    Assert-FvrFreeSpace $Root ([long]$Manifest.unpacked_bytes + [long]$Manifest.bytes)
    Restore-FvrPackageRepair $Root
    $version = $Manifest.version
    $target = Get-FvrChild $Root "versions/$version"
    $repair=Test-Path -LiteralPath $target
    if($repair){
        $verified=$false
        try{$null=Test-FvrPackage $target $version;$verified=$true}catch{}
        if($verified){throw 'That version is already installed and verified; use rollback to select it.'}
    }
    $id=[guid]::NewGuid().ToString('N')
    $stageName='staging-'+$id;$stage=Get-FvrChild $Root $stageName
    $journalPath=Get-FvrChild $Root 'package-repair.json'
    try {
        Expand-FvrPackage $Zip $stage ([long]$Manifest.unpacked_bytes)
        $null = Test-FvrPackage $stage $version
        Assert-FvrIdle # A game may have started during the download.
        $activePath=Get-FvrChild $Root 'active.json';Assert-FvrNoLinks $activePath
        $active=@{current=$null;previous=$null};$activeBefore=[byte[]]::new(0)
        if(Test-Path -LiteralPath $activePath){
            if((Get-Item -LiteralPath $activePath).Length -gt 16384){throw 'Installed version pointer is too large; installation unchanged.'}
            $activeBefore=[IO.File]::ReadAllBytes($activePath);$active=Read-FvrJson $activePath
            $null=Assert-FvrVersion $active.current
            if(-not $active.Contains('previous')){$active.previous=$null}
        }
        $previous=Get-FvrVerifiedPrevious $Root $active $version
        $null = New-Item -ItemType Directory -Path (Join-Path $Root 'versions') -Force
        Assert-FvrNoLinks $target
        if($repair){
            $journal=@{schema='fvr.desktop.package-repair.v1';id=$id;version=$version;phase='prepared';active_before=[Convert]::ToBase64String($activeBefore)}
            Write-FvrJson $journalPath $journal
            try{
                [IO.Directory]::Move($target,(Get-FvrChild $Root ('repair-backup-'+$id)))
                [IO.Directory]::Move($stage,$target)
                Write-FvrJson $activePath @{current=$version;previous=$previous}
                $journal.phase='committed';Write-FvrJson $journalPath $journal
            }catch{
                $repairError=$_.Exception.Message
                try{Restore-FvrPackageRepair $Root}catch{throw "Repair could not finish or restore automatically. Original package is retained; run the latest Setup again in this folder. $repairError"}
                throw "Repair failed; the original package and installed-version pointer were restored. $repairError"
            }
            Restore-FvrPackageRepair $Root
        }else{
            [IO.Directory]::Move($stage, $target)
            try { Write-FvrJson $activePath @{current=$version;previous=$previous} }
            catch { Remove-FvrOwnedTree $Root "versions/$version"; throw }
        }
        # Prune only our version directories. User settings, saves, local build,
        # recordings and runtime cache live outside these immutable packages.
        foreach ($dir in Get-ChildItem -LiteralPath (Join-Path $Root 'versions') -Directory) {
            if ($dir.Name -notin @($version,$previous) -and $dir.Name -match '^\d+\.\d+\.\d+$') { Remove-FvrOwnedTree $Root ('versions/'+$dir.Name) }
        }
    } finally { if(-not(Test-Path -LiteralPath $journalPath)){Remove-FvrOwnedTree $Root $stageName} }
}
function Switch-FvrPrevious([string]$Root) {
    Assert-FvrIdle
    Restore-FvrPackageRepair $Root
    $active = Read-FvrJson (Join-Path $Root 'active.json')
    if (-not $active.previous) { throw 'There is no previous installed update yet.' }
    $null = Assert-FvrVersion $active.previous
    $null = Test-FvrPackage (Get-FvrChild $Root ('versions/'+$active.previous)) $active.previous
    $previous=Get-FvrVerifiedPrevious $Root $active $active.previous
    Write-FvrJson (Join-Path $Root 'active.json') @{current=$active.previous;previous=$previous}
}
