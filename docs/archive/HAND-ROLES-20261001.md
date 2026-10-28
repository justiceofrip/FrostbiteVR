# Shared hand poses and BC2 mechanism grasp — October 1

The user accepted the fixed sight flip mechanically in session102629-129 but
reported that the sight hand still looked like the weapon's under-barrel grip.
They requested a modular correction and parallel manual-reload preparation.
The run was ended on explicit authorization; acceptance/evidence is recorded in
reports/headset-sight-grasp-feedback-20261001-102629/acceptance.json.

## Implementation

HandPose is portable policy-free pose generation: Free, WeaponSupport and
MechanismGrip consume named-role bindings supplied by an adapter, reference
transforms, explicit joint axes/ranges, curl/pinch targets and a resolved wrist.
It changes only the hand subtree. It does not know native addresses, weapon names,
input routing, render timing or ammunition. WeaponSupport retains the original
native shape; an unchanged wrist is a byte-exact no-op.

Bc2HandPose verifies the measured ordered names/parents/inverse-bind fingerprint
a7f219a1426216ab and resolves LeftHand plus five three-joint finger chains. Bind
geometry supplies anatomical across, forward and palmar axes, plus each joint's
flexion axis. No per-weapon hand offsets or inherited Refractor indices are used.
Free fingers use the open reference at neutral, index curl from trigger, and other
curls from squeeze. There is no capacitive-touch or optical finger tracking claim.

The controller binding uses OpenXR grip/pose, whose tube axis differs from the
straight-finger grip_surface pose. After canonical handedness conversion, across
maps to +Z, fingers to -Y and the left palm to +X: wristToGrip columns [N,-F,A].
See the [Khronos standard pose definition](https://registry.khronos.org/OpenXR/specs/1.1/html/xrspec.html#semantic-path-standard-pose-identifiers).
The reference anatomy was measured read-only in reports/hand-bind-20261001.json.

The sight's visible wrist now uses that anatomical controller mapping and a
separate compact mechanism pose. Its anchor is the midpoint of the generated
thumb/index distal-joint centers. These joints have no fingertip descendants:
this is a joint-center proxy, not measured glove-pad or rail collision geometry.
Authored curl ranges are initial visual design values, not discovered joint limits.
The measured center gap decreases from67.95mm in bind to38.21mm in this pose.

Bc2RigPublication adds free/mechanism finger writes only to the private render
palette after arm solving. Actual weapon support keeps its accepted native hand
pose, and right-hand weapon/muzzle placement is unchanged. Raw sight contact and
signed gesture input keep their accepted independent path; only the displayed
grasp frame/anchor changes. The fixed grasp follows the sight until native mode
dispatch. Existing release/equip/space token invalidation falls back to the complete
new free-hand palette. Native animation buffers remain untouched.

## Validation

Build.ps1 passes44 x86 /43 x64 suites, including HandPose, Bc2HandPose and
ManualReload. The independent hand audit passes its valid capture and rejects
seven mutated cases: source writes, missing mechanism coverage, inherited gun grip,
changed support orientation, wrong free wrist,2mm anchor error and2mm bone-length error.

Final native trace105057-836 / receiver105057 completed:
- GL -> XM8 -> GL,2 requests/2 acknowledgements/2 commits,0 cancellations.
- 240 stereo pairs,0 timeouts,346 mechanism preview poses.
- 1847 Free /305 WeaponSupport /346 MechanismGrip pose publications.
- No source, packing, fallback, camera-restore or GPU failure counters.
- Maximum resolved sight anchor error0.122mm; accepted launcher support preserved.
- Hooks retired; game remained responsive; no new crash report during bounded watch.

Audits: reports/hand-roles-audit-20261001.json,
hand-roles-sight-preview-20261001.json and
hand-roles-launcher-alignment-20261001.json.
Local matrix telemetry subtracts large float world coordinates (~652m); its
translation audit has a magnitude-derived rounding allowance capped at0.5mm.
Rotation limits remain1e-4. Bone-length and anchor mutations still fail.

Earlier runs remain evidence, not erased successes:104630 lost contact after
one grab during a long frame and did not toggle;104834 completed both toggles and
hand checks but had one receiver timeout. Final105057 still had one108ms delivery
outlier despite zero timeouts. No claim of hitch-free performance or a timing fix.
See reports/hand-roles-history-20261001 and each original native/receiver folder.

Reviewed raw eye images from104834 show the distinct mechanism hand at the sight
and released/free hand. They establish rendered output, not comfortable controller
alignment or exact finger-pad contact. New headset acceptance remains pending.

## Reload preparation and next test

ManualReload is an engine-free physical-operation/ack coordinator with an immutable
bounded plan, per-operation tokens, actor/equip/space identity, neutral rearming,
repeated shell insertion, timeouts and cancellation.22 standalone cases pass on
both architectures and the full suites include it. It is not enabled in BC2.
Existing native action29 proves automatic reload, not staged magazine/shell/chamber
control. See MANUAL-RELOAD-20261001.md for the binding/capture plan.

Next headset test: relaxed off-hand away from the weapon; squeeze/trigger curl;
normal XM8/launcher support grip; acquire the upper sight frame, raise/lower,
release; rotate the left controller; recenter or remove/reconnect the headset.
Look specifically for wrist alignment, grasp seating and release returning to an
open hand. Empty right hand, capacitive posing, physical inventory, manual reload
bindings, LOD polish, death crash and VR menu access remain separate work.

Launch only when the user is ready: Start-BC2VRSession.ps1 -SightFlip. Leave it
running until they finish. Keep BC2 on the left monitor and preserve desktop focus.
Incremental checkpoint: reports/hand-roles-20261001/checkpoint.json over sight-grasp.
