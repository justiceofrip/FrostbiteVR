# Weapon coverage and shared interaction families

The offline pipeline discovers installed weapon **and gadget** resources and
generates reviewable interaction blueprints. It does not install runtime profiles
or make every discovered gun VR-ready. The game installation remains read-only;
only names, hashes, counts, bounds and coverage information leave the parsers.

Run from the source root with Python 3.10 or newer (standard library only):

```powershell
python -B tools/bc2_weapon_asset_pipeline.py --game "D:\Games\Battlefield Bad Company 2" --output reports/weapon-assets.json --family-plan profiles/weapon-family-work-plan.json --blueprints reports/weapon-blueprints.json --config-archive Dist/win32/levels/sp_common/level-00.fbrb --runtime-snapshot profiles/runtime-weapon-scope-20261003.json --runtime-root .
```

The optional runtime snapshot is tied to hashes of the exact source that declared
the existing SPAS/XM8 capabilities. A changed source rejects that annotation; omit
the two runtime options to run discovery, or explicitly review and update the
snapshot. Never change hashes merely to make an unrelated build pass.

The October 3 local scan found 323 asynchronous weapon archives and 55 distinct
weapon/gadget mesh resources. It decoded 315 of 322 mesh occurrences. Seven
unrecognized layouts stayed explicit gaps. These counts include shared variants
and gadgets such as ammunition boxes; they are not a count of supported guns.
The shared single-player archive also supplied 177 weapon DBX dictionaries.

## What is reusable

The shared `TrackedRig` acquisition now waits for the authored weapon/wrist
attachment to settle regardless of whether the weapon has a measured aiming-axis
profile. Selected-AEK-labelled samples reproduced a 37.6 cm first-frame error;
the corrected acquisition removes that numerical error in replay. The user
identified the reload footage as XM8, so these samples do not establish an
AEK animated grip or magazine profile. Runtime/headset checking
is still separate from this offline result.

The existing aim/support/muzzle registry remains exact: `SPAS12_sp`, `XM8_sp_s`
and the measured XM8 launcher mode have their own feature evidence. Sharing a mesh,
skeleton, category or reload family grants no additional feature. A stable grip
alone does not prove muzzle translation, support contact, physical reloading,
holstering or a launcher relationship.

`weapon-family-work-plan.json` is explicit development routing data, not runtime
configuration. Ordinary detachable magazines share one policy; tube pumps, bolt
actions, belt feeds, revolver cylinders and en-bloc clips stay distinct. Magazine-
fed LMG and shotgun exceptions are listed separately. Unresolved roster entries
retain missing bindings instead of invented asset names.

Underbarrel eligibility is independent of all those families. Garand is excluded
by the requested scope. The six discovered Mk14 EBR variants contain no launcher
variant; that bounded observation is not a universal attachment assertion.
Shared Type5/Garand animation names do not establish Garand launcher eligibility.

## Output contract

`fvr.bc2.installed_weapon_inventory` v1 contains exact archive/index/resource
hashes, animation names, LOD sections, normalized skin influences and per-bone
bind bounds. The decoder rejects unsupported layouts. Candidate part names are
hash matches; bounds are asset-bind coordinates, not ready-made loading sockets.

`fvr.weapon_interaction_blueprints` v1 joins those resources to an explicit family
proposal and an optional existing native-asset scope. Every generated profile has
`runtime_enabled: false`; every weighted part is initially unassigned. Capture
jobs specify the observations/configuration fields needed for the selected family.
The jobs do not operate the game or select native offsets.

The typed configuration decoder verifies complete bounded DBX trees and follows
exact resource/GUID references from `SoldierWeaponData` through `WeaponFiringData`
to `FiringFunctionData`. Its scalar schema explicitly distinguishes strings,
booleans, signed integers and floats; other binary arrays remain opaque. The
shared single-player archive resolves 127 weapon-definition chains from 177
documents, without parse failures, unresolved chains or missing requested fields.
These are definitions and variants, not 127 different supported guns.

```powershell
python -B tools/bc2_weapon_config_pipeline.py --game "D:\Games\Battlefield Bad Company 2" --archive Dist/win32/levels/sp_common/level-00.fbrb --output reports/weapon-configurations.json
```

`fvr.bc2.authored_weapon_configuration` v1 records exact resource hashes, GUIDs,
native names/classes, first-person mesh references, authored pump flags and the
explicit firing/ammunition fields. Missing values stay missing; no implicit
inheritance or defaults are invented. This archive's chains are directly
materialized; inheritance semantics and other archives remain unverified.
Every result has `runtime_admitted: false`. Authored configuration is not a
native object layout, current live capacity, or callback ownership receipt.

For example, authored AEK971_sp has capacity 30 and reload time 3.2 seconds;
scoped XM8 has capacity 30 and 2.8 seconds. Authored SPAS capacity is 4 even
though a live campaign branch has been observed at 8: current native capacity
overrides/multipliers still govern. Never replace live state with this export.

## From extracted data to the shared runtime

The physical magazine and native reload consumers now select the same immutable
profile. Ordinary rifles no longer need XM8 launcher membership. Current AEK
configuration is registered under the same verified native automatic/magazine
implementation; geometry remains absent, so physical AEK interactions stay off.

1. Resolve exact authored resource/GUID/mesh references in batches. Reuse verified
   executable-wide native enum/layout/family mappings, then compare actual current
   native configuration. Authored values never replace live capacity or ownership.
2. Decode authored hand/weapon animation tracks against the matching skeleton and
   mesh to derive initial grips and moving-part contacts. This reader is under
   development. A selected item label or shared skeleton is insufficient proof
   that a captured pose belongs to that item's rendered mesh.
3. Supply per-weapon geometry to the shared family consumers. Shared defaults can
   cover interaction tolerance and behavior; magazine position, rail direction,
   bone correspondence and special mechanisms come from exact asset data.
4. Exercise the connected pickup/grip/fire/remove/return/seat path on another
   ordinary rifle, preserving ammunition and native completion receipts. Then
   expand the data set and check exceptions rather than copying implementations.

AEK is that second rifle, not a bespoke development track. Family mechanisms for
pumps, bolts, belt feeds, revolvers and launchers are separately incomplete.
Playable coverage must be recorded separately from extraction and CPU tests.

Run the new parser and blueprint regressions with:

```powershell
python -B -m unittest discover -s tests -p test_bc2_weapon_asset_pipeline.py
python -B -m unittest discover -s tests -p test_bc2_weapon_config_pipeline.py
```

No original game assets or private captures belong in a published source package.

## Resolve first-person mesh identity

```powershell
python -B tools/bc2_weapon_mesh_bindings.py --game "D:\Games\Battlefield Bad Company 2" --inventory reports/weapon-inventory.json --output reports/weapon-mesh-bindings.json
```

The join follows every state's exact Meshes1p resource/GUID to the actual
Render.SkinnedMeshAsset instance, verifies its authored Name, and associates the
resource with the supplied geometry inventory. The installed shared campaign
archive resolved193 of193 references from127 weapon definitions;124 references
match the handheld geometry inventory. References missing from that inventory
remain explicit, including separately stored attachments. Multiple meshes,
states and content variants are preserved. No primary-instance shortcut, class
name, shared filename or sibling runtime capability establishes a binding.

The output records source hashes and remains runtime_admitted:false. Supplied
geometry is a snapshot, so native selection/LOD/bytes still need current proof.
No underbarrel capability is inferred from these first-person mesh references.

Native SPAS/XM8 configuration matching now uses reviewed data descriptors with
one shared matcher and typed double-read timing check. Compatibility tests retain
the predecessor acceptance sets and read ordering. Unreviewed descriptors cannot
match or read native memory. The AEK descriptor now reuses reviewed family code,
but absent geometry still prevents physical AEK interaction.
Geometry, carried-item identity, branch timing and native receipts remain separate.
