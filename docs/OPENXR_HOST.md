# Modular OpenXR host and display acceptance

FvrOpenXR is a separate x64 library. BC2XrHost uses the vendored Khronos 1.1.61
loader by absolute path and the installed active runtime. It requests OpenXR
1.0 core with the advertised D3D11 extension, following the 2142 compatibility
lesson. SDK/loader licenses and hashes are recorded in THIRD_PARTY_NOTICES.md
and openxr-source-manifest.json. The existing BF2142 checkout is not a build or
runtime dependency.

Implemented session path:

- Runtime/HMD and required GPU LUID/feature-level discovery.
- D3D11 device creation on that adapter; two separate primary-stereo swapchains.
- Runtime format negotiation, including sRGB (required by the observed SteamVR
  configuration). Compatible typeless runtime backing textures are supported.
- Session state events, LOCAL/VIEW spaces, predicted-time eye and head poses,
  and reference-space generation changes at the event's effective time.
- Wait/begin/end frame discipline; bounded retained presentation on missing fresh pairs,
  image acquire/wait/release, and whole-pair projection submission.
- The D3D11 consumer copies shared images only when their exact tracking sample,
  predicted time, reference-space generation, size/format and GPU match the
  projection views. Images rendered for an older pose are never relabeled with
  a newer head pose. Providers receive GPU-consumption feedback on failures.

IFrameProvider is a local C++ interface, not a process ABI. FvrFrameChannel now supplies a versioned x86/x64 tracking/ticket channel behind it; the hardware probe passed. See FRAME_CHANNEL.md. A bounded native BC2 producer is connected, but its eye geometry fails acceptance. Controller transport remains unimplemented. Controller actions, recenter bindings, gameplay input, headset
removal policy, and native BC2 camera/recoil/rig binding also remain pending.

## Commands

```powershell
& <local-workspace>\build\x64\BC2XrHost.exe --probe
& <local-workspace>\build\x64\BC2XrHost.exe --session --seconds 15
& <local-workspace>\Start-VRDisplayTest.ps1 -Seconds 60
```

The probe creates an instance/system query only. The session check creates a
session and processes tracking/frame timing with no image source. The display
test supplies a synthetic, clearly labeled BC2 VR DISPLAY TEST room with a cube,
grid and world-anchored lettering. It renders each eye from the exact runtime
pose/FOV and passes the pair through separate D3D11 devices and the shared
texture bridge. It never captures or substitutes a flat image of BC2.

The display-test script writes a unique report directory and the exact binary
hash. It may start the installed runtime through its normal OpenXR behavior;
run it after connecting the headset. No registry/runtime-default change is made.
A missing headset is an explicit failure, not an invitation to poll/restart
SteamVR continually.

Headset-free diagnostic acceptance:

```powershell
& <local-workspace>\build\x64\BC2SceneProbe.exe <local-workspace>\reports\stereo-scene
```

This rendered and read back both eyes, checked geometry, stereo parallax and
head translation, and verified copies into typeless XR-style backing textures.
The left-eye PNG was visually inspected for readable labeling and perspective.
This proves the diagnostic renderer and GPU copy path, not headset appearance.

## Actual runtime observations — 2026-09-25

The active runtime is SteamVR. With the user's Quest 3 connected, the host
successfully created an instance, queried the HMD and discovered 2064 x 2208 per
eye, adapter LUID 0:105430, minimum D3D11 feature level 0xb000. SteamVR reports
its Meta compatibility mode. The initial session was created but rejected the
host's original UNORM-only format list before any frames were submitted. The
format support was then corrected and hardware-tested with sRGB and typeless
textures.

The next attempt failed before instance creation because the wireless headset
was disconnected. SteamVR logged VRInitError_Driver_WirelessHmdNotConnected.
The user confirmed the headset was off and Steam Link had closed SteamVR.
A temporary per-process Meta runtime query also reported no available HMD; the
system runtime selection was never changed. Later on September 25, BC2 reached
the headset but failed visual acceptance with double imaging and severe flicker.
No subsequent headset acceptance has occurred; BC2 is not yet playable in VR.

Reference: Khronos [frame submission guide](https://github.com/KhronosGroup/OpenXR-Guide/blob/main/chapters/frame_submission.md)
and [swapchain acquisition contract](https://registry.khronos.org/OpenXR/specs/1.0/man/html/xrAcquireSwapchainImage.html).


## September 27 continuity implementation

TryGetCompletedPair returns the exact tracking sample used to render an asynchronously
completed pair. The runtime continues Wait/Begin/End while the source is pending;
`--frame-budget` is that request's lifetime, not a sleep in this path. The separate
synchronous API remains available for transport probes. No wire ABI change.

RetainedPresentation keeps original source eye poses/FOV for at most 250 ms.
Focus/tracking/reference-space/requirements changes clear it. Incoming images copy
into private scratch textures first; only successful complete copies replace XR
swapchain images. Invalid eye metadata is rejected before any swapchain release.
The existing released images may then be submitted on following runtime frames,
with current displayTime and the original render pose/FOV. This follows Khronos'
[released-image reuse contract](https://github.com/KhronosGroup/OpenXR-Docs/blob/main/specification/sources/chapters/rendering.adoc).

Counters distinguish fresh submitted pairs, presented frames, reused frames,
blank frames, provider misses, longest provider call and maximum retained age.
`ipc_pairs_received` is derived from successful IPC submissions; it does not
assert correct native geometry or headset acceptance.

Explicit GPU regression, without SteamVR or a headset:

```powershell
& <local-workspace>\build\x64\OpenXrPresentationTests.exe <local-workspace>\build\x64\FvrTestOpenXr.dll
```

This loads a test-only runtime by absolute path without registering it. It checks
actual host submissions and real GPU pixels, including missed frames, malformed
pose data, bad keyed-mutex sequence, expiry, focus, recenter and tracking loss.
Latest result: 100 frames, 13 fresh, 53 reused, 34 blank (including deliberate suspension/staleness), 2 rejected,
0 errors. It does not test SteamVR timing or fix BC2's wrong-eye rendering.
