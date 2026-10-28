# SPAS physical reload interaction and presentation candidate

October 1, 2026. New adapter planning code; default off, no live native calls,
source animation writes, ammo edits, new game assets or enabled capability.

`Bc2ReloadInteraction` consumes the existing portable `HandInteraction` claim
results and `ReloadInsertion` policy. Its exact SPAS asset/mesh/rig gate uses the
measured row58 grasp and row62 terminal placement from
`reports/spas-reload-candidate-20261001.json`. The 50mm stroke, capture cone,
alignment and dwell times remain explicit design choices. The existing receiver
has closed faces in this area; this is not a verified open loading aperture.

`Bc2ReloadPresentation` supplies an independent shell bone target and the native
row58 left-hand pose, including all15 finger joints. Its candidate palette has
17 edits: jntWpn_7, LeftHand and the fingers. It never moves the gun root, right
hand or unrelated geometry. Source palette bytes and SIMD padding are preserved
by BuildRigPosePlan. All translations are converted from metres to the current
native unit scale before composing with the placed weapon world.

## Integration boundary

Keep one `Bc2ReloadInteraction{true}` instance for the actor interaction domain
only after the feature gate is explicitly enabled. The default constructor is
inactive. Obtain the left AmmoObject and right GunHold claims from the SAME
existing HandInteraction arbiter; this module cannot steal WeaponSupport or
Sight ownership. Actual shell acquisition/resource identity remains adapter
work; do not fabricate a successful contact merely to obtain a claim.

The native hold service supplies a fresh `Bc2ReloadNativeLease`: exact actor,
equip and space owner; held weapon key; monotonic native cycle; original observed
and expiry times; and proof all required native firing copies are held. The
lease is not a successful reload acknowledgement. A lease/cycle/claim/space
change cancels the interaction; a new cycle must observe the hand outside the
entry before a new magnetic capture. A physical seat emits InsertRound exactly
once and remains only a candidate for the native one-shell completion service.

Example wiring (inside the adapter's existing serialized update):

```cpp
Bc2ReloadInteractionSample sample;
sample.insertion.identity = {owner, weaponKey, shellInstanceKey, trackingEpoch};
sample.insertion.itemClaim = *hands.Current(InteractionHand::Left);
sample.insertion.weaponClaim = *hands.Current(InteractionHand::Right);
// Fill original sequence/geometrySequence, observed/deadline/now, focus,
// tracking, held and exact resource eligibility from the immutable input.
sample.native = nativeHoldLease;
sample.assetName = currentAsset;
sample.meshPath = verifiedSelectedMesh;
sample.rigFingerprint = currentRigFingerprint;
sample.selectedMeshIdentityVerified = coherentSelectedMeshProof;
sample.rawLeftWristWorldMeters = rawPreIkLeftWrist;
sample.weaponWorldMeters = placedWeaponMeters;
const auto reload = reloadInteraction.Update(sample);
// A seat goes to the native transaction coordinator, never directly to ammo.
if (reload.insertion.seat) queuePhysicalSeat(*reload.insertion.seat);
```

The example assumes both Current calls succeeded and their claims are the
required kinds; production must branch on absence. Profile identity and local
hand geometry are filled inside the adapter from the exact measured profile,
not from caller-supplied guessed offsets. `rawPreIkLeftWrist` must never be the
magnetically guided target or a preceding rendered palette.

At RigPublication, bind with `BindBc2ReloadPresentation` using the exact current
rig and selected mesh. For the same input generation, current native cycle and
unexpired targets, supply `Bc2ReloadPresentationObservation` plus the placed
weapon world. Its selected mesh/section/visibility booleans are current producer
proof, not persistent user configuration. `BuildBc2ReloadPresentation` returns
private writes and an exact-byte candidate palette. Resolve the left arm IK to
that SAME wrist, compose the 15 finger and shell writes after ordinary weapon
retargeting, and publish once through the existing immutable native request
boundary. A reach-clamped wrist must not silently diverge from the shell's
measured grasp; cancel or resolve the pair coherently. Neither eye may mutate
the animation source or integrate an interaction a second time.

Duplicate input cannot move a raw-held shell or wrist even if a new caller
passes different geometry under the same sequence. It cannot generate another
seat. Native completion must reconcile one shell and invalidate the consumed
shell instance before obtaining a fresh resource/claim for another shell.

## Smallest remaining shell visibility observation

Existing metadata establishes exact mesh ownership and both ammo section skin
maps. It does not establish that their draws are submitted throughout a held
native reload or after the native jntWpn_7 leaf collapses.

| Section | Triangles / indices | Asset first index | Vertex byte offset / stride | Vertices | Local palette |
| --- | --- | --- | --- | --- | --- |
| jntWpn_7_Ammo_Brass | 90 /270 |16725|348384 /48|96|[40]|
| jntWpn_7_ammo_plastic |30 /90|16995|352992 /48|32|[40]|

All128 vertices have a single255 weight. MeshSet palette identifier40 hashes to
jntWpn_7 (`f7f9bcd4`). That is NOT necessarily GPU CB slot40: the section's local
bone index0 must be joined through its actual draw remap. Materials are
Objects/Weapons/Common/Common_shaders/Ammo/Ammo_Brass and Ammo_Plastic. Metadata
source: reports/reload-spas-mesh-evidence-20261001.json; no mesh data is bundled.

NativeProbe::CaptureGeometryBuffers currently rejects index counts below300,
so it misses both standalone shell draws. CapturePassBuffers deduplicates by
VS/PS/RT/DSV and does not retain DrawIndexed start/base or IA bindings. Existing
empty/aggregate evidence cannot show that the shell stopped being submitted.

Request one bounded producer/render capture covering a naturally visible shell,
state11 hold and later collapsed leaf, without changing any animation:

- Producer: native frame/request identity, selected weapon/state/mesh identity,
  rig pose identity, jntWpn_7 raw evaluated and inverse-bind transforms/hidden
  flag, native hold cycle and begin/deadline. Retain actual section eligibility
  or submitted section bits if the verified producer exposes them.
- Render: eye/native frame, draw ordinal, count/startIndex/baseVertex,
  IA vertex/index buffer IDs, offsets/stride/index format, shader IDs and the
  submitted skin-palette/remap binding. Filter270/90-index draws only as a
  candidate selector, then prove identity via the exact mesh/section/ranges.
- Preserve repeated draws and original frame/hold times. If ranges are repacked
  or materials batched, compare bounded derived geometry fingerprints privately
  to the exact asset sections; do not label another270-index draw as the shell.

If these exact shell sections are still submitted while only their native leaf
collapses, an explicit owned physical-shell palette override may intentionally
replace that collapse with the authored rigid target during a current reload
claim. That will require a narrow opt-in publication path; the generic hidden
leaf rule need not be removed. If native section submission is disabled, palette
placement alone cannot display the shell; its actual section visibility owner
must be identified instead. Current code returns NativeShellHidden or
VisibilityUnverified until this boundary is established, without speculative
uncollapse or generated replacement geometry.

## Validation

The focused interaction suite has five cases covering measured rail geometry,
default-off/exact native gates, duplicates/cancellation, coherent frames and real
shared-hand arbitration. The presentation suite has five cases covering exact
private palette writes/padding, independent row58 thumb/index reconstruction,
unit/scene invariance, visibility/ownership expiry and malformed topology.
Both suites pass x86 and x64 isolated builds. Root owns full build integration
and native testing. No headset acceptance or playable manual reload is claimed.
