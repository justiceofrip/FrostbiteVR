# Chest knife: current native evidence

The requested interaction is an empty-hand chest grab, held knife without an
attack on acquisition, and deliberate motion attacks. Physical grenades remain
deferred. No chest-knife runtime feature is enabled yet.

Read-only305 inventory survey and307 metadata follow-up identify the campaign
knife as KNV-1, native slot7, class11 (`wcKnife`), with asset
`Objects/Weapons/Handheld/Knife/Knife` and its configured mesh. It already exists
as an inventory item even though ordinary play presents it as a quick animation.
The native switch map routes melee action37 from ordinary weapon slots to7;
there are no outgoing slot7 routes in that captured map.

Reflected SoldierWeaponData fields show `SwitchToPreviousWeaponAfterFire` at
offset0x123 is true and `AllowSwitchingToWeaponOutOfAmmo` at0x124 is true for this
knife. `SwitchToPrimaryWhenOutOfAmmo` and `HideWhenOutOfAmmo` are false.
These are observed fields, not an authorization to patch them. They explain why
native selection and persistent physical knife custody must be considered
separately. They do not prove safe draw-without-attack or hand-directed hits.

Local evidence:

- `bc2vr-recovery/knife305-survey/native-items.json`: two matching bounded
  inventory/route reads with unchanged owner before and after; not an atomic
  native snapshot.
- `bc2vr-recovery/knife305-survey/knife-flags307.json`: reflected field names,
  offsets and bytes from the same owned actor before and after the read.

Next binding work must distinguish selection from the attack input, prove the
native attack direction/origin and hit path, and preserve empty-hands/holstered
long-gun ownership across the engine's return-to-previous-weapon behavior.
Tracking discontinuity, recenter and the acquisition gesture must not issue an
attack. Existing native ammo/hit values must not be fabricated to implement it.

## Read-only309 follow-up

Current exact executable SHA256:
`3911fcc8914b0158c434cee36f19295316e715d4c9a0e70c857e8c39e3d54258`.
Bounded reads in `knife309-binding/firing-fields.json` and
`projectile-inheritance.json` retain unchanged local ownership around each read;
they are not atomic snapshots and make no game calls or writes.

The knife's FiringFunctionData uses a ShotConfigData with SpawnDelay0.5s,
InitialPosition(0,0,0.1), InitialDirection(0,0,1) and InitialSpeed(0,0,1.75).
Its configured projectile is BulletEntityData, InstantHit=true, gravity0 and
StartDamage/EndDamage120. ActiveForceSpawnToCamera=true while
ForceSpawnToCamera=false. The inherited ProjectileEntityData has TimeToLive1
and InitialSpeed2000. These are configuration values, not proof of the final
ray length or spawn origin: their actual native interpretation still needs
tracing. In particular, do not assume the1.75 vector is a measured melee range.

This rules out implementing a physical stab by merely sending the quick-melee
button: selection, hand-directed hit timing/origin and persistence are separate
bindings. The existing weapon-aim path may be reusable, but a500ms delayed hit
and camera spawn setting must first be observed at the owning callback.

Static and isolated x86 emulation establish selection setter0x6d7c80's thiscall
ABI (slot, checkEligibility; ret8), optional eligibility callback, previous-slot
update and change notification(old,new). Four cases cover accepted, rejected,
same-slot and explicit unvalidated selection, with balanced stacks. Callback
bodies were stubbed; this is NOT a live knife draw or a permission to bypass
eligibility. See `knife309-binding/selection-emulation.json`.

The actual notification at0x8a0e80 deploys/undeploys multiple client branches;
those nested calls are not covered by the setter emulation. Static0x88280a
reads SwitchToPreviousWeaponAfterFire and0x88286e calls the setter with
eligibility checking disabled for native automatic return. Eligibility0x891bf0
also constrains switching away from an item with that flag. No shared weapon
data has been patched and no knife runtime capability is enabled.

`knife309-binding/selection-owners.json` pairs native selectors to the existing
verified client/server actor chain. The actor+0x248 selector has no callback in
this capture; actor+0x24c uses0x8a0e80/0x891bf0, and server+0x2b4 uses
0x6b1770/0x69ecc0 for notify/eligibility. Prefixes match the installed executable
and ownership remains unchanged across the read. The three firing copies must
not be assumed to have three independently callable selection interfaces.
The initial null-callback read was rejected without any process write.

## Knife_Equipable asset check310

Read-only SP_01 archive decoding found Knife_Equipable, but it still authors
SwitchToPreviousWeaponAfterFire=true. Its fire input is EiaMeleeAttack, while
the regular knife uses EiaFire. It uses the regular knife mesh with a separate
first-person animation tree. The name does not establish a persistent draw
solution. No live selection or attack has been invoked. Exact DBX/index hashes
and fields: recovery/knife309-binding/equipable-fields.json.

## Isolated selection eligibility312

The original server eligibility function0x69ecc0 was exercised in isolated x86
emulation for nine scenarios. Its selected/previous getters execute original code;
weapon/state queries are explicit stubs. A knife marked AllowSwitchingToWeaponOutOfAmmo
can pass selection without ammo, but the firing-state eligibility gate still applies.
While a SwitchToPreviousWeaponAfterFire item is selected, this route permits return
to the previous item and refuses direct selection of another rifle. Same-item,
missing-item, invalid-slot and unavailable-firing-state cases reject as expected.

This matters to chest-knife integration: a generic holster request to any arbitrary
rifle would violate native selection rules. Drawing, returning and stowing must
retain the previous-item relationship rather than disabling eligibility globally.
The notify callback also performs native deploy/undeploy work; these nine cases
do not prove it or client/server replication. No live knife selection or attack was
made. Evidence and original code hashes: recovery/knife312-binding/eligibility-emulation.json.
