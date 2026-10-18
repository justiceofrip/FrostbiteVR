#requires -Version 7.5
# Portable runtime policy. All mutable files live under the chosen data drive.
Set-StrictMode -Version 3
function Get-Bc2RuntimeConfig {
    $config=Read-FvrJson (Join-Path $PSScriptRoot 'runtime.json')
    if($config.schema -ne 'bc2vr.runtime.v1' -or $config.diagnostic_only -or $config.source_sha256 -notmatch '^[a-f0-9]{64}$'){throw 'Unsupported runtime package.'}
    foreach($file in $config.files){
        $path=Get-FvrChild $PSScriptRoot $file.path; Assert-FvrNoLinks $path
        if((Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant() -ne $file.sha256){throw "Runtime file changed: $($file.path)"}
    }
    return $config
}
function Resolve-Bc2Tool([string]$Override,[string[]]$Names,[string]$Label) {
    if($Override){if(-not [IO.Path]::IsPathFullyQualified($Override) -or -not(Test-Path -LiteralPath $Override -PathType Leaf)){throw "$Label override must name an existing executable."};return $Override}
    foreach($name in $Names){$cmd=Get-Command $name -CommandType Application -ErrorAction SilentlyContinue|Select-Object -First 1;if($cmd -and $cmd.Source -notmatch '[\\/]WindowsApps[\\/]'){return $cmd.Source}}
    throw "$Label is required for first-time local asset preparation. Install it on your data drive and add it to PATH (or set FVR_PYTHON/FVR_FFMPEG)."
}
function Get-Bc2ProcessIdentity($Process) {
    $null=$Process.Handle
    return @{pid=$Process.Id;start_utc=$Process.StartTime.ToUniversalTime().ToString('o');path=$Process.Path}
}
function Find-Bc2OwnedProcess($Identity) {
    if(-not $Identity){return $null}
    $p=Get-Process -Id $Identity.pid -ErrorAction SilentlyContinue
    if(-not $p){return $null}
    try {if($p.StartTime.ToUniversalTime() -ne [DateTimeOffset]::Parse($Identity.start_utc).UtcDateTime -or $p.Path -ne $Identity.path){return $null};$null=$p.Handle;return $p}catch{return $null}
}
function ConvertTo-Bc2CommandArgument([string]$Value) {
    if($Value.Contains([char]0)){throw 'NUL in process argument.'}
    # CommandLineToArgvW quoting, including quotes and trailing backslashes.
    return '"'+[regex]::Replace([regex]::Replace($Value,'(\\*)"','$1$1\"'),'(\\+)$','$1$1')+'"'
}
function Start-Bc2Child([string]$File,[string[]]$Arguments,[string]$Folder,[string]$LogName,[hashtable]$Environment=@{}) {
    $options=@{FilePath=$File;WorkingDirectory=$Folder;WindowStyle='Hidden';PassThru=$true;Environment=$Environment
        RedirectStandardOutput=(Join-Path $Folder ($LogName+'.stdout.log'));RedirectStandardError=(Join-Path $Folder ($LogName+'.stderr.log'))}
    if($Arguments.Count){$options.ArgumentList=(@($Arguments|ForEach-Object{ConvertTo-Bc2CommandArgument $_}) -join ' ')}
    $p=Start-Process @options;$null=$p.Handle;return $p
}
function Get-Bc2Session([string]$StateRoot) {
    $record=Join-Path $StateRoot 'active-session.json'
    if(-not(Test-Path -LiteralPath $record)){return $null}
    $active=Read-FvrJson $record
    $folder=Get-FvrChild $StateRoot $active.relative_folder; Assert-FvrNoLinks $folder
    if(-not $active.relative_folder.StartsWith('sessions/')){throw 'Invalid session path.'}
    $active.folder=$folder
    return $active
}
function Assert-Bc2RuntimeSpace([string]$StateRoot) {
    Assert-FvrNoLinks $StateRoot
    Assert-FvrFreeSpace $StateRoot 768MB
    # No silent deletion of failure evidence. Internal session accumulation is
    # capped; the launcher explains what to remove before starting more work.
    [long]$used=0
    if(Test-Path -LiteralPath $StateRoot){foreach($f in Get-ChildItem -LiteralPath $StateRoot -File -Recurse){if($f.Attributes -band [IO.FileAttributes]::ReparsePoint){throw 'Linked runtime evidence refused.'};$used+=$f.Length}}
    if($used+768MB -gt 4GB){throw 'BC2 VR runtime evidence reached its 4 GiB cap. Keep wanted feedback logs, then remove old folders under data/runtime/sessions before launching again.'}
}
function Get-Bc2AudioEnvironment([string]$Cache) {
    # This map is supplied only to the fresh game process. Keep the validated
    # runtime cloud correction enabled and exclude inherited diagnostic modes.
    return @{BC2_MECHANISM_AUDIO_REPLACE_SPAS='1';BC2_MECHANISM_AUDIO_CACHE=(Join-Path $Cache 'pump');BC2_SHELL_RELOAD_AUDIO_REPLACE='1';BC2_SHELL_RELOAD_AUDIO_CACHE=(Join-Path $Cache 'shell');
        BC2_WEATHER_TRANSLATION='1';BC2_WEATHER_OBSERVER=$null;BC2_WEATHER_TRANSLATION_EXPERIMENT=$null;BC2_WEATHER_OBSERVER_DELAY_MS=$null}
}
function Test-Bc2AssetCache([string]$Cache,[string]$Recipe,[string]$GameHash) {
    $record=Join-Path $Cache 'assets.json'
    if(-not(Test-Path -LiteralPath $record)){return $false}
    try {
        $manifest=Read-FvrJson $record
        if($manifest.schema -ne 'bc2vr.local-assets.v1' -or $manifest.recipe_sha256 -ne $Recipe -or $manifest.game_sha256 -ne $GameHash -or $manifest.files.Count -ne 6){return $false}
        foreach($f in $manifest.files){$path=Get-FvrChild $Cache $f.path;Assert-FvrNoLinks $path;if((Get-FileHash -LiteralPath $path).Hash.ToLowerInvariant() -ne $f.sha256){return $false}}
        return $true
    } catch{return $false}
}
