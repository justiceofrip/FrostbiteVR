[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$ReportFolder)
$ErrorActionPreference='Stop'
function ConvertTo-BC2SessionStartUtc([object]$Value) {
    # PowerShell 7 may already deserialize ISO JSON timestamps as DateTime.
    # Parsing that object's culture-formatted string loses its UTC kind.
    if($Value -is [DateTimeOffset]){return $Value.UtcDateTime}
    if($Value -is [DateTime]){
        if($Value.Kind -eq [DateTimeKind]::Unspecified){throw 'Session start timestamp has no timezone.'}
        return $Value.ToUniversalTime()
    }
    if($Value -is [string] -and $Value -match '^\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}(?:\.\d{1,7})?(?:Z|[+-]\d{2}:\d{2})$'){
        return [DateTimeOffset]::Parse($Value,[Globalization.CultureInfo]::InvariantCulture).UtcDateTime
    }
    throw 'Invalid session start timestamp.'
}
$folder=[IO.Path]::GetFullPath($ReportFolder)
$state=Get-Content -LiteralPath (Join-Path $folder 'session.json') -Raw | ConvertFrom-Json
if($state.state -ne 'running'){throw 'This session is not running.'}
$expectedStop=[IO.Path]::GetFullPath((Join-Path $folder 'stop.request'))
if(-not $state.stop_file -or [IO.Path]::GetFullPath($state.stop_file) -ne $expectedStop){throw 'Session does not expose its own graceful stop file.'}
$expectedHost=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot 'build\x64\BC2XrHost.exe'))
$hostProcess=Get-Process -Id $state.host_pid -ErrorAction Stop
if($hostProcess.Path -ne $expectedHost){throw 'Host identity mismatch.'}
$null=$hostProcess.Handle
if($hostProcess.StartTime.ToUniversalTime() -lt (ConvertTo-BC2SessionStartUtc $state.started_utc)){throw 'Host predates this session.'}
[IO.File]::WriteAllText($expectedStop,'Requested by Stop-BC2VRSession.ps1')
if(-not $hostProcess.WaitForExit(10000)){throw 'Graceful exit is pending; no process was force-stopped. Inspect this session before taking further action.'}
[ordered]@{stop_requested=$true;host_exited=$true;host_pid=$state.host_pid;report_folder=$folder;native_cleanup_pending=$true} | ConvertTo-Json
