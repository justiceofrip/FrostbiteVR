# Ground weapons and committing a holster swap

**Clarified requirement:** the temporarily held ground gun must fire by the normal
trigger before holstering, with a fresh release required after pickup/tracking
changes. The preview-only transaction below is one foundation component and does
not fulfill that complete interaction. See `TEMPORARY-HELD-FIRE-20261003.md` for
the separate native-equip/firing contract and post-fire settlement requirement.
Both BC2 native capabilities remain disabled pending binding proof.

The requested interaction is to grab a weapon from the world, handle it temporarily,
then place it in a chosen occupied holster to replace that holster's weapon. The old
weapon must become a real native world pickup. Temporary handling must not add an
owned inventory slot or create ammunition.

The portable transaction is implemented and tested. BC2 world pickup is **not
enabled**. Its capability declaration returns all false. There is no live world
target reader, reversible world-mesh preview, targeted native exchange, complete
pickup ammo ledger or outgoing-drop receipt yet. Passing portable tests does not
establish any of those bindings.

## Existing BC2 evidence

`Bc2BodyInventory` coherently reads the current soldier's carried inventory twice,
including native item, data, persistence, category, slot and selection routes. Its
physical item generations change across observed replacement or observation gaps.
`ResolveBodyDraw` selects already-owned items using verified native selection
actions; it does not pick up a world entity. The normal Interact27 action is an
untargeted Use input. Sending it or seeing a HUD change cannot acknowledge pickup,
temporary ownership, ammunition movement or a dropped outgoing weapon.

Offline inspection of the supported executable found these static configuration
types and fields. They are discovery leads, not runtime entity offsets or method
ABIs:

| Configuration type | Relevant fields | Consequence for integration |
|---|---|---|
| PickupEntityData | ForceWeaponSlotSelection, IgnoreNullWeaponSlots, UseHudSelection, UnspawnOnPickup, UnspawnOnAmmoPickup, HasAutomaticAmmoPickup, TimeToLive | Observe target selection, automatic ammunition effects and disappearance separately. |
| WeaponPickupData | Weapon, WeaponSlot, AltWeaponSlot, LinkedToWeaponSlot, MinAmmo, MaxAmmo | Native pickup may replace linked slots and may initialize ammunition; do not equate asset name with one physical instance. |
| DynamicWeaponPickupSlotData | WeaponSlot, AltWeaponSlot, LinkedToWeaponSlot | A rifle/launcher bundle can occupy multiple native entries while remaining one carried gun. |
| WeaponPickupEntityData | Weapons, Soldier, UseForPersistence | Content ownership and persistence require runtime evidence. |

The executable also contains ClientPickupEntity, ServerPickupEntity and interaction
entity type names. No target/object lifetime or pickup/drop call signature follows
from those names. Workspace evidence records the executable SHA-256 and decoded
static metadata; no game bytes or assets belong in a public source package.

## Portable transaction

`GroundPickup` uses a `WorldPickupKey` with world and entity generations, separate
from an owned `BodyItemKey`. The adapter assigns a globally distinct shared-hand
interaction key to the ground object. The temporary hold uses the existing
BodyInventory claim kind, not a native GunHold and not an extra inventory item.

1. `Begin` requires a fresh complete carried inventory, exact world item/contents,
   and an actual current shared-hand claim. It emits only a preview request.
2. `Preview` requires the adapter's reversible world concealment, both private eye
   copies and native action-suppression receipt. No pickup/equip or ammo operation
   is emitted. Release requests restoration of that exact world object.
3. `Stow` requires a fresh current holster assignment bound to its exact owned item,
   native slot and inventory revision. It emits one immutable exchange request.
   The adapter must call `DispatchAllowed` with fresh evidence at its native update
   boundary, then report NotStarted, Accepted or Unknown. Generic Use is not a
   targeted exchange adapter.
4. Native success requires a later authoritative inventory with the same carried
   capacity/count, one exact root bundle replaced, every other bundle unchanged,
   incoming contents bound to a new owned item, and the outgoing item bound to a
   fresh dropped world entity. Linked-member counts may differ. Whole-domain ammo
   ledgers must conserve every ammunition kind, counting shared reserves once.
5. Native success only enters `HolsteringAcquired`. A separate receipt must prove
   the temporary presentation/claim was released and the new owned item was
   presented in the requested holster through the normal paired/suppressed body
   pipeline. It cannot reuse the temporary-world preview receipt as a holster ack.

Contents identities represent immutable, actually observed weapon/attachment and
loaded/chamber state; they are not asset hashes. They do not replace ammunition
ledger evidence. The first contract intentionally rejects native automatic ammo
grants or unrelated ammo changes. If ordinary BC2 pickup legitimately grants ammo,
that behavior needs an explicit observed native accounting rule before adapting
this contract, not a fabricated conservation receipt.

After an exposed request, cancellation or timeout enters reconciliation. It cannot
pretend no native operation ran. Recovery needs either the actual exchange receipt
or an exact non-execution/drained receipt plus unchanged fresh source/inventory.
Actor retirement can clear the transaction only with old-generation retirement,
callback drain and temporary-presentation release proof. A logical pending phase
never grants indefinite fire suppression: `PresentationAuthority` expires with the
unchanged actual receipt deadline and is scoped to the exact actor/world.

The policy does not implement temporary firing, body/world grip geometry, pose
calibration, physics, native item spawning or save persistence. Rendering a held
preview alone does not make an unowned weapon safe to fire. Empty holster expansion
is also outside this first one-for-one replacement contract.

## First BC2 native evidence sequence

Do this in singleplayer on the monitor, with no physical pickup adapter enabled.
Use the original game's actions; never test guessed calls or modify native counts.

1. Select an actual reachable ground weapon in the campaign and capture the
   current coherent carried inventory/selection before approaching it. Record all
   linked entries, native/persistence identities and available ammo evidence.
2. Establish a read-only interaction-target reader from the supported binary:
   prove the ClientInteractionEntity/pickup relationship, target generation,
   world ownership, server counterpart and the exact currently eligible target.
   The current project has no such reader. Do not substitute nearest mesh, HUD
   text or the configured pickup asset. Verify two coherent observations before
   adding any callback or hook.
3. Observe approach/retreat without pressing Use. Compare ammunition, target and
   visibility to distinguish automatic ammo collection from weapon exchange.
   Confirm whether the observed entity is a static spawn or a dropped dynamic gun.
4. Select outgoing gun A using the original game, perform one normal pickup, and
   capture before/after native inventory and the outgoing world entity. Repeat for
   outgoing gun B and for a rifle with a linked launcher. Derive whether selection,
   native slot metadata or HUD targeting determines the replacement slot.
5. Repeat one no-op/rejected pickup and one target disappearance/level-lifetime
   boundary. Record ordinary request, exact target, server acceptance, linked
   inventory deltas, ammunition ledgers and outgoing drop. Sending Interact27 alone
   never counts as success. Observer gaps remain unknown.
6. Only after those seams are verified, bind the portable exchange receipt and a
   reversible temporary world/held-pose presentation lease. Prove release-to-world
   before testing stow-to-swap. Both headset and native tests are still required.

Existing selected-weapon reload preflight tools do not capture this whole pickup
domain. Their successful counts are not a substitute for ground-item/server or
whole-inventory ammunition evidence. No native launch command is provided for an
adapter that does not exist.

## Validation and integration scope

The staged C++ tests use the real portable hand arbiter plus authoritative adapter
fixtures. They cover preview-only handling, linked bundle replacement, exact slot
binding, unchanged surviving inventory, ammo conservation, entity/world reuse,
missing/drop/paired receipts, native uncertainty, expired suppression, scene
retirement and duplicate observation immutability. Both x86 and x64 pass 24 groups
with `/W4 /WX`.

Integration adds a portable FvrCore source, a test, a default-disabled BC2 capability
header and this document. It changes no Gameplay path, launcher flags, native
offsets, ammo authority, physical magazine, SPAS reload or body-holster behavior.
