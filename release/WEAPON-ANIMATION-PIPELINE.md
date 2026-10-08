# Authored weapon animation reader

The offline reader extracts named weapon, hand and finger tracks directly from
installed BC2 archives. XM8 and AEK use the same reader and skeleton evaluator;
there are no gun-specific offsets in the decoder. Python's standard library and
the included `inspect_bc2_mesh_asset.py` archive reader are the only dependencies.
No game assets or generated poses are included in the source package.

```powershell
python tools/bc2_weapon_animation_pipeline.py `
  --game-root "D:/Games/Battlefield Bad Company 2" `
  --archive "D:/Games/Battlefield Bad Company 2/Dist/win32/async/weapon/us_rgl_xm8-00.fbrb" `
  --clip Animations/Weapons/Handheld/US_rgl_XM8/1P/HandsIkPose.res `
  --skeleton-archive "D:/Games/Battlefield Bad Company 2/Dist/win32/levels/sp_common/level-00.fbrb" `
  --skeleton-resource Characters/Skeletons/ske01.res `
  --bone RightHand --bone LeftHand --bone jntWpn_6 --time 0 `
  --output reports/xm8-authored-pose.json
```

For AEK, select its `ru_rgl_aek971-00.fbrb` archive and
`Animations/Weapons/Handheld/RU_rgl_AEK971/1P/HandsIkPose.res` resource. Exact paths
are inputs, not inferred from similar names. Repeated `--bone` arguments select
fingers or other parts. `--clip` without bone/time arguments emits track metadata.

For batch tools, import `inspect_archive(path, game_root=None)`. It inflates each
archive once and returns schema `fvr.bc2.authored_animation_batch`, version 1.
Each clip retains the exact archive path, optional root-relative archive path,
archive-index hash, resource name/type/hash, duration, named curve types and an
explicit decoded/partial/unsupported status. `DeltaAnimation` is unsupported as
an absolute pose. Deduplicate by exact resource name and content hash; archive
co-occurrence alone does not associate an animation with a particular weapon.

`Clip(data)` and `Skeleton(data)` accept resource bytes from the archive reader.
`Clip.evaluate(skeleton, time, names, weapon='jntWpn_1')` returns
`weapon_relative[name]` matrices. All required ancestor tracks must be present;
missing data never falls back to a different weapon. `Skeleton.metadata()`
provides named topology, inverse-bind consistency error and the mod's ordered
name/parent/canonical-float32-inverse-bind fingerprint.

Transforms use row vectors: local times parent, then bone world times inverse
weapon world. Output reflects Z on both sides of the matrix and uses metres.
The skeleton's authored units/basis must match the supported format. A clip
with no art metadata explicitly inherits the caller-selected skeleton basis;
conflicting metadata is rejected. When combining these poses with raw mesh
vertices, reflect the vertex Z before applying canonical inverse-bind matrices.

`bone_evaluation_status` identifies constant authored controls separately for
each requested bone and its complete ancestry. `static_authored_pose` is useful
for initial authored grip profiles. Varying controls are
`decoded_spline_candidate`: interior spline evaluation has not been compared
with the BC2 runtime. Endpoints and decoded curve payloads do not certify a
reload mechanism, chamber state or active native mesh. Exact caller-selected
clip/skeleton pairing and named-track correspondence are recorded separately
from runtime mesh identity; the reader never grants runtime admission.

The supported container is the observed Frostbite version-10 wrapper containing
uncompressed, 32-bit little-endian GR2 version 7 sections. Compressed sections,
other wrapper flags, unknown track flags and unsupported curve formats fail
explicitly. Resource, string, section, object, type-depth and decoded-value
budgets bound malformed input. Nonfinite or overflowing transforms are rejected.
The decoded formats are identity, constant 3/4-vector, float knots/controls,
quantized 3-vector and quantized normalized quaternion variants; only degrees
0–2 are evaluated. See `licenses/Norbyte-LSLib-MIT.txt` for attribution and format
references.

Run `python tests/test_bc2_weapon_animation.py`. Tests cover container bounds,
reference amplification, quaternion layout, constants, repeated knots, basis,
hierarchy, reflection, inverse transforms, overflow and static/dynamic labels.
Installed archive replays are local development evidence and stay outside the
source export. They do not replace runtime or headset validation of a profile.
