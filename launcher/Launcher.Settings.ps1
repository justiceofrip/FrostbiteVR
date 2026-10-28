#requires -Version 7.5
# Requires Launcher.Core.ps1. Settings changes never start or inspect a game.
function New-FvrLauncherSettings {
    return @{schema='fvr.desktop.settings.v1';game_path='';local_backend=$null;steam_compatibility=$false}
}
function ConvertFrom-FvrSettingsText([string]$Text) {
    $value=ConvertFrom-Json -InputObject $Text -AsHashtable -DateKind String
    if($value -isnot [Collections.IDictionary]){throw 'Settings must be a JSON object.'}
    if(-not $value.Contains('steam_compatibility')){$value.steam_compatibility=$false}
    elseif($value.steam_compatibility -isnot [bool]){throw 'Steam compatibility must be a JSON boolean (true or false).'}
    foreach($key in @('game_path','local_backend')){if(-not $value.Contains($key)){$value[$key]=$(if($key -eq 'game_path'){''}else{$null})}}
    if(-not $value.Contains('schema')){$value.schema='fvr.desktop.settings.v1'}
    return $value
}
function Write-FvrLauncherSettings([string]$Path,$Settings) {
    # Serialize before touching the original. Preserve unknown/backend fields,
    # and refuse values deeper than the serializer can retain.
    $json=ConvertTo-Json -InputObject $Settings -Depth 100 -WarningAction Stop
    Assert-FvrNoLinks $Path
    $temporary=$Path+'.'+[guid]::NewGuid().ToString('N')+'.tmp'
    try{
        [IO.File]::WriteAllText($temporary,$json,[Text.UTF8Encoding]::new($false))
        [IO.File]::Move($temporary,$Path,$true)
    }finally{if(Test-Path -LiteralPath $temporary){Remove-Item -LiteralPath $temporary -Force}}
}
function Backup-FvrLauncherSettings([string]$Path,[byte[]]$Bytes) {
    $hash=[Convert]::ToHexString([Security.Cryptography.SHA256]::HashData($Bytes)).ToLowerInvariant()
    $folder=Join-Path (Split-Path $Path -Parent) 'settings-backups';Assert-FvrNoLinks $folder
    $null=[IO.Directory]::CreateDirectory($folder)
    $backup=Join-Path $folder ('settings-'+$hash+'.json');Assert-FvrNoLinks $backup
    if(Test-Path -LiteralPath $backup){
        if((Get-FileHash -LiteralPath $backup -Algorithm SHA256).Hash.ToLowerInvariant() -ne $hash){throw 'The existing settings backup changed; original settings were kept.'}
    }else{
        $file=[IO.File]::Open($backup,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::Read)
        try{$file.Write($Bytes,0,$Bytes.Length)}finally{$file.Dispose()}
    }
    return $backup
}
function Read-FvrLauncherSettingsLocked([string]$InstallRoot,[switch]$RefuseRepair) {
    $path=Get-FvrChild $InstallRoot 'data/settings.json';Assert-FvrNoLinks $path
    $state=@{settings=(New-FvrLauncherSettings);notice='';backup=$null;needs_reset=$false;read_only=$false;repaired=$false}
    if(-not(Test-Path -LiteralPath $path)){return $state}
    if((Get-Item -LiteralPath $path).Length -gt 1MB){throw 'Settings exceed the 1 MiB safety limit. The original was left unchanged.'}
    $bytes=[IO.File]::ReadAllBytes($path)
    $text=[IO.File]::ReadAllText($path)
    try{$state.settings=ConvertFrom-FvrSettingsText $text;return $state}catch{}
    if($RefuseRepair){throw 'Settings are damaged. Reopen the launcher to recover them before changing Steam compatibility. The original file was left unchanged.'}
    # Never globally escape backslashes: that could alter a valid local-backend
    # profile or conceal a second syntax error. Repair only one raw Windows
    # game_path value, and require the entire remaining document to parse.
    $repaired=$null
    $matches=[regex]::Matches($text,'(?i)"game_path"\s*:\s*"(?<value>[^"\r\n]*)"')
    if($matches.Count -eq 1){
        $span=$matches[0].Groups['value'];$raw=$span.Value
        if($raw -match '^[A-Za-z]:\\[^"\x00-\x1f]*$' -and -not $raw.Contains('\\')){
            $encoded=ConvertTo-Json -InputObject $raw -Compress
            $candidate=$text.Substring(0,$span.Index-1)+$encoded+$text.Substring($span.Index+$span.Length+1)
            try{
                $parsed=ConvertFrom-FvrSettingsText $candidate
                # The candidate must be the top-level game path, not a nested
                # property with the same name in an unrelated backend object.
                if($parsed.game_path -is [string] -and $parsed.game_path -ceq $raw){$repaired=$parsed}
            }catch{}
        }
    }
    $state.backup=Backup-FvrLauncherSettings $path $bytes
    if($repaired){
        Write-FvrLauncherSettings $path $repaired;$state.settings=$repaired;$state.repaired=$true
        $state.notice="The game-folder path was repaired. Your other settings were kept. Original settings: $($state.backup)"
    }else{
        $state.needs_reset=$true
        $state.notice="Settings could not be read. Your original file is unchanged and backed up at $($state.backup). Reset and choose your game folder to continue; any local-checkpoint settings will remain in the backup."
    }
    return $state
}
function Read-FvrLauncherSettings([string]$InstallRoot,[switch]$LockHeld,[switch]$RefuseRepair) {
    $lock=$null
    try{
        if(-not $LockHeld){$lock=Enter-FvrLock $InstallRoot}
        return Read-FvrLauncherSettingsLocked $InstallRoot -RefuseRepair:$RefuseRepair
    }catch{
        return @{settings=(New-FvrLauncherSettings);notice=$_.Exception.Message;backup=$null;needs_reset=$false;read_only=$true;repaired=$false}
    }finally{if($lock){$lock.Dispose()}}
}
function Resolve-FvrGameFolder([string]$Selection) {
    if([string]::IsNullOrWhiteSpace($Selection) -or -not [IO.Path]::IsPathFullyQualified($Selection) -or $Selection -match '[\x00-\x1f]'){
        throw 'Choose the folder containing BFBC2Game.exe.'
    }
    $item=Get-Item -LiteralPath ([IO.Path]::GetFullPath($Selection)) -ErrorAction Stop
    if($item.PSIsContainer){$folder=$item.FullName}
    elseif($item.Name -ieq 'BFBC2Game.exe'){$folder=$item.DirectoryName}
    else{throw 'Choose BFBC2Game.exe in your Battlefield Bad Company 2 folder.'}
    if(-not(Test-Path -LiteralPath (Join-Path $folder 'BFBC2Game.exe') -PathType Leaf)){
        throw 'That folder does not contain BFBC2Game.exe. Choose your game installation.'
    }
    return $folder
}
function Save-FvrGameFolder([string]$InstallRoot,[string]$Selection,[switch]$ResetInvalidSettings) {
    $folder=Resolve-FvrGameFolder $Selection
    $lock=Enter-FvrLock $InstallRoot
    try{
        # Reread under the operation lock: never overwrite backend settings with
        # the stale copy that happened to be visible when the launcher opened.
        $state=Read-FvrLauncherSettingsLocked $InstallRoot
        if($state.needs_reset -and -not $ResetInvalidSettings){throw 'Settings are damaged. Use Reset and choose game folder to confirm recovery; the original backup is retained.'}
        $state.settings.game_path=$folder
        $path=Get-FvrChild $InstallRoot 'data/settings.json'
        $null=[IO.Directory]::CreateDirectory((Split-Path $path -Parent))
        Write-FvrLauncherSettings $path $state.settings
        if($state.needs_reset){$state.notice="Settings reset. Original settings, including any local-checkpoint profile, are saved at $($state.backup)."}
        elseif(-not $state.notice){$state.notice='Game folder saved.'}
        $state.needs_reset=$false;return $state
    }finally{$lock.Dispose()}
}
function Save-FvrSteamCompatibility([string]$InstallRoot,[bool]$Enabled) {
    $lock=Enter-FvrLock $InstallRoot
    try{
        # Keep installer/backend updates made since the window opened. A click
        # is not permission to repair or reset malformed settings.
        $state=Read-FvrLauncherSettingsLocked $InstallRoot -RefuseRepair
        $state.settings.steam_compatibility=$Enabled
        $path=Get-FvrChild $InstallRoot 'data/settings.json'
        $null=[IO.Directory]::CreateDirectory((Split-Path $path -Parent))
        Write-FvrLauncherSettings $path $state.settings
        $state.notice='Steam compatibility saved.'
        return $state
    }finally{$lock.Dispose()}
}
function Read-FvrBoundedSessionJson([string]$Path) {
    Assert-FvrNoLinks $Path
    $file=[IO.File]::Open($Path,[IO.FileMode]::Open,[IO.FileAccess]::Read,([IO.FileShare]::ReadWrite -bor [IO.FileShare]::Delete))
    try{
        if($file.Length -gt 32768){throw 'Session record exceeds 32 KiB.'}
        $bytes=[byte[]]::new(32769);$count=0
        do{$read=$file.Read($bytes,$count,$bytes.Length-$count);$count+=$read}while($read -gt 0 -and $count -lt $bytes.Length)
        if($count -gt 32768){throw 'Session record exceeds 32 KiB.'}
        $value=ConvertFrom-Json -InputObject ([Text.Encoding]::UTF8.GetString($bytes,0,$count).TrimStart([char]0xfeff)) -AsHashtable -DateKind String
        if($value -isnot [Collections.IDictionary]){throw 'Session record must be an object.'}
        return $value
    }finally{$file.Dispose()}
}
function Get-FvrRuntimeSessionNotice([string]$InstallRoot,[string]$Version,$Operation) {
    # A terminal runtime notice may replace only its successful Launch status.
    # Updates, setup checks and feedback keep their own status. No process is
    # inspected or acted on; identity is joined between two owned records.
    try{
        if($Operation.action -ne 'Launch' -or $Operation.state -ne 'started'){return $null}
        $operationUtc=[DateTimeOffset]::Parse($Operation.utc)
        $runtime=Get-FvrChild $InstallRoot 'data/runtime'
        $active=Read-FvrBoundedSessionJson (Join-Path $runtime 'active-session.json')
        if($active.schema -ne 'bc2vr.active-session.v1' -or $active.version -cne $Version -or $active.relative_folder -notmatch '^sessions/[A-Za-z0-9_-]+$'){return $null}
        $folder=Get-FvrChild $runtime $active.relative_folder
        $session=Read-FvrBoundedSessionJson (Join-Path $folder 'session.json')
        if($session.schema -ne 'bc2vr.session.v1' -or $session.version -cne $Version -or $session.state -notin @('failed','ended','stopped')){return $null}
        if($active.supervisor.pid -ne $session.supervisor.pid -or $active.supervisor.start_utc -cne $session.supervisor.start_utc -or $active.supervisor.path -cne $session.supervisor.path){return $null}
        $ended=[DateTimeOffset]::Parse($session.ended_utc)
        $started=[DateTimeOffset]::Parse($session.started_utc)
        if($started -gt $ended -or $started -gt $operationUtc){return $null}
        if($Operation.Contains('started_utc') -and $Operation.started_utc){
            # Entry can observe running immediately before the supervisor fails,
            # then Worker publishes its successful operation a moment later.
            # Join to the launch's beginning, not that final publication time.
            if($started -lt [DateTimeOffset]::Parse($Operation.started_utc)){return $null}
        }elseif($ended -lt $operationUtc){return $null} # Conservative legacy record.
        $message=if($session.state -eq 'failed'){
            if($session.error -is [string] -and $session.error){'VR session failed: '+$session.error.Substring(0,[Math]::Min(2048,$session.error.Length))}
            else{'The VR session failed. Use Report feedback to save its logs.'}
        }elseif($session.state -eq 'stopped'){'The VR session stopped. BC2 may still be open.'}else{'The VR session ended. BC2 may still be open.'}
        return @{state=$session.state;message=$message;ended_utc=$session.ended_utc}
    }catch{return $null} # Incomplete atomic replacement or untrusted/old record.
}
