# Authored grip bindings

`tools/bc2_authored_grip_bindings.py` connects decoded static hand poses to actual
weapon definitions. It consumes the authored configuration, exact mesh-binding
inventory and hand-pose batch, then rereads the installed archive references.
It never opens the game process or enables a native capability.
Produce the hand-pose input with `tools/bc2_authored_hand_pose_batch.py`, which
uses the same authored-animation decoder across the selected installed archives.

Generate that input from an existing installed-asset inventory and an explicitly
chosen skeleton. The batch keeps archive/index hashes, content versions and
unsupported resources; it never assigns this skeleton to a live weapon:

```powershell
python tools/bc2_authored_hand_pose_batch.py `
  --game "D:/Games/Battlefield Bad Company 2" `
  --inventory reports/installed-weapon-inventory.json `
  --skeleton-archive Dist/win32/levels/sp_common/level-00.fbrb `
  --skeleton-resource Characters/Skeletons/ske01.res `
  --output reports/authored-hand-pose-batch.json
```

The subsequent typed binding verifies the skeleton independently. Static hand
evaluation does not depend on optional finger or weapon-part decode success.
Run `python tests/test_bc2_authored_hand_pose_batch.py` for its provenance and
partial-result regressions. All derived output remains private.

The binding follows each `SoldierWeaponData` GUID and `WeaponState.AnimTree1p`
to `Animation.SpecificAnimTreeData.ReplacmentAnimations`. It selects the
`HandsIkPose` role from the referenced `AnimationAsset.Name`, independently of
list position. The asset's `Skeleton` GUID must resolve to a typed
`Animation.SkeletonAsset`, whose `Name` selects an actual `GrannyModel` resource.
The decoded topology, skeleton content and canonical rig fingerprint must match
the pose batch. First-person mesh references must match the same weapon, state,
mesh GUID, typed `Render.SkinnedMeshAsset.Name` and document content.

If a clip exists in the configuration archive, its actual bytes determine the
content variant. Otherwise only a unique exact resource/content version in the
explicit batch can supply it. The tool rereads that version from a recorded
archive and verifies index and content hashes. It never chooses a nearby weapon
directory. Ambiguous variants remain gaps. Both required hand transforms are
re-evaluated through the shared decoder and compared with the saved batch.

```powershell
python tools/bc2_authored_grip_bindings.py `
  --game "D:/Games/Battlefield Bad Company 2" `
  --archive Dist/win32/levels/sp_common/level-00.fbrb `
  --configurations reports/authored-configurations.json `
  --mesh-bindings reports/authored-mesh-bindings.json `
  --hand-poses reports/authored-hand-pose-batch.json `
  --output reports/authored-grip-bindings.json `
  --header reports/Bc2AuthoredGrips.generated.h `
  --asset AEK971_sp --asset XM8_sp_s
```

Output schema `fvr.bc2.authored_grip_bindings`, version 1, contains `profiles`
and explicit `gaps`. Each profile supplies exact `native_asset_name` and
`configured_mesh_path`, weapon/state/animation/skeleton provenance,
`rig_fingerprint`, canonical row-vector metre matrices
`right_hand_in_weapon` and `left_hand_in_weapon`, separate static statuses,
optional constant finger/part matrices and an immutable `binding_digest`.
`runtime_accepted` and `active_native_mesh_binding` remain false.

The optional C++ header requires an explicit repeated `--asset` allowlist. It
contains `fvr::bc2::generated::AuthoredGrips` using the shared
`AuthoredGripProfile` contract. Equivalent runtime keys may be deduplicated only
when clip, skeleton and both hand transforms agree; conflicting keys reject.
Multiple configured states are not emitted by this initial runtime adapter.
The generated JSON and header are private derived game data and are not included
in the source package.

The runtime must still prove current native equipment, configuration/mesh array,
rig, actor, tracking space and freshness. Authored reference binding does not
prove which mesh consumed a particular live animation palette. It supplies an
initial hand placement, not aim/muzzle, reload, holster or chambering authority.
The generator also rereads exact `WeaponClass` and records `authored_rifle_support`
for `wcAssault` and `wcSmg` only. Explicitly generated entries can use their static
left-hand socket through the existing support-grip policy and hand arbiter.
This is an experimental authored contact, not native or headset acceptance.
Its original equipment/configuration/input lease remains required. Reload-held
objects, release, tracking loss and actual equipment changes retain priority.
The shared policy rotates both right grip and aim by the same relative delta;
absolute model-to-aim alignment remains a separate explicit reference.

`--model-axes reviewed-axes.json` optionally includes experimental visual model
alignment in `--header`. Omission keeps all axes absent. A descriptor must match
the exact emitted asset, mesh path, rig, binding digest, animation/skeleton and
mesh content hashes; unknown or duplicate keys reject. Each entry supplies unit,
perpendicular `model_forward` and `model_up` vectors plus a reviewed evidence
record (geometry image, authored firing fields and static flash evidence hashes).
The schema is `fvr.bc2.authored_model_axes`, version 1, with at most 128 `profiles`.
No weapon class supplies default axes. The generated entry retains the evidence
digest separately from the grip binding; both are reported by the runtime.

After fresh authored binding, the renderer uses the same pure model-axis math as
`WeaponAimFrame`. The native profile verification gate and accepted defaults stay
unchanged. Original identity/lease checks still guard output and both eye packs.
This aligns the visible model to controller aim and keeps the authored wrist at
its tracked position. It does not add translated muzzle origin, native firing
geometry verification, reload or holster support; headset validation is pending.
Existing accepted profiles remain controlled by the runtime's explicit priority
policy. Varying hand curves are not promoted to static grips; their interior
interpolation requires separate validation.

Run `python tests/test_bc2_authored_grip_bindings.py`. The generator uses the
standard library and the included config, mesh, archive and authored-animation
readers. It adds no external decoder dependency.
