# FrostbiteVR

**Checkpoint 415 integration — unreleased developer source.** A modular
Frostbite VR framework with a Bad Company 2 adapter. This source combines the
checkpoint 414 lower-chest ammunition placement and checkpoint 413 pump/bolt
recovery/audio work with main's build optimizations and documentation cleanup.

Start with [current state and known limits](docs/CURRENT.md), the
[415 integration handoff](handoff/checkpoint415/README.md),
[build instructions](BUILDING.md), and [weapon coverage](docs/WEAPON-COVERAGE-CURRENT.md).

## Evidence and limits

Both integrated builds passed 284/284 suites, and all four runtime binaries are
byte-for-byte identical to frozen checkpoint 414. The desktop launcher stays on
that verified runtime; no new game/headset test is needed for this merge.
Python and launcher validation also passed. The changes improve build/test organization
and documentation; they do not add weapon support or headset performance claims.

Checkpoint 414 passed 284 suites on each architecture and a 6,048-pose sweep
across 18 magazine configurations. Bounded actual BC2 checks confirmed lower-chest
magazine and shell placement. Checkpoint 413 has actual-game emulation evidence
for pump recovery, reload/holster sequences and movement-driven SPAS audio.
See [414](handoff/checkpoint414/README.md) and [413](handoff/checkpoint413/README.md)
for their exact scope and retained failures.

**The latest human headset run was checkpoint 412 and exposed a SPAS lockout.**
Later automated passes are not headset acceptance. No complete campaign
playthrough, all-weapons acceptance or public player release is claimed.
Spent-shell ejection, wider reload/mechanism coverage, scopes, full-body IK and
multiplayer remain unfinished. [CURRENT.md](docs/CURRENT.md) tracks the current
implementation and validation gaps; historical notes describe their own checkpoint.

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
