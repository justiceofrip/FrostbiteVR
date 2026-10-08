# AEK / GP30 horizontal sight candidate

The installed singleplayer AEK and GP30 configurations reference the same AEK
first-person mesh but different exact animation trees. The GP30 `1P_AltDeploy`
opens `jntWpn_17`; the rifle `1P_AltDeploy` closes it. This is a horizontal hinge,
not an XM8-style upward ladder or a slide. Parent-relative travel settles at
about 0.718 radians (41 degrees), with a brief opening overshoot to 0.781 radians.
The sight is a 320-triangle rigid part, approximately 6.8 x 25.9 x 37.5 mm.

The actual chain is `jntWpn_17 -> jntWpnwpnJnt_16 -> jntWpn_1`. An unskinned child,
`jntWpn_18`, must remain accounted for by any eventual subtree presentation.
There are no mixed sight triangles or other weighted sight descendants in the
measured LOD0. Part coordinates and transforms are derived in canonical metres.

## Implemented offline

- `bc2_authored_sight_geometry.py` resolves explicit config GUIDs, state index,
  mesh reference, animation role, skeleton and source archives through existing
  bounded parsers. It measures reciprocal transitions and emits derived private
  JSON / C++ profile metadata. It does not copy mesh bytes into public source.
- `Bc2AuthoredSight.h` measures raw anatomical hand contact against the current
  descendant sight's measured bounds. Exact configured mesh, config/asset pair,
  rig, topology, hidden state, generation and deadline are checked. This is
  geometric evidence, not an independently authoritative native family binding.
- `SightGraspBinding` accepts an optional maximum travel; existing calls retain
  their 90-degree default. `SightVisualHandoff::BeginWithGeometry` accepts the
  actual hinge axis, pivot and travel. Existing XM8 calls retain their API.
- The candidate composes geometry -> `SightFlip` -> grasp -> handoff and generates
  all 15 procedural finger joints. No stable authored hand-on-sight contact was
  found in either AltDeploy clip. The grip uses the existing complete procedural
  MechanismGrip and its distal-knuckle proxy; it is not represented as a captured
  contact or a measured collision pad.

The generic decoder found tiny nonconstant controls in HandsIkPose ancestry;
nine decoded parent-local samples bound their settled variation. That is recorded
as sampled evidence, not silently promoted to constant-track or native playback
proof. Transition clips are sampled at 30 Hz. Hinge coordinate regression includes
a rotated parent basis, ensuring the derived local axis does not rotate twice.

## Remaining native integration

This candidate does **not** enable the GP30 sight in the prepared headset build.
Its configuration evidence must be joined to a fresh selected AEK/GP30 runtime
family and exact equipment owner, preserving the current hand claims and mode
request/ack lifetime. The generic asset string `40mmgl` is shared by multiple
launchers and is insufficient. Do not reuse XM8's native mode slot IDs or its
front/rear direct-child writes. The new contact leaves legacy `previewValid`
false because that flag specifically promises the old two-leaf preview.

Next integration must supply the measured profile to the existing paired source
publication; dispatch through the verified current family; render the actual
sight subtree and complete hand together; restore exact original native bytes;
then test opening/closing, release, reload/equip interruptions, and both eyes.
Manual launcher reload work remains separate. This does not enable sniper zoom.

## Validation

The isolated x86 and x64 builds pass five new interaction groups (including 18
identity/geometry negatives) plus the existing SightGrasp, SightVisualHandoff,
SightFlip, Bc2SightContact and Bc2HandPose suites. Ten Python groups exercise exact
typed references, wrong/duplicate roles, malformed motion, reciprocal endpoints
and parent-basis invariance. The installed extractor completes using exact SP
configuration and async weapon archives, and its private C++ output compiles.
No game launch, native hook, GPU or headset verification was performed here.
