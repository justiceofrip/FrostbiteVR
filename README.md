# FrostbiteVR

**Checkpoint 426 — work-in-progress singleplayer VR framework.** This repository
contains the framework and Bad Company 2 adapter source. The separate
[BC2VR 0.1.5 release](https://github.com/justiceofrip/BC2VR/releases/tag/v0.1.5)
provides the published pre-alpha installer, updates and feedback tracker.

Start with [current state and known limits](docs/CURRENT.md), the
[426 handoff](handoff/checkpoint426/README.md), [build instructions](BUILDING.md),
and [weapon coverage](docs/WEAPON-COVERAGE-CURRENT.md).

## Evidence and limits

The frozen normal-r4 source passed **291/291 C++ suites on x86 and x64**.
The published package passed 25 installation/package checks and 49 launcher checks.
Its latest native knife run retired the automatic-stow blocker and prepared one
client attack. Server acquisition, damage and repeat-jab recovery remain unverified.
Checkpoint 425's opening cloud correction was accepted by the user in a headset.
426 adds exact opening Nambu100 grip/support and magazine metadata, selected-knife
motion integration, a recenter correction and bounded shot-origin diagnostics.
An actual-game Nambu check verified manual magazine operations and firing afterward.

These are bounded results. Selected-knife enemy damage, downstream Nambu launch
alignment and 426 headset placement remain unverified. The reported crash after deaths/reloads remains unresolved; the
scene-owner guard is not a proven crash fix. No complete campaign, all-weapons,
reconnect or extended-session acceptance is claimed. See the handoff for exact
source hashes, native results and retained failures.

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
[CURRENT.md](docs/CURRENT.md) for current claims; a data row or passing unit test
does not mean a weapon or future game is playable.

## Lineage and license

MIT, with retained third-party notices. This work builds on lessons and shared
math/policy from [BFVR](https://github.com/JayBiggsGMG/BFVR-Battlefield-1942-VR-Mod)
and [BF2142VR](https://github.com/justiceofrip/BF2142VR). Refractor-native layouts
are not reused as Frostbite bindings. See [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

Independent community project; not affiliated with EA or DICE. The project
license does not grant rights to Battlefield or its game assets.
