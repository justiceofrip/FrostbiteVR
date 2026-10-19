# Explicit hardware check (not CTest): the real FvrXrHost.exe on the repository's fake
# OpenXR runtime (FvrTheaterTestXr: theater fixture, ~18 s) with the emulated BF4 process
# as the game. Proves: proxy -> adapter -> theater link (menu lane) -> XR host -> quad layer
# carrying the game's pixels. No game, no headset, no SteamVR; needs a D3D11 GPU.
#   pwsh tools/emulated/Run-Bf4TheaterXr.ps1 [-Build build/x64]
param([string]$Build = (Join-Path $PSScriptRoot '..\..\build\x64'))
$ErrorActionPreference = 'Stop'
$b = (Resolve-Path $Build).Path
$env:TEMP = Join-Path $b 'scratch'; $env:TMP = $env:TEMP
foreach ($f in 'xr-channel.txt', 'xr-host.json', 'xr-host.log') { Remove-Item (Join-Path $b $f) -ErrorAction SilentlyContinue }
$xrHost = Start-Process -FilePath (Join-Path $b 'FvrXrHost.exe') -WorkingDirectory $b -PassThru -WindowStyle Hidden `
    -ArgumentList @('--ipc', '--controllers', '--theater-input', '--loader', (Join-Path $b 'FvrTheaterTestXr.dll'), '--seconds', '25',
                    '--channel-file', (Join-Path $b 'xr-channel.txt')) `
    -RedirectStandardOutput (Join-Path $b 'xr-host.json') -RedirectStandardError (Join-Path $b 'xr-host.log')
$env:FVR_BF4_EXTERNAL_XR_HOST = '1'
try {
    Push-Location $b
    $emu = & (Join-Path $b 'Bf4EmuHost.exe') 2>&1
    $emuExit = $LASTEXITCODE
} finally {
    Pop-Location
    Remove-Item Env:FVR_BF4_EXTERNAL_XR_HOST
}
$null = $xrHost.WaitForExit(40000)
$report = Get-Content (Join-Path $b 'xr-host.json') -Raw
$log = Get-Content (Join-Path $b 'xr-host.log') -Raw
$menuFrames = if ($report -match '"menu_frames":(\d+)') { [int]$matches[1] } else { 0 }
$quads = if ($log -match 'TheaterTest frames=\d+ quad_layers=(\d+) panel_samples=(\d+) panel_centre=([0-9a-f]{6}) errors=(\d+)') {
    @{ quads = [int]$matches[1]; samples = [int]$matches[2]; centre = $matches[3]; errors = [int]$matches[4] } } else { $null }
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
