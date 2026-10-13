# Body-prop scene depth investigation340

This is diagnostic infrastructure, not a shipped occlusion fix. The existing
host compositor gives props their own depth buffer, which sorts props against
one another but cannot hide them behind BC2's gun, arms or world. The reported
gray SCAR magazine is normal physical size. Shrinking or moving it is not a
substitute for scene depth and material work.

Normal x86 and x64 builds pass all244 suites with
`BC2_SCENE_DEPTH_CAPTURE=OFF`. Frozen normal binaries are in
`bc2vr-recovery/normal340-player-checkpoint`. The private x86 build enables a
one-pair depth/color capture at the owned post-draw boundary and records the
corresponding prop world/view/projection matrices. It only copies resources and
polls staging memory without waiting; it adds no draws, dispatches or target
bindings to BC2. Capture success is independent of mechanics-test completion.
Private evidence and installed geometry must not be distributed as source.

## GPU experiments

`depth340-sharing` verifies creation and import across two hardware D3D11 devices:

| Format | Legacy shared | Keyed NT handle |
| --- | --- | --- |
| R24G8 typeless (BC2 scene format) | Succeeds | E_INVALIDARG |
| R32 typeless | Succeeds | Succeeds |
| R32 float | Succeeds | Succeeds |

Creation/import alone does not prove transferred contents. `depth340-copy`
tests a1920x1080 depth/stencil pattern with a completed producer event query and
a completed consumer event query. Its original bit-exact assertion fails on
518397 of2073600 words. The bounded follow-up `depth340-copy-detail` establishes
zero stencil differences and a maximum depth difference of one24-bit unit.
Keep both results: the path is not bit-exact, and no BC2 projection association
is implied by this standalone test.

The intended copy path requires matching resource types/format families and
explicit GPU completion. Reference: [D3D11 CopyResource](https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11devicecontext-copyresource)
and [D3D11 event queries](https://learn.microsoft.com/en-us/windows/win32/api/d3d11/ne-d3d11-d3d11_query).
Do not equate the CPU finishing CopyResource with the GPU finishing that copy.

## Integration requirements

Before enabling host depth comparison, verify that the captured scene depth
corresponds to the same eye and projection as its color. BC2's first-person
projection retains distinct depth terms; world projection alone may be wrong
at weapon/hand pixels. Offline comparison images are candidate analyses, not
visual acceptance or native-depth proof by themselves.

Any eventual shared-depth transport must preserve pair atomicity and retirement
on scene/size/format changes. In the existing legacy-fenced path, producer depth
copy must precede the color publication fence; consumer depth copy must precede
the color read-completion fence and acknowledgement. The keyed path does not
have that same CPU completion guarantee. It needs its own proven ownership
mechanism, not reuse of legacy assumptions. No such transport is enabled in340.

The disk filled during symbol staging after both successful normal builds.
Generated test PDB files were losslessly compressed in place; no game files,
source or evidence were deleted. Staging resumed and all four normal binaries
and symbol files were hash-verified in `normal340-player-checkpoint`.

## Captured BC2 result

`depth340-native01` captured both1920x1080 color/depth eyes and exact canonical
prop transforms. The post-draw D24S8 buffer contains the world but not the
first-person gun/arms; stencil is zero throughout. The magazine projects outside
the captured viewport, so this pair does not establish its occlusion behavior.
Directly using this buffer cannot solve clipping against the gun/hands. No shared
depth transport was enabled. The next diagnostic observes distinct bound depth
targets during indexed drawing and copies them at the same owned eye boundary.

The input-driven sequence failed at shell start after a completed manual pump.
See [341 shell-start investigation](SHELL-START-341.md); preserving these failed
results is necessary even though the depth capture itself completed. Later
recorded SCAR and SPAS frames show intact assemblies, but the first depth-pair
color contains a transient weapon deformation. That isolated frame must not be
presented as either normal appearance or proof that all visual defects are gone.
