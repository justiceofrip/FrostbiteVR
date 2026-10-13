# Preserve aiming through physical pump return — 384

The corrected controller driver in383 exposed a production handoff failure.
Physical pumping and native completion succeeded, but returning the support
hand failed because its captured aiming correction ended one renderer packet
too soon. See [the reproducer](STATIONARY-CONTROLLER-TESTING-383.md).

The physical pump now exposes a read-only observation of its existing pending
support return. It grants no claim or native action. The shared mechanism-aim
helper retains its exact captured right-hand correction for that consumer's
bounded200ms return window, with current controller input and the same actual
gun claim. Tracking loss, release, different ownership/reference/scale, stale
input and a changed or expired submission timestamp reject continuation.

The adapter passes that guided frame into ordinary support adoption after
fresh rendered contact acquires the real support claim. Subsequent normal
two-hand updates inherit the same orientation. Native ammunition, firing and
cycle completion remain independent. A missing or failed return cannot
manufacture a support claim or extend the aiming window indefinitely.

## Validation so far

- With guided aim reaching its renderer, the old connected fixture reproduces
  native383's first-cycle Settle timeout under stationary controllers.
- The corrected fixture completes eight held-support shots/pumps, empty-tube
  shell loading, support return, holsters, rifle magazine return and rifle fire
  in34.68 simulated seconds. The former simple route takes43.57s. Native
  responses are mocked in these fixtures.
- Shared aim tests exercise the real arbiter and support policy through
  support→mechanism→support, preserving both grip and aim orientations. Invalid
  return windows, old support seeds and mismatched claims reject.
- Focused support, physical pump/recovery, controller/recovery and persistent
  inventory-composition suites pass.

Both full builds pass all 246 suites. The added accessor assertion initially
confused the earlier contact observation with the current submission time;
correcting that test expectation required no production change.

The actual-game run completes four shots, native pump acknowledgements and
support returns with the corrected stationary-controller driver. No support
return is lost. Cycle five then suspends its native hold following an input
gap and expires after three seconds without a neutral gesture. It cancels
with `Expired`, control rejection 7, and zero structural link differences.
The run fails overall; hooks drain and BC2 survives. This is a separate failure
from the headset's recorded `PublishedLinks` rejection and must not be described
as that rejection's proven cause. Evidence is under
`bc2vr-recovery/overnight384/native01`. Source digest:
`c55bbda273fb976293cf3bc898583f6ef4839970ac2355ea62973c86f483f0a5`.
There is no new headset acceptance or whole-campaign claim. The recorded
native third-cycle ownership cancellation, residual animation distortion and
return-to-pump testing after holsters remain outstanding.
