# FrostbiteVR

An experimental, modular VR framework for Frostbite games, with **Battlefield:
Bad Company 2 single-player** as the first adapter. Native BC2 game views feed a
separate OpenXR host; reusable interaction code is kept separate from the game's
rendering, input, animation and ammunition bindings.

**Developer source snapshot / unreleased WIP.** This repository is available for
research, contributions and forks. There is no supported player installer or
release binary yet. BF3, BF4, Hardline and other Frostbite games are future ports,
not supported games.

## Start here

- [Build instructions](BUILDING.md)
- [Fork guide and code map](CONTRIBUTING.md)
- [Current status and roadmap](docs/STATUS.md)
- [Shared weapon-system pipeline](docs/WEAPON-SYSTEM-PIPELINE.md)
- [Frostbite porting roadmap](docs/FROSTBITE-PORTING-ROADMAP.md)
- [Architecture](docs/ARCHITECTURE.md)

## Where the project stands

Local checkpoint **205** adds repeated reload diagnostics and 30 offline
input-loss/recovery cases through persistent consumers, with mocked native and
renderer boundaries. See [coverage and remaining live work](docs/RELOAD-RECOVERY-205.md).

The latest actual-game acceptance, **204**, passes a combined shoulder-switch
and chest-reload sequence through persistent normal consumers. All 186 C++ suites
pass on each architecture; the strict native audit confirms conserved ammo, no
reload cancellations or magazine visual fallbacks, and clean restoration. See
[the evidence and limits](docs/COMBINED-INVENTORY-RELOAD-204.md). This local work
does not automatically update the published fork snapshot.

Earlier headset tests demonstrated native stereo, tracked weapons and support
hands, roomscale/recentering, controller menu interaction, chest ammunition,
shoulder holsters, selected manual reloads and the tested boat's head aiming and
firing. These features remain experimental, and later tests exposed regressions.

The prior checkpoint, **202 (October 7, 2026, US Eastern)**, passed an automated
controller sequence in the actual game: return a partly used XM8 magazine,
remove it again, carry a replacement with fast motion and insert it. The strict
audit verified conserved ammunition, no cancellations or magazine visual
fallbacks, stereo delivery and cleanup. Shared reload-start timing and diagnostic
completion retention were corrected. That development composition passed all
186 C++ suites on both architectures. Two suites use local test-only fixtures
that are excluded from this repository; the initial public build had 184 suites.

**This checkpoint has not had a new headset acceptance test.** Reliable behavior
across weapon changes, holsters, pickups, empty ammunition and campaign transitions
still needs broader validation. Manual pumping, bolt/charging-handle mechanics,
belt-fed reloads, physical zoomed scopes, full-body IK and multiplayer are unfinished.
Known visual issues include the floating optic dot and some LOD/animation artifacts.

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
