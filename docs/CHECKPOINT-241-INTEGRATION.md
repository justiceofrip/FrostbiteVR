# Shared equipment integration241 — actual combined sequence completed

The requested outcome remains a substantial combined headset test after
automated actual-game checks, including pump and bolt mechanisms. No new
headset acceptance is claimed here.

Full normal240 passed228 C++ suites on x86 and x64. Its frozen source digest is
`3a1fc16fc9e91327e41b3db22054ba532e2a1ebe3c2de549f0fdd4a38beeceef`.
It includes rigid magazine assemblies and ordinary bolt custody/mapping
foundations; normal pump/bolt activation is still off. Thirteen assembly Python
tests also pass after integration. Private caches remain outside the repository.

## Changes after the full240 build

Source241: `3e2d80d785859f4bd0946eca982a47abc995b97559f8d8dbe657acd4efca7d92`.

- Shared native release submission now uses processing time to decide whether
  a scoped held callback was already running. Original input deadlines remain
  unchanged; repeated submissions cannot move this boundary.
- Already-submitted shell insertion survives a typed transient ammo-copy
  mismatch under its original operation deadline and current ownership checks.
  The regression reproduces the earlier cancellation and completes the full
  magazine/pump/shell/support/holster/rifle-fire sequence after correction.
- Measured M95 forward prediction correction is accepted only after sibling
  native completion, followed by the destination's own idle update and the
  server's completion. A copied ready snapshot alone cannot produce readiness.
  Captured239 callbacks and adversarial variants exercise the same validator.

## Actual239 evidence

`resource-pump239-01`: first two rifle magazine cycles and pump completed; shell
loading cancelled during a submitted operation. Input was fresh2.33ms and the
receiver's maximum publication gap was32ms. Native count transfers straddled the
failure while the old observation lease had expired. The typed gap verdict is
inferred from that interval and the reproduced consumer branch; the diagnostic
ring did not retain the direct verdict. The entire combined run failed.

`m95-physical239-01`: gun ownership remained exact and ammunition changed5/45 to
4/45. One physical release occurred, but a legitimate native forward Restore
caused cancellation before readiness. Its missing server exit prevents treating
that failed run as successful after the code fix. The two-cycle retry is pending.

## Parallel integration remaining

Actual `resource-scar241-01` completed all three magazine cycles, SPAS firing,
physical pumping, shell insertion, both support returns, shoulder return to the
SCAR and final firing. Native magazine operations6 completed/0 rejected;
SCAR ended29/180. All240 stereo pairs arrived without asynchronous timeouts.
The strict recording audit remains inconclusive:381 retained holds all restored,
but runtime counters indicate382; nine recording lock drops prevent reconstructing
the missing row. This is functional completion, not complete recording or headset
acceptance. The read-only postflight tool's hardcoded asset filter also excluded
SCAR; it now accepts the expected exact asset and was checked with M95.

Actual `m95-physical241-01` completed one physical bolt cycle, returned ordinary
support and gun custody, and fired the second shot. The second cycle cancelled
on native Restore2903 before manipulation (failure7). Loaded ammunition5→3 is
therefore not proof of two completed cycles. The retained-state placement fix
is supported by the first-cycle return; the pre-hold prediction progression
needs correction. Both processes remained responsive after detached probes.

The combined private build passed23 focused suites per architecture; the M95
private build passed28. Full normal241 has not run. MAIN now has subsequent
ordinary dispatcher integration work and must be pinned again before building.

Ordinary pump activation, ordinary bolt Gameplay wiring, family dispatch,
interruption recovery, F2000 exact enrollment and measured pistol hand-pose
selection remain under development. Native completion should follow a shared
documented convergence contract rather than accumulate incidental callback-order
exceptions. Belt-fed LMG interaction/native completion is still a separate gap.

The current campaign and build paths are tracked in local ACTIVE-WORK.json.
No GitHub update or headset launch is part of this checkpoint.
