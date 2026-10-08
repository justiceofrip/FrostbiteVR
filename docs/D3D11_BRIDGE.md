# D3D11 stereo texture bridge

FvrD3D11 is an independent graphics module with no BC2 addresses, native engine
structures, Refractor/D3D8/D3D9 wrapper, or OpenXR dependency. It implements real
GPU texture transfer between an x86 producer and an x64 consumer. It is connected to the bounded native BC2 tracked-eye diagnostic. The new OpenXR host consumes this bridge; its headset acceptance is pending (OPENXR_HOST.md).

Run after both architecture builds:

```powershell
& <local-workspace>\Test-D3D11Bridge.ps1
```

The probe creates its own hardware D3D11 devices and hidden child processes.
It does not open or modify the running game. The consumer selects the producer's
DXGI adapter LUID. Eight complete pairs exercise two sequential frames for RGBA8/BGRA8 UNORM and sRGB, with compatible typeless consumer textures. Every pixel in each eye is checked after GPU transfer. Both source
and destination use different slices and nonzero mip subresources. The probe
also checks adapter/descriptor mismatch, wrong session/epoch, future and repeated
tickets, aliased eyes, and producer backpressure. The report records the exact
x86/x64 executable hashes. Readback is a probe-only check, not the transfer path.

## Ownership and wire contract

TextureDescriptor is 64 bytes; PairTicket is 96 bytes, with layout assertions in
both architectures. Only fixed-width fields cross the boundary. Each resource
set gets a fresh GUID session, a resource epoch, a GPU adapter identity, explicit
dimensions and typed format. Both eyes use separate D3D11 textures with NT shared
handles and keyed mutexes. Resource names use Local\FrostbiteVR.Texture.v2 plus
the session, epoch and eye. They cannot collide with 2142's IPC namespace.

For sequence N (starting at 1):

1. Producer acquires both mutexes using key 2*(N-1), without waiting.
2. It copies both resolved eye images, flushes the device context, then releases
   both mutexes using key 2*N-1. Only then does it return a valid PairTicket.
3. Consumer validates the ticket/session/epoch and acquires both keys 2*N-1.
4. It copies both images into caller-owned textures, flushes and releases both
   using key 2*N. A duplicate ticket cannot reacquire a later frame.

Acquiring the second eye can fail. The first eye is then released back with the
same acquisition key; no partial pair is transferred. Busy drops/defer the entire
pair. A device error, abandoned mutex or failed release invalidates the stream;
recreate it with a new descriptor/session. Never retry failed ownership keys.
A consumer crash may require stream recreation. No infinite GPU waits are used.

The producer owns and closes its NT handles. COM references keep resources
alive. Consumer resources open by name, after checking the adapter LUID and
actual texture descriptors. TextureSlice is a local C++ interface, never an IPC
pointer. Call on each device's immediate-context thread, with caller-serialized
context use. Transfer uses GPU copies, with no pipeline binding changes.

## Integration limits

- The initial bridge has one pair slot. A slow consumer prevents overwrite and
  causes Busy; queue depth/latency tuning requires actual VR frame timing.
- Supported transport formats: typed RGBA8/BGRA8 UNORM or sRGB. Source and destination may use compatible typeless/UNORM/sRGB backing textures within the same color-channel family. Copies preserve bytes; no gamma conversion or channel swapping is performed. Dimensions must match and samples must be resolved first. HDR and shader format conversions are not implemented.
- The caller must reliably deliver descriptors/tickets, verify current XR space
  generation and timing, and discard both destination images on any failure.
  The probe uses private inherited pipes; these are not a production XR channel.
- Reset resource/session ownership on native device/owner transitions. Native
  frame IDs must increase within a stream. The bridge does not classify scenes.
- Bounded native BC2 rendering and exact camera restoration pass; production lifecycle and frame pacing remain pending. The OpenXR host implements tracking/projection submission, with headset acceptance still pending. Passing this probe proves
  GPU transport, not playable stereo, headset comfort or rendering performance.

Implementation references: Microsoft's
[AcquireSync contract](https://learn.microsoft.com/en-us/windows/win32/api/dxgi/nf-dxgi-idxgikeyedmutex-acquiresync)
requires explicit handling of WAIT_TIMEOUT and WAIT_ABANDONED (both can pass
SUCCEEDED); shared resources use
[CreateSharedHandle](https://learn.microsoft.com/en-us/windows/win32/api/dxgi1_2/nf-dxgi1_2-idxgiresource1-createsharedhandle)
and [OpenSharedResourceByName](https://learn.microsoft.com/en-us/windows/win32/api/d3d11_1/nf-d3d11_1-id3d11device1-opensharedresourcebyname).
NT sharing requires Windows 8 or later.


## Version2 legacy compatibility (2026-09-25)

BC2's process rejected keyed-mutex texture creation with E_INVALIDARG on its
existing and a freshly created device. Legacy shared textures succeed. This
was reproduced with KF VR closed; the process-level compatibility cause is
not yet identified. The automatic fallback is explicit fenced legacy IPC.

Descriptor/ticket sizes remain64/96 bytes. Version2 uses descriptor offset56 for
TextureSharing and offset60 for a zero reserved word. Named mode keeps a GUID
in session; legacy mode stores two fixed-width uint64 DXGI resource identifiers.
Legacy storage uses UNORM while the semantic UNORM/sRGB interpretation remains
explicit. These are legacy resource identifiers, never NT handles to close or
duplicate. Old protocol versions are rejected; control objects use Control.v2.

Legacy producer copies both eyes then publishes only after its GPU EVENT query
completes. Consumer acknowledges success only after its own GPU copy completes
(bounded20ms wait outside the game). Failure feedback retires the entire source
resource generation; positive feedback releases its next write. No CPU image
readback is part of this transport. Capture and producer polling stay on the
native graphics thread; tracking acquisition stays on the visibility thread.

The exact-pixel fixture covers both modes, split callback threads and discarded
pair recovery: Test-FrameChannel.ps1 -FrameBridge / -LegacyIpc. Eight actual BC2
pairs passed at19:33 with matching tracking identities and restored native state.
This proves native transport, not headset comfort or sustained presentation.

References: [legacy resource lifetime](https://learn.microsoft.com/en-us/windows/win32/api/dxgi/nf-dxgi-idxgiresource-getsharedhandle),
[opening shared textures](https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11device-opensharedresource),
and [GPU query completion](https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11devicecontext-getdata).
