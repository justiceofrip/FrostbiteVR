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
function Test-Bc2GameInstallation([hashtable]$Settings,[hashtable]$Config,[string]$StateRoot) {
    $report=@{schema='bc2vr.launch-check.v1';utc=[DateTime]::UtcNow.ToString('o');version=$Config.version;game_found=$false;game_supported=$false;game_sha256=$null;file_version=$null;error=$null}
    try {
        if(-not $Settings.game_path -or -not [IO.Path]::IsPathFullyQualified($Settings.game_path)){throw 'Choose your Battlefield Bad Company 2 installation folder using Choose game folder in the launcher.'}
        $exe=Join-Path $Settings.game_path 'BFBC2Game.exe'
        if(-not(Test-Path -LiteralPath $exe -PathType Leaf)){throw 'BFBC2Game.exe was not found in the selected folder. Use Choose game folder to select the game installation.'}
        $report.game_found=$true
        $report.game_sha256=(Get-FileHash -LiteralPath $exe -Algorithm SHA256).Hash.ToLowerInvariant()
        $report.file_version=[Diagnostics.FileVersionInfo]::GetVersionInfo($exe).FileVersion
        $report.game_supported=$report.game_sha256 -in $Config.game_sha256
        if(-not $report.game_supported){throw ('This BC2 executable has not been verified for this pre-alpha. No VR hook was attached and the game was not changed. Use Report feedback > Save diagnostics to share its build fingerprint. SHA-256: '+$report.game_sha256)}
        return @{exe=$exe;sha256=$report.game_sha256}
    } catch {$report.error=$_.Exception.Message;throw}
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
            if($row.input_samples -gt 0){return $row}
        }
    } finally {$file.Dispose()}
    return $null
}
function Get-Bc2StartupFailure([string]$Folder,[string]$Component,[string]$Fallback) {
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
