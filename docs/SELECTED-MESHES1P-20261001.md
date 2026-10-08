# Current selected item to first-person mesh evidence

`Bc2SelectedMeshes1p` implements the existing Python `Inspector.mesh_links` proof
as a callback-only BC2 resolver. It is explicitly disabled by default. This
candidate does not change rendering, ammo, input, game memory or any native hook.

Discovery uniquely finds the same context, manager, soldier weak getter and
ClassInfo inheritance constructor in the supplied PE32 executable. Every enabled
read compares their inspected code ranges to that file. Reflected object getters
must be the exact six-byte `mov eax, typeInfo; ret` form; Meshes1p array getters
must exactly read count at +12 and storage at +16. Parent inheritance uses the
verified ClassInfo +0x14 field, with cycle/depth/size guards.

The resolver checks the context's current manager/local player, weak soldier,
back-reference, on-foot controlled actor, inventory and unique selected item.
The expected token includes actor, equip and tracking-space generations supplied
by the adapter. It follows SoldierWeaponData.WeaponStates → WeaponStateData
Meshes1p → exact SkinnedMeshAsset objects and their inherited Name field.

Each complete config/mesh/type/name observation is repeated, followed by the
entire owner/inventory chain. Changing the selected slot, inventory items,
configuration pointer, mesh list, name or actor flags invalidates the result.
This is coherent repeated reading, not a simulation-atomic snapshot; an ABA
change between reads cannot be excluded. The callback must provide safe reads.

Bounds: 64 inventory items, eight authored states, eight meshes/state, 64 fields
per reflected type, eight inheritance levels, 512-byte mesh paths, 128-byte weapon
names; 131072 read callbacks / 2 MiB total / 4096 bytes per callback maximum.
No unbounded native allocations or exported game assets. Reflection is diagnostic
sampling and must stay outside the per-draw hot path.

## Exact interpretation

`soleConfiguredArray` is nonzero only for one authored state. Its value is the
address of that state's Meshes1p array, not a mesh object, rig pose or GPU resource.
`FindSelectedMesh` returns a unique exact known asset from that single state only
for the identical full owner token and the original unexpired (at most 250 ms)
lease. Duplicate matching assets and multiple states remain ambiguous. Unknown
assets remain in the snapshot with kind Unknown instead of becoming an XM8/SPAS
fallback. XM8 and 40mmgl may legitimately reference the same XM8/ACOG assets.

This proves a current selected item's **configured** first-person asset. It does
not prove the currently active authored state, submitted skin instance, palette
remap or GPU draw ownership. All those capability flags remain false. Do not
enable reticle suppression or uncollapse a shell just because this lookup works.

## Parent integration

1. Keep the startup executable byte vector alive, and discover the binding once.
2. Sample `ReadSelectedMeshes1p(memory, binding, base, currentOwner, sequence,
   observedNs, originalDeadlineNs, diagnosticsEnabled)` from an existing safe
   diagnostic boundary. No callback may call game methods or write memory.
3. Retain the returned value as an immutable snapshot. Use the same full owner
   token (including equip generation and space) when joining a producer request.
4. Record `soleConfiguredArray`, mesh asset address/path, owner, sequence and
   original times alongside the producer snapshot. This adds asset ownership
   evidence. Actual same-request packed palette and exact GPU geometry evidence
   remain separate required observations before a rendering behavior change.

Add the new cpp to BC2Camera and add the focused test in the parent's next build.
No staged/original files or the previous binding-candidate were edited here.

## Validation

`Build-Focused.ps1 -Architecture x86` and `-Architecture amd64` build and run 11
deterministic cases covering successful SPAS and XM8/launcher identity; immutable
results; default-off/lease/owner guards; selected slot/weak/manager mismatch;
array/overflow bounds; altered getter/constructor/reflection/inheritance rejection;
mid-read changes; ambiguous/unknown assets; unsupported discovery; empty arrays;
read failures; and maximum 8×8 mesh arrays.

The optional `-GameExe` argument performs offline discovery only. The installed
EXE resolves context 0x015713d8 and inheritance constructor RVA 0x00100660, matching
`reports/reload-mesh-links-20261001.json`. No new live read or native test was
performed by this subagent. Full x86/x64 builds and the first native observation
of this C++ resolver belong to the parent integration step.
