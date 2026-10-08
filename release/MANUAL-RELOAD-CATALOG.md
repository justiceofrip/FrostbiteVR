# Authored manual-reload catalog

This offline catalog joins installed SP and MP definitions, exact mesh references,
weighted parts and named animation channels. It produces calibration jobs and
shared native-dispatch groups. It does not enable gameplay or add chambering.

Run the existing asset inventory and SP mesh-binding tools first. Keep their
JSON output private. Then use Python with the source `tools` directory:

```powershell
python tools/bc2_weapon_config_pipeline.py --game $Game --output $SpConfig
python tools/inspect_lmg_common.py --game $Game --tools tools --inventory $Inventory --all-weapons --output $MpCommon
python tools/inspect_reload_animations.py --game $Game --inventory $Inventory --output $Animations
python tools/build_manual_reload_catalog.py --config $SpConfig --bindings $SpBindings --common-metadata $MpCommon --inventory $Inventory --plan profiles/weapon-family-work-plan.json --animations $Animations --output $Catalog
```

All paths are supplied by the caller. `--common-metadata` and `--animations` may
repeat. The common reader retains its default LMG-only behavior without
`--all-weapons`. Its MP archive bound remains 640 MiB, streamed in 1 MiB chunks,
with at most 32 MiB selected DBX data. It reuses the existing typed decoder.
The animation batch reuses the shared Granny reader and stores no curves,
vertices, image pixels or original resource bytes. Optional `--hand-poses` joins
an authored hand-pose batch by exact resource, content hash and archive source;
the catalog records available evidence without copying its transform payload.

## Schema version 1

The root schema is `fvr.bc2.manual_reload_catalog`. Its primary collections are:

| Collection | Identity and meaning |
|---|---|
| `configurations` | Archive/index + weapon resource/hash/GUID. SP and MP remain separate even when their bytes match. Each preserves the exact firing/function reference chain, typed action/logic/timing/ammo fields, states and animation-tree references. |
| `native_family_groups` | Exact authored fire logic, reload type, action mapping and bolt-release flags. Different weapon names or reload durations do not create a different ABI group. Timing/capacity variants remain explicit members. No group grants native admission. |
| `meshes` | Exact resource and geometry-content versions, weighted part metadata and source archives. Animation cooccurrence is labelled as such; it is not an animation-tree selection proof. |
| `calibration_jobs` | One job per available exact mesh/content version, shared by its configuration/state references. Retains native-family groups and required geometry/hand/active-mesh evidence. Jobs never execute automatically. |
| `unresolved_configurations`, `unbound_meshes` | Missing bindings remain visible, including installed resources outside the supplied common archives. |
| `unmatched_animation_observations` | Decoded archive/resource observations that cannot join the exact mesh inventory. They are not silently reassigned by basename. |
| `input_sha256` | Hashes of every supplied source snapshot, including optional animation/pose batches. |

The companion JSON schema describes the top-level interchange contract. All
tools reject duplicate identities, mismatched inventory/index snapshots and
nonfinite numeric data. Partial or unsupported animation tracks remain explicit.

## Evidence boundaries

Native `rtMagazine` does not prove a detachable box magazine. It also appears on
belt-fed weapons, launchers and gadgets. `rtSingleBullet` does not prove a tube.
The existing work-plan physical families are retained as proposals: belt feed,
detachable magazine, tube/pump, bolt magazine, cylinder and clip. For example,
MG36/XM8 LMG proposals remain detachable, while the belt-fed LMG proposals remain
separate. Underbarrel modes may reference a parent rifle mesh and never inherit
its proposed magazine operation.

Named reload/bolt/charge/pump clips are inspection candidates. Decoded varying
controls prove authored motion data exists; they do not identify a charging
handle, closed stop, active native animation or chamber contents. Part-role
assignment and dynamic contact calibration remain explicit jobs. The catalog
retains animation-tree references so an exact typed graph resolver can provide a
stronger configuration-to-clip link separately.

The shared BC2 executable dispatch proof can be reused for matching native
families. Current owner/config/state/capacity checks still apply at runtime;
authored capacity is not necessarily the effective native capacity. The catalog
does not request one independent ABI investigation per weapon name.

No game assets or extracted private matrices belong in the source package.
The catalog generators, schema, synthetic tests and this document are portable.
No headset or native-runtime result is implied by successful extraction.
