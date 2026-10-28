# Temporary ground weapon: active firing, separate permanent ownership

The clarified requirement is explicit: a gun picked up from the ground must fire
normally while held, before it is placed in a holster. Grabbing the gun must never
fire it. Holding a trigger while acquiring, changing or regrabbing a weapon must
require a fresh release before a later press can fire. Reconnect/recenter recovery
has the same requirement.

The earlier `GroundPickup` preview/holster transaction alone does **not** meet this
requirement. This increment supplies the separate portable native-equip/firing
contract, trigger gate and post-fire settlement validator. Both BC2 temporary-equip
capabilities remain false. No native world-weapon equip, shot, ammo change or pickup
is implemented or claimed by these tests.

## Active weapon is not permanent inventory

`TemporaryHeldIdentity` binds one physical world instance to a temporary native
equip lease, exact client/server firing targets, native ammunition authority,
current actor/world, control owner, input epoch and shared GunHold token. A held
preview's BodyInventory claim can become GunHold only after the adapter proves a
real native active-weapon relationship. Transferring the hand token alone does not
establish that relationship or add a permanent holster assignment.

`TemporaryEquipProjection` explicitly permits two independently gated native
implementations:

| Mode | Required native proof | Permanent ownership |
|---|---|---|
| External active entity | Native firing can target the exact held entity outside the carried slots; native ammo and server effects actually belong to it. | Existing holster assignments remain unchanged. |
| Borrowed native slot | Native equip temporarily projects the held gun into an existing engine slot; the displaced owned item and all linked contents remain owned and have a verified reversible restoration token. | No extra holster is granted and no outgoing item is dropped yet. |

Neither mode is verified in BC2. The existing ordinary fire-origin bindings resolve
the current carried/native weapon and corresponding player/server effects. They
cannot be redirected to a world mesh or another firing object by asset-name match.
No new offsets, calls or pointer writes are included here.

If BC2 requires slot borrowing, the original invariant “raw native inventory never
changes until stow” must change. The useful invariant is **permanent assignments and
owned contents never change until stow**. Raw native inventory may change only
under the explicitly verified reversible projection. The existing body-inventory
reader cannot simply consume that raw temporary slot as a newly acquired permanent
item. A verified projection adapter must separate those views and suspend automatic
stow/pickup reconciliation for the provisional held item. Saving an old pointer or
ignoring changed inventory is not such a projection adapter.

## Normal trigger and aim gate

`TemporaryHeldFire::Update` emits a level-trigger command for the normal native
input boundary. It never invokes a shot, decrements rounds, creates projectiles,
sets fire cadence or replays simulation. `triggerPressed` is an input edge, not an
instruction to spawn one shot. Full-auto timing, dry clicks, recoil and ammunition
consumption remain native.

Every command requires all of these current sources:

- A native readback receipt proving Fire was cleared across the equip boundary
  before the temporary target became active, plus current ownership of its trigger
  route. Calling the gate after native equip cannot undo an already fired round.
- An actual native receipt for the temporary active weapon, client/server firing
  targets and ammunition authority, with permanent weapon suppression and paired
  held presentation.
- A current shared GunHold claim for that exact held instance/control owner.
- A focused, tracked input packet with an active trigger channel. Missing trigger
  capability is not a zero/released trigger sample.
- The adapter's normal calibrated aim/muzzle receipt, bound to the exact input
  epoch, space, source packet, raw grip and raw aim. Render IK or a decorative held
  mesh pose cannot be substituted. The matrix must pass existing rigid validation.

The pickup/identity transition packet always yields `triggerHeld=false`. A distinct
later packet with trigger at most 0.1 arms the gun; only a subsequent value at least
0.75 can request ordinary Fire. The gate disarms on native proof/read loss, focus
or tracking loss, trigger capability loss, owner/target/claim changes, input epoch
or space changes, clock reversal, stale packets and long source gaps. Repeating or
mutating a packet cannot manufacture a new release or press. A left-hand tracking
loss does not itself block a valid right-hand gun.

The command retains exact target identity and the earliest real input/native/claim
deadline. The adapter must revalidate at its native input boundary and route only
the matching current target. Revocation identifies the previous lease for cleanup;
it is not permission to send a release to a replacement weapon. Existing per-event
client/server muzzle consistency and native cadence remain required; this policy
does not replace `FiringPoseHistory` or native callback owner checks.

## Firing changes the stow baseline

The original ground transaction stores immutable pre-fire weapon contents and ammo
snapshots. After a real shot those snapshots are stale. Reusing them would either
reject legitimate stow or invent ammunition on drop/return. The integration must
first obtain a `TemporaryHeldSettlementReceipt`:

1. Clear the temporary trigger at the verified current native boundary and drain
   native callbacks. Stop further shots before measuring settlement.
2. Resolve any borrowed-slot projection while retaining the same permanent items,
   linked members and capacity. Capture the complete actual permanent and held
   ammunition domains without counting shared reserve twice.
3. Supply the actual native consumption receipt and current held contents. Shot
   count, trigger edges or elapsed time are not ammo consumption evidence.
4. Validate that original permanent plus held ammo equals current permanent plus
   held ammo plus actual native consumption for every kind. No count is written.
5. Establish a fresh holster-commit baseline from those observed settled sources;
   do not patch the old transaction's counts or fabricate a world/drop receipt.

`ValidateTemporarySettlement` implements the read-only identity, unchanged permanent
bundle/capacity, source freshness, callback-drain and accounting checks. It does
not produce native restoration proof or mutate the earlier `GroundPickup` object.
The complete coordinator is not connected: the old preview policy also retains
its original BodyInventory claim, so it cannot silently accept the temporary
GunHold token. A future adapter must settle and retire the firing lease, perform
an explicit shared-claim transfer, and establish fresh transaction evidence before
stow. No current call sequence converts this isolated portable gate into playable
BC2 ground pickup.
The first settlement contract requires the same world-item lifetime identity. If
the proven native implementation unspawns and re-creates that entity, it needs an
explicit old-to-new native lifetime/contents mapping extension before acceptance.
Native automatic ammo grants or pickups are similarly unsupported by this strict
consumption-only accounting rule.

## Native investigation and acceptance still required

First establish a real target/lifetime pickup reader and the normal BC2 pickup/drop
sequence described in `GROUND-PICKUP-20261003.md`. Then inspect whether the exact
held world weapon can be an external native active weapon or whether reversible
slot borrowing is necessary. Verify the server/client ammo owner, firing target,
native aim route, restoration and linked-weapon behavior before enabling either
capability. No inferred schema or fake portable receipt authorizes native writes.

The first future native test must include pickup while the trigger is held (zero
shots), an actual release then press (native shot from the held gun), unchanged
permanent assignments, actual ammo consumption, reconnect while pressed (zero
carryover shots), release-to-world with remaining ammo, and stow-to-swap dropping
the exact previously assigned weapon. A headset test must then confirm alignment
and usability. These have not been run.

Offline tests compose real shared-hand preview-to-GunHold transitions, ordinary
raw controller input and existing pose validation. They cover held-trigger pickup,
repeat polls, target/regrab/reconnect/focus boundaries, exact aim proof, expired or
changed native evidence, independently disabled borrowing and actual-ledger
settlement. The native adapter remains disabled regardless of portable success.
