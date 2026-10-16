# Current state: checkpoint 412

This is the only document that describes the current state. Overwrite it when
the state changes; do not append. Detail lives in the per-topic notes in `docs/`
and the frozen logs in [history/](history/README.md). Each section names its sources.

## What 412 is

Sources: [README](../README.md), [412 handoff](../handoff/checkpoint412/README.md),
[CHECKPOINT-412](CHECKPOINT-412.md), [validation.json](../handoff/checkpoint412/validation.json).

- Unreleased developer source: all work since public GitHub checkpoint 390
  (`0e5ad46`) plus the presentation/launcher follow-up to the normal410 headset
  run. Not a player release and not whole-campaign acceptance.
- **Latest human headset run: normal410**, ended at the user's request on
  2026-10-16 UTC (October 15 local). The user reported substantially better
  pumping and reliable manual shell loading, with residual stock loading-shell
  animation, no spent-shell ejection, high chest magazines, awkward ammo-counter
  placement and slow startup. This is partial feature feedback, not campaign
  acceptance. It supersedes older log entries naming failed human390 as latest.
- 412 changes:
  - The current manual gun owns a separate `ReloadShellPresentation` lease that
    keeps the verified SPAS loading-shell leaf hidden after the reload
    transaction retires. Physical shell previews and equipment recovery take
    priority. Ammunition, pump timing and hand ownership are unchanged.
  - Chest ammo and its grab target share one body anchor, lowered from -0.25 m
    to -0.45 m (reference-head-relative) for every ammo type.
  - A read-only native loaded/reserve counter sits beside the gun hand, offset
    (-0.15, +0.035, -0.12) m on the right controller's aim axes. It is hidden on
    tracking loss, a changed reference space, stale counts, menus, or a placement
    too close to or behind the head. There is no health bar or invented ammo.
  - Launch runs one fresh storage inventory, then a single-use child handoff
    receipt (at most 180 s). Builds, captures and other launches cannot reuse it.
- No new weapon registrations, native operation/configuration rows or
  multiplayer. Native offsets/ABI are unchanged from 410: config 1220 bytes,
  input version 3, frame channel version 7.
- Player opt-ins: knife, grenade and contextual-tool code is compiled in the
  normal composition, but those runtime opt-ins are OFF in the current personal
  launch profile. Synthetic controller drivers are OFF. Native knife attacks
  remain disabled for players.
- The machine-local [desktop launcher](DESKTOP-LAUNCHER.md) 0.1.2 points to
  frozen normal412 binaries. The GitHub checkpoint is source only; no public
  native installer is released.

## Evidence vocabulary

Keep these separate in every claim ([CONTRIBUTING](../CONTRIBUTING.md), [AGENTS](../AGENTS.md)):

- **Deterministic test**: C++ suites, Python tests, mocked runtimes, offline
  asset readers. No game or headset.
- **Actual-game check**: a bounded automated or scripted run against real BC2
  with native traces. Not human acceptance.
- **Headset acceptance**: a human run in the headset.

A mock receipt, static profile, compiled capability count, final ammo count or
passing unit test does not prove native success or a working weapon.

## Verified for 412: deterministic only

412 has **not** been run in the game or a headset (`native_game_tested_this_candidate`
and `headset_tested` are false in validation.json).

| Check | Result and scope |
| --- | --- |
| Full x86/x64 builds | 283 C++ suites each |
| Ammo HUD | D3D11 WARP pixel readback; mocked OpenXR lifecycle, stereo layer, pose and freshness. No headset |
| Installed body cache | 46 parts rebuilt; six magazine profiles (XM8, AEK, SCAR suppressed/unsuppressed, F2000, MP443) clear the face. CPU geometry only |
| Storage accounting / launch handoff | 34 / 19 checks; handoff includes a real hidden child process |
| Desktop launcher | 38 checks; no game or VR process started |
| Source package | 35 Python checks |
| Personal launch preflight | Frozen binary architecture/hash and 24 dependency pins; no launch |

Independent read-only reviews covered shell ownership/recovery precedence, XR
coordinate spaces and sample lifetime, and launch-receipt process/expiry checks.
Storage inventory went from 27.08 s to 7.50 s. That is a storage timing, not an
end-to-end game/headset startup time.

## Earlier bounded results that still stand

Actual-game checks, not headset acceptance. Sources: [history/HANDOFF](history/HANDOFF.md)
(410 section), [history/STATUS](history/STATUS.md) (390 and 408 sections),
[409](CAMPAIGN-DIAGNOSTICS-409.md), [410](CAMPAIGN-DIAGNOSTICS-410.md),
[411](CAMPAIGN-DIAGNOSTICS-411.md), [README](../README.md).

- 408: pump mechanics and both-eye stock appearance pass 120 pairs.
- 409: held knife session close returns the knife in 738 ms with unchanged ammo
  and drained hooks (22-check assessment).
- 410: mission08 stereo damage presentation delivers 240 pairs with exact
  restoration of 24 scoped values. All 24 saved eye pairs show matching tint,
  blur and darkening. This is not proof of death/restart or headset acceptance.
- 411 (private diagnostic): native death and player recovery on SP_009, 240
  stereo pairs, 38 collection checks. Both eyes darken and recover together.
  A flash before event export is not covered. Do not repeat this unchanged run.
- 407: mounted firing (a 900 ms hold reached two exact Shoot callbacks).
- 390: continuous SCAR/SPAS swaps, recovery after a VR-menu checkpoint restart,
  and five M95 bolt cycles through empty.
- Earlier headset runs showed native stereo, tracked weapons and support hands,
  roomscale, controller menus, chest ammo, shoulder holsters, selected manual
  reloads and boat head-aim/fire. All remain experimental; later runs exposed
  regressions.

## Broken, open or unverified

Sources: [412 handoff](../handoff/checkpoint412/README.md), [CHECKPOINT-412](CHECKPOINT-412.md),
[411](CAMPAIGN-DIAGNOSTICS-411.md), [history/HANDOFF](history/HANDOFF.md) (storage cleanup), [README](../README.md).

- No 412 headset run. The chest, counter and shell changes have automated
  coverage only. Counter comfort and chest/holster alignment are still
  subjective checks.
- **Spent-shell ejection is not implemented.** The casing mesh and effect were
  found offline, but no verified SPAS ejection-port transform or native dispatch
  exists ([shell notes](SHELL-PRESENTATION-412.md)).
- Immediate post-shot pump eligibility is deliberately unchanged; the user
  deferred it.
- M95 magazine refill still uses the stock fallback.
- Not established: every weapon, belt-fed reloads, physical zoomed scopes, full-body
  IK, all campaign missions or a full playthrough, multiplayer, sustained Steam
  Link stability. Capability/registration counts are prerequisites, not working
  guns ([coverage](WEAPON-COVERAGE-CURRENT.md)).
- Load-time crashes before VR attachment: 411 mission09 crashed while loading,
  and without a fault-time module map or dump its cause is unknown. The earlier
  mission05 SP009 load also crashed before any VR receiver attached.
- Superseded raw test frames were pruned on 2026-10-15 with user approval; bulk
  raw captures from 390, 407 and 408 remain. Older reports may name raw files
  that no longer exist; do not regenerate them.

## Build and test

Requirements: Windows, VS C++ x86/x64 tools, Windows SDK, CMake 3.24+, Ninja,
PowerShell 7, Python 3.11+ ([BUILDING](../BUILDING.md)). None of these commands start
BC2 or a headset runtime.

```powershell
.\Build.ps1 -Architecture x86 -Jobs 2
.\Build.ps1 -Architecture x64 -Jobs 2
python -m pip install -r requirements-dev.txt
python -B -m unittest discover -s tests -p 'test_*.py'
```

The normal412 composition (run the architectures one after the other and reuse
the existing build trees):

```powershell
.\Build-Checkpoint.ps1 -Architecture x86 -Jobs 2 -OrdinaryManualCycles -NativeCycleRecovery -BodySceneDepth -PlayerGrenades -PlayerKnife -ContextualTools
.\Build-Checkpoint.ps1 -Architecture x64 -Jobs 2 -OrdinaryManualCycles -NativeCycleRecovery -BodySceneDepth -PlayerGrenades -PlayerKnife -ContextualTools
```

- Omitting switches builds a different composition. Build caches keep options;
  use a fresh build directory or clear the optional CMake variables.
- Compiled capability output goes to `reports/<architecture>-compiled-capabilities.json`.
- Shell-presentation suites: `Bc2ReloadPreviewTests`, `Bc2PhysicalReloadTests`.
- The installed-game signature test is opt-in: `FVR_TEST_INSTALLED_BC2=1`.

## Pinned native-operation receipt

Sources: [BUILDING](../BUILDING.md), [412 handoff](../handoff/checkpoint412/README.md),
[CHECKPOINT-412](CHECKPOINT-412.md), `Build-Checkpoint.ps1`.

- `Build-Checkpoint.ps1` passes `profiles/checkpoint412/source-with-header.json`
  as `BC2_NATIVE_OPERATION_SOURCE_RECEIPT`. It binds the native dependency closure
  to SHA-256 `5d0542ffed1acdd2688df8b05534f9b465b4780fb233f52fb45aa07c31a1370d`.
- **Editing any covered native file breaks `Build-Checkpoint.ps1`** with
  "exact source verification failed". The receipt is byte-sensitive, and
  `.gitattributes` (`* -text`) disables line-ending conversion.
- Review and validate a changed adapter before updating its receipt. A new hash
  alone is not native evidence. Default `Build.ps1` builds remain available for
  policy work. The XR host, launcher and tests are outside the closure.
- Other selected profiles: `aim339` (gun hand), `support373` (support contacts),
  `resource-enrollment295-pistol` (magazines). Older numbers identify retained
  data, not the current build.

## Storage budget

Sources: [STORAGE-BUDGET](STORAGE-BUDGET.md), [AGENTS](../AGENTS.md).

- C: is OS-only. Keep generated data, logs and scratch on G:, with process-local
  TEMP/TMP/TMPDIR. Never change Windows-wide settings.
- Call `tools/StorageBudget.ps1` (`Assert-FvrStorageBudget`) before every build,
  capture or session, including private wrappers. Never bypass a refusal.
- Limits: 40 GiB generated (disk bytes), 15 GiB free on the output and temp
  volumes, 512 MiB of raw capture per run. A build reserves 3 GiB and a session
  640 MiB.
- Raw captures are opt-in. Budget frames x size x both eyes x streams first.
- Reuse the x86/x64 build trees. Keep source patches and small summaries, not
  build copies. Run large jobs one at a time.
- Keep binaries named by current launch/test receipts, even in old build
  directories. Cleanup needs an exact reviewed manifest.

## Next work named by the 412 sources

From the [412 handoff](../handoff/checkpoint412/README.md) "Limits and next work" and
[CONTRIBUTING](../CONTRIBUTING.md). The sources set no strict order.

1. Human comfort check of the chest height, counter placement and shell
   presentation, when the user asks for a headset run.
2. Spent-shell ejection as a local cosmetic transient. It needs a rigid-mesh
   decoder, measured port placement and a transient manager. No ammo writes or
   extra native shot.
3. Immediate post-shot pump timing (deferred by the user).
4. Wider work: interrupted reload and holster/pickup coverage through the
   persistent consumers; AR/SMG data joined by exact configuration; pump, bolt,
   charging-handle and belt-fed mechanisms after native verification; scopes,
   optic dots, full-body IK and campaign/vehicle coverage.

## Where to look

- Code entry points per area: the table in the [412 handoff](../handoff/checkpoint412/README.md).
- Rollback: branch `checkpoint/410-headset-rollback` at `57e04e90e8d9f1d8f9448556db549725c888fa9a`.
- Older detail: search [history/](history/README.md) by checkpoint number.
