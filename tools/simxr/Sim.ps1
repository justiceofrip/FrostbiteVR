# Drives the developer simulated headset (FvrSimOpenXr, SimOpenXrRuntime.cpp) through
# simxr.txt in its control directory (%FVR_SIMXR_DIR% of the XR host; for BF4 developer
# sessions <install root>\research). Line formats: see SimOpenXrRuntime.cpp.
#   Sim.ps1 -Dir <dir> -Set 'head 20 0 0 0 1.7 0','right input 1 0 0 0 0 0 0 0'  # replace lines by key
#   Sim.ps1 -Dir <dir> -Snap world1                                              # eyes/quads -> PNG
param([Parameter(Mandatory)][string]$Dir, [string[]]$Set = @(), [string]$Snap = '', [int]$Width = 1600)
$ErrorActionPreference = 'Stop'
$dir = $Dir
$file = Join-Path $dir 'simxr.txt'
$ack = Join-Path $dir 'simxr-ack.txt'
$lines = [System.Collections.Generic.List[string]]::new()
if (Test-Path $file) { foreach ($l in Get-Content $file) { if ($l.Trim()) { $lines.Add($l) } } }
function KeyOf([string]$l) { $t = $l.Trim().Split(' ', [StringSplitOptions]::RemoveEmptyEntries); if ($t.Count -ge 2 -and $t[1] -eq 'input') { "$($t[0]) input" } else { $t[0] } }
foreach ($s in $Set) {
    $k = KeyOf $s
    for ($i = $lines.Count - 1; $i -ge 0; --$i) { if ((KeyOf $lines[$i]) -eq $k) { $lines.RemoveAt($i) } }
    $lines.Add($s)
}
$id = ''
if ($Snap) {
    $id = [string][DateTimeOffset]::Now.ToUnixTimeMilliseconds()
    for ($i = $lines.Count - 1; $i -ge 0; --$i) { if ((KeyOf $lines[$i]) -eq 'snap') { $lines.RemoveAt($i) } }
    $lines.Add("snap $id $Snap")
}
[IO.File]::WriteAllText($file, ($lines -join "`n") + "`n")
if (-not $Snap) { if (Test-Path $ack) { Get-Content $ack }; return }
$deadline = (Get-Date).AddSeconds(8)
while ((Get-Date) -lt $deadline) {
    Start-Sleep -Milliseconds 200
    if ((Test-Path $ack) -and ((Get-Content $ack -Raw) -match "snap $id")) { break }
}
Get-Content $ack
Add-Type -AssemblyName System.Drawing
foreach ($bmp in Get-ChildItem $dir -Filter "$Snap-*.bmp") {
    $img = [System.Drawing.Image]::FromFile($bmp.FullName)
    try {
        $scale = [Math]::Min(1.0, $Width / $img.Width)
        $w = [int]($img.Width * $scale); $h = [int]($img.Height * $scale)
        $out = New-Object System.Drawing.Bitmap $w, $h
        $g = [System.Drawing.Graphics]::FromImage($out)
        $g.InterpolationMode = 'HighQualityBilinear'
        $g.DrawImage($img, 0, 0, $w, $h)
        $g.Dispose()
        $png = [IO.Path]::ChangeExtension($bmp.FullName, '.png')
        $out.Save($png, [System.Drawing.Imaging.ImageFormat]::Png)
        $out.Dispose()
        $png
    } finally { $img.Dispose() }
    Remove-Item $bmp.FullName
}
