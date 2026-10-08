# Rendering repair - September 27, 2026

## Headset visual success - September 27, 20:59 UTC

User ran the corrected 30-second campaign test and reported: "amazing! it's
perfect except theres a weird visual bug with like these black dots ... other
than that little bug amazing!" Record this as user-reported stereo/presentation
success WITH a remaining artifact, not artifact-free or complete VR acceptance.
The dots were near the center and disappeared when looking in other directions;
user suspects an asset/LOD issue. Cause is unconfirmed. Captured surface marks
appear different between eyes; compare decal/culling/LOD paths before changing
any settings. No new headset session is needed merely to inspect saved frames.

Reports: native-xr-20260927-205855-275 (host + separate user-feedback.json),
native-trace-20260927-205856-732 (native evidence). Preserve machine host output:
its headset_visuals_verified=false is an automated report, supplemented by the
separate user report. Immutable source/binary checkpoint:
reports/headset-success-20260927-205855/checkpoint.json.

SteamVR OpenXR/Meta compatibility mode, 1920x1080 per eye: 909 fresh pairs,
1740 reused frames, 2649 presented frames, 0 rejected pairs. Native published 910,
consumed 909, discarded 1; 1820 eye captures and correction scopes, zero binding
or camera restoration failures. Both sampled GPU matrices match their own eye
(max X/Y/W errors ~3e-5). Hooks disabled; native lists/callbacks/pool restored;
game remained alive with no new crash for the 15-second post-test observation.
The host runs about 15 seconds longer than the producer: its 1315 blank frames
include the producer shutdown interval; aggregate counters do not locate each
blank. Do not claim this number proves in-session flicker.

Core rendering fix is now headset-tested. Physical scale, full temporal/history
isolation, HUD/weapons, controllers/IK and the direction-dependent black-dot
artifact remain open. Production capability bits are still zero. BC2 PID 66212
remains running, inactive probe DLLs retained until exit.

## 20:53 UTC update: native projection correction passes

The last/right camera binding consumption is now corrected during native Prepare
and Draw using validated per-eye constants and exact restoration. Native job order
is unchanged. The left contexts were proven distinct and unchanged, correcting the
earlier allocator-overwrite hypothesis. This is a camera-binding fix within the
bounded probe, not complete renderer/history isolation or production VR completion.

See [current handoff](HANDOFF.md) and
[hashed evidence](../reports/rendering-fix-20260927-2053/summary.json).
The initial corrected 8-pair run passed all image checks (-97 px measured vs -96
expected) and both sampled GPU matrices exactly. Subsequent checks delivered 240
static pairs, 32 moving-pose pairs and 16 alternating-FOV pairs with zero correction
or restoration failures. All nine saved images from the 240-pair run pass; the
16 checked FOV-control images match their exact intended shifts. GPU eye ownership
passes in each longer run after explicitly decoding each sampled shader layout.
Unknown layouts stay inconclusive. Original broken captures still fail.

Current validation: 22 x86 + 21 x64 suites and 2 Python projection regressions pass.
Hooks disabled and native state restored after every run, no new crash, game alive
through 15-second observations. No post-fix headset test yet. Headset-visible flicker,
fusion/comfort and full temporal/scale/HUD validation remain pending.

## Earlier checkpoint (historical baseline and failed experiment)

The user resumed development while sleeping. The last real headset test remains
the failed September 25 run. This checkpoint makes no new headset success claim.

## Implemented and verified

- Shared RetainedPresentation stores original rendered poses/FOV and requirements;
  expires after 250 ms and clears on invalid tracking/focus/space/compatibility.
- OpenXrHost stages a complete GPU pair in private textures before replacing its
  released XR images. Bad provider poses are rejected before replacement. It
  reuses the previous complete pair while awaiting fresh images and records
  fresh/reused/blank/missed-request and timing counters.
- FrameChannel exposes BeginRequest/PollRequest/CancelRequest with zero-wait lock
  attempts. RemoteFrameProvider preserves the original tracking request while
  polling later XR frames. Delivered GPU ownership survives timeout/cancellation
  until feedback. Protocol v2 is unchanged; synchronous probes remain supported.
- A test-only OpenXR runtime drives the actual host against real D3D11 textures.
  It checks every pixel and original pose/FOV across delays, rejected copies,
  invalid returned poses, focus/space changes, shouldRender and tracking loss.
  It is loaded by explicit path and never registered as a system runtime.
- Native receiver supports controlled static/asymmetric FOV, asynchronous polling
  and a four-step FOV sweep. The native probe saves camera blocks, first-geometry
  constant buffers and pipeline statistics for one tracked frame. These are
  diagnostic-only; the production adapter capability mask remains zero.

Latest checks:

| Check | Result |
| --- | --- |
| x86 deterministic/ABI suites | 21 passed |
| x64 deterministic/ABI suites | 20 passed |
| Actual host + test XR + real GPU | 100 frames; 13 fresh, 53 reused, 34 blank, 2 injected failures rejected, 0 errors |
| Cross-process x86 -> x64 NT texture bridge | 4 exact pixel pairs; drop/exit/abandoned-mutex checks passed |
| Cross-process x86 -> x64 fenced legacy bridge | 4 exact pixel pairs; drop/exit/abandoned-mutex checks passed |
| Native static/asymmetric async stream (stable path) | 8 pairs, 0 GPU copy failures, 0 camera restore failures |
| Controlled projection acceptance | FAIL: measured shift 0; expected -96 pixels at analysis width 480 |
| HMD visual acceptance after changes | Not performed |

The 34 blank fixture frames include startup and deliberate invalid/stale periods.
The test does not establish SteamVR performance or resolve native binocular fusion.
The source-only projection analyzer also passed independent -96/0/+96 pixel controls.

## Native evidence

Reports are relative to <local-workspace>/reports.

| Receiver / native trace suffix | Purpose / result |
| --- | --- |
| 154849 / 154850-047 | Static asymmetric baseline: both captures align at zero shift |
| 155417 / 155417-433 | Per-eye secondary camera experiment: no fix, reverted |
| 155725 / 155725-794 | Native GPU queries prove both geometry passes execute |
| 155854 / 155855-175 | FOV sweep: both captures follow the last/right view |
| 160219 / 160220-254 | Prepare-near-Draw experiment: no fix, reverted |
| 160643 / 160643-503 | Actual shader constants: both draws match right VP X/Y/W |
| 161622 / 161622-519 | Deferred right visibility experiment: native job stall, reverted |

Prefixes are native-ipc-20260927- and native-trace-20260927- respectively.
All six successful bounded tests restored state and left BC2 responsive with no
new crash report. Do not apply that statement to the final stalled experiment.

Strongest evidence: `native-ipc-20260927-160643/projection-check-0.json`:
expected horizontal shift -96, measured 0, same-row correlation 0.98970096.
Native frame 262782 matches the shader diagnostic frame. Eye 0 X/Y/W rows match
right-camera VP exactly; eye 1 differs by only 3.73e-9. Compared with the left
camera, both differ by 74.448 in the transposed matrix. Native depth slicing changes
Z, so that row is explicitly excluded. This samples the first full-size geometry
draw only. Complete projection or IPD acceptance needs additional scene evidence.

The FOV sweep uses centered/centered, left/right, left/left and right/right FOVs.
Measured common image shifts against the centered frame are -48, +48 and -48 at
width 480, respectively. Asymmetric camera math works; shared last-view state
makes both eye captures follow the right input. Image inequality was an inadequate
acceptance criterion and is now explicitly distinguished from projection correctness.

## Reverted experiment and next native work

`b9c990` resets the world+5d4 command allocator with `98d7d0` / `9cbd30` while
gathering each view. `b972f0` resets world+c80/fd0 job aggregates and per-renderer
batch lists. The original frame loops all visibility gathers and all preparations
before drawing each view. This shared storage is the leading explanation for the
observed last-eye constants; complete job/pointer ownership still needs proof.

The experiment deferred right visibility and Prepare until the left draw completed,
using owned copies of the 0x100-byte parameters and optional value arrays. It still
ran WorldUpdate once and each per-eye visibility once. It stalled after capturing
the left image, inside the derived native wait function (thread 16232 stack contains
0x508caf). No complete pair was published, and hooks/pool could not be restored.
Thread 64372 was also waiting; full captured stacks are in `hang-threads.json`.
Windows Responding=true is insufficient to verify renderer progress.

PID 60132 was closed to remove the resident hooks. The change was reverted and the
stable source explicitly recompiled. A later clean launch (PID 62488) was closed
normally. No BC2 probe, exception watcher, XR host or BC2 process was left running.
Another Killing Floor session appeared during closeout and was left untouched;
no additional live BC2 test was attempted while it was running.

Next: inspect native job submit/completion lifetimes, dependency aggregation and the
update/render lock boundary. Establish an ordering or independently owned renderer
context that preserves each eye's data without running WorldUpdate twice. Do not
copy opaque renderer objects or move job waits under graphics locks speculatively.
Then require the controlled projection check to pass before scheduling another HMD
test. Physical units/IPD, temporal effects, HUD/viewmodel, hands/IK and input remain.

## Reproduction

Build with Build.ps1 for x86 and x64. GPU checks:

```powershell
& <local-workspace>\build\x64\OpenXrPresentationTests.exe <local-workspace>\build\x64\FvrTestOpenXr.dll
& <local-workspace>\Test-FrameChannel.ps1 -FrameBridge
& <local-workspace>\Test-FrameChannel.ps1 -LegacyIpc
```

With BC2 alone in campaign, headset off:

```powershell
& <local-workspace>\Test-NativeStream.ps1 -Seconds 6 -Pairs 8 -Asymmetric -StaticPose -Async
```

Analyze a saved capture with Python, numpy and Pillow:

```powershell
python <local-workspace>\tools\analyze_eye_projection.py <local-workspace>\reports\native-ipc-20260927-160643 --trace <local-workspace>\reports\native-trace-20260927-160643-503
```

Exit 2 is the expected saved-baseline projection failure; 3 means inconclusive.
Use -FovSweep for the four input controls. Allow the 40-second native exception
watcher to detach before starting another attach. `unavailable_attempts` counts
missed polls, including pending work; it is not a count of expired requests.

Source backups and discarded experiments are retained under
`reports/rendering-repair-20260927-source-before`. The workspace has no Git metadata;
there is no commit to cite. Original BF2142 files, installed BC2 binaries/assets,
OpenXR selection, startup entries and scheduled jobs were not modified.
