#requires -Version 7.5
# Public startup checks use the same runtime choice as the x64 OpenXR loader.
# No registry, machine environment, game executable or game settings are changed.
function Get-Bc2RegisteredOpenXrRuntime {
    $base=[Microsoft.Win32.RegistryKey]::OpenBaseKey([Microsoft.Win32.RegistryHive]::LocalMachine,[Microsoft.Win32.RegistryView]::Registry64)
    try {$key=$base.OpenSubKey('SOFTWARE\Khronos\OpenXR\1');try {if($key){return [string]$key.GetValue('ActiveRuntime')}}finally {if($key){$key.Dispose()}}}finally {$base.Dispose()}
}
function Get-Bc2OpenXrSelection {
    $source='Windows active OpenXR runtime'
    $manifest=$env:XR_RUNTIME_JSON
    if($manifest){$source='XR_RUNTIME_JSON override'}else{$manifest=Get-Bc2RegisteredOpenXrRuntime}
    if(-not $manifest){throw 'No active OpenXR runtime is configured. Select SteamVR, Virtual Desktop VDXR, or your headset software as the OpenXR runtime, connect your headset, then try again.'}
    if(-not [IO.Path]::IsPathFullyQualified($manifest) -or -not(Test-Path -LiteralPath $manifest -PathType Leaf)){throw "The $source points to a missing runtime manifest. Repair the runtime selection in your headset software, then retry."}
    $json=Read-FvrJson $manifest
    if(-not $json.ContainsKey('runtime') -or -not $json.runtime.ContainsKey('library_path') -or -not $json.runtime.library_path){throw 'The selected OpenXR runtime manifest has no runtime library. Repair your headset software.'}
    $library=[string]$json.runtime.library_path
    if(-not [IO.Path]::IsPathFullyQualified($library)){$library=[IO.Path]::GetFullPath((Join-Path (Split-Path $manifest -Parent) $library))}
    if(-not(Test-Path -LiteralPath $library -PathType Leaf)){throw 'The selected OpenXR runtime library is missing. Repair your headset software.'}
    $label=if($json.runtime.ContainsKey('name') -and $json.runtime.name){[string]$json.runtime.name}else{[IO.Path]::GetFileNameWithoutExtension($manifest)}
    return @{source=$source;manifest=[IO.Path]::GetFullPath($manifest);library=$library;label=$label}
}
function Get-Bc2ExecutablePeLayout([string]$Exe) {
    # File-only structural evidence. It does not verify native signatures or
    # authorize a hook. Read at most the headers and 96 section records.
    $result=@{schema='bc2vr.executable-pe.v1';valid=$false;file_bytes=$null;machine=$null;timestamp=$null;optional_magic=$null;image_base=$null;image_size=$null;entry_rva=$null;characteristics=$null;dll_characteristics=$null;section_count=$null;sections=@();error=$null}
    $file=$null
    try {
        $file=[IO.File]::Open($Exe,'Open','Read',[IO.FileShare]::Read)
        $result.file_bytes=$file.Length
        if($file.Length -lt 64 -or $file.Length -gt 512MB){throw 'Executable length is outside the supported inspection limit.'}
        $readAt={param([long]$Offset,[int]$Count)
            if($Offset -lt 0 -or $Count -lt 0 -or $Offset -gt $file.Length -or $Count -gt $file.Length-$Offset){throw 'PE structure extends outside the executable.'}
            $null=$file.Seek($Offset,[IO.SeekOrigin]::Begin);$buffer=[byte[]]::new($Count);$read=0
            while($read -lt $Count){$n=$file.Read($buffer,$read,$Count-$read);if($n -le 0){throw 'PE structure is truncated.'};$read+=$n}
            return ,$buffer
        }
        $dos=& $readAt 0 64
        if([BitConverter]::ToUInt16($dos,0) -ne 0x5a4d){throw 'Executable has no MZ header.'}
        $nt=[long][BitConverter]::ToUInt32($dos,60)
        if($nt -lt 64){throw 'PE header offset is invalid.'}
        $coff=& $readAt $nt 24
        if([BitConverter]::ToUInt32($coff,0) -ne 0x4550){throw 'Executable has no PE signature.'}
        $result.machine=[int][BitConverter]::ToUInt16($coff,4)
        $result.timestamp=[long][BitConverter]::ToUInt32($coff,8)
        $result.characteristics=[int][BitConverter]::ToUInt16($coff,22)
        $result.section_count=[int][BitConverter]::ToUInt16($coff,6)
        $optionalBytes=[int][BitConverter]::ToUInt16($coff,20)
        if($result.section_count -lt 1 -or $result.section_count -gt 96 -or $optionalBytes -lt 224 -or $optionalBytes -gt 4096){throw 'PE section count or optional-header size is invalid.'}
        $optional=& $readAt ($nt+24) $optionalBytes
        $result.optional_magic=[int][BitConverter]::ToUInt16($optional,0)
        $result.image_base=[long][BitConverter]::ToUInt32($optional,28)
        $result.image_size=[long][BitConverter]::ToUInt32($optional,56)
        $result.entry_rva=[long][BitConverter]::ToUInt32($optional,16)
        $result.dll_characteristics=[int][BitConverter]::ToUInt16($optional,70)
        $headers=[long][BitConverter]::ToUInt32($optional,60)
        $tableAt=$nt+24+$optionalBytes;$tableBytes=40*$result.section_count
        if($headers -lt $tableAt+$tableBytes -or $headers -gt $file.Length -or $headers -gt $result.image_size){throw 'PE header extent is invalid.'}
        $table=& $readAt $tableAt $tableBytes
        for($i=0;$i -lt $result.section_count;$i++){
            $at=$i*40
            $section=@{index=$i;rva=[long][BitConverter]::ToUInt32($table,$at+12);virtual_bytes=[long][BitConverter]::ToUInt32($table,$at+8);raw_offset=[long][BitConverter]::ToUInt32($table,$at+20);raw_bytes=[long][BitConverter]::ToUInt32($table,$at+16);characteristics=[long][BitConverter]::ToUInt32($table,$at+36)}
            $extent=[Math]::Max($section.virtual_bytes,$section.raw_bytes)
            $result.sections+=,$section
            if($section.raw_offset -gt $file.Length -or $section.raw_bytes -gt $file.Length-$section.raw_offset -or
                $section.rva -gt $result.image_size -or $extent -gt $result.image_size-$section.rva -or
                ($section.raw_bytes -gt 0 -and $section.raw_offset -lt $headers) -or ($extent -gt 0 -and $section.rva -lt $headers)){throw 'PE section bounds overlap headers or extend outside the file/image.'}
            for($j=0;$j -lt $i;$j++){
                $prior=$result.sections[$j];$priorExtent=[Math]::Max($prior.virtual_bytes,$prior.raw_bytes)
                if(($extent -gt 0 -and $priorExtent -gt 0 -and $section.rva -lt $prior.rva+$priorExtent -and $prior.rva -lt $section.rva+$extent) -or
                    ($section.raw_bytes -gt 0 -and $prior.raw_bytes -gt 0 -and $section.raw_offset -lt $prior.raw_offset+$prior.raw_bytes -and $prior.raw_offset -lt $section.raw_offset+$section.raw_bytes)){throw 'PE sections overlap.'}
            }
        }
        if($result.machine -ne 0x14c -or $result.optional_magic -ne 0x10b){throw 'Experimental Steam startup requires a PE32/I386 executable.'}
        if($result.timestamp -ne 1314210909 -or $result.image_base -ne 0x400000 -or $result.image_size -ne 0x1913000){throw 'Experimental Steam executable layout differs from native build 795745.'}
        $result.valid=$true
    } catch {
        # Our fixed validation messages are safe; filesystem exception messages
        # can contain a player's private installation path and are not exported.
        $result.error=if($_.Exception -is [System.Management.Automation.MethodInvocationException] -or $_.Exception -is [IO.IOException] -or $_.Exception -is [UnauthorizedAccessException]){'Executable could not be read for PE inspection.'}else{$_.Exception.Message}
    } finally {if($file){$file.Dispose()}}
    return $result
}
function Test-Bc2ExecutableAdmission {
    param([string]$Exe,[string]$Hash,[hashtable]$Config,$ExperimentalSteam=$false)
    $candidate='22e96001382eb847d3ab0a02b59ec7bb79ba47e04146e74c6c49e38465f38c96'
    if($Hash -cne $candidate -and $Hash -in $Config.game_sha256){
        return @{experimental_steam=$false;profile='verified_release';game_verified=$true;pe_layout=$null}
    }
    $pe=Get-Bc2ExecutablePeLayout $Exe
    if($Hash -cne $candidate){
        $message='This BC2 executable has not been verified for this pre-alpha. No VR hook was attached and the game was not changed. Use Report feedback > Save diagnostics to share its build fingerprint. SHA-256: '+$Hash
    }elseif($ExperimentalSteam -isnot [bool] -or -not $ExperimentalSteam){
        $message='This Steam executable is an experimental candidate. Enable the experimental Steam option in the launcher to try it. Native compatibility is not verified; no game was started. SHA-256: '+$Hash
    }elseif(-not $pe.valid){
        $message='Experimental Steam startup refused: '+$pe.error+' No game was started. Use Report feedback > Save diagnostics.'
    }else{
        # Recheck the exact candidate after the bounded file-only inspection.
        $rechecked=$null
        try {$rechecked=(Get-FileHash -LiteralPath $Exe -Algorithm SHA256).Hash.ToLowerInvariant()}catch{}
        if($null -eq $rechecked){
            $message='BC2 executable could not be rechecked during experimental Steam admission.'
        }elseif($rechecked -cne $Hash){
            $message='BC2 executable changed during experimental Steam admission.'
        }else{return @{experimental_steam=$true;profile='experimental_steam_795745';game_verified=$false;pe_layout=$pe}}
    }
    $failure=[InvalidOperationException]::new($message);$failure.Data['bc2_pe_layout']=$pe;throw $failure
}
function Test-Bc2GameInstallation([hashtable]$Settings,[hashtable]$Config,[string]$StateRoot) {
    $report=@{schema='bc2vr.launch-check.v1';utc=[DateTime]::UtcNow.ToString('o');version=$Config.version;game_found=$false;game_supported=$false;game_verified=$false;experimental_steam=$false;profile=$null;pe_layout=$null;game_sha256=$null;file_version=$null;error=$null}
    try {
        if(-not $Settings.game_path -or -not [IO.Path]::IsPathFullyQualified($Settings.game_path)){throw 'Choose your Battlefield Bad Company 2 installation folder using Choose game folder in the launcher.'}
        $exe=Join-Path $Settings.game_path 'BFBC2Game.exe'
        if(-not(Test-Path -LiteralPath $exe -PathType Leaf)){throw 'BFBC2Game.exe was not found in the selected folder. Use Choose game folder to select the game installation.'}
        $report.game_found=$true
        $report.game_sha256=(Get-FileHash -LiteralPath $exe -Algorithm SHA256).Hash.ToLowerInvariant()
        $report.file_version=[Diagnostics.FileVersionInfo]::GetVersionInfo($exe).FileVersion
        $steam=$Settings.ContainsKey('steam_compatibility') -and $Settings.steam_compatibility -is [bool] -and $Settings.steam_compatibility
        $admission=Test-Bc2ExecutableAdmission -Exe $exe -Hash $report.game_sha256 -Config $Config -ExperimentalSteam $steam
        $report.game_supported=$true;$report.game_verified=$admission.game_verified;$report.experimental_steam=$admission.experimental_steam
        $report.profile=$admission.profile;$report.pe_layout=$admission.pe_layout
        return @{exe=$exe;sha256=$report.game_sha256;experimental_steam=$admission.experimental_steam;profile=$admission.profile;game_verified=$admission.game_verified;pe_layout=$admission.pe_layout}
    } catch {
        $report.error=$_.Exception.Message
        if($_.Exception.Data.Contains('bc2_pe_layout')){$report.pe_layout=$_.Exception.Data['bc2_pe_layout']}
        throw
    }
    finally {Write-FvrJson (Join-Path $StateRoot 'launch-check.json') $report}
}
function Invoke-Bc2OpenXrPreflight([string]$RuntimeRoot,[string]$StateRoot,[hashtable]$Selection) {
    Write-Information ('BC2VR: Checking headset through '+$Selection.label+'…') -InformationAction Continue
    # The loader starts the selected runtime when required. Do not start SteamVR
    # alongside VDXR, overwrite ActiveRuntime, or require Steam to be installed.
    $p=Start-Bc2Child (Join-Path $RuntimeRoot 'bin/BC2XrHost.exe') @('--probe','--loader',(Join-Path $RuntimeRoot 'bin/openxr_loader.dll')) $StateRoot 'xr-preflight' @{XR_RUNTIME_JSON=$Selection.manifest}
    if(-not $p.WaitForExit(45000)){$p.Kill();$null=$p.WaitForExit(3000);throw 'The selected OpenXR runtime did not respond. Connect your headset to its PC streaming app and retry. See Report feedback for diagnostics.'}
    $result=$null
    try {$result=Read-FvrJson (Join-Path $StateRoot 'xr-preflight.stdout.log')}catch{}
    if($p.ExitCode -ne 0 -or -not $result -or -not $result.instance_created -or -not $result.system_available){
        $detail=if($result -and $result.error){[string]$result.error}else{'The headset runtime exited before reporting a connected headset.'}
        throw ('OpenXR headset check failed using '+$Selection.label+': '+$detail+' Connect your headset and select the matching OpenXR runtime in SteamVR, Virtual Desktop, or your headset software. The game has not been started.')
    }
    return $result
}
function Get-Bc2NativeStartupSample([string]$Path) {
    if(-not(Test-Path -LiteralPath $Path -PathType Leaf)){return $null}
    $file=[IO.File]::Open($Path,'Open','Read',([IO.FileShare]::ReadWrite -bor [IO.FileShare]::Delete))
    try {
        $size=[int][Math]::Min(65536,$file.Length);$null=$file.Seek(-$size,[IO.SeekOrigin]::End)
        $buffer=[byte[]]::new($size);$read=$file.Read($buffer,0,$size)
        $lines=[Text.Encoding]::UTF8.GetString($buffer,0,$read).Split([char]10)
        for($i=$lines.Count-1;$i -ge 0;$i--){
            try {$row=$lines[$i] | ConvertFrom-Json -AsHashtable -ErrorAction Stop}catch{continue}
            if($row -isnot [hashtable] -or -not $row.ContainsKey('input_samples') -or -not $row.ContainsKey('gpu_failure_stage') -or -not $row.ContainsKey('camera_restore_failures')){continue}
            if($row.gpu_failure_stage -ne 0 -or $row.camera_restore_failures -ne 0){throw 'The native VR graphics bridge reported a startup error. Use Report feedback to save its logs.'}
            $menu=if($row.ContainsKey('menu_connection')){$row.menu_connection}else{$null}
            $menuComplete=$menu -is [hashtable] -and $menu.ContainsKey('ready') -and
                $menu.ContainsKey('input_samples') -and $menu.ContainsKey('published') -and $menu.ContainsKey('graphics_failures')
            if($menu -is [hashtable] -and $menu.ContainsKey('graphics_failures') -and $menu.graphics_failures -ne 0){throw 'The native VR menu bridge reported a startup error. Use Report feedback to save its logs.'}
            if($row.ContainsKey('utc_filetime_100ns')){
                $age=[DateTime]::UtcNow.ToFileTimeUtc()-[long]$row.utc_filetime_100ns
                if($age -lt 0 -or $age -gt 30000000){return $null}
            }
            if($row.input_samples -gt 0 -or
               ($menuComplete -and $menu.ready -is [bool] -and $menu.ready -and
                $menu.input_samples -gt 0 -and $menu.published -gt 0 -and
                $menu.ContainsKey('graphics_failures') -and $menu.graphics_failures -eq 0)){return $row}
            # Only the newest complete sample may establish current readiness.
            # A partial trailing write is skipped; an older ready sample cannot
            # override a newer loss of focus/menu ownership.
            return $null
        }
    } finally {$file.Dispose()}
    return $null
}
function Read-Bc2NativeFailureError([string]$Path) {
    $file=$null
    try {
        Assert-FvrNoLinks $Path
        if(-not(Test-Path -LiteralPath $Path -PathType Leaf)){return $null}
        $file=[IO.File]::Open($Path,'Open','Read',([IO.FileShare]::ReadWrite -bor [IO.FileShare]::Delete))
        # Normal completed traces can be large. Read only a complete small
        # failure object; a partial write or unrelated trace uses log fallback.
        $count=$file.Length;if($count -le 0 -or $count -gt 65536){return $null}
        $bytes=[byte[]]::new([int]$count);$read=0
        while($read -lt $count){$n=$file.Read($bytes,$read,[int]$count-$read);if($n -eq 0){return $null};$read+=$n}
        if($file.Length -ne $count){return $null}
        $row=[Text.UTF8Encoding]::new($false,$true).GetString($bytes) | ConvertFrom-Json -AsHashtable -Depth 8 -ErrorAction Stop
        if($row -isnot [hashtable] -or $row.state -cne 'failed' -or $row.error -isnot [string] -or -not $row.error.Trim()){return $null}
        $detail=($row.error -replace '[\x00-\x1F\x7F]',' ').Trim()
        return $detail.Substring(0,[Math]::Min(1600,$detail.Length))
    } catch {return $null}
    finally {if($file){$file.Dispose()}}
}
function Get-Bc2ActivationProcessOutcome($OriginalProcess) {
    # Only the retained original Process can distinguish exit from a failed
    # identity/access check. Never infer exit from a missing lookup result or
    # read ExitCode while the original process is still live.
    $result=@{schema='bc2vr.activation-process.v1';ownership_verified=$false;original_process_state='unavailable';exit_observed=$false;exit_code=$null}
    try {$exited=$OriginalProcess.HasExited}catch{return $result}
    if($exited -isnot [bool]){return $result}
    if(-not $exited){$result.original_process_state='live';return $result}
    $result.original_process_state='exited';$result.exit_observed=$true
    try {
        $code=$OriginalProcess.ExitCode
        if(($code -is [int] -or $code -is [long] -or $code -is [uint32]) -and
           [long]$code -ge [long][int]::MinValue -and [long]$code -le [long][uint32]::MaxValue){
            # Process.ExitCode is signed Int32; retain the Windows DWORD value
            # for comparison with native diagnostics, including a real zero.
            $result.exit_code=([long]$code -band [long]4294967295)
        }
    }catch{} # A proven exit can still have an unavailable exit code.
    return $result
}
function Get-Bc2StartupFailure([string]$Folder,[string]$Component,[string]$Fallback) {
    if($Component -eq 'native'){
        $detail=Read-Bc2NativeFailureError (Join-Path $Folder 'native-trace.json')
        if($detail){return ($Fallback+' '+$detail+' Use Report feedback to save diagnostics.')}
    }
    foreach($name in @(($Component+'.stderr.log'),($Component+'.stdout.log'),($Component+'.log'))){
        $path=Join-Path $Folder $name
        if(Test-Path -LiteralPath $path -PathType Leaf){
            $text=(Get-Content -LiteralPath $path -Tail 8 -ErrorAction SilentlyContinue) -join ' '
            if($text){return ($Fallback+' '+$text.Substring(0,[Math]::Min(1600,$text.Length))+' Use Report feedback to save diagnostics.')}
        }
    }
    return $Fallback+' Use Report feedback to save diagnostics.'
}

function Save-Bc2AssetPreparationResult([string]$Cache,[string]$StateRoot,[string]$Recipe,[string]$GameHash,[int]$ExitCode,[bool]$CacheValid) {
    $report=@{schema='bc2vr.asset-preparation.v1';utc=[DateTime]::UtcNow.ToString('o');recipe_sha256=$Recipe;game_sha256=$GameHash;state='failed';stage='asset tool startup';error=$null;game_started=$false;exit_code=$ExitCode;cache_valid=$CacheValid}
    $path=Get-FvrChild $Cache 'preparation.json'
    try {
        Assert-FvrNoLinks $path
        if((Test-Path -LiteralPath $path -PathType Leaf) -and (Get-Item -LiteralPath $path).Length -le 8192){
            $detail=Read-FvrJson $path
            if($detail.schema -eq $report.schema -and $detail.recipe_sha256 -eq $Recipe -and $detail.game_sha256 -eq $GameHash){
                foreach($key in @('stage','state','error')){
                    if($null -ne $detail[$key]){$text=([string]$detail[$key]) -replace '[\r\n]',' ';$report[$key]=$text.Substring(0,[Math]::Min(1200,$text.Length))}
                }
            }
        }
    } catch {} # Missing/malformed diagnostics never authorize an unverified cache.
    if($ExitCode -eq 0 -and $CacheValid){$report.state='complete';$report.stage='complete';$report.error=$null}
    else {
        $report.state='failed'
        if(-not $report.error){
            if($ExitCode -ne 0){$report.error="Preparation process exited with code $ExitCode. Its detailed output is in launcher-last.log."}
            else{$report.stage='cache verification';$report.error='Generated assets did not pass content verification.'}
        }
    }
    Write-FvrJson (Get-FvrChild $StateRoot 'asset-preparation.json') $report
    return $report
}

function Read-Bc2NativeRunOutcome([string]$Path,[uint32]$ExpectedPid) {
    $file=$null
    try {
        Assert-FvrNoLinks $Path
        if(-not(Test-Path -LiteralPath $Path -PathType Leaf)){return $null}
        $file=[IO.File]::Open($Path,'Open','Read',([IO.FileShare]::ReadWrite -bor [IO.FileShare]::Delete))
        $count=$file.Length;if($count -le 0 -or $count -gt 8192){return $null}
        $bytes=[byte[]]::new([int]$count);$read=0
        while($read -lt $count){$n=$file.Read($bytes,$read,[int]$count-$read);if($n -eq 0){return $null};$read+=$n}
        if($file.Length -ne $count){return $null}
        $row=[Text.UTF8Encoding]::new($false,$true).GetString($bytes)|ConvertFrom-Json -AsHashtable -Depth 4 -ErrorAction Stop
        if($row -isnot [hashtable] -or $row.schema -cne 'bc2vr.native-run-outcome.v1'){return $null}
        foreach($key in @('pid','outcome','remote_thread_exit_observed','probe_exit','game_exit_code','report_bytes','report_nonempty')){
            if(-not $row.ContainsKey($key)){return $null}
        }
        if(($row.pid -isnot [long] -and $row.pid -isnot [int]) -or $row.pid -ne $ExpectedPid -or $row.pid -le 0){return $null}
        if($row.remote_thread_exit_observed -isnot [bool]){return $null}
        foreach($key in @('probe_exit','game_exit_code')){
            $value=$row[$key]
            if($null -ne $value -and (($value -isnot [long] -and $value -isnot [int]) -or $value -lt 0 -or $value -gt [uint32]::MaxValue)){return $null}
        }
        if($row.outcome -ceq 'game_exited'){
            if($row.remote_thread_exit_observed -or $null -ne $row.probe_exit){return $null}
        }elseif($row.outcome -ceq 'remote_thread_exited'){
            if(-not $row.remote_thread_exit_observed -or $null -eq $row.probe_exit -or $null -ne $row.game_exit_code){return $null}
        }else{return $null}
        if($null -eq $row.report_bytes){if($null -ne $row.report_nonempty){return $null}}
        elseif(($row.report_bytes -isnot [long] -and $row.report_bytes -isnot [int]) -or $row.report_bytes -lt 0 -or
            $row.report_nonempty -isnot [bool] -or $row.report_nonempty -ne ($row.report_bytes -gt 0)){return $null}
        return $row
    }catch{return $null}finally{if($file){$file.Dispose()}}
}
