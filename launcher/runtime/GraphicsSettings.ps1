#requires -Version 7.5
# Optional MSAA-only lease. Dot-source after Launcher.Core and Runtime.Core.
# Call recovery before a later launch, even when performance mode is off.
# No game is closed here: Stop VR may leave the bound game and this lease alive.
function Get-Bc2GraphicsSettingsPath {
    return [IO.Path]::GetFullPath((Join-Path ([Environment]::GetFolderPath('MyDocuments')) 'BFBC2/settings.ini'))
}
function Get-Bc2GraphicsHash([byte[]]$Bytes) {
    return [Convert]::ToHexString([Security.Cryptography.SHA256]::HashData($Bytes)).ToLowerInvariant()
}
function Convert-Bc2GraphicsMsaa([byte[]]$Bytes,[ValidateSet('0','1','2','3')][string]$Value='0') {
    # Decode strictly and round-trip before locating the single value. Preserve
    # BOM, line endings, whitespace, comments and every other setting byte.
    $skip=0;$label='utf-8';$encoding=[Text.UTF8Encoding]::new($false,$true)
    if($Bytes.Length -ge 3 -and $Bytes[0] -eq 239 -and $Bytes[1] -eq 187 -and $Bytes[2] -eq 191){$skip=3}
    elseif($Bytes.Length -ge 2 -and $Bytes[0] -eq 255 -and $Bytes[1] -eq 254){$skip=2;$label='utf-16-le';$encoding=[Text.UnicodeEncoding]::new($false,$false,$true)}
    elseif($Bytes.Length -ge 2 -and $Bytes[0] -eq 254 -and $Bytes[1] -eq 255){$skip=2;$label='utf-16-be';$encoding=[Text.UnicodeEncoding]::new($true,$false,$true)}
    try {$text=$encoding.GetString($Bytes,$skip,$Bytes.Length-$skip)}
    catch {
        if($skip){throw 'Invalid BOM-marked graphics settings encoding.'}
        $label='windows-1252';$encoding=[Text.Encoding]::GetEncoding(1252,[Text.EncoderExceptionFallback]::new(),[Text.DecoderExceptionFallback]::new())
        $text=$encoding.GetString($Bytes)
    }
    $body=$encoding.GetBytes($text)
    if($text.Contains([char]0) -or $body.Length -ne $Bytes.Length-$skip){throw 'Graphics settings encoding is not lossless.'}
    for($i=0;$i -lt $body.Length;$i++){if($body[$i] -ne $Bytes[$skip+$i]){throw 'Graphics settings encoding is not lossless.'}}
    $graphics=$false;$sections=0;$keys=0;$offset=-1;$old=$null
    foreach($line in [regex]::Matches($text,'[^\r\n]*(?:\r\n|\r|\n|$)')){
        $content=$line.Value.TrimEnd([char[]]"`r`n")
        $section=[regex]::Match($content,'^\s*\[([^\]]+)\]\s*(?:[;#].*)?$')
        if($section.Success){$graphics=$section.Groups[1].Value.Trim() -ieq 'Graphics';if($graphics){$sections++};continue}
        if($content.TrimStart().StartsWith('[')){throw 'Malformed graphics settings section.'}
        if(-not $graphics -or $content -notmatch '^\s*MSAA\s*='){continue}
        $keys++
        $setting=[regex]::Match($content,'^(?<prefix>[ \t]*MSAA[ \t]*=[ \t]*)(?<value>[0-3])(?<suffix>[ \t]*(?:[;#].*)?)$',[Text.RegularExpressions.RegexOptions]::IgnoreCase)
        if(-not $setting.Success){throw 'Graphics MSAA must be one unambiguous value from 0 to 3.'}
        $offset=$line.Index+$setting.Groups['value'].Index;$old=$setting.Groups['value'].Value
    }
    if($sections -ne 1 -or $keys -ne 1){throw 'Expected exactly one Graphics section and one MSAA key.'}
    $replacement=$encoding.GetBytes($text.Substring(0,$offset)+$Value+$text.Substring($offset+1))
    $result=[byte[]]::new($skip+$replacement.Length)
    [Array]::Copy($Bytes,0,$result,0,$skip);[Array]::Copy($replacement,0,$result,$skip,$replacement.Length)
    return @{bytes=$result;value=$old;encoding=$label}
}
function Write-Bc2GraphicsDurable([string]$Path,[byte[]]$Bytes) {
    Assert-FvrNoLinks $Path
    $stream=[IO.File]::Open($Path,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::None)
    try {$stream.Write($Bytes,0,$Bytes.Length);$stream.Flush($true)} finally {$stream.Dispose()}
}
function Write-Bc2GraphicsAtomic([string]$Path,[byte[]]$Bytes,[string]$ExpectedHash='') {
    Assert-FvrNoLinks $Path
    $temporary=$Path+'.'+[guid]::NewGuid().ToString('N')+'.tmp'
    $preparedHash=Get-Bc2GraphicsHash $Bytes;$originalHash='';$attempts=0;$replaceFailure=$null
    try {
        Write-Bc2GraphicsDurable $temporary $Bytes
        Assert-FvrNoLinks $Path
        $originalExists=[IO.File]::Exists($Path)
        if($originalExists){$originalHash=Get-Bc2GraphicsHash ([IO.File]::ReadAllBytes($Path))}
        $assertUnchanged={
            Assert-FvrNoLinks $Path;Assert-FvrNoLinks $temporary
            $currentHash=''
            if($originalExists){$currentHash=Get-Bc2GraphicsHash ([IO.File]::ReadAllBytes($Path))}
            elseif([IO.File]::Exists($Path)){throw 'Graphics destination appeared before creation; backup retained.'}
            if($currentHash -cne $originalHash -or ($ExpectedHash -and $currentHash -cne $ExpectedHash)){
                throw 'Graphics settings changed before replacement; backup retained.'
            }
            if((Get-Bc2GraphicsHash ([IO.File]::ReadAllBytes($temporary))) -cne $preparedHash){throw 'Graphics temporary replacement changed; backup retained.'}
        }
        # One monotonic one-second budget covers every retry; neither a retry
        # nor a successful integrity check renews it. Creation is never retried.
        $retryClock=[Diagnostics.Stopwatch]::StartNew()
        while($true){
            & $assertUnchanged
            if($attempts -gt 0 -and $retryClock.Elapsed.TotalMilliseconds -ge 1000){throw $replaceFailure}
            try {
                # Replace preserves the existing destination's Windows metadata/ACL;
                # never rewrite permissions or substitute inherited temporary-file ACLs.
                $attempts++
                if($originalExists){[IO.File]::Replace($temporary,$Path,[NullString]::Value)}
                else {[IO.File]::Move($temporary,$Path)}
                break
            } catch {
                $replaceFailure=$_;$cause=$_.Exception.GetBaseException()
                # Win32 sharing/lock violations (32/33), or inability to remove
                # the replaced file (1175), are eligible only when BOTH files
                # still contain their exact pre-attempt bytes. Permissions and
                # unknown/partial replacement outcomes always fail closed.
                if(-not $originalExists -or $cause -isnot [IO.IOException] -or
                   $cause.HResult -notin @(-2147024864,-2147024863,-2147023721)){throw}
                try {& $assertUnchanged}
                catch {$replaceFailure.Exception.Data['graphics_retry_refusal']=$_.Exception.Message;throw $replaceFailure}
                $remaining=1000-$retryClock.Elapsed.TotalMilliseconds
                if($remaining -le 0){throw}
                Start-Sleep -Milliseconds ([int][Math]::Min(50,[Math]::Ceiling($remaining)))
                if($retryClock.Elapsed.TotalMilliseconds -ge 1000){throw}
            }
        }
        Assert-FvrNoLinks $Path
        if((Get-Bc2GraphicsHash ([IO.File]::ReadAllBytes($Path))) -cne $preparedHash){throw 'Graphics replacement verification failed; backup retained.'}
    } catch {
        # Preserve the original failure and replacement semantics while making
        # intermittent filesystem failures diagnosable from the retained error.
        $failure=$_.Exception
        $failure.Data['graphics_path']=$Path;$failure.Data['graphics_temporary_path']=$temporary
        $failure.Data['graphics_expected_sha256']=$ExpectedHash
        $failure.Data['graphics_original_sha256']=$originalHash;$failure.Data['graphics_prepared_sha256']=$preparedHash
        $failure.Data['graphics_attempts']=$attempts
        $failure.Data['graphics_base_hresult']=$failure.GetBaseException().HResult
        $failure.Data['graphics_target_exists']=[IO.File]::Exists($Path)
        $failure.Data['graphics_temporary_exists']=[IO.File]::Exists($temporary)
        try {$failure.Data['graphics_current_sha256']=Get-Bc2GraphicsHash ([IO.File]::ReadAllBytes($Path))}
        catch {$failure.Data['graphics_current_sha256']='unavailable'}
        try {$failure.Data['graphics_temporary_sha256']=Get-Bc2GraphicsHash ([IO.File]::ReadAllBytes($temporary))}
        catch {$failure.Data['graphics_temporary_sha256']='unavailable'}
        throw
    } finally {if([IO.File]::Exists($temporary)){[IO.File]::Delete($temporary)}}
}
function Write-Bc2GraphicsJournal([string]$Path,$Journal) {
    Write-Bc2GraphicsAtomic $Path ([Text.UTF8Encoding]::new($false).GetBytes(($Journal|ConvertTo-Json -Depth 8)))
}
function Get-Bc2GraphicsLeasePaths([string]$StateRoot) {
    if(-not [IO.Path]::IsPathFullyQualified($StateRoot)){throw 'Graphics lease requires an absolute runtime state root.'}
    Assert-FvrNoLinks $StateRoot
    $folder=Get-FvrChild $StateRoot 'graphics-settings';Assert-FvrNoLinks $folder
    return @{folder=$folder;journal=(Join-Path $folder 'lease.json');lock=(Join-Path $folder 'lease.lock')}
}
function Enter-Bc2GraphicsLease($Paths,[DateTime]$Deadline=[DateTime]::MinValue) {
    $null=[IO.Directory]::CreateDirectory($Paths.folder)
    while($true){
        Assert-FvrNoLinks $Paths.lock
        try {return [IO.File]::Open($Paths.lock,[IO.FileMode]::OpenOrCreate,[IO.FileAccess]::ReadWrite,[IO.FileShare]::None)}
        catch {
            $cause=$_.Exception.GetBaseException()
            $remaining=($Deadline-[DateTime]::UtcNow).TotalMilliseconds
            # Only a sharing violation can be the restoration watcher briefly
            # owning this lease. Never reset the caller's fixed deadline or
            # retry permissions, invalid paths, or other I/O failures.
            if($cause -isnot [IO.IOException] -or $cause.HResult -ne -2147024864 -or $remaining -le 0){throw}
            Start-Sleep -Milliseconds ([int][Math]::Min(50,[Math]::Ceiling($remaining)))
            if([DateTime]::UtcNow -ge $Deadline){throw}
        }
    }
}
function Assert-Bc2GraphicsIdentity($Identity,[string]$ExpectedPath='') {
    if($null -eq $Identity -or ($Identity.pid -isnot [int] -and $Identity.pid -isnot [long] -and $Identity.pid -isnot [uint32]) -or [long]$Identity.pid -le 0 -or [long]$Identity.pid -gt [int]::MaxValue -or
       $Identity.path -isnot [string] -or -not [IO.Path]::IsPathFullyQualified($Identity.path) -or
       $Identity.start_utc -isnot [string] -or $Identity.start_utc -cnotmatch '^\d{4}-\d\d-\d\dT\d\d:\d\d:\d\d\.\d{7}Z$'){
        throw 'Graphics lease process identity is incomplete.'
    }
    $null=[DateTimeOffset]::Parse($Identity.start_utc).UtcDateTime.ToFileTimeUtc()
    if($ExpectedPath -and $Identity.path -ine $ExpectedPath){throw 'Graphics lease process path differs.'}
}
function Get-Bc2GraphicsProcessState($Identity) {
    Assert-Bc2GraphicsIdentity $Identity
    $process=$null
    try {
        try {$process=[Diagnostics.Process]::GetProcessById([int]$Identity.pid)}
        catch [ArgumentException] {return 'exited'}
        $actual=Get-Bc2ProcessIdentity $process -RequireLive $true
        if($actual.pid -eq $Identity.pid -and $actual.path -ieq $Identity.path -and
           [DateTimeOffset]::Parse($actual.start_utc).UtcDateTime -eq [DateTimeOffset]::Parse($Identity.start_utc).UtcDateTime){return 'alive'}
        return 'different'
    } catch {
        if($process){try {if($process.HasExited){return 'exited'}}catch{}}
        return 'unknown'
    } finally {if($process){$process.Dispose()}}
}
function Get-Bc2GraphicsLiveGames {
    # Every relevant game blocks restoration, including another installation or
    # an image whose path/birth cannot be queried. Do not swallow enumeration errors.
    foreach($process in @(Get-Process -Name BFBC2Game -ErrorAction SilentlyContinue -ErrorVariable gameErrors)){
        try {
            try {Get-Bc2ProcessIdentity $process -RequireLive $true}
            catch {if(-not $process.HasExited){@{pid=$process.Id;unknown=$true}}}
        } finally {$process.Dispose()}
    }
    foreach($failure in $gameErrors){if($failure.FullyQualifiedErrorId -notlike 'NoProcessFoundForGivenName*'){throw $failure}}
}
function Get-Bc2GraphicsParentPid([int]$ProcessId) {
    $record=Get-CimInstance Win32_Process -Filter "ProcessId=$ProcessId" -ErrorAction Stop
    if(-not $record){throw 'Graphics game parent is unavailable.'}
    return [int]$record.ParentProcessId
}
function Read-Bc2GraphicsLease($Paths) {
    Assert-FvrNoLinks $Paths.journal
    if(-not [IO.File]::Exists($Paths.journal)){return $null}
    if((Get-Item -LiteralPath $Paths.journal).Length -gt 16384){throw 'Graphics lease journal is oversized.'}
    $j=Read-FvrJson $Paths.journal
    if($j.schema -cne 'bc2vr.graphics-settings-lease.v1' -or $j.id -cnotmatch '^[a-f0-9]{32}$' -or
       $j.state -cnotin @('preparing','prepared','bound','restoring','restored') -or $j.original_msaa -cnotmatch '^[0-3]$' -or
       $j.original_sha256 -cnotmatch '^[a-f0-9]{64}$' -or $j.prepared_sha256 -cnotmatch '^[a-f0-9]{64}$' -or
       $j.settings_path -ine (Get-Bc2GraphicsSettingsPath) -or -not [IO.Path]::IsPathFullyQualified($j.game_path) -or
       [IO.Path]::GetFileName($j.game_path) -ine 'BFBC2Game.exe' -or $j.backup -cne ($j.id+'.original.ini')){throw 'Graphics lease journal is invalid.'}
    Assert-Bc2GraphicsIdentity $j.supervisor
    if($j.game){Assert-Bc2GraphicsIdentity $j.game $j.game_path}
    if($j.state -ceq 'bound' -and -not $j.game){throw 'Bound graphics lease has no game identity.'}
    if($j.state -ceq 'restoring' -and ($j.restore_sha256 -cnotmatch '^[a-f0-9]{64}$' -or $j.restore_from_sha256 -cnotmatch '^[a-f0-9]{64}$')){throw 'Graphics restore journal is incomplete.'}
    $backup=Get-FvrChild $Paths.folder $j.backup;Assert-FvrNoLinks $backup
    if((Get-Item -LiteralPath $backup).Length -gt 1MB){throw 'Graphics settings backup is oversized.'}
    $original=[IO.File]::ReadAllBytes($backup)
    if((Get-Bc2GraphicsHash $original) -cne $j.original_sha256){throw 'Graphics backup hash differs; preserve it for manual recovery.'}
    $plan=Convert-Bc2GraphicsMsaa $original
    if($plan.value -cne $j.original_msaa -or (Get-Bc2GraphicsHash $plan.bytes) -cne $j.prepared_sha256){throw 'Graphics backup does not match the prepared lease.'}
    return $j
}
function Get-Bc2GraphicsRestoreBlock($Journal) {
    if($Journal.game){
        $lifetime=Get-Bc2GraphicsProcessState $Journal.game
        if($lifetime -ceq 'alive'){return 'game_alive'}
        if($lifetime -ceq 'unknown'){return 'game_identity_unknown'}
    } else {
        $lifetime=Get-Bc2GraphicsProcessState $Journal.supervisor
        if($lifetime -ceq 'unknown'){return 'supervisor_identity_unknown'}
        # The creating supervisor can clean up its own prelaunch failure. Other
        # launchers/watchers must not restore across its create/bind crash gap.
        if($lifetime -ceq 'alive' -and $Journal.supervisor.pid -ne $PID){return 'supervisor_alive'}
    }
    if(@(Get-Bc2GraphicsLiveGames).Count){return 'relevant_game_alive'}
    return $null
}
function Restore-Bc2GraphicsSettingsLease([string]$StateRoot) {
    $paths=Get-Bc2GraphicsLeasePaths $StateRoot
    if(-not [IO.File]::Exists($paths.journal)){return @{status='none';reason=$null}}
    $lock=$null
    try {
        try {$lock=Enter-Bc2GraphicsLease $paths}
        catch {
            $cause=$_.Exception.GetBaseException()
            if($cause -is [IO.IOException] -and $cause.HResult -in @(-2147024864,-2147024863)){
                return @{status='pending';reason='lease_busy';journal=$paths.journal}
            }
            throw
        }
        $j=Read-Bc2GraphicsLease $paths
        if(-not $j){return @{status='none';reason=$null}}
        if($j.state -ceq 'restored'){return @{status='restored';reason=$null;journal=$paths.journal}}
        $blocked=Get-Bc2GraphicsRestoreBlock $j
        if($blocked){return @{status='pending';reason=$blocked;journal=$paths.journal;backup=(Join-Path $paths.folder $j.backup)}}
        Assert-FvrNoLinks $j.settings_path
        if((Get-Item -LiteralPath $j.settings_path).Length -gt 1MB){throw 'Graphics settings are oversized.'}
        $current=[IO.File]::ReadAllBytes($j.settings_path);$hash=Get-Bc2GraphicsHash $current
        $alreadyRestored=$hash -ceq $j.original_sha256 -or ($j.state -ceq 'restoring' -and $hash -ceq $j.restore_sha256)
        if(-not $alreadyRestored -and $j.original_sha256 -cne $j.prepared_sha256){
            $plan=Convert-Bc2GraphicsMsaa $current $j.original_msaa
            if($plan.value -cne '0'){throw 'Graphics MSAA changed after preparation; restoration pending, backup retained.'}
            # Merge only the leased value into the current file; unrelated user
            # edits survive. A changed/missing/duplicate value is a conflict.
            $j.state='restoring';$j.restore_from_sha256=$hash;$j.restore_sha256=Get-Bc2GraphicsHash $plan.bytes
            Write-Bc2GraphicsJournal $paths.journal $j
            $blocked=Get-Bc2GraphicsRestoreBlock $j
            if($blocked){return @{status='pending';reason=$blocked;journal=$paths.journal}}
            Write-Bc2GraphicsAtomic $j.settings_path $plan.bytes $hash
        }
        $j.state='restored';$j.restored_utc=[DateTime]::UtcNow.ToString('o')
        Write-Bc2GraphicsJournal $paths.journal $j
        return @{status='restored';reason=$null;journal=$paths.journal;backup=(Join-Path $paths.folder $j.backup)}
    } catch {return @{status='pending';reason='conflict_or_unavailable';message=$_.Exception.Message;journal=$paths.journal}}
    finally {if($lock){$lock.Dispose()}}
}
function Start-Bc2GraphicsSettingsLease([string]$StateRoot,[string]$GamePath,[bool]$Enabled=$false) {
    if(-not $Enabled){return @{status='disabled';reason=$null}}
    $paths=Get-Bc2GraphicsLeasePaths $StateRoot
    if(-not [IO.Path]::IsPathFullyQualified($GamePath) -or [IO.Path]::GetFileName($GamePath) -ine 'BFBC2Game.exe'){throw 'Graphics lease requires the exact game executable path.'}
    Assert-FvrNoLinks $GamePath
    $lock=Enter-Bc2GraphicsLease $paths
    try {
        $prior=Read-Bc2GraphicsLease $paths
        if($prior -and $prior.state -cne 'restored'){throw 'Previous graphics settings restoration is pending; recover it before launch.'}
        if(@(Get-Bc2GraphicsLiveGames).Count){throw 'Close BC2 normally before preparing graphics settings.'}
        $settings=Get-Bc2GraphicsSettingsPath;Assert-FvrNoLinks $settings
        if((Get-Item -LiteralPath $settings).Length -gt 1MB){throw 'Graphics settings are oversized.'}
        $original=[IO.File]::ReadAllBytes($settings);$plan=Convert-Bc2GraphicsMsaa $original
        $id=[guid]::NewGuid().ToString('N');$backup=$id+'.original.ini'
        $ownerProcess=Get-Process -Id $PID
        try {$supervisor=Get-Bc2ProcessIdentity $ownerProcess -RequireLive $true} finally {$ownerProcess.Dispose()}
        Assert-Bc2GraphicsIdentity $supervisor
        if($prior){
            $archive=Join-Path $paths.folder ($prior.id+'.completed.json')
            if(-not [IO.File]::Exists($archive)){Write-Bc2GraphicsDurable $archive ([IO.File]::ReadAllBytes($paths.journal))}
        }
        Write-Bc2GraphicsDurable (Join-Path $paths.folder $backup) $original
        $j=@{schema='bc2vr.graphics-settings-lease.v1';id=$id;state='preparing';settings_path=$settings;game_path=[IO.Path]::GetFullPath($GamePath);
            original_msaa=$plan.value;original_sha256=(Get-Bc2GraphicsHash $original);prepared_sha256=(Get-Bc2GraphicsHash $plan.bytes);
            encoding=$plan.encoding;backup=$backup;supervisor=$supervisor;game=$null;created_utc=[DateTime]::UtcNow.ToString('o')}
        # The flushed backup and recoverable journal precede the only mutation.
        Write-Bc2GraphicsJournal $paths.journal $j
        if(@(Get-Bc2GraphicsLiveGames).Count){throw 'BC2 started during graphics preparation; recovery is pending.'}
        if($j.original_sha256 -cne $j.prepared_sha256){Write-Bc2GraphicsAtomic $settings $plan.bytes $j.original_sha256}
        elseif((Get-Bc2GraphicsHash ([IO.File]::ReadAllBytes($settings))) -cne $j.original_sha256){throw 'Graphics settings changed during preparation.'}
        $j.state='prepared';Write-Bc2GraphicsJournal $paths.journal $j
        return @{status='prepared';reason=$null;journal=$paths.journal;backup=(Join-Path $paths.folder $backup);original_msaa=$j.original_msaa}
    } finally {$lock.Dispose()}
}
function Set-Bc2GraphicsSettingsGame([string]$StateRoot,$GameIdentity) {
    $paths=Get-Bc2GraphicsLeasePaths $StateRoot
    $deadline=[DateTime]::UtcNow.AddSeconds(5)
    $lock=Enter-Bc2GraphicsLease $paths -Deadline $deadline
    try {
        $j=Read-Bc2GraphicsLease $paths
        if(-not $j -or $j.state -cne 'prepared' -or $j.game){throw 'Graphics lease is not ready to bind a game.'}
        Assert-Bc2GraphicsIdentity $GameIdentity $j.game_path
        if($j.supervisor.pid -ne $PID -or (Get-Bc2GraphicsProcessState $j.supervisor) -cne 'alive' -or
           (Get-Bc2GraphicsProcessState $GameIdentity) -cne 'alive' -or (Get-Bc2GraphicsParentPid $GameIdentity.pid) -ne $PID){throw 'Graphics lease requires the exact live supervisor-owned game.'}
        Assert-FvrNoLinks $j.settings_path
        if((Get-Bc2GraphicsHash ([IO.File]::ReadAllBytes($j.settings_path))) -cne $j.prepared_sha256){throw 'Prepared graphics settings changed before game binding.'}
        $j.game=@{pid=$GameIdentity.pid;path=$GameIdentity.path;start_utc=$GameIdentity.start_utc};$j.state='bound'
        Write-Bc2GraphicsJournal $paths.journal $j
        return @{status='bound';reason=$null;journal=$paths.journal;game=$j.game}
    } finally {$lock.Dispose()}
}
function Start-Bc2GraphicsSettingsWatcher([string]$StateRoot) {
    $paths=Get-Bc2GraphicsLeasePaths $StateRoot;$j=Read-Bc2GraphicsLease $paths
    if(-not $j -or $j.state -ceq 'restored'){return $null}
    # One hidden durable watcher; it never terminates or sends input to BC2.
    return Start-Bc2Child (Join-Path $PSHOME 'pwsh.exe') @('-NoProfile','-NonInteractive','-ExecutionPolicy','Bypass','-File',(Join-Path $PSScriptRoot 'RestoreGraphicsSettings.ps1'),'-StateRoot',$StateRoot) $paths.folder ('restore-'+$j.id)
}
