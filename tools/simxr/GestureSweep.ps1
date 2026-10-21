# Developer: drive the simulated headset's controllers through BF4's VR gestures while a level is
# running (developer session, xrsim route), printing the adapter's gesture line after each step
# and taking eye snapshots where the result is visual (<Root>\research\<Name>-<tag>-eyes.png).
# Start it once the level runs with the world lane on. Zones for a 1.70 m head facing -Z
# (Bf4BodyGestures / BodyFrameEstimator): back right (0.15 1.57 0.23), back left (-0.15 1.57
# 0.23), hip right (0.17 1.05 0.09), hip left (-0.17 1.05 0.09), chest (0 1.40 -0.01), belt
# pouch (-0.10 1.10 -0.03); magazine well 7 cm below / 6 cm ahead of the right grip.
# The grenade is thrown at whatever is ahead and can land close: keep it last.
#   GestureSweep.ps1 -Name g1                          # every step
#   GestureSweep.ps1 -Name g2 -Steps reload, tactical  # some
param([string]$Root = 'G:\FrostbiteVR\runtime\bf4', [string]$Name = 'sweep',
      [string[]]$Steps = @('fire', 'reload', 'drop', 'tactical', 'hold', 'holsters', 'sidearm', 'knife', 'ads', 'crouch', 'grenade'))
$ErrorActionPreference = 'Stop'
$dir = Join-Path $Root 'research'
$log = Join-Path $Root 'logs\bf4-adapter.log'
$sim = Join-Path $PSScriptRoot 'Sim.ps1'
$rightRest = 'right 0.15 1.45 -0.35 0 0 0'      # the gun held forward, level
$leftRest = 'left -0.20 1.15 -0.15 0 0 0'       # the left hand down at the side
$noInput = 'left input 0 0 0 0 0 0 0 0', 'right input 0 0 0 0 0 0 0 0'
function Set-Sim([string[]]$lines) { & $sim -Dir $dir -Set $lines | Out-Null }
function Wait([int]$ms) { Start-Sleep -Milliseconds $ms }
# One research command to the running adapter (developer session), waiting for its ack.
function Send-Research([string]$command) {
    $cmd = Join-Path $dir 'cmd.txt'
    $ack = Join-Path $dir 'ack.txt'
    $last = 0
    if (Test-Path $cmd) { foreach ($l in Get-Content $cmd) { if ($l -match '^(\d+)\s') { $last = [math]::Max($last, [int64]$matches[1]) } } }
    $id = $last + 1
    Add-Content -Path $cmd -Value "$id $command" -Encoding ascii
    $end = (Get-Date).AddSeconds(20)
    while ((Get-Date) -lt $end) {
        if ((Test-Path $ack) -and (Get-Content $ack | Where-Object { $_ -match "^$id\s" })) { return }
        Wait 250
    }
    throw "no ack for research command '$command'"
}
function Gestures {
    $l = Get-Content $log -Tail 4000 | Select-String 'gestures gun=' | Select-Object -Last 1
    if ($l) { $x = $l.Line; $x.Substring(0, 13) + ' ' + $x.Substring($x.IndexOf('gestures ') + 9) } else { '(no gestures line)' }
}
function Report([string]$what) { "== $what"; Gestures }
function Snap([string]$tag) { (& $sim -Dir $dir -Snap "$Name-$tag" | Select-Object -Last 1) }

Set-Sim (@('sway 0', 'flip off', 'head 0 0 0 0 1.7 0', $rightRest, $leftRest) + $noInput)
Wait 1500
Report 'start'

foreach ($step in $Steps) {
    switch ($step) {
        'fire' { # right trigger: shots, a pulse each
            Set-Sim 'right input 1 0 0 0 0 0 0 0'; Wait 1200
            Set-Sim 'right input 0 0 0 0 0 0 0 0'; Wait 800
            Report 'fire (right trigger 1.2 s)'
            Snap 'fired'
        }
        'reload' { # B tap drops the magazine; the left hand takes one at the belt, seats it at the well
            Set-Sim 'right input 0 0 0 0 0 1 0 0'; Wait 150
            Set-Sim 'right input 0 0 0 0 0 0 0 0'; Wait 600
            Report 'B tap (magazine out)'
            Snap 'magout'
            Set-Sim 'left -0.10 1.10 -0.03 0 0 0'; Wait 500
            Set-Sim 'left input 0 1 0 0 0 0 0 0'; Wait 500
            Report 'left grip at the belt (magazine in hand)'
            Set-Sim 'left -0.05 1.25 -0.25 0 0 0'; Wait 300
            Snap 'maginhand'
            Set-Sim 'left 0.15 1.38 -0.41 0 0 0'; Wait 700
            Report 'left hand at the magazine well (seated)'
            Set-Sim 'left input 0 0 0 0 0 0 0 0', $leftRest; Wait 3500
            Report 'after BF4''s reload'
            Snap 'reloaded'
        }
        'drop' { # B tap, take one at the belt, let go before the well: dropped; then seat a second one
            Set-Sim 'right input 0 0 0 0 0 1 0 0'; Wait 150
            Set-Sim 'right input 0 0 0 0 0 0 0 0'; Wait 600
            Set-Sim 'left -0.10 1.10 -0.03 0 0 0'; Wait 500
            Set-Sim 'left input 0 1 0 0 0 0 0 0'; Wait 500
            Set-Sim 'left -0.05 1.30 -0.10 0 0 0'; Wait 400
            Set-Sim 'left input 0 0 0 0 0 0 0 0'; Wait 600
            Report 'magazine let go before the well (dropped; still out)'
            Set-Sim 'left -0.10 1.10 -0.03 0 0 0'; Wait 500
            Set-Sim 'left input 0 1 0 0 0 0 0 0'; Wait 500
            Set-Sim 'left 0.15 1.38 -0.41 0 0 0'; Wait 700
            Set-Sim 'left input 0 0 0 0 0 0 0 0', $leftRest; Wait 3500
            Report 'second magazine seated'
        }
        'tactical' { # a magazine into a gun that still has one: tactical reload
            Set-Sim 'right input 1 0 0 0 0 0 0 0'; Wait 500
            Set-Sim 'right input 0 0 0 0 0 0 0 0'; Wait 500
            Set-Sim 'left -0.10 1.10 -0.03 0 0 0'; Wait 500
            Set-Sim 'left input 0 1 0 0 0 0 0 0'; Wait 500
            Set-Sim 'left 0.15 1.38 -0.41 0 0 0'; Wait 700
            Set-Sim 'left input 0 0 0 0 0 0 0 0', $leftRest; Wait 3500
            Report 'tactical reload (magazine into a gun that has one)'
            Snap 'tactical'
        }
        'hold' { # B held: the pad's X (BF4's reload / interact)
            Set-Sim 'right input 1 0 0 0 0 0 0 0'; Wait 600
            Set-Sim 'right input 0 0 0 0 0 1 0 0'; Wait 900
            Set-Sim 'right input 0 0 0 0 0 0 0 0'; Wait 3000
            Report 'B held 0.9 s (pad X)'
        }
        'holsters' { # left hand over the left shoulder: gadget (3); right hand over the right shoulder: primary (1)
            Set-Sim 'left -0.15 1.57 0.23 0 0 0'; Wait 500
            Set-Sim 'left input 0 1 0 0 0 0 0 0'; Wait 400
            Set-Sim 'left input 0 0 0 0 0 0 0 0', $leftRest; Wait 2000
            Report 'left grip over the left shoulder (gadget, key 3)'
            Snap 'gadget'
            Set-Sim 'right 0.15 1.57 0.23 0 0 0'; Wait 500
            Set-Sim 'right input 0 1 0 0 0 0 0 0'; Wait 400
            Set-Sim 'right input 0 0 0 0 0 0 0 0', $rightRest; Wait 2000
            Report 'right grip over the right shoulder (primary, key 1)'
        }
        'sidearm' { # right hand at the right hip: sidearm (2)
            Set-Sim 'right 0.17 1.05 0.09 0 0 0'; Wait 500
            Set-Sim 'right input 0 1 0 0 0 0 0 0'; Wait 400
            Set-Sim 'right input 0 0 0 0 0 0 0 0', $rightRest; Wait 2000
            Report 'right grip at the right hip (sidearm, key 2)'
            Snap 'sidearm'
            Set-Sim 'right 0.15 1.57 0.23 0 0 0'; Wait 500
            Set-Sim 'right input 0 1 0 0 0 0 0 0'; Wait 400
            Set-Sim 'right input 0 0 0 0 0 0 0 0', $rightRest; Wait 2000
            Report 'back to the primary'
        }
        'knife' { # left hand grip at the chest: knife (F)
            Set-Sim 'left 0 1.40 -0.01 0 0 0'; Wait 500
            Set-Sim 'left input 0 1 0 0 0 0 0 0'; Wait 400
            Set-Sim 'left input 0 0 0 0 0 0 0 0', $leftRest; Wait 2000
            Report 'left grip at the chest (knife, F)'
        }
        'ads' { # left trigger held: BF4's ADS FOV (status every ~10 s), the render's stays
            $n0 = @(Get-Content $log).Count
            Set-Sim 'left input 1 0 0 0 0 0 0 0'; Wait 6000
            Snap 'ads'
            Wait 6000
            Set-Sim 'left input 0 0 0 0 0 0 0 0'; Wait 800
            '== ADS (left trigger held 12 s)'
            Get-Content $log | Select-Object -Skip $n0 | Select-String 'view_override=' | ForEach-Object {
                $x = $_.Line; $i = $x.IndexOf('game_fov='); $x.Substring(0, 13) + ' ' + $x.Substring($i, 36) }
        }
        'crouch' { # physical crouch on: 45 cm down crouches, back up stands (toggles in the periodic line)
            Send-Research 'setting physical_crouch 1'; Wait 2000
            Set-Sim 'head 0 0 0 0 1.25 0'; Wait 3000
            Set-Sim 'head 0 0 0 0 1.7 0'; Wait 3000
            Send-Research 'setting physical_crouch 0'
            $n0 = @(Get-Content $log).Count
            $deadline = (Get-Date).AddSeconds(15)
            while ((Get-Date) -lt $deadline -and -not (Get-Content $log | Select-Object -Skip $n0 | Select-String 'gestures gun=')) { Wait 1000 }
            Report 'physical crouch: down 45 cm, back up (crouch_toggles +2 expected)'
        }
        'grenade' { # left hand grabs at the left hip, released 40 cm away: thrown (G)
            Set-Sim 'left -0.17 1.05 0.09 0 0 0'; Wait 500
            Set-Sim 'left input 0 1 0 0 0 0 0 0'; Wait 400
            Set-Sim 'left -0.10 1.50 -0.40 0 0 0'; Wait 400
            Set-Sim 'left input 0 0 0 0 0 0 0 0'; Wait 300
            Set-Sim $leftRest; Wait 3000
            Report 'grenade: grab at the left hip, release in front'
            Snap 'grenade'
        }
        default { throw "unknown step '$step'" }
    }
}
Set-Sim (@('sway 1', $rightRest, $leftRest) + $noInput)
'== theater_link'
$t = Get-Content $log -Tail 4000 | Select-String 'theater_link' | Select-Object -Last 1
if ($t) { $x = $t.Line; $i = $x.IndexOf('native_pairs='); $j = $x.IndexOf('marked_pairs='); $x.Substring($i, 40) + ' ' + $x.Substring($j, 20) }
