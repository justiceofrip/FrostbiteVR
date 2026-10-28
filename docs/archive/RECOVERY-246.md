# Interrupted mechanism recovery — checkpoint 246 in development

The checkpoint245 headset session failed after a SPAS shot: interactions locked,
then the user attempted a campaign checkpoint reload and the game crashed.
The first cancellation and crash instruction were not captured. Treat these as
separate unresolved failures; the work below does not establish their exact cause.

## Changes

- Integrated the previously isolated physical242 and native243 recovery payloads
  after verifying every original and candidate source hash. Both pump and bolt
  consumers share the typed physical cycle/convergence policy.
- Added the default-OFF `BC2_NATIVE_CYCLE_RECOVERY` option and the corresponding
  `Build-Checkpoint.ps1 -NativeCycleRecovery` switch. Ordinary manual cycles are
  a prerequisite. No native recovery acceptance is implied by enabling the build.
- Fixed a routing race: recovery could begin after a cached route check but before
  normal evaluation, allowing an original Update without the recovery guard.
  The route and decision are now reserved under one policy gate. Native reads
  and the original callback remain outside that gate; already-issued held
  callbacks drain through their original path.
- Added an immutable first-terminal-failure journal. Native callbacks publish
  copied values once; the worker writes `native-cycle-first-failure.json` at
  up to100 ms polling intervals, independently of the final trace. Reading this
  journal never acquires the native policy gate or renews controller evidence.
  An immediate process termination before the worker polls can still lose it.
- Corrected the combined diagnostic's final-shot assumption. The100 ms pulse
  may produce one or two shots. The diagnostic now requires exact unchanged
  ownership/reserve/capacity, monotonic loaded counts and fresh stable evidence.
  This changes the test driver, not the ammunition consumer. The failed245
  F2000 report is preserved; future native firing audits remain independent.

## Recovery limits

An established, recently observed three-copy Held cycle may convert to guarded
native convergence after a physical interruption. Real native idle evidence and
a fresh neutral/regrip/full stroke are still required; native idle cannot count
as a completed physical gesture. Pending exact outcomes remain durable.

A service already terminally Cancelled, an interruption before all three copies
reach Held, changed actor/space lifetime, or lost native guard continuity remain
unresolved. The candidate must not erase owed work to make controls appear usable.
These limits mean this is not yet a general fix for every observed headset lock.

Scene review found no crash instruction: old stereo cleanup and new-owner rebuild
both completed. A missing scene-to-Gameplay lifetime barrier and possible input
thread migration require separate work. Local review evidence is retained in
`bc2vr-recovery/scene246-review`; it is not a claim that either caused the crash.

## Validation so far

The initial integrated recovery composition passed235 C++ suites on each
architecture. Routing regression19 groups and the first-failure concurrent
publication tests passed on x86 and x64. The composed build including these
corrections passed all237 C++ suites on both architectures. Its exact source
digest is `bf7ac6e6f796ab209adb2a36a52f2a163d8dcfbfbd325139f04333faee906bc3`.

Actual `resource-f2000246-01` failed during the first magazine warmup, before
the pump or any ammunition operation. The armed magazine driver returned
failure2 about3.65 seconds into its6-second warmup. A native resource
publication expired just before that failure; subsequent investigation is
separating this observation gap from an actual change of weapon. The rifle
remained at30 loaded/210 reserve. BC2 remained responsive and the native hooks
disabled/drained cleanly. There was no terminal native-cycle failure because
the pump sequence never started. This is not native recovery acceptance.

The next isolated interruption emulator reproduced starvation when newer
native idle observations overtook an otherwise fresh neutral controller packet.
The shared-policy fix and held-support/tracking-loss trajectory are being
validated separately before integration into247.

Root owns native actions. Other agents stage isolated final-fire auditing,
an input-only interruption trajectory and a reusable scene-epoch barrier.
Their work is not integrated merely because a candidate directory exists.
