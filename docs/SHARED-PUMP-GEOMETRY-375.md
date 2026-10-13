# Shared pump geometry and combined native regression

Checkpoint375 moves the selected asset, mechanism id, root/fore-end names,
weapon-relative axis, stroke and fixed assembly into explicit calibration data.
The physical consumer, renderer contact conversion, support return, recovery
path and private controller emulator use that data. Existing SPAS measurements,
hand shape, gesture policy and native operation admission are unchanged.

Previously the common consumer used SPAS constants, and its fixed stock parts
were selected by a rig fingerprint shared with other weapons. Fixed pieces now
come only from the selected calibration. An empty fixed-assembly list changes
no fixed pieces. The adapter rejects duplicate parts, the moving part itself,
and fixed descendants of the moving part. The current moving-part binding still
requires an exact named direct-child leaf; arbitrary animation hierarchies are
not newly supported.

This is reusable presentation/gesture infrastructure, not a grant to pump any
weapon with a matching skeleton. SPAS remains the only production pump family.
Other weapons need measured data and a separately verified native operation
binding. The 69 support variants and 18 magazine prerequisites remain unchanged.

## Deterministic and build validation

Eight focused suites pass. A synthetic second family uses a different asset,
mechanism id, part name and 135mm stroke, testing diagonal and longitudinal axes
in both directions, rotated weapon placement and two world scales. The actual
physical consumer and private palette adapter complete one gesture and preserve
the original rig bytes; the native acknowledgement is mocked. Fixed-assembly
selection and invalid bindings have separate regression coverage.

Both full normal builds pass246 suites: x86118.10s and x6450.17s. The normal
binaries are frozen separately from the two private input-driver builds.
Source SHA256: `8b2f2b4ec35fbd5fb938d16021817cb1e3d7900a178f246bf71ca516a88320c8`.

## Actual-game runs

Both fresh SCAR/SPAS campaign runs complete three magazine cycles with six
native transfers, rifle support, both shoulder swaps, one SPAS shot/manual pump,
one shell insertion, immediate support return and subsequent rifle fire. Each
consumes240 stereo pairs, reports zero magazine-presentation fallbacks, restores
the hooks and leaves the game alive before the owned process is closed.

| Evidence | Post-shot acquisition/sideways inspection | Held support and tracking loss |
| --- | ---: | ---: |
| Shell-to-support delay |127.160ms|130.743ms|
| Final SCAR counts, all three branches |29/180|28/180|
| Moving pump poses / paired copies |120/240|48/96|
| Resting pump poses / paired copies |431/862|427/854|
| Authored hand poses |120/120|48/48|
| Rejected pump poses or fallbacks |0|0|
| Discarded stereo pairs |1|0|
| Recorded callback lock drops |10|6|

The second run holds the support grip through the shot, adopts the pump without
releasing squeeze, loses tracking at30.105mm travel for500.710ms, requires fresh
neutral input, completes the owed stroke, and returns to normal equipment use.
Its commanded movement includes220mm rearward travel and80mm lateral movement
against the95mm virtual rail. All267 recorded recovery guards restore exactly;
two original idle boundaries in each native branch independently verify the
recovered outcome. No extra game shot or ammo transfer is inferred from motion.

Private evidence folders beside this repository:

- `bc2vr-recovery/pump375-native01`: `combined-grip-audit.json`,
  `receipt-review.json`, `pump-presentation-audit.json`, `authored-hand-audit.json`.
  Trace SHA256 `d044896be70d1e5d807eef01d829116052ad1fc3dfbaf97ccfbd5e3475b62eb9`.
- `bc2vr-recovery/recovery375-native01`: `recovery-integration-audit.json`,
  `receipt-review.json`, `pump-presentation-audit.json`, `held-support-audit.json`.
  Trace SHA256 `630fce27a2671bc7bf93d827c9a8c69449a4085af8e169ed1db5299b74c3334e`.

## Limits and retained failures

The ordinary inspection mode uses post-shot acquisition. A historical held-grip
audit correctly rejected that mode; `scenario-scope.json` records the limit.
The distinct interruption mode supplies actual held-at-shot recovery evidence.

The recovery run's strict final-shot audit fails because one of two client1
shot transitions is absent from the recorded callbacks. The trace goes from
29 rounds at invocation11702 to28 before11708. The cause of that missing
transition is not established. Both authoritative server transitions and all
three final counts agree. The narrower recovery integration audit explicitly
reports incomplete client shot-transition coverage; the failed strict audit is
retained. Neither run claims complete callback coverage.

Six selected side-on captures show the stock/ring attached to the receiver.
These images do not cover every frame or prove controller alignment, pump feel,
or stereo comfort. Handset acceptance remains required. The first run's one
discarded pair is recorded, not assigned an unverified cause.

An initial stock launch exited during campaign loading before any mod injection;
its root cause remains unknown. Its60-second observer had already detached.
A second observer request rejected an unsupported180-second duration without
attaching; the game remained open. A supported60-second observation was then
started at the loading step and recorded no exceptions. No claim is made that
startup/restart crashes are fixed.

The next normal player candidate is375;374 and369 remain preserved. No new
weapon's manual reload or pump is declared accepted by this checkpoint. See
[the coverage index](WEAPON-COVERAGE-CURRENT.md) for remaining campaign gaps.
