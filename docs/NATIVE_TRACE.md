# Native pass-through trace acceptance

Start-NativeTrace.ps1 is an explicit, bounded in-process diagnostic for the
already-running BC2 campaign. It copies the current x86 probe DLL to a unique
private report directory, records game/bootstrap/payload hashes and the loaded
module map, and invokes a three-second trace. It does not launch the game,
change settings, or install game files.

This tool DOES temporarily patch native code using the privately compiled,
licensed MinHook library. It forwards each original world, visibility preparation, render preparation and draw
call once. The default trace does not replay frames, change cameras, or write
render targets. Explicit -CameraPulse and -VisibilityPulse modes temporarily
replace two camera value blocks for one original callback and restore them.
Signatures, concrete view constructor/vtable relationships, live bytes, native
object ownership and x86 ret 4/8/12/24 argument cleanup are validated first.
NativeHookAbiTests exercises these calling conventions and original returns
through actual hook trampolines in an isolated x86 test process.

At completion the entry patches are disabled. Trampolines and the DLL remain
resident until game exit to avoid unloading code under a returning render
thread. There is no persistent scanner, auto-start hook or background trace.
This is a diagnostic module, not the final BC2Adapter gameplay implementation.

## Observed campaign test — 2026-09-25 09:21:51 UTC

Evidence: reports/native-trace-20260925-092151-830. Process 42740 remained
responsive and did not exit. The report confirmed hooks disabled; a subsequent
external observer verified native entry bytes and the concrete world view.

- 180 world calls, 180 prepares and 180 draws on render thread 50508.
- 180 distinct native frame serials; one active view per request.
- All captured requests changed state 1 -> 3.
- Prepare/draw increased the world frame arena by 112 bytes per request.
- The primary RenderView and first 0x64 prepared-data bytes were unchanged by
  draw in these samples. Nested pointed-to data was not covered by that hash.
- The view's +0x1670/+0x1674 work queue was empty before/after each sampled draw.
  Static code clears this queue; empty observations do not prove replay safety.
- Bound color targets before/after draw were 1920x1080, RGBA8 UNORM (28), single
  sample. Dimensions/format alone do not identify them as the swapchain buffer.

These observations establish the tested callback path and timing. They do not
prove stereo replay, full native-state restoration, culling correctness,
temporal history, simulation/animation ownership or headset-visible BC2.
No native VR capability is enabled from this trace alone.

MinHook sources remain unchanged from the recorded licensed dependency. Its
upstream C code emits MSVC /W4 warnings for anonymous unions, narrow internal
fields, an unused x86 argument and a prefix-loop initialization analysis. The
BC2 additions build without new warnings; the x86 ABI exercise passes.


## Native camera and visibility acceptance — 09:36–09:58 UTC

BC2Camera::BuildRenderViewCopy owns an aligned 0x460-byte value copied from a
verified BC2 RenderView. It maps canonical eye poses and asymmetric FOVs into
native projection parameters, preserving unknown fields and LOD references.
It does not write the live camera. Dirty caches require verified native rebuilds.

The bounded probe calls native view/projection/frustum math on owned copies.
The baseline rebuild matched both original matrices exactly. Eight translated,
rotated asymmetric eye cameras matched expected view, projection and VP matrices
(view absolute error at most 6.11e-5; projection 1.79e-7 in the recorded runs).
Eight stereo union cameras passed 54 sampled points each against all six native
world-space planes, with a 0.001-unit numerical tolerance. BC2 planes face
outward: inside means n dot position + d <= 0. An initial probe used the wrong
sign and reported failed containment; the corrected sign follows native plane
construction and the later probe passed. Preserve that failed report as evidence.

The native frustum ignores projection crop fields. Six parameter experiments
confirmed that crop origin +0.1 adds +0.2 to P[2][0/1], while crop width/height
0.8 changes scales and centers; none of those crop changes alter the frustum.
Never treat an asymmetric projection alone as valid stereo visibility.

DiscoverVisibilityPath links world virtual slot 3 (0x00bb4980), its call at
+0x383 to per-view preparation 0x00badff0, and both ret 24 epilogues. The
pass-through hook forwards the six arguments unchanged and traces owner links,
frame serials, camera hashes, returned job groups and QPC intervals.

Latest ownership trace: reports/native-trace-20260925-095833-525:

- 601 original world/prepare/draw calls; bounded 256 detailed records.
- Visibility runs on thread 51472; draw on thread 50508. Both use the same world,
  request and view in this sample. All 256 visibility owner checks passed.
- Native world serial matches GameRenderer serial in these samples.
- No visibility/draw interval overlaps among the captured matching frames.
  This observation is not a replacement for identifying native synchronization.
- Visibility mutates primary/secondary caches and returns two job groups; the
  third/fourth camera blocks remain unchanged in these samples.
- Draw sees the camera hash produced by matching-frame visibility preparation.
- BC2 stayed responsive; all entry hooks were disabled. Every copied probe DLL
  remains inert/resident until game exit.

## Actual BC2 color readback

The probe now issues ONE CopyResource from the color target bound immediately
after the original world draw into an owned staging texture. Map uses
DO_NOT_WAIT and retries on subsequent original draws, up to 120 attempts.
No GPU wait loop, resource binding change, eye replay or native camera write.

reports/native-trace-20260925-095531-017 captured native frame 508627 as
1920x1080 RGBA8 UNORM, one sample. Readback needed one busy retry. The owned
world-color.rgba was converted to world-color.png for inspection; it visibly
contains world geometry and the first-person weapon. A later ownership probe
also captured frame 545053. This is MONO evidence of the BC2 image-output path,
not an eye pair, OpenXR output, gamma acceptance or headset validation.

Native camera regressions now cause a nonzero probe exit. Failed or unfinished
experiments are not silently counted as successful camera acceptance.


## Visible camera pulse and inactive view experiment

reports/native-trace-20260925-100638-606 changed pose only at Draw entry for one
original callback and restored exact bytes. Geometry did not visibly move;
that boundary was too late for the camera data already prepared by visibility.
reports/native-trace-20260925-100858-070 recorded three inherited shadow children,
with distinct camera hashes; those children are not assumed to be eye views.

reports/native-trace-20260925-101244-595 applied the same bounded pose change
before original visibility preparation. Captures at frames 715274/715276/715278
visibly show the world shifting during the pulse and returning afterward.
Applied/restored/owner checks passed. Native visibility changed cache bytes,
as expected; exact saved values were restored before the hook returned. The
weapon remained approximately fixed on screen, indicating additional viewmodel
work. This is visual camera control, not stereo: no frame/animation replay,
complete BC2 eye pair, controller integration or headset output was tested.
The diagnostic translation was 0.25 engine units, with 0.03 radian yaw;
physical scale remains uncalibrated.

The 11:35 inactive factory test passed its immediate ownership checks but BC2
exited afterward. It is quarantined and must not count as stable acceptance;
see CRASH-20260925.md. The launcher now includes 15 seconds of post-test process
observation and preserves newly written crash reports. All native stereo
capabilities remain disabled until two-eye rendering and lifecycle validation.
