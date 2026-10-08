# Fixed sight grasp and continuous preview — October1

The preceding geometry repair achieved headset acceptance for switching:
eight requests/acks/commits, with the user reporting other handling excellent.
The next requested improvement is a fixed grip on the rectangular ladder frame
and visible feedback while pulling it.

## Implementation

BC2 measures the hand contact from the centroid of four named finger-base joints,
each validated as a direct child of LeftHand. The previous wrist-origin contact
was about7cm behind that area in observed poses. Measurements are per native pose,
not copied weapon offsets. Contact uses the independent raw tracked hand.
The upper4–11cm of the observed rear-ladder extent supplies an approximate frame
region; exact mesh rail bounds have not been extracted.

Portable SightGrasp binds one coherent sight/hand/contact snapshot. It freezes
the selected point in sight space and keeps the authored palm landmark on it,
while preserving the captured wrist orientation and roll. Both opening and
closing use drift-free absolute transforms in weapon-local metres. Common
world/gun movement composes afterward. The accepted gesture threshold, dwell and
native request/ack policy remain unchanged.

BC2 validates jntWpn_9 and jntWpn_11 as visible leaf siblings of jntWpn_1 in the
verified exact scoped-XM8 family. Rear+X/front−X preview rotations replace only
their private rendered palette entries. The left arm solves toward the fixed
palm attachment. All original source buffers and native animation remain intact;
right-hand/grip/muzzle placement is preserved.

The preview follows the pull while Manipulating. At native request dispatch,
the ordinary animation completes the flip. This is continuous pre-detent
feedback plus native completion, not a claim of manual control through the full
90-degree travel. Release/expiry/equip/recenter selects an ordinary tracked
palette. An atomic preview token guard updates before the publication mutex,
preventing lock contention from retaining a released preview.

Current fingers keep native weapon animation. Open free hands, grip curls and
capacitive finger posing are a later user request. No controller haptic path is
added; feedback in this candidate is visible hand/sight movement.

## Validation and limitations

41x86/40x64 regression suites pass. New pure tests cover fixed contact, hinge
sign/pivot, wrist roll, world composition, no drift, clamps and malformed inputs.
Palm acquisition tests verify native names/topology, unit conversion and the
upper-frame contact. Nine independent audit mutations reject pivot/contact/
rotation/ownership/source/cleanup failures.

Native100656-293 / receiver100655 passes launcher→rifle→launcher:
2requests,2native acks,2commits;339preview poses;240pairs,0timeouts.
Fixed contact drift<0.0001mm; desired palm error<0.0001mm; resolved palm error
0.150mm. Rear/front rotation residual<0.000002degrees. Right grip and launcher
support/free-hand audits pass. All hooks retire; game remains responsive; no
new crash/source/packing/fallback/camera/GPU errors.

Native images30/45/150 were reviewed. Hand attachment is visible and no obvious
stretched mesh appears; the hand partly obscures the rear frame. Full headset
feel and visual comfort still need user testing. No standalone proof of the
mutex-contention branch is claimed; that branch was corrected by code review.

Evidence: reports/sight-grasp-20261001.json;
reports/sight-grasp-launcher_alignment-20261001.json;
reports/sight-grasp-sight_preview-20261001.json;
reports/sight-preview-audit-mutations-20261001/results.json;
reports/sight-grasp-visuals-20261001/inspection.json.
Accepted fallback remains launcher-geometry-20261001/checkpoint.json.
