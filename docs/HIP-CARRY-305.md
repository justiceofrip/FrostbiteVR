# Hip carry candidate 305

The body inventory now has a right hip weapon anchor in addition to its two
shoulder anchors. Grabbing, stowing, selected hidden-weapon rendering and
unselected carried-weapon rendering use the same anchor lookup. The ammunition
chest/belt contacts remain separate from weapon slots.

BC2's installed MP443_sp data reports wcSmg. Native firing classification does
not determine physical carry location. A reviewed carry metadata table assigns
that exact asset to the hip; ordinary wcSmg assets keep their shoulder behavior.
This classification grants no reload, chamber, visibility or native-call support.
Additional sidearms can use the same policy through reviewed metadata entries.

For the ambiguous native class, the inventory reader includes the asset name in
both coherent reads using the existing SoldierWeaponData Name binding. A changed,
unreadable, malformed or unterminated name cannot silently preserve old carry
identity. In-place replacement changes physical identity, while unchanged items
retain their shoulder assignments. Render configuration must match that captured
name. The hip does not become a third slot for long guns.

The default hip center is 24 cm right, 62 cm below, and 2 cm forward of the
upright head/body reference, with a 16 cm contact radius. This is comfort geometry,
not a measured anatomical hip. It needs headset placement feedback.

Focused CPU regressions cover exact pistol versus SMG routing, draw through the
existing native selector, empty-air/old-shoulder rejection, three carried items,
replacement and pointer reuse, recentering, malformed/racing asset reads, and
both body-render paths. Their native/render acknowledgements are synthetic.

Five focused suites pass on x64. Both full normal builds pass all 243 suites:
x64 58.70 seconds, x86 142.01 seconds. The normal native DLL has no synthetic
input driver enabled. The finite test supplied poses and buttons through a
separate receiver, using the ordinary body-inventory and magazine session.

Native `hip305-native02/hip-carry-audit.json` verifies hip stow and draw, two
committed transactions with no cancellations, followed by one shot. Both client
copies and the server agree on 17/85 before and 16/85 after. The receiver consumed
240 stereo pairs without a camera restoration failure, discard or GPU failure.
All hooks drained, original native view order returned exactly, and BC2 stayed
alive. Fresh pistol and paired SPAS body metadata reached both eyes. This does
not establish rendered prop pixels, headset visibility or anatomical placement.
Three simultaneous carried items remain CPU-tested only; the native test had
two ordinary weapons, not a newly added third native inventory slot.

The first native attempt used an incompatible legacy diagnostic flag; normal
build configuration rejected it before installing hooks. The successful second
attempt retained the production guards and used receiver-only emulation.

Source SHA-256: bf015cb7cc19283c296bf302da800eec555c7386f1ebd564116967740e66535d.
The isolated 304 headset candidate remains preserved. No new pistol
slide/chamber, belt-fed reload, M95 manual magazine, whole-roster or headset
acceptance is claimed.

Next equipment work is the chest knife: grab with an empty hand, hold, then
attack through deliberate motion. The read-only 305 survey found the existing
KNV-1 item (wcKnife, slot 7), its mesh and the switch-map melee action 37. That
establishes identity, not safe draw/hold or motion-damage behavior. Physical
grenades are deferred at the user's request.

## Third native slot investigation311

Read-only current inventory/switch-map survey found slot2 empty beside the two
long guns, but native action33 routes through slots2/3 as underbarrel targets.
From slot0 it prefers2; from slot1 it prefers3. Occupying2 with a pistol would
alter attachment selection and cannot be treated as a general third gun slot.
The existing native assignment routine is vector-indexed, not intrinsically
limited to0/1, but its empty-slot and replication behavior has not been tested
for a third held weapon. The0/1 limit in the proven provisioning helper is an
explicit evidence bound. Do not widen it as an implementation shortcut.
A third pistol needs independent slot allocation plus a verified native
selection/replication route, preserving attachment slots and both long guns.
No game writes were performed in this investigation. Evidence:
knife309-binding/third-slot-survey311.json and the full exact-EXE901-byte
assignment disassembly in assign-third-slot-static.txt.
