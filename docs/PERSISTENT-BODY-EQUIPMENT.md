# Persistent assigned equipment publication

This source candidate extends the real Gameplay → rig publication → native eye
packet → existing host compositor path. It does not change the prepared headset
build, native inventory, weapon selection, suppression, or ammunition.

The actual `Bc2BodyInventory` policy now exports an immutable display snapshot
after its repeated coherent native inventory read and current slot assignment.
It includes every assigned slot, exact native item/data/persistence/native slot,
physical item generation, actual selected owner, and original controller lease.
The current body policy assigns two shoulders; the transport remains bounded to
eight props. Native inventory objects without a real body assignment are not
invented display slots. A linked underbarrel's verified physical rifle is treated
as selected, so it does not appear simultaneously as a spare weapon.

Unselected carried items use a separate configured-mesh type and entry point.
The reader reuses the existing verified executable binding, reflected
`WeaponStates`/`Meshes1p` code, and exact configured-array parser. It verifies the
actual selected actor/owner, the unselected slot's unique inventory membership,
data and persistence, reads the complete configuration twice, then repeats the
owner/inventory bytes. It never substitutes the spare's pointer into a selected
owner token. Its result cannot implicitly convert to `SelectedMeshesSnapshot`.
The existing observer owns the executable binding and caches the immutable
configuration for its original 200 ms interval, refreshing at the existing
100 ms cadence. Display is still limited by the original input's at-most-100 ms
interval. A cached metadata observation never renews an input or inventory lease.

Only the two existing closed-weapon cache profiles, exact configured scoped XM8
and SPAS mesh paths, can currently produce instances. No new geometry or weapon
capability is admitted. This is shared assigned-slot publication, not an assertion
that every inventory weapon has display geometry. The current selected committed
hide source remains separate and unchanged in its authority requirements.

The renderer pins its actual input/shot guard, rereads the complete native carried
inventory, and checks every represented item's data/persistence/asset identity
before and after constructing the batch. Pose uses the existing recentered body
and shoulder transform, including world scale. Original inventory/config/input
deadlines are intersected; no `now + TTL` authority is created at rendering.
Pickup, drop, same-pointer replacement, selection, actor/space changes, and lost
observation revoke the old cohort. Existing cancellation/reload-busy policy paths
withhold this display until fresh assignments return; this candidate does not
weaken those policy boundaries merely to keep a prop visible.

The new batch is all-or-none across its pre/post draw observations and on capacity
overflow. A monotonically increasing display cohort changes on any selected,
inventory or assigned-slot identity change. The existing 2352-byte
`BodyPropFrame`/128-byte instance and eight-instance capacity are unchanged.
`BodyPropWireEpoch` uses a checked source-kind namespace plus bounded source epoch
and slot ordinal to prevent shifting ammo/selected/carried packet indexes from
matching identical geometry belonging to another source. These wire values grant
no gameplay authority. Overflow returns zero and rejects display. Ammo and the
selected-hide helpers encode their original validated equipment epochs through
the same helper. Original native generations remain in the typed source objects.

## Validation and limits

New actual-consumer tests cover coherent assignments, unchanged duplicate leases,
drop/reacquisition, same-pointer persistence/asset change, selection, boat-like
controlled-actor loss, focus/read cancellation, recenter recovery, linked selected
rifle exclusion, typed configuration, cache expiry, original pre/post deadlines,
whole-batch rejection and wire source/slot isolation. Existing inventory,
selected-reader/observer and committed-hide tests remain part of the focused run.
All tests use memory callbacks or CPU composition; no native game or graphics
execution occurs. The three actual x86 native translation units are compiled.

Next native acceptance: while on foot with the two known carried weapons, verify
the unselected shoulder model, both models after a real committed stow, removal of
the newly drawn model from its shoulder, replacement/drop retirement, and no props
while mounted or after owner loss. Recenter should move both through the same
body anchor math. This has not been run. Host solid-color rendering and private
depth remain the existing material/occlusion limits; no native scene/hand depth
proof or full-body mesh binding is claimed.
