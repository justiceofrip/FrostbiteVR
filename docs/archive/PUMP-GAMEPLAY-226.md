# SPAS pump gameplay and renderer candidate, 226

This candidate connects the physical pump consumer to BC2 Gameplay, the native
cycle service, and the private rig palette. It remains an explicit private trial.
No ordinary profile, runtime option, or default call site enables it. Native hold
admission, completed native cycles, and headset operation are separate checks.

## Implemented path

`Bc2PhysicalPump` publishes the current native/physical owner mapping to the
native service. A successful native shot and its three held branches must yield
an actual service lease before the physical consumer begins. Current right-hand
GunHold is required. Left-hand acquisition or explicit WeaponSupport transfer
then follows the shared arbiter and pump policy. The back-and-forward gesture
emits one exact release request; only the matching native ready outcome clears
the obligation. A failed submission has an ambiguous outcome and cannot invent
a rejection or readiness. Retained outcomes are reconciled before selection
changes, without renewing their original timestamps.

The renderer measures the independent anatomical left wrist before support,
pump, or IK guidance, converts it to weapon-local metres, and applies the
verified hand binding's mechanism contact point. That packet returns through
`ReadPumpContact`, including its original owner, sequence, observation and
deadline. The shared consumer now accepts an original renderer N-1 packet using
the arbiter's recorded-history `AcquireFrom`, `TransferFrom`, and `RenewFrom`
operations. Only processing time advances. Current release, lost right-hand
ownership, lost native access, or changed contact binding immediately revokes
physical presentation. Physical interruption preserves native debt.

Gameplay excludes simultaneous shell, magazine, sight, and support consumers
while the pump owns the hand. Fire remains suppressed while the native cycle
or unresolved physical release requires it. Reload is suppressed in this
exclusive private trial. The hand remains left Mechanism/right GunHold; the
separate bolt handedness candidate is outside this payload.

The renderer derives the wrist target and `jntWpn_4` fore-end from the immutable
closed calibration plus gesture travel. It never adds travel to an already
animated native fore-end. It solves the left arm, rejects reach clamping, poses
the mechanism fingers, and publishes a private palette. Each native pack checks
current claims, the original target deadline, owner, input, current held cycle,
and unchanged native source bytes. Loss falls back to the saved ordinary
palette. Native animation buffers are never edited. Atomic report counters cover
contacts, attempted/accepted poses, reach and pose rejections, actual verified
copies/pairs, fallbacks, controls, physical targets, releases and acknowledgements.

## Measured calibration

`Bc2PumpCalibration225.h` contains an explicit calibration factory, with no
enabling call site. The private source is `pump-capture225-01/native-trace.json`,
SHA256 `c712a64becad8b59591c792c891cdb7d8e3fff17cccc2ab29c09f47ff680d03c`.
Its 612 original rig observations have fingerprint `a7f219a1426216ab`.
The chosen closed matrix is actual row 95/input 174, the translation medoid of
87 coherent, all-three-idle, loaded-eight observations. It is an original rigid
matrix, not an average of rotations. Fore-end closed Z is `-0.846984863` metres.
Rear extrema in both shots, rows 136 and 273, move `+0.094635009` metres along
weapon-local Z, with under 0.023 mm lateral displacement. Rear direction is +Z.
The existing measured stroke `0.09495844` metres is retained; its difference
from these sampled extrema is 0.323 mm. Gesture tolerances and dwell values are
explicit controller policy, not claimed native measurements.

State 7 includes moving native animation and cannot by itself identify closed
geometry. The independent fixed-duration diagnostic in
`pump-capture226-02/native-trace.json`, SHA256
`ed11bdb50d36bd21f96dd83a711e6583b5e833ece347ce773497421a313a7d0a`,
contains 14 coherent Holding rig observations with the same fingerprint.
Maximum held fore-end position error from the calibration is 0.194 mm.
The native wrist continues settling after hold entry; the final eight held
observations agree with the chosen wrist within 0.231 mm. The immutable idle
calibration is therefore retained. This fixed-duration diagnostic does not
establish a controller-driven release or admit the sustained native service.

## Integration and verification

Merge the manifest's exact baseline diffs; Gameplay and RigPublication are
shared files and must retain other agents' changes. Add `Bc2PhysicalPump.cpp`
to the BC2 integration target and register `Bc2PhysicalPumpTests.cpp` with the
existing BC2 camera/profile and pump-part dependencies. The runtime lifecycle
and Restore-aware service fixes are separate payloads owned by the root task.
This candidate does not alter their files.

For a deliberate private trial, build with `FVR_BC2_MANUAL_CYCLE_CANDIDATE`,
install HandPoses/TwoHand and request hooks, then before Gameplay Start call
`EnablePhysicalPumpCandidate(MeasuredSpasPump225())`. It rejects incompatible
shell, magazine, inventory, or reload fixtures. The native runtime separately
requires its private enable condition and exact admission. A test driver must
send ordinary controller packets through Gameplay and the renderer contact
channel; directly passing fabricated leases or raw contact is not a live test.
Normal builds and disabled profiles remain unadmitted.

Focused validation on x86 and x64 passes:

- 21 physical weapon-cycle groups, including complete N-1 strokes, original
  claim deadlines, forged source-history rejection, support transfer, and
  immediate current release.
- 8 BC2 physical pump adapter groups using the actual raw-contact builder,
  arbiter, shared policy, private part builder, and exact outcome reconciliation
  with a mocked native service. They cover a full stroke and matching ready,
  ownership loss, stale private palettes, wrong owner/rig/source/asset, rejected
  native control, missing native views, and changed contact binding.
- Actual Gameplay and RigPublication translation units compile in ordinary
  and private candidate modes for both architectures. Portable/adapter tests
  and adapter code use `/W4 /WX`; native integration uses `/W4` and retains the
  project's existing shadowing and x64 checks of x86 pointer code.

The candidate build script and logs are preserved beside the payload. Root
performs the combined full x86/x64 Build.ps1 checks after merging.

Remaining live checks are the Restore-aware sustained native service, physical
gesture release through the real Gameplay/renderer loop, exact native tail and
ammo conservation, actual paired private palette submission, current tracking
loss/owner cancellation, and a headset grasp/reach check. Uncertain unfinished
native cycles still require verified retirement before another owner can bind.
No final-round/empty-feed charging or manual ammo creation is included.
