# Right hand binding and trigger feedback — 2026-10-01

The right-hand implementation adds visual analog trigger feedback without changing the accepted weapon wrist, grip attachment, or native firing path. It also prepares an anatomical right free-hand binding; the adapter decides when a player is actually unarmed. This work does not enable empty weapon slots or infer capacitive touch from a button press.

## Native evidence

The bounded read-only capture is `reports/right-hand-bind-20261001.json`. It used PID 146632 and verified the full executable path `D:\Games\Battlefield Bad Company 2\BFBC2Game.exe` and SHA-256 `3911fcc8914b0158c434cee36f19295316e715d4c9a0e70c857e8c39e3d54258`. Process access was query/read only (`0x410`). Local player, soldier weak reference and ownership back-reference, first-person rig ownership, skeleton/count/definition relationships, and metadata were checked before and after capture. The single bounded snapshot succeeded; stable inverse-bind records and name pointers were reread. No gameplay input, injection, native call, or game memory write occurred.

The complete ordered rig metadata fingerprint is `fnv1a64:a7f219a1426216ab`. The saved evidence contains only the 16 right-hand bind joints plus names of weapon bones; it does not distribute the full rig palette. Both production hand binders require that exact fingerprint and unique named direct chains:

- `RightHand`
- `RightHandThumb1/2/3`
- `RightHandIndex1/2/3`
- `RightHandMiddle1/2/3`
- `RightHandRing1/2/3`
- `RightHandPinky1/2/3`

In canonical wrist-local coordinates, right bind finger-base center is approximately `[0.01484466, 0.02469657, -0.06995824]` metres. The right thumb-base dot product with `A cross F` is **−10.7176 mm**; the separately captured left value is **+10.7160 mm**. Therefore right anatomical palmar normal is `−(A cross F)`. This is explicit handedness, not an assumed world-space curl axis.

Both wrist calibrations use columns `[A cross F, -F, A]`: fingers extend along grip −Y, indexward across the hand maps to grip +Z, and the palmar normal maps to +X on the left / −X on the right. The accepted left branch retains its existing calculations and authored ranges.

## APIs and private pose behavior

`BindRightHandPose(names, parents, inverseBind, unitsPerMetre)` returns the same `Bc2HandBinding` shape as the left binder, with `rightHand=true`. Its bind-relative curl axes come from the measured anatomical tangent and palmar normal; thumb opposition uses the index distal-knuckle direction. `RightHandTargets(role, squeeze, trigger)` makes the existing free-role mapping reusable for an eventual empty right hand. It does not select that role or hide a weapon.

`PoseRightTrigger(parents, currentWorld, binding, trigger)` accepts a normalized finite trigger and a verified right binding. It returns only right-index subtree edits. Maximum additive angles for the three index joints are **0.12 / 0.22 / 0.12 radians** (approximately 6.9 / 12.6 / 6.9 degrees). These small ranges are tentative authored feedback, not measured native joint limits or a claim of fingertip collision alignment.

The portable implementation is `GenerateFingerCurlOverlay` in `include/fvr/interaction/FingerCurlOverlay.h`. It uses the original current animation's parent-relative transforms, preserves each segment translation/length, and applies bounded rotation around supplied joint-local axes. Index descendants follow the changed chain; wrist, sibling fingers, the other hand, and weapon bones are not returned as edits. A released trigger returns zero edits, preserving the original native finger animation exactly.

The adapter must call this once after solving the right wrist, using a fresh native pose plus the other private layers. It must not feed this overlay's previous output back into its own input; doing so would accumulate curl. Repeated evaluation of the same fresh source is deterministic and does not drift. Source palettes remain unmodified.

## Capacitive and weapon-mesh limits

The generic free-hand target API leaves thumb curl available for a separately validated touch input. This change does not fabricate thumb touch, tracking, or controller capabilities. Existing analog squeeze/trigger roles and future capacitive transport remain distinct.

The saved named weapon metadata contains generic `jntWpn_N` bones and `jntWpn_Flash`; none has an authoritative trigger-mesh role. No weapon mesh trigger bone is selected or animated. Moving the right index is visual finger feedback only. A weapon-trigger mesh feature would need asset/geometry identification or a controlled native animation correlation per applicable weapon family.

## Verification

New portable tests cover exact release, only-index edits, preserved wrist/siblings/hidden unrelated leaves, segment lengths, dependence on the original native animation, world-frame composition, repeated evaluation, and malformed chains/transforms/angles. BC2 tests cover mirrored right anatomy, proper grip mapping, future free role targets, fingerprint rejection, side guards, trigger range validation, and left default-path parity.

Full builds and runtime/headset checks are coordinated by the root agent. The helper implementation alone does not establish that the authored finger ranges look correct on every equipped gun.

## Integrated candidate evidence

The helper is now integrated into Bc2RigPublication's private palette path.
Both full builds pass (48 x86 / 47 x64 suites). Native fixture112224-172 records
12 active and89 neutral index samples; the isolated overlay matches its bounded
angle, neutral adds no change, and wrist/other branches remain preserved.
The joined audit is reports/grip-reload-audit-20261001.json.
Actual headset appearance and gun-trigger mesh motion remain unverified.
Optional left-free-hand touch transport is implemented separately; see
[hand-input candidate](HAND-INPUT-RELOAD-20261001.md). No right empty-hand mode is enabled.
