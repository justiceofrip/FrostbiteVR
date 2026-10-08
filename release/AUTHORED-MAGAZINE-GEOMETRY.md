# Experimental magazine geometry pipeline

`tools/bc2_authored_magazine_geometry.py` derives first-pass detachable-magazine
interaction data from the installed game's exact authored bindings. It uses the
same algorithm for each selected asset. It reads archives offline and performs
no game, graphics, inventory or ammunition operations.

The input is the output of `bc2_authored_grip_bindings.py` plus its exact mesh
binding file. The generator reopens the recorded archives and checks resource
types, archive indices, content hashes, typed weapon identity, configured state,
mesh, animation and skeleton references. The reload skeleton reference must
match both the resource path and instance GUID of the bound SkeletonAsset;
its content and Name must resolve to the same skeleton model and fingerprint.

## Measured data and interaction design

Measured data includes the constant closed part transform, rigid skin ownership,
bone topology, selected triangle hashes, dimensions and the asset's own static
left wrist/finger transforms. Raw vertex Z is reflected before applying the
canonical inverse bind. Output uses row-vector matrices in canonical metres.

A part is a candidate only when it is a uniquely suitable rigid weapon-root leaf,
has magazine-like dimensions and has substantial position-control displacement
in the exact authored `1P_Reload` resource. Bone numbers and the magazine reload
enum do not establish the role. Ambiguous or unsupported cases produce gaps.
This is geometric role evidence; visual review remains necessary before admission.
Reload control extent is a curve-control bound, not a verified native trajectory.

The carry grasp is deliberately generated VR design: the palm is aligned to the
lower third of the magazine's side, using that asset's static finger shape. The
rail follows its longest bone-local extent toward the end nearest the rigid
weapon body. The landmark uses the median of a 3 mm end band. These placements
are **not authored reload-hand poses**. The 100 mm rail, capture/release radii,
travel, dwell and blending values are shared interaction defaults recorded in
each profile. No dynamic spline interpolation is promoted to calibration.

## Local use

Generate the grip/mesh bindings first using the documented authored grip pipeline.
Then, for example:

```powershell
python tools/bc2_authored_magazine_geometry.py `
  --game "D:/Games/Battlefield Bad Company 2" `
  --grip-bindings installed-grip-bindings.json `
  --mesh-bindings authored-mesh-bindings.json `
  --asset XM8_sp_s --asset AEK971_sp `
  --output experimental-magazine-geometry.json `
  --header generated/Bc2ExperimentalMagazineGeometry.h --header-asset AEK971_sp
```

The header allowlist is separate from the inspection list and is mandatory when
requesting a header. It emits `fvr::bc2::generated::ExperimentalMagazineGeometry`
as an array of existing `MagazineGeometryProfile` values. Emission alone does not
enable a feature: an explicit experimental build registry must join the exact
asset to its admitted native reload family and existing current-owner guards.
The native family adapter continues to own ammunition and reload receipts.

Keep generated headers, matrices, extracted metadata and mesh images private
outside source directories. Source packages include the generator, tests and this
document; they exclude installed data and generated profiles by default.

## Current evidence and limits

An installed-archive replay produces two profiles and no gaps for the scoped XM8
and AEK. Offline orthographic plots of their assembled skins identify the selected
parts as the detachable magazines beneath their receivers. The XM8 closed transform
differs from the accepted calibration by 0.027 mm; its generated insertion landmark
differs by 7.1 mm. The synthetic grasp differs by approximately 73 mm and 65 degrees,
so it is not a replacement for the accepted XM8 calibration. The example header
emits AEK only, leaving the accepted XM8 geometry unchanged.

This is an experimental first-pass geometry path, not a native or headset result.
AEK insertion/carry comfort and collision clearance still require actual testing.
Curved, nonrigid, ambiguous, unusually oriented or unsupported magazines may need
explicit geometry overrides; unsupported assets receive no guessed profile.
