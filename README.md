# FrostbiteVR

**Checkpoint 390 — unreleased developer source.** This repository contains a
modular Frostbite VR framework with a Bad Company 2 adapter. The latest source
includes the work since the previous GitHub checkpoint 245.

Start with the [fork handoff](handoff/checkpoint390/README.md),
[build instructions](BUILDING.md), and
[current weapon coverage](docs/WEAPON-COVERAGE-CURRENT.md).

## Current evidence and limits

Both checkpoint390 architecture builds passed 246 C++ suites. Actual-game
emulation passed repeated rifle/shotgun shoulder swaps and manual reloads,
a second shotgun draw and pump, a VR-menu checkpoint restart during an unfinished
pump followed by fresh reloads, and five M95 bolt cycles. The latest shared fix
preserves completed reload retirement when returning to a weapon family, so the
native mechanism can be selected again. See the
[390 evidence and remaining checks](docs/NATIVE-HANDOFF-390.md).

**Headset acceptance is pending.** The last human test, checkpoint375 on
October 12, failed. These automated results do not establish a playable whole
campaign, complete weapon coverage, or a fix for every intermittent crash.
Controller alignment, visual artifacts and sustained stability still need testing.
The M95 magazine refill in the bolt check uses stock fallback. Wider mechanism
coverage, belt-fed reloads, physical zoomed scopes, full-body IK and multiplayer
remain unfinished.

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
