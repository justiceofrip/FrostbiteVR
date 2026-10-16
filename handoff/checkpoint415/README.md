# Checkpoint 415: combine the tested runtime with build optimizations

This integration combines checkpoint 414 (`b1d593e`) with `main` at `c345db7`.
The local branch is `checkpoint/415-integrated-build`. It retains the native source,
weapon calibration and runtime composition of 414 while bringing in main's
build/test and documentation improvements. It is a developer source checkpoint,
not a new player release or headset acceptance.

## What changed

- Ten tests share the standard reload-flow runtime object library; tests with
  distinct compile definitions retain their own compilation.
- CTest runs with the requested job count. The static CRT default is set before
  targets, and CI/YAML and Python discovery fixes from main are retained.
- `docs/CURRENT.md` is the current-state entry point. Historical status, handoff
  and transfer logs stay in `docs/history/`; the 413/414 evidence is preserved.
- Gameplay source, native operations, placement and calibration are unchanged
  from 414. These build improvements do not claim faster headset rendering.

## Validation status

- Both full normal-composition builds passed **284/284 C++ suites**. CTest took
  12.96 seconds on x86 and 5.81 seconds on x64 in these runs; these are local
  observations, not a controlled speed comparison.
- Native source receipt verification passed with the retained 414 closure.
- All four rebuilt runtime binaries (native probe, trace tool, IPC receiver and
  XR host) are **byte-for-byte identical** to frozen 414. The private
  `overnight415/runtime-parity.json` records the comparison.
- All 41 local links in the current entry documents and relocated 413/414 history
  preambles resolve, including referenced fragments; no merge markers remain
  in those documents.
- Python discovery ran 986 tests: 985 passed and one skipped. The renamed tool
  suite passed all nine tests.
- Launcher validation passed five parser checks and the UTC, culture, routes
  and setup checks without starting BC2 or an XR runtime.

The [compact validation record](validation.json) captures the local integration
results. No remote CI result for this combined merge is claimed.

The desktop launcher intentionally remains on frozen 414. The identical runtime
outputs need no new actual-game or headset run to validate this build/documentation
merge. This checkpoint combines development tooling without changing the
player-facing binaries or upgrading prior automated results to headset acceptance.

## Retained runtime evidence

[414](../checkpoint414/README.md) passed 284 suites on x86 and x64, 6,048 physical
poses across 18 magazine configurations, installed mesh checks and actual BC2
lower-chest placement checks for XM8 magazines and SPAS shells. The shared body
contact is (-0.16, -0.55, +0.06) m, and alternate belt pickup remains available.
No firing or reload was requested during the placement checks.

[413](../checkpoint413/README.md) supplies shared pump/bolt recovery and staged
SPAS audio. Actual-game emulation covered interrupted pump recovery and an
independent eight-shot pump/reload/holster/rifle sequence. PC endpoint audio and
final route/driver ownership checks have separate bounded evidence. No Steam
Link audio-timing or headset acceptance is implied.

The latest human headset result is the [412 SPAS lockout](../../docs/HEADSET-FAILURE-412.md).
Those failures remain preserved. Automated corrections do not establish campaign
reliability. Spent-shell ejection, wider weapon geometry/mechanisms, full-body IK,
zoom scopes and multiplayer remain unfinished; see [current state](../../docs/CURRENT.md).

## Build and rollback

Use [the checkpoint composition](../../BUILDING.md#checkpoint415-player-composition).
The native operation profile intentionally remains `profiles/checkpoint414`:

- Native dependency closure:
  `3d976c48307f0f6559269f8e197b384563033cf5c3401b96a8bcf3e43efdd7a0`.
- Config 1220 bytes, input version 3 and frame channel version 7.
- Retained aim/support/magazine data: `aim339`, `support373` and
  `resource-enrollment295-pistol`.

The desktop launcher app remains 0.1.2 with the frozen 414 game profile. Tags
`checkpoint-414` and `checkpoint-413`, their exact local profiles and referenced
binaries/caches remain intact. The older `checkpoint/410-headset-rollback` branch
is also preserved. Launcher app rollback and game-profile selection are separate.

No game binaries/assets, private recordings, raw traces or machine-local launch
configuration belong in this source checkpoint. All generated validation work
stays on G: within the existing storage budget.

Public source locations are `main` and tag `checkpoint-415`. A fast-forward of
`main` contains both the checkpoint414 and optimization histories; no history
rewrite or duplicate public integration branch is needed. The remote workflow
will run after publication, and its result is not part of the local checks above.
