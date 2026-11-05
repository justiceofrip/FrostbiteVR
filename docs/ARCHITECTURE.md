# Engine boundaries

Durable contracts and invariants only. The per-candidate design log (R33-R56 and the
September-October entries) moved verbatim to [history/2026-10-28-architecture-log.md](history/2026-10-28-architecture-log.md)
on 2026-10-28; search it by candidate number. [CURRENT.md](CURRENT.md) records
the installed build and its evidence limits.

## Contract versus engine implementation

The shared core has no Windows, DirectX, OpenXR SDK, native process, vtable or
Refractor dependency. Math functions with `Lh` names explicitly mean canonical
row-vector left-handed matrices; they are not assertions about Frostbite's
native layout. OpenXR poses use metres, +Y up, -Z forward. A verified adapter
must convert native handedness, matrix layout, units and depth convention.
`WorldFrame.farPlane` is an absolute distance, not Refractor's far delta.
The renderer must handle culling and any reverse-Z/native projection conversion.

`FvrCore` and `FrostbiteDiscovery` are separate static libraries. `BC2Adapter`
is a DLL with a fixed-width, versioned C metadata entry point. The current ABI
reports identity/status/capabilities, with an optional BC2-specific C discovery export; render and input callbacks are not
exported yet. `IRenderAdapter`/`IStereoSink` are internal C++ contracts. Extending
the native callback ABI requires a versioned struct, explicit sizes, same
architecture, and explicit resource ownership. Never expose C++ vectors,
exceptions, STL objects, or borrowed game pointers across a DLL/IPC boundary.

The executable and native game module may be Win32. The established 2142 design
used an x64 OpenXR presenter to avoid 32-bit runtime limitations. Preserve that
separation where necessary, but define a distinct Frostbite protocol and named
objects rather than silently reusing BF2142 IPC names or layout. The presenter
and frame producer must agree on protocol, GPU adapter, resource generation,
texture format, synchronization, reference-space generation and timing.
FvrD3D11 provides GPU texture transfer between processes. FvrFrameChannel
supplies versioned tracking/ticket IPC with deadlines, GPU feedback and peer-exit
handling. FvrOpenXR owns the host and projection submission path. A transport
check does not establish native gameplay or physical-headset acceptance.

## Required invariants

1. One native simulation/animation advance supplies both eye passes. A second
   call to a Frostbite top-level frame/tick is not an acceptable render adapter.
2. Save native state once; restore after either-eye failure and before external
   publication. An ownership/device change invalidates the entire pair.
3. Both eye images belong to the same native frame and device generation.
   They cannot alias the same resource slice. Never substitute menu rendering
   when world stereo is unavailable.
4. OpenXR head/eye/controller timestamps share a runtime clock. Recenter changes
   the reference-space generation; stale generations do not reach gameplay.
5. Recoil remains in native weapon/simulation behavior. The viewing and movement
   basis may remove recoil only after the corresponding engine behavior is
   understood. Both hands, HUD and body must share the same turn basis.
6. Native capabilities start disabled and are granted independently from
   verified signatures, executable section ownership, ABI, object identity and
   relationships. Hashes identify evidence; a new hash alone is not a hook.
7. Rig profiles supply bone names/indices, parent topology, palm axes and weapon
   attachments. Restore only exact previous mod writes before native animation;
   apply after native animation. Never treat the previous solved pose as the
   authored arm length source. Respawn/skeleton replacement changes generation.
8. Input outputs are semantic requests. BC2 owns key/button mapping, stance
   support, interaction rules, authoritative fire matrices, and ownership.
   No Refractor keycodes, item IDs, native prone toggle or vehicle seat numbers.
9. Diagnostics are on-demand developer tools. No per-frame logging/scanning,
   startup registry changes or background polling is installed.
10. Original-magazine tracking suspension invalidates transient pose authority
    and hand claims while retaining the exact ammunition resource. Recovery
    requires current same-owner evidence and fresh contact/claims. A tracking
    gap cannot complete motion, discard the original or reverse a native transfer;
    explicit release and lifetime transitions remain separate decisions.

Frostbite engine integration and BC2 game rules remain separate so another
Frostbite game can provide its own bindings without duplicating the VR policies.
Frostbite versions are not assumed binary-compatible.

The supported BC2 profile uses RH/padded camera conventions and a D3D11
renderer. FrostbiteCamera explicitly converts those conventions; see
BC2_NATIVE_DISCOVERY.md. Camera and stereo activation must use their verified
native bindings; transport capability alone cannot grant either.


## Portability and visibility

BC2 is the first adapter, not a binary definition of Frostbite. A BF3/BF4 port
reuses FvrCore, tracking/ticket transport and graphics/XR modules where the
verified APIs fit. It supplies its own executable discovery, native camera
layout/depth/scale, ownership/lifecycle, visibility scheduling, eye target and
render-state implementation, plus game input and rig profiles. No port inherits
BC2 offsets or a capability just because both games use Frostbite.

IRenderAdapter::PrepareViews receives a StereoViewSet before eye rendering.
The shared core can enclose both finite eye frusta in a symmetric conservative
cone, including asymmetric views and angled eye poses. stereoVisibility is a
separate required capability. A later engine may instead provide per-eye
visibility; the optional cone does not dictate that implementation. If native
visibility was prepared earlier, PrepareViews must validate the exact owner,
frame and tracking sample already used. BC2's visibility and draw callbacks run
on different threads; no game object or GPU call belongs in an unverified
cross-thread shortcut.

runtime/IFrameProvider.h is the generic frame-source boundary. IPC and native
production have no dependency on the OpenXR host header or SDK. RemoteFrameProducer supplies a single graphics-thread pump. StagedFrameProducer
now acquires an immutable request before visibility and accepts a complete pair
after native restoration on the graphics thread. Its local owner/frame/device
key and connection generation reject stale completion and native-frame replay.
Native view construction and scheduling must preserve these ownership and
restoration rules. Historical test failures belong in their dated evidence;
they do not describe the capability state of a later installed build.
