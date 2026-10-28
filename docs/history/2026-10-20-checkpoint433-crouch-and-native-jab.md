# Checkpoint 433: crouch traversal and native jab evidence

Both complete normal builds passed 294/294 suites. Native source is
`bdd1006f36909942f55c184a36a995fe73b8782609e8174a3f18c5666260652e`;
the frozen normal-runtime receipt is
`be9b4894713448895c84bc7efb37e4272c52e569f8d5f48fc66d7d99505a3b50`.
This runtime is not installed or published.

The native action adapter now supplies the QPC clock for menu leases separately
from the XR/input clock. Actual emulated input crouched under the opening fallen
tree and stood again: native eye/body height was 1.548, 0.943 and 1.547 metres,
with 427 delivered packets and no menu pauses. The three-second approach moved
the actor 3.61 metres. This is native traversal evidence, not headset comfort
acceptance.

A subsequent physical blade gesture completed the original acquire/attack and
damage callbacks. Serial 1/input 627 targeted native object 1231476320; the
callback recorded health 12.5 to zero. No pending or unknown attack remained.
The target was not independently joined to a spawn GUID, and the objective
transition was not captured. Do not treat this as complete opening-mission
acceptance.

That approach cancelled when its Gameplay lease expired about 114.6 ms after
the hit. No menu/loading epoch transition was observed. A neutral follow-up
rendered normally and detached. A further approach cancelled before movement:
a measured 162.357 ms native frame gap outlasted the 100 ms menu lease. The
receiver stopped before frames resumed. A private startup-only diagnostic
correction is being prepared; movement/attack freshness must remain enforced.

The final failed test returned trace 2 / probe 12 / receiver 1. Independent
review verified all 43 non-exit teardown predicates, exact survivor graph,
references, callbacks and zero restoration failures. Published 1 / consumed 0
failed the separate stream acceptance minima of 4 / 3. Preserve the actual
failed exits; the old helper's zero-exit requirement cannot serve as a cleanup
receipt for this run without a reviewed separation of those two outcomes.

Private evidence: `bc2vr-recovery/checkpoint433` and
`bc2vr-recovery/knife430/{approach43301,approach43302,guard-observe43301,progress43303}`.
BC2 remains open; no OS keyboard/mouse input was used.

The separately verified nested-launcher cache correction passed 21 targeted
tests. Release preparation now targets a launcher-only 0.1.11 overlay on the
exact 0.1.10 native runtime. The unaccepted 433 gameplay changes must not be
included or described as released with that hotfix.

Storage remained bounded. Lossless compression reclaimed 410.17 MiB from
126 completed compiler outputs and another 269.01 MiB from 57 completed raw
captures, preserving content hashes. No user recordings or source were deleted.
