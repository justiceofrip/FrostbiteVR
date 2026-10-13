# Optional converted prop materials

Subsequent360 actual-game captures now show the converted SCAR chest magazine
in both eyes after a deliberate shoulder draw.361 adds reusable inspection input.
The unsuccessful earlier views and startup crashes remain evidence;
[see the exact results and limits](BODY-INSPECTION-361.md). The historical
startup status at the end of this document describes359's original attempts.

The host can now render explicitly converted albedo textures on the existing
body-prop geometry. The shared renderer accepts triangle-order texture
coordinates, declared linear or sRGB storage, wrap or clamp addressing, and
validated mip levels. It owns immutable GPU resources and restores the caller's
graphics state, including conflicting texture and sampler bindings. The opaque
path uses RGB; texture alpha does not introduce transparent geometry.

`BodyPropMaterials` loads an explicitly requested private sidecar. Each section
must match its geometry key, section index, source skin/position fingerprints
and exact output vertex count. A bad later assignment rejects the entire
replacement. Unknown assets, duplicate assignments, malformed mips, invalid
coordinates and trailing data reject. File, texture, section and allocation
bounds are explicit. No material names or native pointers enter the shared
renderer. Existing geometry caches remain unchanged.

The OpenXR host and diagnostic receiver accept `--body-props-materials` alongside
the existing geometry-cache argument. Supplying an invalid material cache fails
explicitly; omitting the option preserves the plain-color path. No material
sidecar is automatically discovered or included in the player launcher.

## Validation

Both full normal builds pass245 suites. Additional software-rendered checks on
both architectures verify actual texel sampling, negative/repeated coordinates,
wrap/clamp differences, sRGB conversion, supplied mip selection and retained
uploads after invalid replacement. Textured geometry also passes both-eye
world/first-person depth composition, self-depth and atomic expiry/failure.
These tests use software D3D11 and do not establish headset acceptance.

Four Python writer checks cover the binary layout, selected-part triangle
ownership and reflected winding, mip bounds and rejected ambiguous data.
Three material-baker checks cover bilinear texel centers, addressing,
barycentric correspondence, separate charts and invalid input. The CPU loader
also rejects every truncated prefix of a known-valid cache and verifies that
the source catalog remains untouched.

`BodyPropAssetPreview` renders installed private geometry/material caches in
software without starting BC2 or OpenXR or sending input. Its comparison output
uses the same renderer as the host. It is a diagnostic view with simple lighting,
not an authored weapon pose or controller calibration.

## Conversion progress and limits

`body_prop_materials.py` serializes reviewed converted data. The independent
`rigid_prop_material_bake.py` evaluates a supplied base-color function onto a
bounded triangle atlas, preserving the source-to-output vertex correspondence.
It has no weapon names or shader-selection heuristics. UV mapping, shader role
and material interpretation remain the BC2 asset adapter's responsibility.

The first private conversion follows one reviewed Ammo shader's exact
coordinates. It covers cartridge geometry for two SCAR configurations, not the
large magazine casing. Side-view comparisons change only0,1 and59 pixels; this
was retained as insufficient visible evidence, not called a gray-magazine fix.

A second private conversion bakes the casing shader's static base-color portion
from five explicitly bound texture channels. It excludes lighting, environment
reflection, normal mapping and specular terms. The exact reviewed pixel-shader
hash and source textures/coordinates are recorded. The three casing views change
19277,27269 and21882 pixels; unassigned assets remain byte-identical. The visible
casing gains surface detail. The same code processes both matching shader
configurations without an asset-name switch in the renderer.

These private sidecars are **previews only**. Native SRV color-space selection,
active pass selection and complete material shading remain unverified. Baked
multi-part holster/pistol geometry still needs its explicit output-vertex map.
The example shader recipes are not full weapon-material coverage. No extracted
pixels, shader bytecode or game geometry belong in public source archives.

Normal359 binaries are frozen separately. The ready player package remains
357, whose combined gameplay evidence is in
[authored pump validation](AUTHORED-PUMP-HAND-357.md). The passive actual-game
material/depth check could not start: two fresh launches exited before any VR
or material DLL was attached. Their first-chance faults have EIP0 and an
`amdxx32.dll+0x1066ab` stack return, matching the previously observed relative
site. That does not establish the producer or root cause. The campaign-Continue
helper did run, so these are not entirely unmodified-game baselines. Dumps and
module maps are saved under `materials359-start01/exceptions` and
`materials359-start02/exceptions` in the private recovery directory. No third
launch, driver change, graphics-setting change or speculative patch was made.
