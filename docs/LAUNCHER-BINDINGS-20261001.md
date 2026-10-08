# Launcher native bindings — October 1, 2026

Read-only inventory/reflection and original-executable inspection recovered a
native mode-selection path for the exact scoped XM8/XM320 pair. This is now
implemented by `Bc2WeaponMode`; the root agent's bounded no-fire roundtrip
confirmed both selection requests. No guessed equipped-pointer write or new
thumbstick inventory interaction is needed.

This research used verified BC2 PID 132784 through `tools/read_bc2.py`, which
opens only query/read access and checks the executable path. No native function,
game input, global input, or memory write was issued by this research agent.
The root's separately bounded test used the original native input/selection path.

## Exact family relationship

The live `SoldierWeaponData` reflection identifies:

| Field | Native offset | Reflected type |
| --- | --- | --- |
| Name, inherited from GameObjectData | 0x0c | String |
| WeaponAssetName | 0x40 | String |
| Persistence | 0x64 | PersistentWeapon |
| WeaponStates | 0x88 | ArrayBase of WeaponStateData |
| WeaponFiring | 0x98 | WeaponFiringData |
| AimingController | 0x9c | SoldierAimingSimulationData |

Current live instances:

| Item | Slot | WeaponAssetName |
| --- | --- | --- |
| XM8_sp_s | 1 | Objects/Weapons/Handheld/US_rgl_XM8/SP_rgl_XM8_Scoped |
| 40mmgl | 3 | Objects/Weapons/Handheld/US_rgl_XM8/SP_rgl_XM320_Scoped |

Both `Persistence` fields refer to the same reflected object, whose authored
`Id` at offset 0x40 is `sp_xm8_s`. SPAS references a different persistent
weapon. This is actual asset relationship evidence, not an inference from names,
inventory proximity or matching skeletons.

The implemented resolver requires exact item names, exact asset paths, both
reflected SoldierWeaponData types, the shared reflected PersistentWeapon and its
Id, matching native selection maps in both directions, and stable owner inventory.
These are deliberately narrow pilot bindings. Another loadout/profile must
establish its own pair; the observed pointers are never compiled as identities.

Evidence:
- `reports/launcher-bindings-live-20261001.json`
- `reports/launcher-reflection-20261001.json`
- `reports/launcher-parent-reflection-20261001.json`

## Native input and selection semantics

The executable's EntryInputActionEnum contains:

| Action | Value | Input cache field |
| --- | --- | --- |
| EiaGrenadeLauncher | 33 | Boolean word +0x9c, bit 1 |
| EiaDynamicGadget2 | 36 | Boolean word +0x9c, bit 4 |
| EiaThrowGrenade | 38 | Separate existing action; never used here |

Action 36's generic name is not renamed globally. It happens to select the
second primary weapon in this campaign's loaded switch map. The adapter validates
that actual map before using it as the inverse launcher interaction.

The native inventory's data pointer at +4 reflects `WeaponSwitchingData`.
Its `SwitchMap` field is at +0x10, with entry bounds at +0x18/+0x1c.
Each WeaponSwitchingMapData entry has a 24-byte stride:

| Field | Offset |
| --- | --- |
| ToWeapon array, element bounds | +8 / +0x0c |
| Action | +0x10 |
| FromWeapon | +0x14 |

Reflected WeaponSwitchingEnum values include primary1=0, primary2=1,
launcher1=2, launcher2=3, last-primary=9 and other-primary=10.

The current map explicitly contains:

- Primary2 (1), EiaGrenadeLauncher (33): ordered targets [3,2].
- Launcher2 (3), EiaDynamicGadget2 (36): ordered targets [1].
- Launcher2 (3), EiaGrenadeLauncher (33): [2,3], so pressing that same action
  again is **not** the validated return-to-rifle path.
- Primary2 (1), EiaSwitchPrimaryWeapon (7): [0,3,2,5,1].

The native selector reverses an action's ordered targets when its scalar value
is negative. Consequently a negative primary-cycle pulse while XM8 is selected
starts with current slot 1 and stays there. This explains why another
thumbstick-cycle pulse is not an appropriate launcher capture method.

`ResolveWeaponMode` produces only action 33 or 36 plus an expected target.
It validates the first populated target and refuses an earlier populated foreign
item or pseudo-slot. It does not call the game's eligibility function. Native
ammo/cooldown/availability and the original selector remain authoritative, so a
request is distinct from acknowledgement. The caller must observe the expected
selected item before accepting a physical sight-mode transition.

Evidence: `reports/launcher-switch-map-20261001.json`.

## Original executable consume proof

Addresses below are VAs for the inspected image at preferred base 0x00400000.
Discovery returns RVAs and validates relationships/signatures; these addresses
are evidence, not a bypass around discovery.

| Routine | VA | Verified behavior / comparison span |
| --- | --- | --- |
| Inventory constructor | 0x006ec700 | Stores WeaponSwitchingData at +4; builds per-state action maps from FromWeapon/Action/ToWeapon |
| Selector | 0x006ef590 | Chooses current +0x14c state, tests action edges, orders eligible targets; span 0x1b1 |
| Native update | 0x006f1980 | Calls selector at +0x4d, original selection setter at +0x64; span 0x86 |
| Native selection setter | 0x006d7c80 | Calls ownership/eligibility callback before changing state and notifying equipment |
| Action edge reader | 0x0061c400 | Previous false/current true through cache readers; span 0x37 |
| Boolean reader | 0x00612970 | Reads low/high masks at cache +0x98/+0x9c; span 0x88 |
| Scalar reader | 0x006128f0 | Boolean actions resolve to 0/1; span 0x7c |
| Current-cache scalar thunk | 0x0061c3e0 | Loads router current cache +0x1a0 then jumps to scalar reader |

The selector calls edge reader 0x0061c400 at +0x4f and scalar thunk 0x0061c3e0
at +0x100. The edge reader calls the same boolean reader twice, using previous
cache +0x1a4 and current cache +0x1a0. The selector reverses its target vector
at 0x006ef69d–0x006ef6b6 only for a negative scalar value.

`DiscoverWeaponMode` extends existing input discovery with action-name/value
checks, unique selector/update/edge signatures, relative-call relationships,
reader masks/cache offsets and ABI cleanup. Root integration additionally checks
live bytes against the inspected executable before enabling the optional mode
binding. The native selector retains normal equipment animation and eligibility.

Root validation: x86/x64 suites passed 38/37 before the bounded roundtrip.
Actual executable discovery passed with 91 mutations rejected, including 11
mode-binding mutations. The unit resolver tests cover missing/mismatched types,
different persistence, wrong asset paths/names, map action/state/order errors,
unavailable slots, duplicate identity, malformed bounds and a changing current slot.

## No-fire native roundtrip

`reports/native-trace-20261001-081204-659/native-trace.json` records two requests
and two acknowledgements, zero mode rejections:

1. XM8_sp_s -> 40mmgl through action 33.
2. 40mmgl -> XM8_sp_s through action 36.

The first acknowledgement arrives 16 ms after its request; the return is observed
in the same millisecond. These times acknowledge native selected identity, not
completion of every animation. Attachment settling remains necessary.

The collector retained 68 XM8 and 51 launcher records across the transition.
Completion reports bootstrap exit 0, hooks disabled, BC2 responsive and no new
crash. The test did not establish physical sight manipulation or headset feel.

## Geometry and animation path for the sight

WeaponStateData is a reflected 200-byte struct. The current pair each contains
one state, with fields:

| Field | Offset | Purpose |
| --- | --- | --- |
| Meshes1p | 0x80 | First-person skinned mesh array |
| MeshZoom1p | 0x94 | Optional zoom mesh |
| HiddenBones1p | 0x4c | Authored hidden-bone names |
| AnimTree1p | 0xb8 | Specific first-person animation asset |
| AnimTree3p | 0xb4 | Third-person counterpart |

For this Meshes1p array implementation, the virtual count getter 0x004e4930
reads +0x0c; data getter 0x004e4940 reads +0x10. Both current states have count 2
and reference the exact same two reflected SkinnedMeshAsset objects:

- Objects/Weapons/Handheld/US_rgl_XM8/US_rgl_XM8_Mesh
- Objects/Weapons/Unlock/ACOG_4X/US_ACOG_4X_Mesh

Their SpecificAnimTreeData objects differ, while their AnimTree references match.
The launcher has no separate Skeleton override in those objects; the rifle has
one. This gives a direct route to inspect animation replacements and mesh skin
weights associated with the folding parts. It does not justify naming a generic
numbered joint solely from topology.

Evidence:
- `reports/launcher-state-fields-20261001.json`
- `reports/launcher-asset-links-20261001.json`
- `reports/launcher-mesh-links-20261001.json`

The separate mechanism pipeline measured two returnable 90-degree changes during
the native roundtrip, `jntWpn_9` and `jntWpn_11`. Their pivot candidates are
near root-local y=0.0536 m, at z=-0.5854 and -0.7436 m respectively, with fitted
return residuals below 0.037 mm. These are strong candidate folding sight parts.
Correlate them with the actual shared mesh/visible folded and raised states before
assigning the sight role or using a contact pivot. The generic names alone are
not semantic proof. See `reports/launcher-mechanisms-20261001/candidates.json`.

The game installation contains original .fbrb packages under Package and
Dist/win32. The workspace's existing local tools are read_bc2.py and
inspect_native.py, with pefile/capstone available in the existing dependency path;
the new capture pipelines use native metadata and NumPy. No FBRB/mesh importer
was found in this workspace, G:\bc2, or the inspected immediate Downloads/Desktop
entries, and none was installed. Package parsing is not required to validate the
native selector or collect the folding bone motion. Native mesh skin-weight
metadata or a separately verified asset reader is the next geometry route if
screen-space correlation is insufficient. No game meshes or packages were copied.

## Aim versus launcher firing origin

The exact shared mesh strongly supports -Z as a candidate visible model-forward
axis. Actual settled roundtrip samples put the native flash basis within
0.000121 degrees of the root basis for both modes.

However, `jntWpn_Flash` has effectively the **same** root-local origin in both:
approximately [-0.000894,+0.036603,-0.846231] metres. This can still be the rifle
muzzle even while the launcher is selected. It is not evidence that the grenade
comes from that point. Do not promote translated muzzle support from the shared
mesh or flash name.

Visible aim/grip, folding-sight interaction and grenade firing origin are separate
feature gates. Root-relative authored wrists now have actual launcher captures;
the physical interaction can request the verified original mode actions and wait
for acknowledgement. Launcher final-shot origin/config and trajectory remain
native until independently verified. No translated-muzzle capability was enabled
by this research.
