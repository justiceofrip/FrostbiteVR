# Body inventory and empty-hands policy — October 1, 2026

## Chest/shoulder consumer integrated — October 1, 22:11 UTC

Explicit -BodyInventory now connects a left chest ammo zone and two shoulder
selection zones to the shared hand/inventory policies. It derives categories and
routes from current native inventory, supports observed pickups/replacements,
and waits for selected-weapon/ordinary-rig evidence before confirming a draw.
Full builds93/92 and launch validation pass; native/headset acceptance is pending.
See BODY-INVENTORY-CONSUMER-20261001.md for geometry and exact limitations.

This is not completed holstering: no gun is relocated to the back, no holstered
mesh is rendered, and empty_hands_enabled remains false. The separate hide/show
candidate is being prepared. Physical torso rotation also remains a later anchor
improvement; initial comfort zones use the recentered reference heading.

## Requested integration update — October 1, 21:39 UTC

The user explicitly requested ammunition on the chest and guns on the back together.
Implement a shared configurable body reference, chest ammo acquisition, and two
shoulder locations for currently carried long guns. Do not hardcode SPAS/XM8 or
assume an inventory index is a permanent weapon type. Pickups/replacements must
update item identity and assignment. Putting a weapon away should free the hand;
drawing requires actual native selection plus verified visible presentation.

The current physical SPAS consumer uses a waist-relative pouch at(-0.23,-0.55,+0.02)m
in its upright HMD/body convention. It is not a chest implementation. The portable
BodyInventory policy already exists; the current agent task is the native consumer,
body contacts, shared claims and verifiable hide/fire/equip behavior. Native render
and empty-hand acceptance remain open; do not substitute a policy flag for proof.

The user reactivated physical body inventory: hold no gun, show an open right
hand, and put shotguns on the back while supporting weapons acquired through
pickups. This increment supplies a portable transaction policy and deterministic
regressions. It does not yet hide BC2 weapons, render holstered meshes, resolve
body contact, or enable a new native equip route. Existing grips remain separate.

## Portable contract

`include/fvr/interaction/BodyInventory.h` accepts complete authoritative inventory
observations. An observation has an actor/lifecycle/space identity, a monotonically
increasing observation sequence, a monotonic loadout revision, a timestamp, the
physical items currently owned, current native selection, and verified presentation.
The loadout revision changes whenever membership, identity, ordering, or slot
preferences change. Repeated observation sequences must have identical contents.
A fresh observation is required to issue or commit a transaction.

Each physical item has an opaque `{id,generation}` key. The adapter must advance
its item generation after removal/replacement, including reuse of a native pointer.
Asset name, array index, class, or persistence identifier alone is not an instance
identity. Rifle/launcher backend modes may map to one physical item only when the
adapter has verified their relationship. The adapter must not infer that all
weapons sharing persistence or a skeleton are the same physical instance.

Each item carries up to four ordered body-slot preferences and a priority. These
come from verified category metadata and the user's preference, not an asset-name
list. Higher priorities reserve contested slots first; surviving items retain a
compatible unclaimed prior slot. Overflow items remain owned but unassigned,
never cloned into another slot. Slot IDs are opaque; spatial anchor geometry and
contact/grip recognition belong to the adapter. A shotgun category can prefer the
back without assuming that inventory slot zero always contains the SPAS.

A body contact emits a fresh serial, operation, slot, and the exact item key read
at contact time. The policy never resolves an old contact against a replacement
that later occupies the same slot. Holster requires that item to be held. Draw
can select an owned assigned item from empty hands or replace the currently held
item; the old item remains in the authoritative inventory. No thumbstick UI is added.

Holster/Draw emits one request containing transaction ID, owner, loadout revision,
source observation sequence, operation, slot, and item key. It commits only on an
exact acknowledgement in a later observation AND the corresponding authoritative
state: holster requires `EmptyHands` with fire suppression; draw requires the exact
item selected and `WeaponVisible`. Seeing a changed selection alone, or receiving
an accepted request alone, does not commit. Duplicate polling cannot dispatch or
commit twice. Explicit failure, timeout, focus/tracking loss, ownership/space
change, loadout refresh, unexpected selection, stale data, or malformed inventory
cancels pending work. Neutral input and a new intent serial are required afterward.

`blockFire` is true while empty, unavailable, or waiting, and on cancellation. The
adapter must enforce it at its native input boundary; it is a request, not proof
that the engine stopped firing. `EmptyHands` may be reported only after the adapter
has verified weapon hiding and fire suppression. A retained native selected weapon
is permitted while holstered; a native "no selected weapon" slot is not assumed.
An engine without a proven hide binding must report `Unknown`. Hand-pose consumers
may use the committed empty state for both free hands once that binding exists.

Defaults are a 1.5-second acknowledgement deadline, 150-millisecond observation
age, 32 physical items, and four preferences per item. Timeout is bounded even
when observations stop advancing. No native pointers or BC2 offsets appear in
the shared policy.

## Current BC2 evidence

A bounded read-only observation at **2026-10-01 11:16:32 UTC** used PID146632 and
`capture_reload_state.Image/Inspector` plus the read-only `Process` wrapper.
The installed executable SHA256 matched
`3911fcc8914b0158c434cee36f19295316e715d4c9a0e70c857e8c39e3d54258`.
Unique context/manager/soldier signatures were rediscovered and compared with
live code, reflection types were checked, and owner plus complete inventory were
reread unchanged. No game calls, hooks, writes, input, launch, or focus changes
were performed. An initial read rejected a null gadget persistence pointer; the
subsequent capture explicitly treats persistence as optional.

The current resolver reads the live soldier weapon-pointer array at +0x260/+0x264,
the flag-dependent inventory object at +0x248/+0x24c, and selection at inventory
+0x14c. These are BC2 evidence, never shared-policy offsets. The native selector
and original selection setter remain authoritative for eligibility and transitions;
see `LAUNCHER-BINDINGS-20261001.md`. The narrow existing mode resolver does not
constitute a dynamic arbitrary-item draw implementation.

`SoldierWeaponData.WeaponClass` is reflected at +0x84 as `WeaponClassEnum`.
The captured values prove category-driven assignment is possible:

| Current item | Native class | Value | Persistence ID |
| --- | --- | --- | --- |
| SPAS12_sp | wcShotgun | 1 | sp_spas12 |
| XM8_sp_s | wcAssault | 0 | sp_xm8_s |
| 40mmgl | wcUgl | 5 | sp_xm8_s |
| LZ-537 | wcLaserDesignator | 12 | null |
| KNV-1 | wcKnife | 11 | mel |
| HG-2 | wcHgr | 6 | hgr |

The XM8 and launcher still share one reflected `PersistentWeapon` object. The
laser designator has no persistence object, so requiring persistence for every
item would discard valid inventory. `wcNone=20` is a weapon-class enum member;
it does not prove a valid native empty-equipment selection or command.

## Visibility lead and remaining proof

`WeaponStateData` has reflected `HiddenBones1p` at +0x4c, `HiddenBones3p` at +0x70,
`Meshes1p` at +0x80, and `MeshZoom1p` at +0x94. The type size is 200 bytes.
The two hidden-bone fields are arrays of strings. A follow-up read validated their
array headers/elements and ownership; the sole current state of SPAS, XM8, and
40mmgl has both arrays empty. These fields identify a native consumer to trace;
they are not a verified holster API and must not be edited as shared asset data.

Existing rig evidence recognizes one native hidden SPAS reload leaf (`jntWpn_7`)
with a collapsed 1e-4 basis and preserves it byte-for-byte. That does not prove
that arbitrary gun bones can be collapsed while keeping all arm geometry intact.
The captured scoped-XM8 topology has 20 joints in the `jntWpn_1` subtree, with no
arm/hand descendants. `jntWpn_0` remains outside that subtree, under Spine. This
makes a private weapon-only palette candidate plausible, but mesh weights and
render-section ownership still need proof: shared-arm vertices or geometry bound
to an outside ancestor must not disappear or stretch. Native source palettes and
authoritative animation must remain untouched.

Required native work before enabling body inventory:

1. Maintain item lifetimes/loadout revisions from coherent inventory observations,
   including pickup/replacement and verified rifle/launcher mode coalescing.
2. Resolve an intended physical item through the current native switching map and
   verify actual selection, without writing equipped pointers or guessing slots.
3. Prove a reversible render-only gun hide/show binding that preserves both hands,
   attachments/effects, and existing accepted weapon grips.
4. Enforce primary/secondary firing suppression while empty or pending, including
   native input sources, then provide exact selection/presentation acknowledgements.
5. Bind category-based body anchors and intentional hand-contact gestures; render
   holstered representations only after their separate mesh/pose proof.

## Validation

`tests/BodyInventoryTests.cpp` covers complete holster/draw acknowledgement flow,
state-without-ack and ack-without-state, stale transaction IDs/owners/items,
pointer-generation replacement, removal during pending draw, stale body contacts,
slot conflicts/reordering/overflow, duplicate observations, timeouts, lifecycle and
tracking/focus invalidation, clock/revision rollback, missing hide/fire bindings,
malformed duplicate items and unversioned loadout mutation.

The standalone x64 and x86 suites passed with C++20 `/W4 /WX`; root owns CMake registration,
both complete architecture builds, integration, and any native/headset acceptance.
No native body-inventory or weapon-hiding result is claimed by these offline tests.
