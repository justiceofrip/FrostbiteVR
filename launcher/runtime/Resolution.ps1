#requires -Version 7.5
# Pure resolution planning/admission. The native helper owns renderer setup;
# this file never edits a player's settings.ini or modifies a running process.
function Get-Bc2ResolutionInteger([hashtable]$Record,[string]$Name,[long]$Maximum) {
    if(-not $Record.ContainsKey($Name)) {throw "Resolution evidence is missing $Name."}
    $value=$Record[$Name]
    if($value -isnot [int] -and $value -isnot [long] -and $value -isnot [uint32] -and $value -isnot [uint64]) {
        throw "Resolution evidence $Name must be an integer."
    }
    if($value -lt 1 -or $value -gt $Maximum) {throw "Resolution evidence $Name is outside the supported range."}
    return [long]$value
}
function Get-Bc2ResolutionPlan([hashtable]$Preflight) {
    foreach($key in @('instance_created','system_available')) {
        if(-not $Preflight.ContainsKey($key) -or $Preflight[$key] -isnot [bool] -or -not $Preflight[$key]) {
            throw 'A connected headset runtime must supply the render resolution before BC2 is started.'
        }
    }
    $width=Get-Bc2ResolutionInteger $Preflight 'recommended_eye_width' 16384
    $height=Get-Bc2ResolutionInteger $Preflight 'recommended_eye_height' 16384
    $maxWidth=Get-Bc2ResolutionInteger $Preflight 'max_eye_width' 4294967295
    $maxHeight=Get-Bc2ResolutionInteger $Preflight 'max_eye_height' 4294967295
    $selectedWidth=Get-Bc2ResolutionInteger $Preflight 'eye_width' 16384
    $selectedHeight=Get-Bc2ResolutionInteger $Preflight 'eye_height' 16384
    if($width -gt $maxWidth -or $height -gt $maxHeight -or $selectedWidth -ne $width -or $selectedHeight -ne $height) {
        throw 'The headset preflight did not select its supported recommended eye dimensions.'
    }
    if($width*$height -gt 32000000) {
        throw 'The runtime requests more than the tested 32-million-pixel per-eye allocation limit. Adjust its render-resolution setting and retry; BC2 has not been started.'
    }
    return @{schema='bc2vr.render-plan.v1';width=$width;height=$height;
        recommended_width=$width;recommended_height=$height;max_width=$maxWidth;max_height=$maxHeight;
        source='openxr_recommendation';native_source_verified=$false}
}
function Assert-Bc2ResolutionPlan([hashtable]$Plan) {
    if(-not $Plan -or $Plan.schema -cne 'bc2vr.render-plan.v1' -or $Plan.source -cne 'openxr_recommendation' -or
       -not $Plan.ContainsKey('native_source_verified') -or $Plan.native_source_verified -isnot [bool] -or $Plan.native_source_verified) {
        throw 'The headset render plan is missing or invalid. BC2 has not been started.'
    }
    $width=Get-Bc2ResolutionInteger $Plan 'width' 16384
    $height=Get-Bc2ResolutionInteger $Plan 'height' 16384
    $recommendedWidth=Get-Bc2ResolutionInteger $Plan 'recommended_width' 16384
    $recommendedHeight=Get-Bc2ResolutionInteger $Plan 'recommended_height' 16384
    $maxWidth=Get-Bc2ResolutionInteger $Plan 'max_width' 4294967295
    $maxHeight=Get-Bc2ResolutionInteger $Plan 'max_height' 4294967295
    if($width -ne $recommendedWidth -or $height -ne $recommendedHeight -or
       $width -gt $maxWidth -or $height -gt $maxHeight -or $width*$height -gt 32000000) {
        throw 'The headset render plan does not match supported recommended dimensions. BC2 has not been started.'
    }
}
function New-Bc2ResolutionLease([hashtable]$Game,[hashtable]$Supervisor,[string]$DllHash) {
    if($DllHash -cnotmatch '^[a-f0-9]{64}$'){throw 'Invalid resolution helper identity.'}
    return @{schema='bc2vr.resolution-lease.v1';pid=[int]$Game.pid;
        creation_filetime=([DateTimeOffset]::Parse($Game.start_utc).UtcDateTime.ToFileTimeUtc().ToString());
        supervisor_pid=[int]$Supervisor.pid;
        supervisor_creation_filetime=([DateTimeOffset]::Parse($Supervisor.start_utc).UtcDateTime.ToFileTimeUtc().ToString());
        session_token=[guid]::NewGuid().ToString('N');dll_sha256=$DllHash}
}
function Assert-Bc2ResolutionIdentity([hashtable]$Observed,[hashtable]$Lease,[string]$Operation) {
    if($Lease.schema -cne 'bc2vr.resolution-lease.v1' -or $Lease.session_token -cnotmatch '^[a-f0-9]{32}$' -or
       $Lease.dll_sha256 -cnotmatch '^[a-f0-9]{64}$' -or
       $Observed.schema -cne 'bc2vr.native-resolution.v2' -or $Observed.operation -cne $Operation){
        throw 'Unrecognized resolution session evidence.'
    }
    foreach($key in @('pid','supervisor_pid')){
        if((Get-Bc2ResolutionInteger $Observed $key 4294967295) -ne (Get-Bc2ResolutionInteger $Lease $key 4294967295)){
            throw "Resolution evidence belongs to another $key."
        }
    }
    foreach($key in @('creation_filetime','supervisor_creation_filetime','session_token')){
        if(-not $Observed.ContainsKey($key) -or $Observed[$key] -isnot [string] -or $Observed[$key] -cne $Lease[$key]){
            throw "Resolution evidence belongs to another session ($key)."
        }
    }
}
function Assert-Bc2ResolutionFlag([hashtable]$Observed,[string]$Name,[bool]$Expected) {
    if(-not $Observed.ContainsKey($Name) -or $Observed[$Name] -isnot [bool] -or $Observed[$Name] -ne $Expected){
        throw "Resolution evidence did not confirm $Name=$Expected."
    }
}
function Assert-Bc2NativeResolution([hashtable]$Plan,[hashtable]$Observed,[hashtable]$Lease) {
    Assert-Bc2ResolutionPlan $Plan
    Assert-Bc2ResolutionIdentity $Observed $Lease 'configure'
    foreach($key in @('passed','applied','hook_restored','callbacks_drained','guard_installed','guard_active')) {
        Assert-Bc2ResolutionFlag $Observed $key $true
    }
    foreach($key in @('guard_disabled','guard_drained','guard_released','guard_owner_lost','guard_cleanup_pending','guard_never_installed')){
        Assert-Bc2ResolutionFlag $Observed $key $false
    }
    if((Get-Bc2ResolutionInteger $Observed 'requested_width' 16384) -ne $Plan.width -or
       (Get-Bc2ResolutionInteger $Observed 'requested_height' 16384) -ne $Plan.height -or
       (Get-Bc2ResolutionInteger $Observed 'width' 16384) -ne $Plan.width -or
       (Get-Bc2ResolutionInteger $Observed 'height' 16384) -ne $Plan.height -or
       (Get-Bc2ResolutionInteger $Observed 'updates' 10000000) -lt 4 -or
       (Get-Bc2ResolutionInteger $Observed 'ui_thread' 4294967295) -le 0) {
        throw 'The original game renderer did not reach the requested headset dimensions. No VR adapter was attached.'
    }
}
function Assert-Bc2ResolutionReleased([hashtable]$Observed,[hashtable]$Lease) {
    Assert-Bc2ResolutionIdentity $Observed $Lease 'release'
    Assert-Bc2ResolutionFlag $Observed 'passed' $true
    foreach($key in @('guard_active','guard_cleanup_pending')){Assert-Bc2ResolutionFlag $Observed $key $false}
    foreach($key in @('guard_disabled','guard_drained')){Assert-Bc2ResolutionFlag $Observed $key $true}
    foreach($key in @('guard_released','guard_never_installed')){
        if(-not $Observed.ContainsKey($key) -or $Observed[$key] -isnot [bool]){throw "Missing resolution cleanup evidence: $key."}
    }
    if(-not $Observed.guard_released -and -not $Observed.guard_never_installed){throw 'The resolution session guard was not released.'}
    if($Observed.guard_released){
        foreach($key in @('hook_restored','callbacks_drained')){Assert-Bc2ResolutionFlag $Observed $key $true}
    }
}
