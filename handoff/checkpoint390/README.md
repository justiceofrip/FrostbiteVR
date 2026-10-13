# Checkpoint390 fork handoff

This source checkpoint includes accumulated work since GitHub checkpoint245
(`761b49a`). The gameplay source is the verified local390 composition; the
publication updates the README and build/handoff instructions only. No player
binary, game asset, recording, or raw live trace is included.

## Start here

- [Build the normal390 composition](../../BUILDING.md#checkpoint390-player-composition).
  Use the manual-cycle, native-recovery and body-depth switches. Input-emulation
  switches belong only in explicitly selected diagnostics.
- [Latest defect, correction and actual-game evidence](../../docs/NATIVE-HANDOFF-390.md).
- [Weapon coverage and gaps](../../docs/WEAPON-COVERAGE-CURRENT.md).
- [Shared weapon system](../../docs/WEAPON-SYSTEM-PIPELINE.md),
  [architecture](../../docs/ARCHITECTURE.md), and
  [porting roadmap](../../docs/FROSTBITE-PORTING-ROADMAP.md).

## Changes to compare

| Area | Main source / notes |
| --- | --- |
| Shared hand ownership, holsters and interruption recovery | `include/fvr/interaction`, `src/interaction`, `src/games/bc2/Bc2BodyInventory*`, `Bc2BodyHolster*`, `Bc2PhysicalPump*`, `Bc2PhysicalBolt*` |
| Ammunition, reload lifetime and native mechanism selection | `Bc2ReloadRequestCycle*`, `Bc2ReloadNativePolicy.h`, `Bc2ReloadFlowRuntime*`, `Bc2NativeCycle*`; [empty-tube correction](../../docs/SHELL-STOP-388.md) and [second-draw reproducer](../../docs/POST-HOLSTER-PUMP-389.md) |
| Magazine rendering, body props, support and aiming | `Bc2MagazinePresentation*`, `Bc2BodyAmmoRenderer*`, `Bc2RigPublication*`, `src/graphics`, and the profiles selected by `Build-Checkpoint.ps1` |
| Asset and weapon-family analysis | `tools/bc2_*`, `profiles`; [authored-action pipeline](../../docs/AUTHORED-ACTION-PIPELINE-379.md) and [partial-data handling](../../docs/PARTIAL-ACTION-DATA-380.md) |
| Actual-game synthetic input and evidence | `Bc2ResourceInventoryProbe*`, `Bc2InventoryReloadProbe*`, `Bc2BoltControllerProbe*`, `tools/Run-OrdinaryResourceInputProbe.ps1`, and the related audits/tests |

Use `git diff 761b49a..HEAD` for the full change. The frozen payloads in
`handoff/checkpoint245/pending` are retained history and must not be applied over
the current implementation.

## Validation and known limits

- Normal x86 and x64 builds: 246/246 C++ suites each.
- 390 actual-game repeated rifle/shotgun sequence: three rifle magazine operations,
  shotgun pump/shell insertion, return to rifle and fire, second shotgun draw and
  pump with support retained, then rifle return.
- 390 actual-game VR-menu restart during an unfinished pump: new actor, boat exit,
  original rifle-magazine removal/return, fresh shotgun pump/shell/support return.
- 390 M95 sequence: five shots and physical bolt cycles through empty; final
  magazine refill is stock fallback.
- Each run delivered 240 stereo pairs, drained hooks, and left the game alive.
  Trace digests and narrower limitations are retained in the390 evidence note.

The native-operation source closure is
`62e55991fc6f2a69992e60153dbf1129aa25fd1baca61570c8c15c9f01f9f27c`;
its verifier, reviewed header and receipt are included. These tests were run
locally before publication; the source bytes were checked against that checkpoint.
Raw evidence stays local, so a fork should reproduce the relevant checks before
making new native claims.

The last human headset375 test failed;390 has no human acceptance yet. Feel,
controller alignment, stock/ring deformation under real motion, intermittent hand
loss/crashes, and sustained campaign behavior remain open. The successful restart
does not establish death recovery. Data coverage is not complete native weapon
support, and this is not a player release.
