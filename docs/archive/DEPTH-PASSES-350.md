# World and first-person depth: private343–349 and compositor350

The reported gray magazine is a normally sized body prop rendered without the
game's scene depth or textures. This investigation establishes a usable depth
source and implements an engine-independent host compositor. **Live depth
transport is not connected; normal VR still uses the earlier body compositor.**
No new headset acceptance is claimed. The normal gameplay implementation is342,
including339's authored SCAR axes and fixed SPAS receiver/stock/ring assembly.

## Native evidence

342's depth observer excluded the actual main depth resource because it required
one sample. The user's original `MSAA=3` setting produces an8-sample1920x1080
R24G8 typeless resource on the tested GPU. The single-sample DSV bound at eye end
contains world depth without the first-person gun and arms.

343–345 temporarily tested MSAA0, with an exact byte backup and compare-before-
restore for each run. The same geometry resource holds world depth earlier and
first-person depth at eye end. The original settings were restored after every
run.345 captured32 depth-writing/stencil-reference1 draws per eye. All32 actual
GPU VS constant buffers contain a matrix matching canonical world/view/projection
within0.002 maximum entry error after folding BC2's viewport depth range into
clip Z. Stencil1 alone does not identify the first-person pass: world geometry
also uses it. Matrix matching is evidence, not a general shader-use proof.

347 observes depth clears using the installed SDK's verified
`ID3D11DeviceContext::ClearDepthStencilView` slot53. On the original8-sample
configuration, each eye has two full-resolution clears of the SAME resource:

1. Clear depth to1 and stencil to0 before world geometry.
2. Clear both again after554 world draws, then render seven first-person draws.

348 copies that resource before the second clear and again at eye end. Both
snapshots from both eyes are successfully read through shared resources on an
independent D3D11 device. The world images contain the dock, water, landscape and
boat; the final images contain the gun and hand silhouettes. First-person depth
covers431322 and443282 pixels, including3523 and4965 partially covered pixels.
These are one-frame measurements, not a universal pass signature for every map,
vehicle, menu or graphics setting. Resource pointers and draw counts are evidence
only and must not become hard-coded production selectors.

The producer adds only copies and event queries, with no additional game draws,
dispatches or target bindings. GPU extraction dispatches happen later on the
independent consumer device, outside BC2's native query scopes. Each run retains
the separate342 mechanics and cleanup report; the graphics captures do not grant
new weapon capability.348's postflight server sampler rejected one changing
ownership read and retained30 valid samples; do not claim every sample passed.

## Sharing and composition

345 verifies shared D24S8 creation, handle import and depth/stencil SRVs at
1×,2×,4× and8×.346 adds a producer-drawn, sample-varying depth/stencil pattern,
producer GPU completion, cross-device sampling and consumer GPU completion.
All four sample counts have zero incorrect stencil/sample/pixel identities;
maximum depth error is one float ULP (`5.96046448e-08`). Creation alone was not
enough; this second check verifies transferred contents.

349 uses the actual348 depth samples and installed SCAR magazine geometry.
The recorded chest pose is outside the viewport. Two explicitly synthetic
placements, with unchanged geometry size, intersect a hand and the dock.
Per-sample GPU occlusion counts match the CPU reference for all2073600 pixels
in each of six eye/case combinations. At the hand placement,15579/15076 pixels
are fully hidden and366/355 have partial coverage. These are composition checks,
not recorded controller poses or human visual acceptance.

`D3D11SceneDepthCompositor` is the reusable350 component. It combines original
color, rendered prop color and prop depth against world and first-person depth
on a host-owned output. Coverage is the intersection of visible samples, not
the minimum of two independent coverage counts. It supports ordinary/reversed
depth, D24/D32 SRVs and linear/sRGB RGBA/BGRA color views. It rejects missing,
mismatched-size, mixed-sample, wrong-device and aliased output resources, restores
the caller's D3D11 context state, and releases borrowed bindings afterward.
The caller still owns same-eye/frame/projection validation and GPU lifetimes.

The explicit WARP test covers96 D24/D32, forward/reversed,1×/2×/4×/8× world/hand
coverage combinations, including disjoint covered samples. Four color-format
checks verify sRGB encoding once. It also checks context restoration, invalid
resources and calls from the wrong thread. No game or headset is launched by
this software test. The component is not called by the normal compositor yet.

## Remaining connection work

- Bind both depth stages to the exact owned native eye and frame lease. Unexpected
  clear order, source identity, dimensions or presentation context must reject the
  pair rather than reuse old depth. Observe more than the single scene above.
- Attach four shared depth resources and their descriptors to the existing color
  pair's versioned IPC metadata. Preserve frame, space, tracking and resource
  generations, and retire old resources on cancellation, resize or scene change.
- In the tested legacy-fenced transport, copy depth before the producer's color
  publication fence. Consume it before the host's read-completion fence and ACK.
  `StagedFrameProducer` prevents a new lease while awaiting feedback, but this
  alone does not prove depth resource lifetime on every discard/disconnect path.
  Keyed/relay transports need their own ownership implementation; do not borrow
  the legacy fence guarantee without verification.
- Provide shader-readable original/prop colors and prop depth to the new host
  component. Measure live cost and skip unchanged pixels before loading all MSAA
  depth samples. Keep native draws outside the game-query scope.
- Recover actual material textures separately. Depth correctness does not remove
  the current flat gray material or establish correct hand/controller alignment.

References: [D3D11 texture copies](https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11devicecontext-copyresource),
[event queries](https://learn.microsoft.com/en-us/windows/win32/api/d3d11/ne-d3d11-d3d11_query),
[multisample depth loading](https://learn.microsoft.com/en-us/windows/win32/direct3dhlsl/sm5-object-texture2dms-load).

Private evidence is under `bc2vr-recovery/depth343-*` through `depth349-*`.
Do not distribute native images, copied game geometry or depth buffers as source.
No package manifest or graphics-setting changes remain applied. BC2's last
diagnostic process was closed; no headset or SteamVR process was closed by this
work. A fresh game is required for the next player test.
