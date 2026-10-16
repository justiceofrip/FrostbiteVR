# FrostbiteVR

**Checkpoint 414 — unreleased developer source.** This repository contains a
modular Frostbite VR framework with a Bad Company 2 adapter. The latest source
places magazine and shell supplies together on the lower chest, retaining the
shared pump/bolt recovery fixes and movement-driven mechanism audio from checkpoint 413.

Start with the [fork handoff](handoff/checkpoint414/README.md),
[build instructions](BUILDING.md), and
[current weapon coverage](docs/WEAPON-COVERAGE-CURRENT.md).

## Current evidence and limits

Checkpoint 414 lowers the shared ammo contact by 10 cm and brings it 4 cm closer.
The display and pickup point agree across ammo families. Expanded emulation
covers 18 magazine configurations and 6,048 physical poses; exact final validation
is recorded in [the 414 handoff](handoff/checkpoint414/README.md). Both full builds
passed 284 suites; actual BC2 placement checks passed for magazines and shells.
The local desktop launcher selects 414. No new headset acceptance is claimed.

Checkpoint413 corrects shared pump/bolt dispatch and interruption handling.
Actual BC2 with synthetic controllers passed interrupted-pump recovery and a
separate eight-shot pump/reload/holster/rifle sequence. Movement-driven SPAS
audio has PC endpoint evidence; final audio-output recovery and bolt custody
checks are documented in [checkpoint413](docs/CHECKPOINT-413.md).
No413 headset acceptance or full campaign playthrough is claimed.

**The October16 checkpoint412 headset run exposed a SPAS lockout:** firing,
pumping and holstering stopped while grip remained usable. Actual loaded binary
hashes match412. Two distinct cancelled native-cycle states are recorded; their
complete original causes were not captured. See [the retained failure investigation](docs/HEADSET-FAILURE-412.md).
The corrective automated results do not establish headset reliability.

The latest normal410 headset feedback reports substantially better pumping and
reliable manual shotgun loading, with remaining shell-animation and placement
issues. Checkpoint412 lowers shared chest ammunition, moves the read-only ammo
counter beside the gun hand, retains stock loading-shell suppression after a
reload finishes, and removes repeated launch storage scans. See the
[current changes and validation limits](docs/CHECKPOINT-412.md).

Prior actual-game checks cover selected rifle/shotgun reloads and holsters,
five M95 bolt cycles, campaign checkpoint recovery, mounted firing, knife
session-close recovery and paired-eye damage/death presentation. These are
bounded observations, not a complete campaign playthrough or weapon acceptance.
The new412 visual changes have no headset acceptance; the user requested a
checkpoint without another headset test. Spent-shell ejection remains open;
immediate pump timing is unchanged. M95 magazine refill still uses stock fallback.
Wider mechanism coverage, belt-fed reloads, physical zoomed scopes, full-body IK
and multiplayer remain unfinished.

Earlier headset tests demonstrated native stereo, tracked weapons and support
hands, roomscale/recentering, controller menu interaction, chest ammunition,
shoulder holsters, selected manual reloads and the tested boat's head aiming and
firing. These features remain experimental, and later tests exposed regressions.
Measured contacts and compiled capability rows are prerequisites, not a count of
headset-accepted weapons. Historical checkpoint notes retain their original scope.

## Build without a game or headset

On Windows, install Visual Studio C++ x86/x64 tools and the Windows SDK,
CMake 3.24+, Ninja, PowerShell 7 and Python 3.11+:

```powershell
git clone https://github.com/justiceofrip/FrostbiteVR.git
cd FrostbiteVR
.\Build.ps1 -Architecture x86 -Jobs 2
.\Build.ps1 -Architecture x64 -Jobs 2
python -m pip install -r requirements-dev.txt
python -B -m unittest discover -s tests -p 'test_*.py'
```

These checks do not start BC2 or a headset runtime. The default build leaves
optional native operation registrations disabled. The separately documented
`Build-Checkpoint.ps1` selects the included checkpoint calibration records and
exact source-bound operation header. See [BUILDING.md](BUILDING.md) before using
any launcher or diagnostic that attaches to the game.

## What is included

C++ source, Python/PowerShell development tools, synthetic tests, small mod
calibration records, research notes, and retained dependency licenses are included.
Game executables, meshes, textures, animations, saves, generated body-model caches,
private recordings, raw live traces and machine-local configuration are excluded.
Tools can prepare the required body geometry locally from your own installation.

Historical research notes describe the checkpoint at which they were written.
Some refer to local evidence that is intentionally not published. Use
[STATUS.md](docs/STATUS.md) for current claims; a data row or passing unit test
does not mean a weapon or future game is playable.

## Lineage and license

MIT, with retained third-party notices. This work builds on lessons and shared
math/policy from [BFVR](https://github.com/JayBiggsGMG/BFVR-Battlefield-1942-VR-Mod)
and [BF2142VR](https://github.com/justiceofrip/BF2142VR). Refractor-native layouts
are not reused as Frostbite bindings. See [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

Independent community project; not affiliated with EA or DICE. The project
license does not grant rights to Battlefield or its game assets.
