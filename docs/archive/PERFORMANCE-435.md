# Performance investigation — checkpoint 435

Date: 2026-10-21. Research and an actual-code scheduling reproduction; no new
runtime build, installation, or release. Installed/public version remains 0.1.12.

## Result

The frame-request schedule imposes a half-refresh ceiling on fresh world images
even before game rendering or GPU work. The shipped launcher also overrides the
runtime's recommended eye dimensions with the desktop game's dimensions. Both
are confirmed from the active code path. Repeated rig reads are a substantial
CPU optimization candidate, but their elapsed cost has not yet been measured.
The evidence does not establish that the external tester is CPU-bound.

The user's relayed headset feedback reports successful opening knife kills,
progression to the scientist, working crouch, loading text and manual reload,
and no VR dropout during that run. It also reports poor performance on an RTX
5080 and cutscene-facing errors. The tester's CPU, headset, refresh rate, runtime,
render scale and diagnostics are unavailable. This is external human feedback,
not a local measurement or full-campaign acceptance.

## 1. Confirmed request scheduling limit

`src/ipc/RemoteFrameProvider.cpp:7` polls an outstanding request. On success it
returns the pair and its original tracking at line 21. It does not begin another
request until a subsequent call. `src/xr/OpenXrHost.cpp:377` calls this once per
XR frame. Source ownership is acknowledged after the host copies/presents the
pair; the producer must then reap that exact acknowledgement before reusing it.

The existing schedule, with an instantaneous synthetic producer, is:

| Logical XR tick | Provider action | Fresh world image |
| --- | --- | --- |
| 1 | Begin request A | No |
| 2 | Deliver A; acknowledge after copying | A |
| 3 | Begin request B | Reuse A |
| 4 | Deliver B; acknowledge after copying | B |

The private `ProviderCadence435.cpp` links the real `FvrFrameChannel.lib` and
`FvrCore.lib`, using the actual provider/channel rather than a reimplementation.
Its single execution passed: 120 ticks, 60 requests, 60 fresh pairs, 60 exact
acknowledgements, 59 reused presentations, one initial blank and zero timeouts.
Distinct tracking samples verify that delivered pixels keep their original pose.
There is no game, GPU, headset, input injection or simulation replay in this test.
This proves the scheduling limit; it is not a measured FPS benchmark.

At 90 Hz, the ideal limit is 45 fresh pairs/second; at 120 Hz it is 60. Rendering,
copy fences and missed polling opportunities can lower it further. A faster GPU
cannot remove the extra request-start tick. Reused layers can still be submitted
at the headset's cadence, so compositor FPS is not fresh game-image FPS.

First implementation target: allow the next immutable request to be queued while
the old pair is being consumed, and let the producer take it after acknowledging
and releasing the old source. Preserve the GPU-copy fence, exact ticket ownership,
original pose/timestamp, original deadlines, and cancellation on menu, recenter,
tracking/session loss or resolution change. Do not overwrite the shared slot,
acknowledge before GPU completion, or label old pixels with current tracking.
Calling the existing provider twice without handling producer reaping is not a
complete fix. This work belongs in the shared frame transport, not a BC2 weapon.

## 2. Confirmed desktop-resolution override

`launcher/runtime/Session.ps1:61` passes `menuReady.width/height` as `--eye-width`
and `--eye-height`. `OpenXrHost.cpp:168` first selects the runtime recommendation,
then replaces it with those values at line 171. Startup checks require the two
sizes to match (`Session.ps1:70`), and native request admission rejects dimensions
that differ from the game's current viewport (`NativeProbe.cpp`, `TryGetPair`).

Consequently a 1920x1080 desktop source produces 1920x1080 per-eye textures even
when the runtime recommends a different size. Merely removing the override would
fail the current source/viewport checks. Upscaling the same source would not be
native-resolution rendering either.

The target is a coordinated render-size setup: obtain the runtime recommendation,
configure actual game eye render targets/viewport, then use consistent color,
depth, relay, body-prop, menu and XR dimensions. Keep the desktop window size
separate where the engine allows it. Record recommended and selected dimensions
and each eye's runtime maximum. Report unsupported allocation/configuration
explicitly; do not silently claim native resolution. The runtime recommendation
and the headset's physical panel dimensions are different concepts.

## 3. CPU candidates, ranked by code evidence

**Batch fresh rig metadata reads.** `Bc2Rig.cpp:37` reconstructs the first-person
rig from `Bc2RigPublication.cpp:1158`, on the native animation output path. Bone
names are read one byte at a time (`Bc2Rig.cpp:28`) through self-ReadProcessMemory.
For a successful snapshot, the read-call count is:

```text
40 + 2 * nativeIK + 5 * boneCount + sum(bone-name lengths)
```

The saved 434 rig has 147 bones, 1,707 name characters and native IK enabled:
**2,484 read calls per successful snapshot**, before caller work. Batch current
bone records and the name-pointer table: for this rig, 588 calls become two,
leaving 1,898 before improving string reads. Bounded, page-aware string reads
with a smaller-read fallback can remove most remaining calls. An otherwise
valid short string near an inaccessible boundary must still be readable.

This first step needs no cross-frame cache. Keep fresh animation matrices,
before/after ownership checks, hidden-leaf handling, exact native bytes and
matrix-consistency validation. Respect the callback's 64 KiB read bound for
larger rigs. Later immutable caches need actor/equipment lifetime and skeleton,
definition and name-table identity; pointer identity alone is insufficient.
Count reduction is not yet a measured frame-time improvement.

**Gate continuous diagnostic collection.** `Bc2RigPublication.cpp:1196` rebuilds
weapon-profile evidence every 100 ms under `frameMutex`, hashing bones, walking
ancestors and allocating/copying strings and vectors. Its consumer is the final
diagnostic report. Make this capture explicit rather than ongoing during normal
play. Do not remove `--pose-observe` indiscriminately: the animation scope enabled
by that path currently permits weapon retargeting. Its separate startup pose and
phase observations are already bounded; they are not indefinite full rig scans.

**Filter irrelevant projection callbacks before reads.** `NativeProbe.cpp:950`
reads the context projection address/matrix before later excluding ineligible
calls. Outside the selected evidence frame, move those eligibility checks ahead
of the reads, retaining the weapon-projection branch and verified depth callers.
This can remove two reads per excluded callback. Frequency and cost need timing.

The original world simulation is called once, not replayed once per eye. Native
view preparation/restoration, rig work and graphics submission still have CPU
costs; this finding does not make the rest of the render path free.

## 4. Existing telemetry and its limits

The explicit local session `20261020-125259-682-d41037e4` is version **0.1.10**,
source `bc035d08a511df6d0d57cbf7bb78689a8d7842977cbc6bee953ac5940621c133`.
It is an older local SteamVR session with a failed end state, not the external
5080 tester's run and not a fresh 0.1.12 benchmark. The surviving host report
and periodic telemetry contain:

| Observation | Value | Meaning |
| --- | --- | --- |
| Eye dimensions | 1920x1080 | Actual source and host dimensions |
| XR frames / recorded span | 29,405 / 334.47 s | About 87.9 host frames/s overall |
| Fresh / presented world pairs | 9,357 / 26,234 | 16,877 reused presentations (64.3%) |
| Positive near-one-second intervals | 301 / 302.64 s | Subset with newly published pairs |
| Fresh-pair interval median / p10 / p90 | 30.51 / 28 / 42 per second | Not whole-session game FPS |
| Mean / maximum async request latency | 19.966 / 102.117 ms | Includes waiting; not GPU execution |
| Async timeouts | 27 / 9,397 requests | Reported transport outcomes |

The private 434 emulated receiver summary separately records 320 consumed pairs,
zero timeouts and mean/max async latency 19.365/31.699 ms. It establishes neither
headset FPS nor CPU/GPU utilization. Its result alone does not pin build identity;
the enclosing 434 acceptance records must be used for that attribution.

World callback counts are not game FPS. Host stage durations are elapsed wall
time, and `HostFrameDiagnostics` stores selected slow/transition rows and zeros
ordinary stage values below 20 ms. Do not derive all-frame percentiles from that
sparse ring. Large `xrWaitFrame` durations can be runtime pacing: the call is
designed to block until the runtime's chosen next-frame time. See the
[Khronos frame-submission guide](https://github.com/KhronosGroup/OpenXR-Guide/blob/main/chapters/frame_submission.md).

The legacy shared-texture consumer (`D3D11SharedPair.cpp:201`) flushes then polls
an EVENT query for up to 20 ms to protect source reuse until its GPU copy finishes.
Removing this fence would violate ownership. Deferred completion is worth
investigating, but first measure fence delay separately from GPU execution.
An EVENT reports completion; paired TIMESTAMP queries within a valid DISJOINT
interval provide elapsed GPU time. See
[Microsoft's D3D11 query definitions](https://learn.microsoft.com/en-us/windows/win32/api/d3d11/ne-d3d11-d3d11_query).

## Implementation and acceptance order

1. Remove the transport's mandatory idle request tick. Extend the actual-code
   reproduction to demonstrate one fresh pair per logical tick after warmup for
   an ideal producer, then cover slow producer/copy, cancellation, old feedback,
   tracking/reference loss and source resize. No busy wait or simulation replay.
2. Add bounded aggregate timings around rig reads/retargeting, view preparation,
   request start/completion/reaping and host stages. Add asynchronous GPU timing
   queries; skip unavailable results instead of blocking. Separate CPU execution,
   elapsed waiting and GPU work. Compare the same scene, runtime and resolution.
3. Batch the rig reads and gate diagnostic-only collection. Verify equal decoded
   rigs and exact restoration, boundary reads and lifetime changes before one
   combined actual-game mechanics/recovery check. This is shared across weapons.
4. Configure actual runtime-recommended eye render targets and measure the extra
   pixel cost. Confirm texture sizes throughout the pipeline, stereo/depth/menu
   correctness, tracking/reconnect behavior and GPU budget. Do not mask the
   scheduling problem by reducing resolution.

Use one bounded baseline/candidate comparison per substantive change, not full
repeated campaigns. Compare fresh-pair rate and retained age in addition to XR
submission rate. Check menus, loading, death/restart and a representative manual
reload/pump to preserve the newly working gameplay. Cutscene facing is a separate
reported camera issue to reproduce; this research has not fixed it.

## Reproduction artifacts

Private directory: `G:\Unity\VRMMO\bc2vr-recovery\performance435`.
It contains about 445 KiB of source, executable/object and JSON evidence, with no
raw captures. Storage admission passed before compilation; all writes were on G:.

- `ProviderCadence435.cpp` and `provider-cadence.json`: real-provider reproduction.
- `build-receipt.json`: fixture, executable, linked-library and source hashes.
- `AnalyzePerformance435.py`: reusable read-only analyzer, explicit session path,
  optional receiver result; no recursive scan. Inputs <=16 MiB each, native rows
  <=20,000, host rows <=4,096, output <=64 KiB. Validates identity and monotonicity.
- `local010-and-emulated434-summary.json`: measured values, source pins and limits.

Analyzer invocation: `python -B AnalyzePerformance435.py --session <directory>
--emulated <receiver/result.json>`. Existing evidence was analyzed once. No game,
headset or desktop-input session was started, and no production source or
installed runtime was changed for this investigation.
