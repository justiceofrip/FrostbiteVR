# Scoped XM8 holster diagnostic

## Scoped-XM8 ordinary admission candidate (October 3 UTC)

The ordinary BodyInventory candidate adds the scoped `XM8_sp_s` to the existing
SPAS acceptance mask. It reuses the same automatic stow, body slots, shared hand
claims, suppression, hide/show receipts and draw policy. Exact current XM8 and
ACOG mesh evidence is still required; other XM8 variants and AR/SMG assets remain
unsupported. Default construction stays disabled, and diagnostic grants never
become production acceptance.

Root admitted this exact profile into the development candidate after the final
native firing audit and both-eye review. Run
`native-trace-20261003-015859-709` restored Held and a new GunHold before an ordinary
bounded trigger pulse. Native counts changed 22/191 to 20/191. Token23600 has
matching client/server effect callbacks, event generation542 and muzzle evidence.
The second server token23615 has no matching recorded client effect copy, so
complete shot-effect pairing remains unproved. This is positive evidence of a
real post-draw shot, not acceptance of every client effect path. Root reviewed
both-eye pairs24/48/96/120 (visible, hidden/free hand, restored); 240 pairs arrived
without timeout. Combined headset handling, pause/death/vehicle-exit continuity
and complete effect pairing need their own checks.

Focused tests use the actual inventory adapter and a physical gun key distinct
from its native pointer. They cover automatic stow only after fresh suppression
and paired hide, same-item draw with a new GunHold, stable slots during held Fire,
cross-draw to SPAS, unchanged native item memory, wrong asset/missing optic/stale
owner or deadline/wrong mesh rejection, and disabled construction. Existing
SPAS and bounded diagnostic tests remain in place.

## Bounded diagnostic

The diagnostic remains separate from ordinary admission. It selects `XM8_sp_s`
with fresh selected XM8 and ACOG mesh evidence; the renderer independently proves
exact mesh paths, palette ownership and paired copies. No other XM8 variant or
weapon family is admitted.

After rebuilding the bootstrap and native DLL together, equip the scoped XM8
on foot in an idle campaign scene, with ammunition remaining. Run the following
only when another game or XR mod does not own the runtime. It needs no headset:

```powershell
.\Test-NativeStream.ps1 -BodyHolsterXm8Probe -Seconds 15 -Pairs 240 -StaticPose -Async
```

The existing neutral receiver is reused. The native diagnostic follows actual
body anchors to hide the selected weapon, move the free right hand between two
poses, and draw the same weapon. It consumes ordinary body intents, fresh native
input-cache suppression receipts, actual paired hide/Show receipts and distinct
GunHold claims. It cannot create its own render or selection acknowledgement.
The existing bounded cache challenge verifies suppression and preserves unrelated
input words. No shot after draw is requested by this fixture.

The new typed diagnostic selector is separate from production acceptance flags.
NativeProbeConfig is now 1180 bytes; old 1176-byte bootstrap/DLL combinations are
rejected. Both binaries must be rebuilt. The single 15-second admission cannot be
renewed or changed to another profile. Unsupported assets, missing scope meshes,
expired input, owner/focus changes and missing receipts fail the trial.

## Required evidence before enabling production scoped-XM8 holstering

- Exact executable, bootstrap and DLL hashes; profile 2 in both manifest and probe.
- Same fresh selected native owner and scoped-XM8 asset throughout the diagnostic.
- Unchanged authoritative loaded/reserve counts and ordinary idle state in the
  read-only before/after captures. This is not proof of every possible fire route.
- Completed baseline, hide, two independent free-hand poses, Show and restored
  phases; current paired copy receipts, no shared source edits or packing failures.
- Actual cache challenge and production suppression readback, unrelated words
  preserved, no suppression failures and no claimed production acceptance.
- A new right GunHold after Show, explicit `blocks_actions:false` and
  `allows_gun_hold:true` throughout restored Held. A real post-draw shot remains
  a separate functional check; an open policy gate alone cannot prove it fires.
- 240 ordered stereo pairs with no timeout/copy failure, saved images covering all
  four stable phases, and human review of both eyes. The audit does not infer
  visual correctness from metadata or sampled pixels.
- Clean hook/camera retirement, responding game and at least 15 seconds of observed
  post-detach stability. No diagnostic receipt may survive its original deadline.

Use `tools/audit_body_holster.py` with the native trace and receiver folders.
It checks the explicit diagnostic profile, actual consumer lifecycle, native
cache challenge, counts and saved-eye coverage. Mechanical success deliberately
leaves GPU review, headset acceptance and production input acceptance false.

Focused deterministic tests cover scoped-XM8 admission, default-SPAS separation,
wrong assets/scope absence, immutable trial bounds, expired ownership, the full
consumer/challenge sequence, blocked restored Held and malformed profile/config
combinations. Synthetic test receipts are explicitly unit-test fixtures.
The original diagnostic checkpoint left native hide/show and post-draw fire pending.
The October 3 evidence and unresolved effect-pairing/headset limits are above.
