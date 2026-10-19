#requires -Version 7.5
[CmdletBinding()]
param([Parameter(Mandatory)][string]$InstallRoot,
      [ValidateSet('Launch','Validate','Stop','Check','Update','Rollback','Feedback')][string]$Action,
      [switch]$Fresh, [switch]$UseLocal, [switch]$Prerelease, [switch]$ActivateGameWindow,
      [ValidateSet('MainMenu','Campaign')][string]$StartAt='MainMenu')
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot 'Launcher.Core.ps1')
. (Join-Path $PSScriptRoot 'Launcher.Settings.ps1')
. (Join-Path $PSScriptRoot 'Launcher.Feedback.ps1')
$null = Set-FvrLauncherStorage $InstallRoot
$data = Join-Path $InstallRoot 'data'
$null = New-Item -ItemType Directory -Path $data -Force
$lock = $null
$operationStarted=[DateTime]::UtcNow.ToString('o')
function Status([string]$Message, [string]$State='working') {
    Write-FvrJson (Join-Path $data 'operation.json') @{action=$Action;state=$State;message=$Message;utc=[DateTime]::UtcNow.ToString('o');started_utc=$operationStarted}
}
function Read-LocalSession($Settings) {
    $record = $Settings.local_backend.session_record
    if (-not $record -or -not (Test-Path -LiteralPath $record)) { return $null }
    $launch = Read-FvrJson $record
    $process = Get-Process -Id $launch.supervisor_pid -ErrorAction SilentlyContinue
    if (-not $process -or $process.StartTime.ToUniversalTime() -ne [DateTimeOffset]::Parse($launch.supervisor_start_utc).UtcDateTime) { return $null }
    return $launch
}
try {
    $lock = Enter-FvrLock $InstallRoot
    Status 'Preparing…'
    $settingsState = Read-FvrLauncherSettings -InstallRoot $InstallRoot -LockHeld
    $settings = $settingsState.settings
    if($Action -in @('Launch','Validate') -and ($settingsState.needs_reset -or $settingsState.read_only)){throw 'Settings need attention. Use Reset and choose game folder in the launcher; your original settings are backed up.'}
    $active = Read-FvrJson (Join-Path $InstallRoot 'active.json')
    $null=Assert-FvrVersion $active.current
    $versionFolder = Get-FvrChild $InstallRoot ('versions/'+$active.current)
    $packageState=Get-FvrInstalledPackageState $versionFolder $active.current
    $package=$packageState.package
    Write-FvrJson (Join-Path $data 'package-check.json') @{schema='fvr.desktop.package-check.v1';expected_version=$active.current;verified=$packageState.verified;error=$packageState.error;utc=[DateTime]::UtcNow.ToString('o')}
    if(-not $packageState.verified -and $Action -notin @('Feedback','Check','Update','Rollback')){throw $packageState.error}
    if ($Action -eq 'Feedback') {
        Status 'Preparing a small, sanitized diagnostics bundle…'
        $bundle=New-FvrFeedbackBundle $InstallRoot $package $settings
        Status 'Diagnostics saved. Review Report.txt, then attach BC2VR-diagnostics.zip to your feedback yourself. Nothing was uploaded.' 'feedback_ready'
        Write-Output ($bundle | ConvertTo-Json)
        exit 0
    }
    if ($Action -in @('Check','Update')) {
        Status 'Checking GitHub for compatible hotfixes…'
        $update = Find-FvrUpdate $InstallRoot -IncludePrerelease:$Prerelease
        if(-not $update){
            if(-not $packageState.verified){throw 'The installation needs repair, but no compatible package was found. Include pre-alpha hotfixes or download the latest BC2 VR Setup and choose this launcher folder.'}
            Status 'No launcher-compatible hotfix is published yet. Your installed build is ready to use.' 'done';exit 0
        }
        if([version]$update.manifest.version -lt [version]$active.current){
            if(-not $packageState.verified){throw 'The installed version pointer is newer than the available verified package. Use Report feedback; the launcher will not guess or downgrade the installed version.'}
            Status 'You have the latest available update.' 'done';exit 0
        }
        if([version]$update.manifest.version -eq [version]$active.current -and $packageState.verified){Status 'You have the latest available update.' 'done';exit 0}
        if($Action -eq 'Check'){
            $message=if($packageState.verified){"Update $($update.manifest.version) is available. Select Install update when BC2 is closed."}else{"Installation needs repair. Select Install update to install verified version $($update.manifest.version). Your settings will be kept. Close BC2 first."}
            Status $message 'update_available';exit 0
        }
        Assert-FvrIdle
        Assert-FvrFreeSpace $InstallRoot ([long]$update.manifest.bytes + [long]$update.manifest.unpacked_bytes)
        Status ("Downloading update $($update.manifest.version)…")
        $download = Join-Path $InstallRoot ('download-'+[guid]::NewGuid().ToString('N')+'.zip')
        try {
            Receive-FvrFile $update.url $download ([long]$update.manifest.bytes)
            Status 'Verifying and installing the update…'
            Install-FvrPackage $InstallRoot $download $update.manifest
        } finally { if (Test-Path -LiteralPath $download) { Remove-Item -LiteralPath $download -Force } }
        Status 'Verified package installed. Close and reopen the launcher to use it. Your settings and the last verified previous version were kept.' 'restart_required'; exit 0
    }
    if ($Action -eq 'Rollback') {
        Switch-FvrPrevious $InstallRoot
        Status 'Previous version selected. Close and reopen the launcher.' 'restart_required'; exit 0
    }
    $localRoute=$null
    if ($Action -in @('Launch','Validate') -and (-not $package.runtime_entry -or $UseLocal)) {
        $localRoute=Get-FvrLocalLaunchRoute $settings.local_backend $StartAt -ActivateGameWindow:($Action -eq 'Launch' -and $ActivateGameWindow)
    }
    if ($Action -eq 'Launch' -and (-not $package.runtime_entry -or $UseLocal)) {
        # Fail before starting SteamVR when the existing native session's disk
        # reserve cannot be met. Its complete storage scan still runs normally.
        Assert-FvrFreeSpace $InstallRoot 640MB
        Assert-FvrFreeSpace ([IO.Path]::GetTempPath()) 640MB
        Status 'Starting SteamVR. Keep your headset connected through Steam Link…'
        if (-not (Get-Process vrserver -ErrorAction SilentlyContinue)) {
            $steam = Join-Path ((Get-ItemProperty 'HKCU:\Software\Valve\Steam' -ErrorAction Stop).SteamPath) 'steam.exe'
            if (-not (Test-Path -LiteralPath $steam)) { throw 'Steam was not found. Start SteamVR, then press Launch again.' }
            $null = Start-Process -FilePath $steam -ArgumentList @('-applaunch','250820') -WindowStyle Hidden
            $until = [DateTime]::UtcNow.AddSeconds(90)
            while (-not (Get-Process vrserver -ErrorAction SilentlyContinue)) {
                if ([DateTime]::UtcNow -ge $until) { throw 'SteamVR did not start. Connect your headset and start SteamVR, then retry.' }
                Start-Sleep -Milliseconds 500
            }
        }
    }
    if ($package.runtime_entry -and -not $UseLocal) {
        Status $(if($Action -eq 'Launch'){'Opening BC2 in VR…'}else{'Checking the installed runtime…'})
        $entry = Get-FvrChild $versionFolder $package.runtime_entry
        & $entry -Action $Action -SettingsPath (Join-Path $data 'settings.json') -StateRoot (Join-Path $data 'runtime') -Fresh:$Fresh -StartAt $StartAt -ActivateGameWindow:($Action -eq 'Launch' -and $ActivateGameWindow) 6>&1 | ForEach-Object {
            if($_ -is [Management.Automation.InformationRecord] -and $_.MessageData.ToString().StartsWith('BC2VR: ')){Status $_.MessageData.ToString().Substring(7)}else{Write-Output $_}
        }
    } else {
        if (-not $settings.local_backend) { throw 'This is the launcher only. Install a release containing the VR runtime before playing.' }
        foreach ($pin in $settings.local_backend.pins) {
            if ((Get-FileHash -LiteralPath $pin.path -Algorithm SHA256).Hash.ToLowerInvariant() -ne $pin.sha256) { throw 'The local launch files changed. Reinstall the local launcher profile after verifying the new build.' }
        }
        if ($Action -eq 'Stop') {
            $session = Read-LocalSession $settings
            if (-not $session) { Status 'No launcher-owned VR session is running.' 'done'; exit 0 }
            Status 'Ending VR and saving its logs. BC2 and SteamVR will stay open…'
            & $settings.local_backend.stop -ReportFolder $session.run
        } elseif ($Action -eq 'Validate') {
            Status 'Checking the installed build and launch dependencies…'
            $routeArguments=$localRoute.arguments
            & $localRoute.entry @routeArguments -ValidateOnly
        } elseif ($Action -eq 'Launch') {
            if ($Fresh -and (Read-LocalSession $settings)) { throw 'VR is already running. Use Stop VR before requesting a fresh launch.' }
            Status $(if($StartAt -eq 'MainMenu'){'Opening the BC2 menu in VR. Aim with the right controller and select with its trigger…'}else{'Continuing campaign for the explicit development run…'})
            $routeArguments=$localRoute.arguments
            & $localRoute.entry @routeArguments -Fresh:$Fresh 6>&1 | ForEach-Object {
                if($_ -is [Management.Automation.InformationRecord]) {
                    $step=$_.MessageData.ToString()
                    if($step -match '^(Verify normal\d+ candidate|Check launch storage|Close previous game|Start BC2|Continue campaign|Wait for campaign|Wait for main menu|Open BC2 window|Attach VR)\.\.\.$'){Status $step}
                } else { Write-Output $_ }
            }
        }
    }
    if ($Action -eq 'Launch') { Status 'VR startup completed. Check your headset. Closing this launcher leaves the game and VR running.' 'started' }
    elseif ($Action -eq 'Stop') { Status 'VR stop requested and headset host closed. BC2 and SteamVR remain open; cleanup may take a few seconds.' 'done' }
    else {
        Assert-FvrFreeSpace $InstallRoot 640MB
        Assert-FvrFreeSpace ([IO.Path]::GetTempPath()) 640MB
        Status 'Build and launch dependencies verified. Connect your headset and press Launch BC2 in VR.' 'done'
    }
} catch {
    # A second instance must not overwrite the live operation's progress.
    if ($lock) { Status $_.Exception.Message 'error' }
    Write-Error $_ -ErrorAction Continue
    exit 1
} finally { if ($lock) { $lock.Dispose() } }
