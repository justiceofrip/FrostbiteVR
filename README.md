# FrostbiteVR

**Checkpoint 436 R56 — work-in-progress singleplayer VR framework.** This repository
contains the framework and Bad Company 2 adapter source. The separate
[BC2VR releases](https://github.com/justiceofrip/BC2VR/releases)
provide the pre-alpha installer, updates and feedback tracker.

This is a developer source checkpoint, not a new installer release. The public
BC2VR launcher remains 0.1.15. The checkpoint includes shared weapon ownership
and ammunition fixes, a correction for detached destruction geometry in stereo,
and the performance, startup and campaign-transition work described in
[current state](docs/CURRENT.md).

Start with [current state and known limits](docs/CURRENT.md), the
[436 R56 handoff](handoff/checkpoint436-r56/README.md), [build instructions](BUILDING.md),
and [weapon coverage](docs/WEAPON-COVERAGE-CURRENT.md).

## Evidence and limits

The frozen normal source passed **305/305 C++ suites on each of x86 and x64**.
Actual-game tests with emulated controller inputs cover representative rifle and
pistol magazine operations, two-handed AEK empty firing, a combined SPAS pump /
shell / holster sequence, and five M95 manual bolt cycles. The handoff records
the exact source revision and limits of each result; these are not fifty
independent weapon approvals.

The reproduced destruction artifact was attached in both eyes in two reviewed
delivered pairs, 3.781 seconds apart, after the projection correction. Other scenes and physical-headset
appearance still need validation. Death/checkpoint recovery, full campaign
vehicle coverage, manual belt-fed LMG loading, and interaction between all
enabled tools remain unfinished. This checkpoint is for development and review;
it does not claim a fully playable campaign or sustained native 90 Hz.

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
