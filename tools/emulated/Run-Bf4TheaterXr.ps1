# Explicit hardware check (not CTest): the real FvrXrHost.exe on the repository's fake
# OpenXR runtime (FvrTheaterTestXr: theater fixture, ~18 s) with the emulated BF4 process
# as the game. Proves: proxy -> adapter -> theater link (menu lane) -> XR host -> quad layer
# carrying the game's pixels. No game, no headset, no SteamVR; needs a D3D11 GPU.
# -World: the world lane instead (head-tracked view path): the adapter answers the host's
# eye-pair requests through the frame bridge (FVR_BF4_WORLD_UNTRACKED: the emulator has no
# camera to move), and the host must submit those pairs as projection layers.
# -FrameChannels 2: the host alternates two frame channels (PipelinedFrameProvider) and the
# adapter serves both bridges oldest request first; both channels must deliver pairs.
#   pwsh tools/emulated/Run-Bf4TheaterXr.ps1 [-Build build/x64] [-World] [-FrameChannels 2]
param([string]$Build = (Join-Path $PSScriptRoot '..\..\build\x64'), [switch]$World, [ValidateSet(1, 2)][int]$FrameChannels = 1)
$ErrorActionPreference = 'Stop'
$b = (Resolve-Path $Build).Path
$env:TEMP = Join-Path $b 'scratch'; $env:TMP = $env:TEMP
foreach ($f in 'xr-channel.txt', 'xr-channel-2.txt', 'xr-host.json', 'xr-host.log') { Remove-Item (Join-Path $b $f) -ErrorAction SilentlyContinue }
$xrHost = Start-Process -FilePath (Join-Path $b 'FvrXrHost.exe') -WorkingDirectory $b -PassThru -WindowStyle Hidden `
    -ArgumentList @('--ipc', '--controllers', '--theater-input', '--loader', (Join-Path $b 'FvrTheaterTestXr.dll'), '--seconds', '25',
                    '--channel-file', (Join-Path $b 'xr-channel.txt'), '--frame-channels', "$FrameChannels") `
    -RedirectStandardOutput (Join-Path $b 'xr-host.json') -RedirectStandardError (Join-Path $b 'xr-host.log')
$env:FVR_BF4_EXTERNAL_XR_HOST = '1'
if ($World) { $env:FVR_BF4_WORLD_UNTRACKED = '1' }
try {
    Push-Location $b
    $emu = & (Join-Path $b 'Bf4EmuHost.exe') 2>&1
    $emuExit = $LASTEXITCODE
} finally {
    Pop-Location
    Remove-Item Env:FVR_BF4_EXTERNAL_XR_HOST
    Remove-Item Env:FVR_BF4_WORLD_UNTRACKED -ErrorAction SilentlyContinue
}
$null = $xrHost.WaitForExit(40000)
$report = Get-Content (Join-Path $b 'xr-host.json') -Raw
$log = Get-Content (Join-Path $b 'xr-host.log') -Raw
$menuFrames = if ($report -match '"menu_frames":(\d+)') { [int]$matches[1] } else { 0 }
$quads = if ($log -match 'TheaterTest frames=\d+ quad_layers=(\d+) panel_samples=(\d+) panel_centre=([0-9a-f]{6}) errors=(\d+)') {
    @{ quads = [int]$matches[1]; samples = [int]$matches[2]; centre = $matches[3]; errors = [int]$matches[4] } } else { $null }
$submitted = if ($report -match '"submitted_pairs":(\d+)') { [int]$matches[1] } else { 0 }
$rejected = if ($report -match '"rejected_pairs":(\d+)') { [int]$matches[1] } else { -1 }
$first = if ($report -match '"async_completed":(\d+)') { [int]$matches[1] } else { 0 }
$second = if ($report -match '"channel2_completed":(\d+)') { [int]$matches[1] } else { 0 }
if ($World) {
    $checks = [ordered]@{
        'emulated game checks passed' = $emuExit -eq 0
        'host submitted eye pairs from the game (submitted_pairs > 20)' = $submitted -gt 20
        'host rejected no pair' = $rejected -eq 0
        'fake runtime validation errors = 0' = $quads -and $quads.errors -eq 0
    }
    if ($FrameChannels -eq 2) {
        $checks['both frame channels delivered pairs (each > 10)'] = $first -gt 10 -and $second -gt 10
    }
    $checks.GetEnumerator() | ForEach-Object { '{0} {1}' -f ($(if ($_.Value) { 'PASS' } else { 'FAIL' })), $_.Key }
    "submitted_pairs=$submitted rejected_pairs=$rejected channel1_completed=$first channel2_completed=$second"
    $emu | Select-String 'theater_link|^FAIL ' | ForEach-Object { $_.Line }
    if ($checks.Values -contains $false) { exit 1 }
    exit 0
}
$checks = [ordered]@{
    'emulated game checks passed' = $emuExit -eq 0
    'host showed game frames on the panel (menu_frames > 10)' = $menuFrames -gt 10
    'compositor received quad layers' = $quads -and $quads.quads -gt 20
    # The emulator clears olive, then red/blue in its stereo phase: any of them is the game image.
    'panel pixels are the game image (olive/red/blue)' = $quads -and @('3d4229', 'ff0000', '0000ff') -contains $quads.centre
    'fake runtime validation errors = 0' = $quads -and $quads.errors -eq 0
}
$checks.GetEnumerator() | ForEach-Object { '{0} {1}' -f ($(if ($_.Value) { 'PASS' } else { 'FAIL' })), $_.Key }
$emu | Select-String 'theater_link|^FAIL ' | ForEach-Object { $_.Line }
if ($checks.Values -contains $false) { exit 1 }
