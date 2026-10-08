# ADS optics roadmap

2026-10-01 — source and saved-evidence review only. No runtime changes, native
writes, hooks, game input, session launches or foreground changes were made.

The requested result is a native magnified scene with the weapon's reticle and
relevant sight HUD visible inside its physical optic when brought to an eye,
while the surrounding world keeps normal head-tracked stereo. Preserve the
working XM8 optic, accepted grips and launcher sight mechanics. The user explicitly
excluded ACOGs from this change; do not apply a blanket scope or ADS patch.

## Open visual bug: ACOG red dot outside the sight picture

Reconfirmed by the user on October 1, 2026: the ACOG red dot appears to render
in front of the scope, leaving a red dead-pixel-like point visible outside the
intended sight picture. Earlier XM8 feedback described a stationary dot ahead
of the optic even when not looking through it. Exact asset/material/pass and
cause are unverified; this is a user-observed rendering defect, not a display
pixel diagnosis. Track separately from the fullscreen sniper/Carl Gustav work
and the deferred general LOD issue. Preserve the accepted view through the optic.
Acceptance for a future repair: both eyes see the reticle correctly through the
optic, while scope housing/aperture occludes it when viewed off axis. Deferred
optics polish; recording this bug does not enable blanket ACOG or ADS changes.

October 1 video evidence: `BF2142-VR-2026-10-01-12-52-43.mp4` actually shows BC2.
At 7, 9, and 14 seconds the red point is visible ahead of the broadside scope against the
wooden wall, with different pixel positions. This confirms a recorded off-axis
artifact. It does not establish depth-test failure: a forward reticle primitive
can be outside the housing silhouette while passing depth normally. Exact draw,
material, projection and attachment ownership remain unknown. Keep this defect
separate from sniper fullscreen zoom and from the UI laser. Next diagnostic is a
bounded broadside/through-optic draw-state capture for the exact selected asset.
See [video evidence](../reports/reticle-video-observation-20261001.json). No optic
rendering change is enabled by this report.

## Observations versus verified bindings

The user reports that sniper-type sights and a Carl Gustav aiming mode switch to
a fullscreen 2D presentation, while the XM8 scope already works well. These are
useful behavioral observations, not an exhaustive asset list or proof that all
snipers, launchers, Carl Gustav variants or ACOGs use the same rendering path.
Exact affected assets and modes still need capture.

The user also reports that pressing the currently bound left-trigger ADS reduces
visible gun detail or blurs the gun, especially XM8, despite physically aiming it
in VR. They request removal of manual ADS from the controller. The current candidate removes
only the physical trigger-to-`AlternateFire` mapping; trigger analog data remains
available for finger posing, and native Zoom remains a semantic capability for
future verified internal use. The cause of the visual change is **unverified**:
depth of field, texture mip bias, LOD, material/shader changes, alternate zoom mesh,
weapon `RenderFov`, or an overlay are competing leads, not diagnoses. This specific
ADS-triggered symptom must not be conflated with the deferred general LOD report.

| Existing evidence | Established fact | Limit for this work |
| --- | --- | --- |
| [BC2 input binding](../src/games/bc2/Bc2InputBinding.cpp), [controls evidence](CONTROLS-20260930.md) | Semantic `AlternateFire` maps to verified native `Zoom`; the original physical left-trigger mapping is removed at the user's request | A Zoom request does not prove ADS acknowledgement, optical magnification, reticle ownership, or identical semantics for every item |
| [AutoAdsPolicy](../include/fvr/interaction/AutoAdsPolicy.h) | Portable alignment/time hysteresis and lowering-to-rearm after native cancellation | Currently used by its core test, not integrated into BC2 gameplay. It has no eye-relief geometry, weapon/mode identity or optic classifier |
| [Saved weapon reflection](../reports/launcher-reflection-20261001.json) | `SoldierWeaponData` names `AimingController` (`SoldierAimingSimulationData`), `FirstPersonCamera`, `Hud`, `RenderFov`, `ZoomRenderFov` and `MeshShaderSetNumberOverride` | These are discovery leads. Units, consumers, affected camera/pass and live zoom state are not established. In particular, `ZoomRenderFov` must not be assumed to mean world magnification |
| [Camera copies](../src/games/bc2/Bc2Camera.cpp) | Verified BC2 view values can be copied with explicit pose/FOV and native cache rebuild requirements | No arbitrary extra scope-view lifecycle, render target or optic culling path is verified |
| [Projection math](../include/fvr/math/ProjectionOverride.h), `ProjectionContextHook` in [NativeProbe](../src/games/bc2/NativeProbe.cpp) | The verified first-person caller receives eye X/Y/W projection coefficients while retaining native clip Z; specific depth-bias paths have separate correction | No scope-pass classification exists. A magnified view must not accidentally receive the normal first-person projection replacement. This is an integration concern, not a diagnosed cause of the user's observation |
| [Saved pass audit](../reports/native-trace-20260928-123847-523/pass-projection-check.json) | 336 sampled passes: 306 recognized eye-camera layouts, 30 unknown, zero recognized wrong-eye samples | This was artifact investigation, not a sniper/Carl Gustav ADS capture or material/reticle binding proof |
| [Weapon profiles](../src/games/bc2/Bc2WeaponProfiles.cpp) | Exact SPAS, XM8 and scoped-XM8 launcher entries with independent aim/support/muzzle gates | No optic feature gate, scoped sniper profile, or verified Carl Gustav optic binding exists |

The native pass collector is particularly important to fix before drawing
conclusions from an empty capture: `CapturePassBuffers` only accepts a single
1920x1080 viewport, retains the first VS/PS/render-target/depth combination, and
copies VS/PS constant-buffer slots 0 and 1. It does not record shader-resource
textures, material identity, stencil/blend state, or later draws with the same
combination. A smaller offscreen optic target or repeated HUD draw can be missed.
Recent launcher traces, including
[113015-478](../reports/native-trace-20261001-113015-478/pass-buffer-evidence.json),
have this optional inventory disabled and an empty array; that proves nothing
about optic materials. No inspected saved capture identifies the reported
fullscreen reticle draw or a reusable magnified scene texture.

## Classify exact asset and aiming mode

Proposed `OpticProfile` data should be separate from the existing three-feature
`WeaponProfile` contract. Unknown profiles make no changes. Index bindings by
adapter/build evidence, exact item/attachment configuration, optic identity and
native aiming mode, not weapon display name or a substring such as `scope`. No profile reintroduces a manual left-trigger ADS binding.

| Classification | Default behavior | Additional evidence to enable a change |
| --- | --- | --- |
| Existing world/3D optic | Preserve its current rendering and input. XM8 is the accepted control; ACOGs are explicitly excluded | None for this task; any later modification needs its own requested scope and evidence |
| Fullscreen magnified scene plus overlay | Candidate for a bounded physical-aperture view | Native zoom state/view, reticle and HUD paths, optic transform/aperture, selective overlay suppression and complete restoration |
| Alternate targeting/weapon ADS | Preserve native behavior until separately characterized | Determine whether it is a magnified sight, range/lock/guidance display, alternate fire mode, or another mechanic; preserve its authoritative targeting state |
| Unknown | Leave native behavior intact | Capture and classify before enabling anything |

Track independent evidence for: native ADS command/state, separation of unwanted ADS visual effects, optical pose and
aperture, zoom semantics, view rendering, reticle/HUD extraction, compositing,
and headset acceptance. A muzzle bone, common skeleton, or shared shader does
not establish an optical axis or native view relationship. No existing lens,
interior tube or mesh surface is presumed. If required geometry is absent,
record that gap; a new local aperture/surface would be an explicit asset/render
solution with its own placement evidence, not a claim about the native mesh.

## Rendering design to prove

Keep three coordinate systems explicit: the unchanged XR eye/world view, the
weapon-local optical assembly, and its magnified scene view. Compose optic pose
from the same placed weapon/animation sample used for the visible gun, in metres
and the adapter's verified canonical basis. Reticle placement must agree with
native aiming/zeroing and projectile behavior; do not make the gun or shots snap
to head gaze to hide a mismatch.

For each eye, transform the eye into the optic frame. Evaluate measured axial
eye relief, lateral displacement, aperture visibility and angular alignment.
The optic needs an eye-conditioned projection/UV mapping and mask: moving sideways
should reveal housing/occlusion or leave the eye box, not keep a fullscreen
reticle stuck to the headset. Keep the non-viewing eye's ordinary world image.
Do not assume duplicating one centered texture into both lenses provides correct
near-object parallax. A single native scene texture may be a useful approximation
only if its projection, eye-box mapping and headset result are explicitly validated.

Recover native magnification and zoom transitions independently of first-person
weapon FOV. Calibrate known scene angles and reticle marks at each native zoom
state; the optic aperture's angular size also affects displayed magnification.
Do not simply divide the HMD FOV by a number taken from a weapon field.
World image projection and submitted XR eye pose/FOV remain coherent with their
rendering; Khronos documents that projection-layer poses/FOV normally derive
from located views. This plan changes the image inside the optic, not the headset's
whole view. [OpenXR projection-view contract](https://registry.khronos.org/OpenXR/specs/1.0/man/html/XrCompositionLayerProjectionView.html).

Choose a render path from evidence:

1. **Reuse an existing native optic scene target**, if one actually exists and can
   be sampled with verified lifetime, scene/projection identity and completion.
   Separate its reticle/HUD composition if the scene target does not include it.
2. **Create a dedicated render-only magnified subview** when native ADS changes
   the main camera and draws an overlay. This requires new BC2 view scheduling,
   visibility and target ownership proof. Render from the same simulation state
   as both eyes, without another tick, animation advance, input update or Present.
3. **Do not treat cropping the final eye image as recovered native optics.** It
   can lose detail, scene coverage and correct optic perspective; a flat copy of
   the whole game's final image is not the requested result.

Composite only the optic scene, native reticle and relevant sight HUD through its
verified aperture, with housing/depth occlusion and the proper per-eye transform.
Keep ordinary HUD, menus, damage indicators and unrelated crosshairs in their
existing paths. A dynamic native range/lock indication must retain its live values;
copying a static texture is not an equivalent implementation. Native ADS may also
change weapon presentation or postprocessing: requesting native aiming for a
future optic must not silently restore the current blur/detail-loss behavior.
Identify its exact consumer/state and separate authoritative ADS behavior from
unwanted visual transitions. Suppress only an individually verified effect in
its owned view/path, with exact restoration. Do not disable global depth of field,
all postprocessing, mip behavior or LOD based on the symptom alone. If gameplay
ADS cannot yet be isolated from the visual change, keep that optic capability
disabled rather than present the coupling as solved. Determine whether
the reticle is a texture, geometry, procedural shader or UI draw before selecting
a method. Prove suppression of the exact original fullscreen component only after
a replacement is valid; no shader-wide or global HUD disable.

Scope visibility may need its own frustum or a conservative union with the world
eyes, including native near/far/depth conventions. Existing world culling is not
proof of optic coverage. Isolate or correctly scope temporal history, jitter,
exposure, depth, LOD reference and view caches; otherwise the extra camera can
contaminate ordinary eye views. Keep the deferred general LOD issue separate:
only investigate changes demonstrably introduced by this optic path.

All targets/resources have device, owner, frame and equipment generations. Observe
native graphics-thread rules; avoid live CPU readback or blocking waits. Save and
restore all changed native and D3D state, including resource slots implicitly
unbound by target changes. D3D11 can null conflicting read/write bindings during
`OMSetRenderTargets`, so restoring the target alone is insufficient.
[Microsoft render-target binding rules](https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11devicecontext-omsetrendertargets).

## Raising the optic and preserving gameplay

Extend portable ADS policy with a coherent sample containing actor/item/mode,
tracking-space and pose generation, optic geometry, both eyes, and authoritative
native ADS state. Use distance and alignment entry/exit hysteresis with dwell;
choose thresholds from the verified eye box and headset testing. The existing
`AutoAdsPolicy` alignment thresholds are not physical eye-relief measurements.
Cancel acquisition on invalid tracking, equip/owner/space change or stale geometry.
A reload/sprint cancellation must not repeatedly reacquire while still held to
the eye; lowering/rearming remains intentional.

The adapter requests native ADS and observes its acknowledgement; it does not
invent readiness, spread, recoil, projectile direction or ammunition. Preserve
native alternate-targeting behavior for unclassified
items without recreating the removed controller ADS binding. Automatic eye-proximity ADS is a separate future opt-in capability, not a replacement left-trigger action. Recenter changes the shared reference, not the weapon-to-optic calibration.
No automatic ADS should alter the accepted XM8/ACOG path in this increment.

## Next isolated discovery experiment

**Proposed, not executed by this research task:** capture one exact affected
weapon in a controlled campaign scene, with the user choosing/equipping it or a
separately verified selection mechanism. Do not assume the current loadout has a
sniper or Carl Gustav, or borrow historical item pointers.

1. Fresh-check build, actor/equipment identity and reflected field relationships.
   Record actual asset/mode, attachments, units and native camera/aim/HUD objects.
2. Capture short, separately identified phases: hip view, ADS entering, settled
   ADS, ADS leaving, then one ordinary native cancellation. Initially observe
   native aiming in a separately authorized diagnostic or unmodified baseline rather than restore the removed VR trigger mapping or add eye-proximity behavior. Use the same scene and
   pose for comparisons; retain original authored rig/weapon data.
3. Add a bounded diagnostic inventory that observes relevant viewport sizes and
   repeated draw/state changes, with capped event counts and explicit drops.
   Record view/projection/target identity, descriptors, shader identity, texture
   resource bindings, relevant constants and blend/depth/stencil state. Any new
   observation hook needs its own ABI/ownership proof. Readback uses private
   staging and later completion, outside the active render work.
4. Correlate Zoom state and native field consumers with changed views/draws:
   which image is magnified, where the reticle/HUD is generated, whether the gun
   is hidden, and whether a physical optic surface remains. Record ADS-related postprocess, mip/sampler, material, mesh/LOD and weapon-projection changes without assuming their cause. Identify the precise
   source/destination relationship; a screenshot or shared shader alone is not enough.
5. Repeat the same observation on unchanged XM8 as a negative control. Keep ACOG
   untouched. Save an evidence matrix and proposed narrow binding, or an explicit
   unknown when the observation cannot distinguish competing paths.

This experiment's deliverable is classification and binding evidence, not an
optics patch. Full GPU inventory can affect timing, so compare diagnostic and
ordinary runs and do not report capture overhead as normal gameplay performance.

## Integration stages and acceptance

| Stage | Implement only after preceding evidence | Required checks |
| --- | --- | --- |
| Portable geometry/policy | Optic-local eye tests, aperture projection, hysteresis, profile and identity gates | Per-eye asymmetry, lateral/axial exit, head+gun moving together, stale/equip/space cancellation, native cancellation/rearm; unknown/XM8/ACOG remain unchanged |
| Native image acquisition | Verified existing target or isolated render-only subview for one exact asset/mode | Correct magnification/reticle, same simulation frame, culling and lifetime evidence, no temporal cross-contamination, clean failure restoration |
| Physical compositing | Per-eye aperture and selective original-overlay replacement | No whole-world zoom, no reticle outside aperture, correct housing occlusion, relevant native HUD retained, no unrelated HUD suppression |
| Eye-proximity activation | Native ADS request/ack, visual-effect separation and cancellation integration | No manual trigger ADS; no repeated press edges; native shot/aim behavior preserved; unwanted ADS blur/detail transition excluded by evidence; reload/sprint/equip/recenter and tracking interruptions recover |
| Expansion | Batch evidence for related optic families and exceptional modes | Reuse policy/render mechanisms; verify each asset's transforms, view/reticle bindings and capabilities rather than copy offsets or guess class names |

Measure additional CPU draw/visibility work, GPU time, target memory, frame age,
and dropped/late pairs at the actual resolution/refresh rate. A dedicated subview
can add one or more scene renders; a small target does not remove CPU submission
or visibility costs. Start with bounded allocation and render only when the
profile/eye state needs it. Choose resolution from projected aperture and measured
quality; decide single-view versus per-eye work from evidence. Budget failure
must leave normal world stereo and native state correct and must not show a stale
scope from another item/frame. Define a bounded fallback before enabling the
pilot; reduce only optic quality/work where validated, never replay simulation or
stall the XR thread indefinitely. No ETA is inferred from GPU model or game age.

Final acceptance needs headset checks with both eyes, fast head/gun movement,
eyes entering/leaving the box, native aiming/firing, interruptions and unchanged
XM8/launcher behavior. This document supplies a plan, not a claim that magnified
physical optics are already implemented.

## Framework fit

Share optic geometry, eye-box/ADS policy, capability schema and rejection tests
across adapters. BC2 owns actual zoom/HUD/material/view/native-fire bindings;
BF3/BF4 and later ports supply their own. Extend the
[weapon capture pipeline](WEAPON-PROFILE-PIPELINE.md) with optic configuration and
phase-tagged evidence rather than repurposing grip candidates as optic profiles.
Follow the [Frostbite porting gates](FROSTBITE-PORTING-ROADMAP.md). Body placement
and optic-local pose must agree with the [full-body plan](FULL-BODY-IK-ROADMAP.md),
while a [desktop/remote visualizer](FLATSCREEN-VISUALIZER-NETWORK-PLAN.md) need not
render another player's private magnified sight view.


## Implemented input correction (separate from this research)

The parent integration removed only the physical left-trigger ADS mapping and
preserved analog finger posing. Both architecture suites pass. Native115047-514
records full left-trigger pressure without an AlternateFire request, while both
launcher gestures still work. The run delivered240 pairs with2 recovered timeouts;
visual blur removal is not headset verified. See
[hand candidate and provenance](HAND-INPUT-RELOAD-20261001.md).
No optics rendering or visual-effect suppression was implemented by this correction.
