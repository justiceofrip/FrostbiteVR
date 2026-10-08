# BC2 native discovery — installed build 795745

Observed installation: `D:\Games\Battlefield Bad Company 2`.
The executable reports file/product version 1.0.1.0; InstallationData.xml
records CurrentVersion 795745. It is PE32/x86, large-address-aware, preferred
image base 0x00400000, SizeOfImage 0x01913000, PE timestamp 1314210909.
SHA-256: `3911FCC8914B0158C434CEE36F19295316E715D4C9A0E70C857E8C39E3D54258`.
The hash identifies this observation, not a hardcoded compatibility allowlist.

## Repeatable observation

```powershell
& <local-workspace>\Inspect-BC2.ps1
& <local-workspace>\Observe-BC2.ps1
```

The installation path is local configuration. The observer opens only the
matching executable with QUERY_INFORMATION | VM_READ. It neither writes memory
nor injects a DLL, invokes a native function, suspends the process, changes
settings, or starts/restarts the game. It uses the compiled native discovery
library; no Python, Capstone or 2142 checkout is needed to run it.

Signatures are scanned uniquely in executable sections. The render-dispatch
call must resolve to the independently found frame function. Singleton operand
addresses must point into a writable, non-executable image section. Native
presentation must occupy vtable slot 5 and width/height getters at slots 10/11
must have the independently expected accessor code. Live code, singleton
references, vtable, process path and module size are checked again before reading
objects. Unsupported profiles produce no writes and no native capabilities.

The BC2 DLL also exports FvrDiscoverBc2Profile, a versioned C discovery entry
point that takes caller-owned PE bytes and returns image-relative evidence.
Render/input callbacks are still unimplemented; discovery success does not
advertise playable stereo.

## Observed relationships

These RVAs are results from this build, not instructions to write addresses.
The source derives them again through signatures and code/table relationships.

| Relationship | RVA / offset | Evidence |
| --- | --- | --- |
| Native graphics singleton | 0x01172a60 | Operand in unique frame entry; repeated native renderer calls |
| GameRenderer singleton | 0x0116d4f8 | Operand in unique dispatch; dispatch calls the independently found frame |
| Top-level frame candidate | 0x001c9b40 | Unique function entry and dispatch target; **not proven render-only** |
| Dispatch | 0x001ca820 | Renderer lock/unlock around frame candidate |
| D3D11 renderer vtable | 0x010497a0 | Present at slot 5, getters at slots 10/11, live object agreement |
| Native presentation function | 0x005eea30 | Unique signature, native vtable relationship |
| DXGI Present wrapper | 0x005e1390 | Loads renderer+0x88, calls COM vtable+0x20 (Present) |
| Renderer device | +0x7c | Live COM vtable owned by d3d11.dll |
| Renderer swap chain | +0x88 | Live COM vtable owned by dxgi.dll, native Present wrapper agrees |
| Renderer dimensions | +0x18 / +0x1c | Native getters; live 1920 x 1080 |
| Game render settings | GameRenderer+0x10 | Native frame accesses; reflected GameRenderSettings metadata |

Both d3d9.dll and d3d11.dll were loaded. The D3D11 conclusion comes from the
active renderer object and its device/swap-chain relationships, not DLL presence.
The reflected Renderer selection field at settings+0x6c was zero while the
D3D11 renderer was active; do not use that field alone as the active API.

## Camera convention discovered

Offsets below are still observation candidates pending setter/update/ownership
proof. They must not be used as a writable camera profile yet.

| GameRenderer-relative candidate | Observation |
| --- | --- |
| +0x20 | View description begins |
| +0x3c / +0x40 | Near and absolute far distances; observed 0.1 / 20000 |
| +0x44 | Width/height aspect, observed 1.77778 |
| +0x70 | Camera-to-world, four padded Vec3 rows; final padding is 0 |
| +0x240 | Row-vector view matrix |
| +0x280 | Transposed view matrix |
| +0x2c0 | Inverse view / homogeneous camera-to-world |
| +0x300 | RH perspective projection; element [2][3] is -1 |
| +0x340 | Transposed projection |

The inverse of the +0x70 transform (after restoring homogeneous W) agrees with
the +0x240 view in live observations. The projection has RH negative-Z forward,
positive clip W and [0,1] clip depth. Core math uses LH coordinates; conversion
is `camera_LH = C * camera_RH * C`, `projection_RH = C * projection_LH`, where
`C = diag(1,1,-1,1)`. Clip space itself is not reflected. Padded Vec3 transforms
must be normalized for matrix math and restored to padded form for the adapter.

FrostbiteCamera is a separate, tested conversion library. Tests cover nonidentity
poses, physical eye separation, forward head movement, asymmetric frustum edges,
near/far depth and invalid inputs. World-unit scale has NOT been calibrated.
Unpaused external camera reads can race native updates; the observer reports
coherence and cannot prove frame synchronization or automatically classify the camera. The user confirmed these observations occurred during campaign gameplay.


## Campaign render-path investigation

The user identified the live scene as campaign gameplay. These findings came
from read-only object reads and disassembly, not from invoking native callbacks.
Addresses below are preferred-image VAs for this exact build; subtract 0x00400000
for RVAs. DiscoverRenderPath now re-derives these functions using unique signatures, relative-call links and vtable ownership. They remain read-only candidates, not a writable or replay-safe profile.

GameRenderer+0x1088 holds five subsystem pointers. The frame candidate iterates
three virtual stages: slot 4 (frame serial), slot 5 (pass index, frame serial and
two floats), slot 6 (frame serial). Graphics locking brackets these calls. In
campaign, slots 0/2/3/4 referenced one object; its slot-5 function 0x008feb30 waits
for jobs and dispatches different paths by pass index. One path 0x00f88830 pops
queued state. These shared stages cannot simply be replayed per eye.

Subsystem index 1 calls its +0x2c object's virtual slot 5 with +0x30 as a render
request, when its active byte +0x38 is set. The nested vtable was 0x01477690;
virtual slot 5 is 0x00bb4d80. That function requires request+0xc4 == 1 and ends
by writing 3 there. It walks the request's view list (+0x24/+0x28), checks each
view's virtual slot 18, prepares it through 0x00bb0ea0, then draws through
0x00bb1db0 with the prepared data. Code labels include worldMainOpaque,
worldMainTransparentDecal and VisualTerrain.SimpleWater. This is stronger world
render evidence than the top-level frame label, but no replay/ownership ABI is
validated. Resetting request state alone would not restore consumed jobs,
prepared view data, culling or native graphics caches.

Native view-cache builder 0x0096f260 takes RenderView in ECX. It derives view,
transpose, inverse, view-projection and inverse/transpose caches from the padded
transform at RenderView+0x50 and the EXISTING projection at +0x2e0. It clears
dirty bit 0. It does not prove that changing only these matrices updates culling,
projection construction, temporal history or downstream per-view buffers.

The bounded pass-through trace now forwards world/prepare/draw calls once; no render replay is enabled. Opt-in scoped camera pulses are documented in NATIVE_TRACE.md. See NATIVE_TRACE.md. Live
object pointers changed between observations, confirming the need for generation
and ownership checks rather than keeping raw pointers indefinitely.

## Still required before playable stereo

- Find a render-only boundary and camera-update/culling/cache ownership. Do not
  replay the top-level frame just because its label says render.
- Verify simulation and animation advance once, native UI separation, clean
  target/state restoration, device loss and native GPU resource ownership. Independent x86-to-x64 texture sharing now passes the hardware probe; see D3D11_BRIDGE.md.
- Connect the implemented x64 OpenXR host and verified D3D11 bridge to the native producer with production tracking/ticket IPC. The standalone display test is implemented; native game transport remains unconnected.
- Verify local player state, authoritative aim, weapon/arm bindings and gameplay
  controls. No native input/skeleton/gameplay writes have been enabled.
## Concrete world-view discovery and campaign observations

DiscoverViewLayout derives concrete view vtable 0x014776f8 from constructor
0x00bab9f0 and its request registration, then validates the camera setters,
getters, owner, active flag and viewport accessors. Three inherited vtables
share the camera setters, so a setter-only match was correctly rejected as
ambiguous until constructor ownership disambiguated the concrete type.

Virtual slots 5/7 are thunks 0x00b97450/0x00b97460, copying RenderView through
0x005d1d80 into view+0x90 / +0x4f0. Getters 6/8/9/10 expose blocks at +0x90,
+0x4f0, +0xc70 and +0x10d0; each block spans 0x460 bytes. Slot 2 returns owning
request at +0x70. Slot 18 reads active byte +0xc52, and slot 16 returns the
integer viewport at +0xc58. The latter was [0,0,1920,1080] in campaign.

The compiled observer verifies live code/table relationships, bounded request
view lists, owner links and snapshot stability. One active concrete view was
observed. All four camera transforms/views were coherent; only the first two
had the observed RH perspective marker. Semantics of the other blocks/history
are still unassigned. Do not update only the top-level GameRenderer camera.

Prepare 0x00bb0ea0 returns its prepared pointer in EAX with ret 8 at +0xf02.
Draw 0x00bb1db0 ends with ret 12 at +0x1e2f and clears a per-view queue ending at
+0x1674. The world callback is ret 4. Their pass-through ABI was exercised in a
native test process and then in the actual campaign; see NATIVE_TRACE.md.


## Camera construction and visibility scheduling (2026-09-25)

DiscoverCameraCaches verifies getter -> builder links, unique executable
signatures, and dirty-bit epilogues:

| Getter | Builder | Native cache |
| --- | --- | --- |
| 0x005bb3e0 | 0x0096f260 | view +0x220, transpose/inverse, VP |
| 0x005bb420 | 0x00973800 | projection +0x2e0, transpose/inverse, VP |
| 0x005bb490 | 0x00965bf0 | frustum +0x90, six outward planes at +0x180 |

RenderView fields: type +4 (0 perspective), RH flag byte +8, FOV scale +0xc,
vertical FOV radians +0x10, derived FOV * aspect * scale +0x14, near/far +0x1c/
+0x20, aspect +0x24, crop x/y +0x3c/+0x40, crop width/height +0x44/+0x48,
padded RH transform +0x50. +0x18 is a reference used by the separate LOD metric
builder; semantics unassigned, retain it. Dirty bits 1/2/4/8 cover view,
projection, metrics and frustum respectively.

For tangents L/R/U/D, BC2Camera uses verticalFov = 2 atan((U-D)/2), aspect =
(R-L)/(U-D), cropX = (R+L)/(2(R-L)), cropY = (U+D)/(2(U-D)), crop sizes = 1.
Eight native owned-copy cases validate the mapping. The frustum builder does
not use crop parameters; a separate symmetric stereo envelope is necessary.
No BC2 offsets are part of the shared math or XR/IPC contracts.

World update 0x00bb4980 advances time and frame serial, resets its frame arena,
and invokes simulation-related callbacks before per-view work. DO NOT replay
it. Per-view work 0x00badff0 builds/copies visibility data and returns job groups
through its last two arguments (six stack arguments total). It copies native
camera/frustum values into downstream job data and child views; the complete
job ownership and synchronization contract is still under investigation.

Camera/visibility math, live callback scheduling and one native color readback
are now tested; see NATIVE_TRACE.md. A bounded visibility-stage camera pulse now visibly moves and restores the world.
Production camera integration and eye rendering remain disabled. Render capabilities remain zero.


## Native view factory ownership (quarantined experiment)

DiscoverViewLifecycle links request vtable 0x014773d0 slot 3 to factory
0x00badf80, which allocates 0x56b0 bytes and calls constructor 0x00bab9f0.
Constructor argument ABI is request/name; factory takes a pointer to a native
character range (begin/end/capacity). It copies the name; do not retain or copy
native strings/vectors by raw whole-view memcpy. Constructor registers the
concrete view in request+0x24/+0x28. Refcount starts zero at view+0x20; native
AddRef 0x00b8f5a0 returns 1, Release 0x00b8f5b0 invokes vtable slot 59 at zero.
Deleting destructor 0x00babb10 calls 0x00ba8b80, which unregisters the main-view
list and releases owned child/queue storage. AddRef has identical code at other
addresses; discovery disambiguates through the concrete view vtable.

Base constructor 0x00bab650 also AddRefs the request, registers in its all-view
list +0x14/+0x18, and calls world+0x9c/+0xa0 callback slot 7. Base destructor
0x00ba48b0 unregisters that list, releases the request and calls slot 8. Those
callback implementations and their synchronization are not verified. Native
view creation cannot be enabled based only on the main-view list roundtrip.
The 11:35 test was followed by an exit and is quarantined; see crash notes.

Both world-update and world-draw loops load the request list again between
iterations. World update 0x00bb4980 MUST NOT be replayed: it updates time,
frame serial, arenas and callbacks once. Native view iteration remains the
candidate approach, pending factory callbacks, per-view jobs and target/history
ownership. Never share unknown view+0x1540/+0x1548 resources between eyes merely
to make a second view resemble the first; one is passed to a dt-taking update.

Native frustum construction clamps the vertical half-angle to
0.9999989867210388 radians before tan (about 114.59 degrees full FOV). Projection
construction does not share that clamp. BuildCullingViewCopy expands asymmetric
extents to a centered cone and rejects cones outside the limit with a small
angular margin. Wider views need a verified alternate visibility strategy.
