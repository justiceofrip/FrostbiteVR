# Candidate289: held grip survives delayed render contact

The288 game run completed six pumps and their support returns, then lost the
seventh grasp at input2372. Controllers remained tracked and gripping, but
render contact still referred to input2366. Keeping custody tied to the older
contact deadline expired the claim. No native completion contradiction occurred;
the input diagnostic timed out while the original pump debt remained held.

An acquired mechanism now renews its hand claim from the current recorded
controller grip. Its movement still uses the original recorded renderer pose,
original pose deadline, native permission and maximum pose-gap bound. A new
read-only arbiter check validates historical geometry without moving a newer
custody claim backward. Missing/expired render contact supplies no guided
target. A contradictory binding, release, tracking loss or expired controller
claim still drops the grasp. A long pose gap still needs neutral/regrip and a
new complete stroke; fresh grip input alone never pumps the gun.

This separation applies to the shared physical-cycle consumer used by pumps
and bolts. Acquisition remains contact-based. The connected regression with a
delayed renderer packet fails on288, even though grip packets remain fresh.
Focused pump/bolt/arbiter checks, full builds and actual-game results must be
recorded independently. It is not a fleet or headset acceptance claim.

Failed288 trace: `pipeline-runtime-20261005/root-monitor/resource-empty288-01`.
SHA256: `9b1044590d04e74916c3d73b1dcda88bf9424b8525d44e6a8b2c3adf79c78caf`.

## Subsequent290 validation

The actual-game controller-emulation run completed eight SPAS shots and physical
pumps (8/24 to 0/24), inserted a shell (1/23), returned support grip, swapped
shoulder weapons, removed/reinserted the full F2000 magazine (30/210 to 0/210
to 30/210), and fired afterward. Support returned 30-48 ms after pumps and
132 ms after shell insertion. The independent ammunition/callback audit passed.
All pump and magazine presentation samples had zero fallbacks. Hooks drained;
the game stayed alive. This is a bounded native result, not headset or complete
campaign acceptance. Nine recorder lock drops mean full callback coverage is
false; all required shot, hold and completion proofs were present.

The shared fixes preserve controller custody independently of older render
poses, protect in-flight completion reads, inhibit the last-shot automatic
reload, and recognize the directed server -> clientB -> clientA completion
chain. The successful run used local tails; forward routes are covered by exact
failed287/289 fixtures and connected regressions, not claimed as live290 hits.

Both normal player builds passed 243 suites. Normal290, headset290 and a local
source checkpoint are being frozen from the same source receipt. Diagnostic
drivers are disabled in the player build. No publication or headset host launch.
Earlier279 native menu/pointer checkpoint recovery and280 loaded M95 bolt
evidence remain preserved without redundant repeats. Last-round M95, native
pistol and belt-fed reloads, broader enrollment, sustained campaign/death
recovery and headset visual acceptance remain unfinished. Seventeen compiled
magazine prerequisites do not mean seventeen accepted weapons.

Evidence: `pipeline-runtime-20261005/root-monitor/resource-empty290-01/empty-tube-receipt-review.json`.
Source: `fb11a03db956d779a22dc1b50f557ab49cc4ccb2ecf48f5b25dad2cff66d1bc7`.
Trace: `4661e1a2b9a5ebb9ede4ff293bc085b08afbccbe75d411aa9b45b123015f8277`.
See [prediction chain](PREDICTION-CHAIN-290.md) and
[controller custody](CONTROLLER-CUSTODY-289.md).

