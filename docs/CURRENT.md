# Current state: checkpoint 415 integration

This is the current-state entry point. Replace its contents when the state
changes; retained checkpoint evidence lives in [history/](history/README.md)
and the [415 handoff](../handoff/checkpoint415/README.md).

## What is combined

Checkpoint 415 combines checkpoint 414 (`b1d593e`) with the build and documentation
work on `main` (`c345db7`). Gameplay source and calibration remain checkpoint 414.

- **414:** magazines and shells share a lower-chest display/pickup contact at
  (-0.16, -0.55, +0.06) m. Alternate belt pickup remains available. No per-weapon
  grasp calibration, ammunition operation or mechanism timing changed.
- **413:** shared pump/bolt dispatch and interruption recovery preserve owed
  cycles and hand custody; SPAS movement/closure audio follows physical stages.
- **Build integration:** ten tests share the standard reload-flow runtime object
  library, CTest uses the requested job count, and the static CRT default is set
  before targets. CI/YAML and Python discovery fixes are retained. These reduce
  development overhead; they do not claim improved headset frame rate or startup.
- **Documentation integration:** this file is the current-state front door;
  historical status, handoff and transfer logs remain in `docs/history/`.

Both integration builds passed 284/284 suites and the native source receipt
verified. All four rebuilt runtime binaries are byte-for-byte identical to
frozen 414. The desktop launcher therefore remains on that exact **414** profile;
no new game/headset run or runtime promotion is needed for this merge.
Python and launcher validation also passed. Player knife, grenade and
contextual-tool code is compiled in the normal composition but runtime opt-ins
remain OFF. Synthetic player controllers are OFF. Native config is 1220 bytes;
input version 3 and frame channel version 7 are unchanged.

## Evidence and acceptance

Sources: [414 handoff](../handoff/checkpoint414/README.md),
[413 handoff](../handoff/checkpoint413/README.md),
[412 failure](HEADSET-FAILURE-412.md), [415 integration](../handoff/checkpoint415/README.md).

| Scope | Result |
| --- | --- |
| 415 C++ builds | Normal composition passed 284/284 suites on each architecture; native source verification passed |
| 415 runtime parity | Probe, trace tool, receiver and XR host match frozen 414 byte-for-byte; desktop runtime intentionally unchanged |
| 415 Python/launcher checks | Discovery: 986 tests, 985 passed and one skipped; renamed tool suite: 9/9; launcher parse/UTC/culture/routes/setup checks passed without game or XR launch |
| 414 deterministic | 284 suites per architecture; 6,048 physical poses across 18 magazine configurations; installed mesh bounds checked |
| 414 actual BC2 | Lower-chest magazine in 239 paired views and shell in 188; contact error below 0.061 mm; hooks drained and game responsive |
| 413 actual BC2 | Interrupted-pump recovery and a separate eight-shot pump/manual-shell/holster/rifle sequence passed; hardened final source passed another bounded cycle/reload/holster/rifle run |
| 413 audio | Physical-stage cues verified at the PC endpoint; final route/driver ownership checks passed. Steam Link timing was not measured |
| Latest human headset run | **412 failed:** firing, pumping and holstering locked out while grip remained usable. Later automated results do not establish headset reliability |

A deterministic test has no game or headset. An actual-game check uses real BC2
with bounded native evidence. Headset acceptance requires a human headset run.
A profile, capability row, mock receipt or final ammunition count is not a
working-weapon claim. No full campaign playthrough is accepted.

The [415 validation record](../handoff/checkpoint415/validation.json) records the
integration results. Public source locations are `main` and tag `checkpoint-415`,
with a fast-forward containing both source histories. The local integration
branch is `checkpoint/415-integrated-build`.
No remote CI result for the combined merge is claimed.

## Open work

- Headset verification of pump recovery, audio alignment, lower-chest comfort
  and holster reliability remains open. No new headset test is required merely
  to combine build tooling when runtime outputs are unchanged.
- Spent-shell ejection is not implemented. Immediate post-shot pump timing is
  unchanged and was deferred by the user. M95 magazine refill uses stock fallback.
- Broader weapon geometry, belt-fed reloads, physical zoomed scopes, full-body IK,
  full campaign/vehicle coverage, multiplayer and sustained Steam Link stability
  remain unverified or unfinished. See [weapon coverage](WEAPON-COVERAGE-CURRENT.md).
- Earlier pre-attachment load crashes lack enough fault-time evidence to identify
  their cause. Bounded paired-eye damage/death recovery checks are not whole-
  campaign or restart acceptance.
- The inherited build notes identify warning-option coverage for early targets
  and some tool-directory Python discovery gaps. See the integration handoff for
  the exact validation scope; this merge alone is not a fix for every build issue.

## Build and source pins

See [BUILDING.md](../BUILDING.md) for prerequisites and default builds. Reuse the
existing architecture trees and run the two builds sequentially:

```powershell
.\Build-Checkpoint.ps1 -Architecture x86 -Jobs 2 -OrdinaryManualCycles -NativeCycleRecovery -BodySceneDepth -PlayerGrenades -PlayerKnife -ContextualTools
.\Build-Checkpoint.ps1 -Architecture x64 -Jobs 2 -OrdinaryManualCycles -NativeCycleRecovery -BodySceneDepth -PlayerGrenades -PlayerKnife -ContextualTools
python -B -m unittest discover -s tests -p 'test_*.py'
```

The operation receipt remains `profiles/checkpoint414/source-with-header.json`,
with native dependency-closure SHA-256
`3d976c48307f0f6559269f8e197b384563033cf5c3401b96a8bcf3e43efdd7a0`.
Covered source is byte-sensitive: do not update a receipt merely to silence a
verification failure. Tests and build scheduling do not change that native proof.
Retained calibration profiles are `aim339`, `support373` and
`resource-enrollment295-pistol`. Capability counts are prerequisites, not accepted
guns. A default `Build.ps1` run uses a different feature composition from the
checkpoint commands above. CI tests the default composition.

## Storage and rollback

Generated files, temporary files, builds and logs stay on G: with process-local
TEMP/TMP/TMPDIR. Read existing runtimes on C: without writing there. Call
`tools/StorageBudget.ps1` before builds/captures/sessions; keep the 40 GiB generated,
15 GiB free-space and 512 MiB raw-capture limits. Reuse build trees and preserve
current source, symbols, receipts and user recordings. See [storage policy](STORAGE-BUDGET.md).

The frozen desktop profile and source tag `checkpoint-414` remain the runtime
rollback for this integration. The exact 413 profile/audio/cache and source tag
`checkpoint-413` remain preserved, as does branch `checkpoint/410-headset-rollback`.
Launcher app rollback and choosing a local game profile are separate operations.
