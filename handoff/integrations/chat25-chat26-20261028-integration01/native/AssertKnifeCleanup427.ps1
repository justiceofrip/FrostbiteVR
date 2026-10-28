# Private root-launched diagnostic receipts only; no native/process actions.
function Test-KnifeUnsigned427($Value,[ulong]$Maximum=4294967295) {
    return ($Value -is [byte] -or $Value -is [ushort] -or $Value -is [uint] -or $Value -is [ulong] -or
        $Value -is [sbyte] -or $Value -is [short] -or $Value -is [int] -or $Value -is [long]) -and
        [decimal]$Value -ge 0 -and [decimal]$Value -le $Maximum
}
function Assert-KnifeProcessIdentity427($Identity,[string]$GameExe) {
    if(-not(Test-KnifeUnsigned427 $Identity.pid) -or $Identity.pid -eq 0 -or
       $Identity.root_launched -isnot [bool] -or -not $Identity.root_launched -or
       $Identity.path -isnot [string] -or $Identity.path -cne $GameExe -or
       $Identity.creation_filetime -isnot [string] -or $Identity.creation_filetime -cnotmatch '^[1-9][0-9]{16,19}$') {
        throw 'Malformed root-launched game identity'
    }
    $parsed=0UL;if(-not [ulong]::TryParse($Identity.creation_filetime,[ref]$parsed)){throw 'Invalid process creation time'}
}
function Read-KnifeCleanupProgress427([string]$ReportPath) {
    $path=Join-Path (Split-Path $ReportPath) 'stereo-progress.jsonl'
    $file=Get-Item -LiteralPath $path -ErrorAction Stop
    if($file.Length -le 0 -or $file.Length -gt 1MB){throw 'Cleanup progress missing or oversized'}
    $line=Get-Content -LiteralPath $path -Tail 1 -ErrorAction Stop
    if(-not $line -or $line.Length -gt 65536){throw 'Cleanup progress incomplete'}
    return ($line|ConvertFrom-Json -ErrorAction Stop)
}
function Assert-KnifeNativeCleanup427($Native,$Bootstrap,$ExpectedPid,$TraceExit,$Progress) {
    if(-not(Test-KnifeUnsigned427 $ExpectedPid) -or $ExpectedPid -eq 0 -or
       -not(Test-KnifeUnsigned427 $TraceExit) -or $TraceExit -ne 0 -or
       $Native.state -isnot [string] -or $Native.state -cne 'observed' -or
       -not(Test-KnifeUnsigned427 $Native.pid) -or $Native.pid -ne $ExpectedPid -or
       $Native.hooks_disabled -isnot [bool] -or -not $Native.hooks_disabled -or
       -not(Test-KnifeUnsigned427 $Bootstrap.pid) -or $Bootstrap.pid -ne $ExpectedPid -or
       -not(Test-KnifeUnsigned427 $Bootstrap.probe_exit) -or $Bootstrap.probe_exit -ne 0 -or
       $Bootstrap.module_retained_until_game_exit -isnot [bool] -or -not $Bootstrap.module_retained_until_game_exit -or
       $Bootstrap.game_files_modified -isnot [bool] -or $Bootstrap.game_files_modified){throw 'Native identity, state or typed cleanup result failed'}
    $probe=$Native.stereo_probe;$shutdown=$Native.stereo_shutdown;$recovery=$Native.stereo_recovery
    foreach($field in @('camera_restore_failures','gpu_failure_stage')){
        if(-not(Test-KnifeUnsigned427 $Native.native_stream.$field) -or $Native.native_stream.$field -ne 0){throw 'Native camera/GPU cleanup failed'}
    }
    if(-not(Test-KnifeUnsigned427 $probe.stage) -or $probe.stage -ne 3){throw 'Native stereo retirement incomplete'}
    foreach($field in @('initialized','lists_restored','callbacks_restored','cameras_preserved','clock_preserved')){
        # These native report fields deliberately serialize integer0/1, not JSON booleans.
        if(-not(Test-KnifeUnsigned427 $probe.$field 1) -or $probe.$field -ne 1){throw "Native stereo cleanup missing: $field"}
    }
    foreach($field in @('refs_before','refs_after','borrowed_redirects','borrowed_restores','main_view','eye_view')){
        if(-not(Test-KnifeUnsigned427 $probe.$field)){throw "Malformed native stereo field: $field"}
    }
    if($probe.refs_before -eq 0 -or $probe.refs_before -ne $probe.refs_after -or $probe.borrowed_redirects -ne $probe.borrowed_restores -or
       $probe.main_view -eq 0 -or $probe.eye_view -eq 0 -or $probe.main_view -eq $probe.eye_view){throw 'Native references or owned view identity differ'}
    foreach($field in @('captured','after_captured')){
        if(-not(Test-KnifeUnsigned427 $shutdown.$field 1) -or $shutdown.$field -ne 1){throw 'Native shutdown graph not captured'}
    }
    if(-not(Test-KnifeUnsigned427 $shutdown.rejected) -or $shutdown.rejected -ne 0 -or
       -not(Test-KnifeUnsigned427 $shutdown.owned_pending_references)){throw 'Native shutdown rejected or malformed'}
    foreach($field in @('historical_original_order','before_release_order','expected_survivor_order','after_release_order')){
        $values=$shutdown.$field
        if($values -isnot [array] -or $values.Count -lt 1 -or $values.Count -gt 128){throw "Missing native view list: $field"}
        $seen=[Collections.Generic.HashSet[ulong]]::new()
        foreach($value in $values){if(-not(Test-KnifeUnsigned427 $value) -or $value -eq 0 -or -not $seen.Add([ulong]$value)){throw "Malformed or duplicate native view: $field"}}
    }
    $original=$shutdown.historical_original_order;$before=$shutdown.before_release_order
    $expected=$shutdown.expected_survivor_order;$after=$shutdown.after_release_order
    if($after.Count -ne $expected.Count -or $after.Count -ne $original.Count -or $before.Count -le $after.Count){throw 'Native view counts differ'}
    for($i=0;$i -lt $after.Count;$i++){if($after[$i] -ne $expected[$i] -or $after[$i] -notin $before){throw 'Native current survivor order differs'}}
    if($probe.main_view -notin $after -or $probe.eye_view -notin $before -or $probe.eye_view -in $after){throw 'Owned native eye survives retirement or main view is absent'}
    # owned_pending_references is a BEFORE-release measurement. Final baseline
    # refs and exact surviving graph above prove release; do not call it a leak.
    foreach($field in @('attempts','completed','failure','rejected')){if(-not(Test-KnifeUnsigned427 $recovery.$field)){throw 'Malformed recovery result'}}
    if($recovery.attempts -ne $recovery.completed -or $recovery.failure -ne 0 -or $recovery.rejected -ne 0){throw 'Native recovery remains incomplete'}
    if($Progress.step -isnot [string] -or $Progress.step -cne 'all_views_restored' -or
       -not(Test-KnifeUnsigned427 $Progress.cached_restored 1) -or $Progress.cached_restored -ne 1){throw 'Native final cached callbacks not restored'}
}
