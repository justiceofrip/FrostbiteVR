# Measured linear mechanisms — offline pipeline376

The shared pump adapter introduced in375 now has an offline measurement tool
for other authored moving parts. `tools/bc2_authored_linear_mechanism.py` consumes
an exact skeleton, motion clip, reference clip and explicitly named part. It
reports an axis, reference transform, lateral/rotation bounds and the decoded
movement range, with source hashes. It does not enroll a native pump or infer
that every moving part is a pump. Normal player375 remains the headset candidate.

## What the tool establishes

The selected part must be a named direct-child leaf of the weapon root. The
tool validates the hierarchy, required tracks and rigid ancestors before
cancelling common ancestry. Per-knot quaternion control hulls prove that the
repository decoder can normalize the common rotation throughout each span;
large common rotation therefore does not falsely reject a straight local rail.

For supported degree0–2 curves, each knot interval becomes a polynomial. Its
endpoints and interior stationary points bound projected travel. The final
interval is clipped to the clip duration, including quantized knots slightly
beyond that duration. One-sided knot limits are included. Lateral movement is
bounded by the entire control hull. These are bounds on this repository's
decoder, not proof of the native interpolation or active animation-tree state.

The reference pose need not be a closed stop. Negative and positive travel
relative to it are retained; an interior reference is explicitly reported.
No automatic clamp, guessed closed boundary, native chamber event or ammunition
authority is generated. The older support-reference bounds remain unchanged.

## Installed-data results

Fourteen exact reference configurations reduce to four distinct asset/mesh/clip
jobs. All input documents, binding digests, archive indexes, skeleton, reference,
mesh and LOD data are checked against their existing hashes before measurement.
The selected motion filename is `1P_BoltAction.res`; its filename alone does not
prove active animation-tree or gameplay semantics.

| Asset / named part | Reference-relative travel | Geometry observed in private preview |
| --- | --- | --- |
| 870MCS / jntWpn_4 | −9.704 to +94.526mm, approximately +Z | Fore-end region,1952 rigid triangles |
| 870MCS / jntWpn_5 | −8.602 to +83.912mm, approximately +Z | Receiver/bolt region,136 rigid triangles |
| NS2000 / jntWpn_4 | −7.204 to +183.142mm, approximately −Z | Fore-end region,984 rigid triangles |
| SPAS12_sp | No motion candidate from this bundle | Exact reference bundle lacks the selected absolute clip |
| T194_sp | Rejected before curve decoding | Nonzero animation-wrapper field is unsupported |

Six 870 configurations share their selected geometry/clip contents, as do six
NS2000 configurations. Their configuration digests remain separate. The private
side/top point previews place the selected piece on the assembled weapon and
show both extrema. The component descriptions above are visual interpretations,
not a native role binding. Full-range sizes are about104.230mm for the 870
fore-end and190.346mm for the NeoStead; neither is automatically adopted as the
runtime stroke. The existing measured SPAS calibration remains unchanged.

The TOZ wrapper cannot safely be treated as the ordinary wrapper with a few
bytes skipped. Its leading words are `10,1,41,1,...`, compared with the supported
`10,0,string_count,...`. Identifying that prefix and following SPAS's actual clip
dependencies are separate tasks. Rejected reports are retained alongside the
final measurements.

## Reuse

For separately extracted local resources:

```text
python tools/bc2_authored_linear_mechanism.py --clip motion.res --reference-clip reference.res --skeleton skeleton.res --part jntWpn_4 --output rail.json
```

The destination must be new. Reports explicitly mark native admission,
closed-boundary verification, chamber semantics and runtime enablement false.
Game resources and the generated geometry previews remain private and are not
included in the source snapshot. Asset paths/hashes are references only.

## Validation and next connection

All144 authored-data tests pass, including11 new mechanism tests for extrema,
opposite/diagonal axes, shared rotation, malformed hierarchies, interior
references and rejection paths. Both full normal builds pass246 suites
(x8621.53s, x6415.02s). The native source hash is unchanged from375. No game was
launched for376 and no headset claim is made.

Private evidence is in `bc2vr-recovery/mechanism-rails376-verified`: `rails.json`,
`visualization.json` and three part previews. The preceding rejected surveys are
preserved. Tooling/source checkpoint376 is separate from the frozen normal375
player stage and its actual-game evidence.

To connect another pump family, establish its live closed reference and native
shot/cycle boundaries, bind its exact selected configuration to this measured
part data, then run the existing shared gesture/recovery and game checks. A
common skeleton or a successful support grip alone grants none of those steps.
