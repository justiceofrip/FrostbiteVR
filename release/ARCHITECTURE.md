# Framework architecture and Frostbite porting

This source tree separates reusable VR behavior from native engine integration and title rules. BC2 is the reference adapter. BF3, BF4 and Hardline are proposed next adapters; none has a verified native binding here. Other Frostbite titles need the same investigation. A shared engine name does not establish binary, renderer, skeleton or gameplay compatibility.

This document describes source boundaries, not current release acceptance. Use `FEATURES.json` and the release notes for enabled features and known issues. Paths below are relative to the exported source root.

## Module map

| Layer | Existing source and responsibility | Port-specific work |
| --- | --- | --- |
| Math and frame policy | `include/fvr/math`, `include/fvr/runtime`: stereo views, visibility envelope, complete-pair and original-pose presentation rules | Native coordinates, scale, depth convention, camera/scene ownership and safe render scheduling |
| Tracking and interactions | `include/fvr/interaction`: action edges, recenter/body frames, movement, `HandInteraction`, support grip, `SightFlip`, `BodyInventory`, hand poses and touch | Native actions, collision response, item identities, mechanism contacts and authoritative completion |
| Rig solving | `include/fvr/interaction/ArmIk.h`, `TrackedRig.h` and `RigPose.h` in that directory: independent wrist targets, anatomy, parent topology and pose generation | Skeleton/bind validation, semantic joints, palm axes, attachment frames and a safe native publication boundary |
| Native discovery | `include/fvr/engine`, `src/engines/frostbite`: bounded PE/signature and binding checks | Exact ABI, instruction/call-site evidence, object ancestry, thread ownership and lifetime for each title/build |
| Transport and graphics | `include/fvr/ipc`, `include/fvr/graphics`, `src/ipc`, `src/graphics/d3d11`: versioned requests, complete image pairs, GPU ownership and synchronization | Verified native graphics-thread/device handoff; a different backend or format needs its own interop implementation |
| XR and platform | `src/xr`, `src/platform/windows`: OpenXR sampling/presentation, menu pointer, feedback and Windows IPC | Runtime/backend compatibility and title menu/input integration; this layer does not own game ammunition or animation |
| BC2 adapter | `src/games/bc2`: native discovery, rendering, gameplay, rigs, reload flow, weapon families, menus and vehicle bindings | All BC2 addresses, layouts, x86 calling conventions, input IDs, mesh fingerprints and native operation semantics stay here |

`FvrCore`, `FrostbiteDiscovery`, `FvrFrameChannel`, `FvrD3D11FrameBridge` and `FvrOpenXR` are separate CMake targets. The interaction/math core has no native game-pointer or OpenXR SDK dependency. Platform and graphics modules have explicit Windows/D3D11 dependencies. Despite its name, `include/fvr/engine/FrostbiteCamera.h` implements the observed BC2 camera conversion; it is not a universal Frostbite memory layout.

## Existing contracts and unfinished adapter seams

`include/fvr/runtime/FrameCoordinator.h` defines internal C++ `IRenderAdapter`/`IStereoSink` contracts: verify capabilities and ownership, save state, prepare stereo visibility, render two eyes without advancing simulation/animation again, restore, then submit a complete pair. BC2's working native path is staged across callbacks in `NativeProbe.cpp`; it is not a direct implementation of that synchronous interface. Preserve the native scheduling model when extracting common services.

`include/fvr/runtime/FrameProvider.h` is the presenter boundary. `StagedFrameProducer` acquires a request before visibility and publishes the restored complete pair; delayed images retain their original poses, field of view and source deadlines. `PairConsumed` reports GPU consumption, not successful headset presentation. Menus have a separate channel and must not be substituted for missing stereo world images.

`include/fvr/engine/ModuleApi.h` is currently a fixed-width, versioned **metadata** ABI. `BC2Adapter` advertises no native capabilities through that metadata module; the opt-in `BC2NativeProbe` path provides the working integration. There is no finished public render/gameplay plugin ABI yet. A future cross-DLL API needs explicit sizes, versions, ownership and matching architecture: never pass STL objects, exceptions, borrowed native pointers or allocator ownership across it. The x64 XR host does not prove a future x64 game's hook ABI.

## Scale weapons through families and measured data

Implement each mechanism family once, then supply validated content profiles. Do not duplicate the whole VR interaction for each weapon.

- `WeaponProfile.h` records identity/revision, model axes and separate evidence for aim alignment, support grip and translated muzzle. It does not grant reload or mesh-hiding authority.
- `HandInteraction.h` supplies shared hand claims. `AmmoSupply.h` represents existing native reserve and exact reservations. `ReloadInsertion.h` provides capture, alignment, guided travel and seat recognition. `ManualReload.h` turns physical operations into one-shot requests awaiting exact acknowledgements. `DetachableMagazine.h` composes removal and replacement; `FeedMechanism.h` supplies additional feed/cover prerequisites, not native LMG support.
- The BC2 SPAS and scoped XM8 consumers are reference implementations of different native ammunition mechanisms. `Bc2ReloadInteraction.cpp` and `Bc2Xm8MagazineCalibration.h` contain measured profile data; `Bc2ReloadFlowRuntime.cpp` owns the title's native lifecycle. A shell insertion and a magazine refill must not share a fabricated completion rule.
- Capture original settled attachments before VR posing; retain units, rig topology/bind fingerprints, asset identity, observation time and provenance. Reuse family calibration only when those relationships still hold. New geometry usually needs profile data; a different mechanism may need new policy and native operations. Candidate data never enables itself.

Native ammunition remains authoritative. A visible seat, elapsed timer or submitted request is not a successful reload. BC2 uses pooled rounds for its current XM8 path: a removed cosmetic magazine is not a reusable ammunition resource. Another game's inventory model must be represented explicitly rather than copied from BC2.

## Port workflow and evidence gates

1. **Identify the target.** Record the exact build, architecture, renderer and a repeatable campaign/offline test route. Begin with read-only camera, owner, rig and device discovery. Verify signatures plus ABI and object relationships; an executable hash alone grants no hook capability.
2. **Prove native stereo.** Supply measured coordinate/depth conversions and visibility handling. Render both eyes from one native simulation/animation advance, restore on either-eye failure, and publish only a coherent pair from the same owner/frame/device generation. Inspect actual pixels, including first-person meshes and temporal effects.
3. **Connect XR and exercise lifetime changes.** Reuse the existing D3D11 transport only where its resource/format contract fits. Keep original poses with images and original expiry with evidence. Test focus, recenter/reconnect, loading, checkpoint restart, respawn, resize and device loss; these invalidate different owners and generations.
4. **Bring up one weapon family.** Bind native action/shot ownership and measured rig roles. Publish immutable private pose edits after native animation; preserve source palettes and unrelated animation. Validate controller/weapon/projectile agreement, independent arms, grip release and collision-backed movement.
5. **Bind one physical reload end to end.** Establish fresh native reserve, request/hold/commit/retirement semantics and exact receipts before enabling geometry. Cancellation must reconcile already-dispatched effects; unknown pending ammunition cannot become a successful receipt. Verify no extra transfers and clean return to ordinary operation.
6. **Expand through profiles.** Batch-capture additional rigs/items, review geometry differences, and admit only individually proven features. Optics, HUD, vehicles, scripted cameras, full-body presentation and multiplayer are separate capabilities. Passing a rifle test does not enable them.
7. **Package and accept the actual build.** Run deterministic tests and architecture builds, native owner/cleanup checks, both-eye image review and headset tests as distinct evidence. Keep unsupported paths disabled. See `BUILDING.md` for isolated source/build manifests and the packaged-payload checks.

BF3 is a useful proposed second consumer to test these boundaries before extracting more shared services; BF4 and Hardline can follow the demonstrated contracts. Do not copy BC2 offsets or rename BC2 trace fields to make another title pass its audits. No cross-title compatibility or porting time saving is claimed until a second adapter supplies real evidence.

## Source distribution

The source archive includes this document, public build/setup docs, allowlisted source/tests and dependency notices. It excludes game assets, executables, private captures/reports and machine configuration. Its generated `.gitignore` covers build/runtime binaries, reports, `test-temp`, local config and caches. Runtime binaries are assembled separately from a verified build; `.gitignore` is convenience, while the explicit packager allowlist is the distribution boundary.
