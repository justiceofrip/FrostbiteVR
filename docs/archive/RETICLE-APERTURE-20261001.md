# ACOG aperture correction — October 1, 2026

The detached red point in the user's 12:52:43 video is consistent with the
installed ACOG's authored red-dot section. Its bind-space geometry sits at least
80.498 mm forward of the scope housing. Exact runtime draw ownership still needs
the bounded collector; this is not yet a headset-validated rendering correction.

## What is implemented

`OpticAperture` is a reusable per-eye visibility policy. It constructs convex
cones from up to two measured aperture rims, then clips a conservative finite
reticle box against them. It reports Visible, Partial, Hidden or Invalid.
Only Hidden grants whole-draw rejection. Partial preserves native rendering
unless a subsequent exact shader/stencil integration clips fragments. Treating
all eight box corners as outside is insufficient: the cone can cross the box's
interior, and the regression covers that case.

The BC2 profile measures both ACOG glass rims and the red-dot bounds directly
from the installed archive. It does not alter asset files, general depth state,
ADS, magnification, the housing or the accepted transparent lens rendering.
The packed-skin helper converts each rendered eye into original asset bind
coordinates using the exact submitted skin transform, including inverse bind.
A centre-eye approximation would incorrectly show the dot to the other eye.

## Exact resource and candidate section

- Archive: `Dist/win32/levels/sp_common/level-00.fbrb`.
- Mesh: `Objects/Weapons/Unlock/ACOG_4X/US_ACOG_4X_Mesh`.
- Material: `Objects/Weapons/Unlock/ACOG_4X/Shaders/ACOG_RedDot`.
- LOD0 section `jntWpn_10_ACOG_RedDot`: 4 triangles / 12 indices / 8 vertices.
- Resource-relative first index 5331, vertex byte offset 165240, stride 68.
- Vertex layout: float3 position, four uint8 skin indices, four uint8 weights.
- Every vertex is rigid weight255 to section palette slot0, asset palette ID35.
- Name hash `f7315722` resolves `jntWpn_10`; this is not a GPU constant slot.
- MeshData SHA256 `801361ce3160ef3392a7bb3317d77ed70b8631fb9abc661b67517314059a36a9`.
- Ordered indexed position12+skin8 FNV1a64 `1b481b02516969b5`.
- Ordered indexed position12 FNV1a64 `9ad7a905a35a527d`.
- Ordered indexed20 SHA256 `930b87a3d77619f5c1f7a0528f466ccaaac0c0358ff9b809d0ca2f2eeffe47f2`.

The same bone controls housing, glass, reticle and ZOnly geometry. A collapsed
bone workaround would remove the scope. LOD1 and LOD2 contain neither glass nor
reticle sections; this feature must not force LOD0 or create replacement dots.

The complete derived bounds/material/hash report is
`reports/acog-aperture-asset-evidence-20261001.json`. The read-only inspection
tool validates all three MeshSet LODs, both material variants and matching buffer
sizes. No original vertex/index/texture buffers are saved or bundled.

## Native integration sequence

1. In the existing bounded DrawIndexed collector, use count12/stride68 solely
   as a cheap candidate filter. Copy the exact indexed vertex data asynchronously
   into private staging and verify both ordered fingerprints. Account for native
   VB/IB offsets, baseVertex and index rebasing; raw API startIndex need not5331.
2. Retain current world/request/view/native-frame/eye and exact selected item,
   owner generation, equip/space and Meshes1p asset identity. Geometry equality
   alone also matches another actor's scope and does not establish ownership.
3. At the verified first-person PackHook, find `jntWpn_10` by unique rig name and
   hierarchy. Retain its EXACT 48 bytes from the actual source chosen for that
   copy: private posed palette or native fallback. Do not substitute a nearby
   pose, the latest published pose, an assumed rig index35, or just a world joint.
4. Join the immutable skin request to the candidate draw/constant buffer.
   Byte-matching the packed matrix in a CB is useful evidence, but duplicate
   matrices or an unrelated request are not an exact association. Keep the
   collector's association flags false until this link is verified.
5. Use the actual per-eye canonical LH camera translation for that draw and
   `AcogEyeInBindSpace(packedSkin, eyeWorld)`. Decode column-packed skin to a row
   matrix; canonicalize by C*M*C, where C flips Z; take its exact near-rigid
   inverse; transform the world eye; flip output local Z to authored bind space.
   The measured BC2 resource uses metres. No arbitrary engine scale is inferred.
6. Evaluate both profile apertures. Once all identity/lifetime checks pass,
   omit only this exact reticle draw when Hidden. Call original exactly once for
   Visible, Partial, Invalid, unknown ownership, missing data or stale generation.
   Never alter a shared native animation palette or run simulation again.
7. Validate through-optic and broadside views in both eyes, switching and
   reconnect/owner invalidation, preserved housing/glass and normal ADS behavior.
   Partial aperture-edge clipping is explicit follow-up; conservative whole-draw
   rejection fixes fully off-axis visibility without inventing a shader mapping.

## Validation and recovery status

Before approval-review failure, the original project received the portable policy,
exact BC2 profile, inspection tool and two test suites. Focused x86 and x64 runs
passed using /W4 /WX. The original does NOT contain the later packed-skin helper.

Automatic approval review then failed due a usage limit, not an unsafe-action
judgment. The rejected write did not execute. The packed helper and its tests
were completed in the allowed recovery workspace only. Both focused suites pass
again on x86 and x64, including asymmetric rotation/translation, handedness,
near-rigid animated bases, malformed bases, per-eye independence, convex clipping,
partial intersections, second-aperture occlusion and invalid input preservation.

The full reviewable staged project is under
`<local-recovery>/staged`; no staged change has been applied back to
`<local-workspace>`. No live gate, native reticle draw rejection or headset claim is enabled
by this document or by passing the deterministic tests.
