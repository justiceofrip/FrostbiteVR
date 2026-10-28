# Exact optic configuration routes

The installed campaign M95, QBU88 and Carl Gustav configurations explicitly use
`MeshZoom1p = Objects/Weapons/Handheld/Scope/Scope_Mesh`, with **null zoomed and
nonzoomed lens filters**. Their native fullscreen-looking optic path therefore
cannot be discovered by observing only `SniperLensScopeFilterData` calls.
The existing lens-filter observer remains useful for its own consumers, but an
absence of those callbacks is not evidence that these weapons have no optic.

`bc2_authored_optic_catalog.py` now joins exact configuration GUIDs to aiming
controllers, zoom levels, first-person meshes, zoom meshes, scope filters and
HUD crosshair IDs. It uses existing bounded DBX/archive readers and emits metadata
only. Known malformed fields/types/duplicate references reject; missing scalar
fields stay explicit. Null filters and unresolved references are different states.
The exact-index ArchivePool permits the existing bounded mp_common extraction;
there is no unbounded archive-reader limit change.

## Installed extraction

| Archive | Configurations | States | No zoom mesh/filter | Zoom mesh only | Zoom mesh + filter | Gaps |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| sp_common | 127 | 128 | 118 | 8 | 2 | 0 |
| mp_common | 276 | 277 | 254 | 20 | 3 | 0 |

These are definitions and variants, not distinct weapon counts or runtime
acceptance. The two archives overlap; they are not403 different guns. DLC/other
archive coverage is not implied.

The common zoom mesh is referenced by exact base sniper definitions including
M24, SV98, SVU, GOL, VSS, QBU88 and M95, as well as particular launcher and MG36
configurations. Meanwhile M95 ACOG variants use a separate ACOG first-person mesh
and no such zoom mesh. Shared weapon names and classes therefore cannot choose
the rendering policy. A serialized GP30 shotgun mode also names its ordinary gun
mesh as MeshZoom1p; a nonnull field alone does not prove a fullscreen scope.

The camera-related fields are retained separately. For example, base campaign
M95/QBU88 zoom levels serialize FieldOfView55 and11.5, while Carl Gustav serializes
55 and30. These are authored values; no unit/axis conversion or magnification
ratio is claimed without the actual native projection consumer. RenderFov and
ZoomRenderFov are not substituted for those zoom-level fields. Zoomed HUD IDs are
also distinct (`sni_m95`, `sni_qbu`, `rl_cg`); no common geometry is mislabeled as
all weapons' reticle.

## Exact shared zoom-mesh evidence

The M95, QBU88 and Carl Gustav async bundles contain byte-identical scope mesh
resources and buffer data. This establishes reusable asset geometry, not a live
draw owner or selected aiming state:

- MeshSet SHA256: `9d3692846fb330400f8e37358da48115626f3bce9d6b65ae484ea17b0899f444`.
- LOD0 data SHA256: `156cf8d63f5ea244c6788b9acfba059f28815fa48849f6ef0e05380c288bf6ef`.
- `jntWpn_0_tube`:754 triangles, stride48, first index0.
- `jntWpn_0_blur`:50 triangles, stride48, first index2262.
- `jntWpn_0_tube_ZOnly`:754 triangles, stride16, first index2412.

All three sections reference the authored `jntWpn_0` skin identity; this is not a
GPU constant slot. The shader references are `Scope/Shaders/tube` and
`Scope/Shaders/blur`. There is no reticle section in this measured mesh. The
crosshair/HUD consumer must be located separately; the catalog does not establish
whether it is texture, UI or procedural rendering. The blur section is a concrete
capture target, not proof of the user's separate XM8 ADS blur symptom.

## Next useful native check

Use a freshly equipped exact campaign M95, QBU88 or Carl Gustav with its ordinary
native zoom presentation. Retain current weapon/actor/equipment/space identities
and original configuration leases. Current `SelectedMeshesSnapshot` publication
describes Meshes1p; the existing filter callback reads MeshZoom1p only while that
callback executes. For these null-filter routes, a separately validated read-only
zoom-mesh snapshot must be obtained from the current WeaponStateData before
joining draw evidence.

Capture the exact three mesh sections and HUD crosshair draws together with the
actual camera/projection and view identities through hip, entering, settled and
leaving zoom. Resource/geometry equality alone cannot select the player's draw,
acknowledge ADS, prove an offscreen magnified target or authorize suppression.
If the game changes the main camera, native render-only scope views still need
their own scheduling/culling/cache ownership proof; cropping the normal eye image
would not recover native detail. Working XM8/ACOG rendering and the removed
left-trigger ADS binding remain unchanged.

## Validation

Ten focused Python groups pass, including null-filter zoom-mesh detection,
independent FOV/HUD fields, exact variant identity, malformed/duplicate scalars,
wrong mesh type/GUID/Name, missing data and reference closure. Both actual archive
catalogs complete with zero unresolved configurations. The shared zoom mesh was
read from three explicit installed bundles with identical hashes. This is offline
source/asset evidence: no process, hook, GPU, scope render or headset test occurred.
