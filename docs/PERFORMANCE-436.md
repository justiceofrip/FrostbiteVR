# Scheduling and fresh rig reads — candidate 436

This unpublished candidate follows the measured baseline in PERFORMANCE-435.md.
It is not yet a headset performance result. Installed/public 0.1.12 is unchanged.

## Latest: R46 matched comparison — October 26

Games90/91 supplied a qualified same-runtime comparison after the host-menu GPU
copy was changed from a blocking20ms wait to the existing asynchronous copy
API. Both normal architectures passed305/305; both actual-game runs then had
zero menu errors, async rejections, timeouts or feedback errors. Normal close,
native teardown and exact restoration of the three user profiles passed.

Both arms used2784x3024 per eye, actual1x world/depth samples, the same adapter,
scene/viewpoint bounds, profiles, installed48-record cache, queue1 and existing
post-Present service. All nine adjacent counter intervals were retained:

| Private policy | Consumed fresh stereo pairs | Time | Pairs/s |
| --- | ---: | ---: | ---: |
| Two sources, no export overlap/refill | 404 | 9.0635697s | 44.574049 |
| Three sources, export overlap/refill | 806 | 9.0461958s | 89.098226 |

The descriptive combined-policy gain is99.8881%. No interior discards were
removed;507/762 full-CPU retained depth records passed. The retained host
prediction-age mean changed31.96→22.50ms, which is not motion-to-photon latency.
Logical allocation rises202,051,584bytes, not a measured resident-VRAM delta.

This comparison changes the combined pipeline policy, not resolution or image
quality between arms. It is not an isolated slot-count experiment and does not
measure public0.1.12, whose default remains the single-source path. It is not
physical-headset FPS, a combat benchmark or proof of full campaign playability.
The final desktop scenes matched visually; startup captures were black and none
falls inside either CPU interval, so paired pixel/headset acceptance remains open.

Frozen runtime receipt SHA
`edec49147b8b91a5bf92597384380b130af5a27fe3d3d8e271ddf0c3fed60a86`.
[Strict comparison](G:/Unity/VRMMO/bc2vr-recovery/performance436/r46-matched-analysis-prep/comparison.json)
SHA `13332ce438b514627f19a2ec2e6dc23be74539c4c24719faa4e44e35c897a254`.
[Independent counter/depth/scene review](G:/Unity/VRMMO/bc2vr-recovery/performance436/r46-matched-analysis-prep/comparison-independent-review.json)
SHA `09b65f1f51036129b33bb9e1e1ae2438df5bc3d6e2676c61cde1bd36e878e211`.
The sections below retain earlier investigations; CURRENT.md owns next actions.

## External tester configuration received October 22

The user relayed AMD Ryzen 9 9900X (12-core, reported 4.40 GHz), Quest 3,
90 Hz and Virtual Desktop VDXR for the earlier RTX 5080 performance report.
The tester does not recall their in-game graphics settings. Virtual Desktop
quality/render dimensions and measured frame timings are still unavailable.
This identifies the target configuration but does not attribute its bottleneck.
There is no benchmark-validated BC2 VR settings preset yet; settings advice
must be labeled a provisional baseline, not a demonstrated 90 FPS solution.

## Next-request scheduling

The actual RemoteFrameProvider/FrameChannel reproduction delivered only 60
fresh stereo pairs over 120 logical XR ticks even with an instantaneous
producer. Delivery returned without starting another request until a later XR
tick. The candidate delivers 119 pairs over the same 120-tick sequence.

Control protocol v8 adds one immutable queued request; there is still only one
shared GPU slot. Receiving a completed pair queues the next tracking sample,
while the delivered ticket, pose, body props and depth remain attached to the
pair actually being presented. The producer cannot promote the queued request
until it has reaped the exact old GPU acknowledgement and the slot is idle.
This does not replay game simulation or acknowledge an image before copying it.

Focus, space and size cancellation removes the queued request before pending
old feedback is flushed. Deadlines do not renew while queued; contradictory
feedback is rejected. Protocol v8 host/native components must be installed
together; the mapped control namespace changed with the wire layout.

PipelinedFrameChannelTests uses the actual named IPC implementation and covers
contention, queued expiry, cancellation, original-pose metadata and the
120-tick schedule. AsyncFrameChannel, StagedFrameProducer, body/depth and
ownership regressions also passed their initial focused runs on x86 and x64.
Complete build and actual GPU/native acceptance are recorded in CURRENT.md and
the private performance436 receipts, rather than inferred from these tests.

## Bounded fresh-read batching

Bc2Rig now reads bone records and name-pointer tables in bounded blocks and
uses page-bounded name reads. It falls back to the required record fields or
individual name bytes when a wider read is unavailable. Native world,
evaluated and skin transforms are still read fresh. Ownership is checked
before and after; no cross-frame pointer or pose cache was introduced.

The measured 147-bone / 1,707-name-character snapshot previously required 2,484
reads. The batched shape needs about 191, with a regression ceiling of 197.
Coverage includes page boundaries, unreadable padding, changed metadata at the
same address, owner replacement and the hidden SPAS leaf. This demonstrates
reduced call count, not measured CPU milliseconds or a CPU-bound diagnosis.

## Validation limits

The full x86 run initially caught stale binding assumptions in MenuPointer and
the emulated SceneRecovery approach. Tests and the actual approach generator
were updated together for Y crouch; the corrected focused checks passed.
Manual equipment and knife behavior were not replaced to obtain these results.

GPU menu/depth checks, a native-sized real-game menu-to-world session with
emulated tracking, and physical-headset timing cover different boundaries.
Synthetic cadence cannot establish headset comfort, streaming performance,
campaign completion or the reported left-eye destroyed-building artifact.

See NATIVE-RESOLUTION-436.md for native target setup and JEEP-TURRET-436.md for
the independent campaign camera correction and outstanding seat acceptance.

## Camera cache correction found by the combined run

The r2 actual-game run found an exact secondary-camera mismatch: dirty bit4
cleared and only the scalar cache at offsets0x210..0x21c changed. Native getters
0x5bb470 and0xb213d0 both call updater0x9739d0. Its verified stores and dirty-bit
clear match those observed changes. The previous preparation rebuilt bits1,2,8
but omitted this cache, allowing a later native getter to mutate the installed
camera after the exact-write snapshot.

The corrective candidate precomputes this fourth cache on its owned camera
copies using the verified original function. Full camera comparisons, actor/view
ownership and exact restoration remain unchanged. Tests reproduce the omitted
cache mutation and verify an unrelated camera write still fails equality.
`camera-scale436-proof.json` records the static executable evidence; actual
native acceptance remains the responsibility of the new combined run.

GPU failure diagnostics now latch operation with the first failure stage. A
successful texture-sharing fallback no longer leaves operation5 attached to a
later source-dimension rejection. Wrong-size sources still fail before copying;
there is no live resolution renegotiation in this change.

## Cancellation and GPU comparison correction

R6 retains cancellation while exact restoration finishes and denies a cancelled
request before its first camera write. A dimension-only mismatch may discard an
unsubmitted BC2 pair; a later pair still needs the original XR dimensions. Device,
format, array, mip and sample failures retain their existing fatal behavior.

The x86 GPU regression exposed a separate input mutation during the old by-value
frame-metadata comparison. A private diagnostic reproduced the caller's color
ticket becoming zero across that call. The comparison now takes const references
and explicitly compares every prior non-color field. The GPU test verifies its
attachment/ticket inputs remain byte-identical across copy, duplicate rejection,
publication and reuse. This is not evidence of a specific compiler defect.

Both normal r6 builds passed 296/296 suites; the real GPU depth/bridge checks
passed on both architectures at 1/2/4/8 samples. Game06 had already delivered
181 actual stereo pairs with zero camera restoration failures before a native
resolution reset. The later r6 combined check stopped before Continue because
resolution reset earlier. A native-only observer has since identified the
original resolution-menu handler selecting 1024x768 without minimizing or a VR
adapter. Fixing that native menu integration remains necessary before accepting
runtime-recommended resolution. No new headset performance claim follows from
these build, GPU or synthetic tests.

## Existing emulated-session timing limits

Reanalysis of idle04 found ten clean, post-start intervals totaling 10.047 seconds
before the failed interval: 11–22 fresh pairs/second, median 15.263. Across its
181 delivered pairs, request-to-delivery latency averaged 61.037 ms. The existing
desktop pacing bypass was active (7,391 of 10,183 originally sync1 Present calls
bypassed); the saved VSync setting alone cannot explain this measurement.

This was a gunner/attached scene: all 2,012 local updates were vehicle-gated,
with zero rig attempts or tracked rig copies. The emulated runtime supplied valid
neutral hand poses, but the native owner was not on foot. Its 256 early world
timing records were also exhausted before the first active pair. Consequently
this session does not measure the rig batching improvement or separate native
render time, GPU transfer/fence readiness and queued-request waiting. It is not
physical-headset FPS. `idle04/performance-analysis.json` records exact input
hashes, interval exclusions and proposed bounded, pair-correlated timing.

The subsequent on-foot check below establishes the actual transition and rig
publication. The earlier idle gunner measurements must not be relabeled as
player-hand rendering evidence.

## R9 resolution acceptance and CPU observation

R9 passed the combined native-resolution check in game13/idle08, including
Continue, minimize/restore and cleanup. Both normal architectures passed
297/297 suites. See NATIVE-RESOLUTION-436.md for the distinct guard-owner-death
check and exact build identity. Installed/public 0.1.12 is unchanged.

During a 10.31-second post-restore window, BC2 consumed 1.6165 equivalent CPU
cores. Its exact gameplay/Continue UI thread consumed 0.9327 core over the
9.30 seconds covered by complete thread intervals. It was substantially busy;
this does not distinguish native game logic, mod work, or a CPU spin wait.
Nine positive publication intervals totaling 9.0467 seconds delivered 187 fresh
pairs, median 21.859 pairs/s. Whole-run request latency averaged 55.239 ms, with
a 109.127 ms maximum and one timeout. These are emulated-runtime measurements,
not physical-headset FPS or a controlled improvement over the earlier run.

The observer itself consumed 6.375 CPU seconds over 74.33 elapsed seconds
(0.0858 core); its repeated full-thread snapshots were expensive. The run also
included 269 MB of initial native surface captures. All 1,665 gameplay updates
were vehicle-gated, with zero rig attempts, tracked poses or copies. Consequently
neither the rig batching gain nor a clean game baseline is established.
`combined-r9/cpu-analysis.json` records source hashes, phase boundaries, sample
completeness and exclusions. A separate retained-handle targeted-thread sampler
avoids repeated enumeration for the next distinct on-foot observation.

Read-only code review found repeated same-invocation rig hashing, quadratic
bone-name duplicate checks, and a continuous 10 Hz weapon-profile sampler under
the pose-publication mutex. These are candidates for measured optimization,
not attributed causes of the observed main-thread utilization. Scene milestone
logging is bounded and runs its file writes on the reporting thread; simply
disabling pose observation would also disable required retargeting context.

## R9 on-foot CPU coverage exposed a presentation failure

Game15/onfoot01 used one verified X press/release through the normal VR input
path, with no gameplay-memory writes or OS input. The same soldier/weak identity
became the controlled actor with no attached vehicle. All 139 read-only monitor
samples retained that identity and route; zero were rejected. Their 14.917-second
span encloses the complete targeted CPU interval with 199 ms before it and
2.714 seconds after it. The harness still failed its required 140 samples. That
raw failure remains unchanged; this independently valid measurement did not
require another run. External observations are sampled and non-atomic.

The exact gameplay/Continue UI thread (196964, creation134370769581239354)
consumed 11.34375 CPU seconds over 12.00464 seconds: **0.94495 core**, including
3.78125 seconds of kernel CPU. The process averaged **1.80219 cores** over
11.89523 seconds. The retained-handle sampler consumed only 15.625 ms CPU over
12.00568 seconds; its measured collection work totaled 5.1734 ms. Unlike the
earlier full-thread observer, it did not enumerate threads while sampling.
These counters identify a busy thread, not the functions or GPU work causing it.

Tracked rig publication was exercised: session totals include 1,754 tracked
poses/weapon outputs, 3,618 copies and 1,754 paired copies, without tracking or
packing rejection. The retained native reference samples identify the same
actor and Nambu100. Those are pre-VR pose references, not proof of submitted
mesh binding. Final rig counts are session aggregates rather than timed CPU
phases; neutral hands do not exercise support-grip, firing or reload workloads.

The important failure is downstream presentation. Eleven positive publication
intervals totaling 11.05994 seconds produced 112 pairs (median 9.949 pairs/s),
but **consumed zero and discarded 112**. The whole run completed 280 host
requests, submitted 120 pairs before the on-foot stall and rejected 160. All
160 supplied scene-depth pairs were copied. The retained host ring contains 128
new-pair rows: every one was rejected and every one had a slow ColorCopy stage,
29.1309–45.5356 ms (median 37.47475 ms). In the CPU interval the same correlation
holds for all 97 retained new-pair rows, with no world layer submitted. SharedOpen
never crossed the 20 ms recording threshold; its whole-session maximum was
3.1492 ms. The ring dropped 122 earlier records, so it does not locate the exact
first rejection at exit. These stage durations include CPU waiting/scheduling;
they are not GPU execution times.

The R9 legacy shared-texture consumer spins for a 20 ms completion budget
then returns Busy as a fault. The host treats any non-Ok copy as failed feedback,
retiring the producer's resource generation. Existing timings and the one-to-one
depth/rejection counts justify correcting this path, but do not log the exact
query return responsible for each rejection. The resulting correction retains the
exact ticket and resources while copy completion is pending, polls without a
per-frame spin, and acknowledges only proven completion. Cancellation and
shutdown must preserve in-flight GPU ownership. R10 implements and separately
validates this asynchronous correction below; the R9 results do not validate it.

Native hooks disabled cleanly, the exact resolution lease was released with
callbacks drained, and the owned game closed normally. Cleanup success does not
turn this failed presentation into campaign acceptance. Source-pinned analysis:
`performance436/onfoot01/analysis.json`, SHA-256
`3f606a4abeaf334465c0683843243fbb781b73a8bd9cce39565e852ac33690e4`.
No additional game run was used for that analysis. R9 was not accepted for a
headset test or release; its failed receipts remain unchanged.

## R10 asynchronous copy and on-foot presentation acceptance

The host now submits the color copy once and polls its completion on later XR
ticks without a per-frame spin. A pending operation retains the exact source
ticket, scratch resources, original tracking, body props and depth. Successful
GPU completion releases source ownership even when focus, recenter, pose age or
a menu makes that image ineligible for presentation. Incomplete work is never
acknowledged as completed. Fault/timeout retirement and shutdown retain the
separate resource-lifetime path. Shared-channel cancellation is sticky across
mutex contention and executes before deferred acknowledgement can promote the
queued next request. Bounded diagnostics distinguish submit, pending, complete,
cancel and retirement without writing a log on every frame.

Both normal R10 architectures passed **297/297** suites, including focused IPC,
GPU copy/depth and diagnostic coverage. Source closure:
`3a482e89c7035bfbb431e1b9ff5575673d997247defe3442603519ce4714730b`.
Normal runtime receipt:
`bbf1ae3c5c0cfd0ac513b229b291e6bf7941a05909a21eec9e6161470340b7c1`.

Game16/onfoot02 then passed one bounded real-game run at 2784x3024 per eye with
emulated tracking. One X press/release used the normal input path to exit the
vehicle; no OS input or gameplay-memory writes were used. The corrected monitor
checks elapsed coverage rather than demanding 140 samples: 139 unchanged actor
and route observations spanned 14.929714 s, with maximum gap 110.3732 ms. They enclose
the full 12.005651 s CPU window. VR was stopped 71.052 ms after its end while the
independent actor witness finished. The source-pinned measurement receipt is
`performance436/onfoot02/measurement-acceptance.json`.

| Measured result | Scope |
| --- | --- |
| 300 submitted copies, 300 completed, 300 presented pairs; 0 rejected | Complete host session, including menu/loading and on-foot gameplay. |
| Maximum pending-copy age 42.934 ms; 0 retired/cancelled copies; final result/error 0 | Pending spans multiple XR ticks; this is elapsed readiness, not GPU execution time. |
| 177 published, 176 consumed, 0 discarded | Eleven adjacent near-one-second intervals totaling 11.051964 s, wholly inside the CPU window with 100 ms boundary margin. |
| Median consumption 15.9328 pairs/s; range 13.9420–16.9176 | Positive measured intervals, not whole-session or physical-headset FPS. |
| 84 distinct completed, successfully ended world pairs | Retained host diagnostic rows wholly inside the CPU window; the 256-row ring is not every frame. |
| Main thread 11.484375 CPU s / 12.005643 elapsed s = 0.95658 core | Exact gameplay/Continue UI thread 194168, creation 134370797161079334. |
| Process 23.0625 CPU s / 11.896414 elapsed s = 1.93861 cores | Process sampling window; no attribution to engine, mod or graphics work. |
| Observer 15.625 ms CPU; collection 4.5407 ms total, 0.1137 ms maximum | Retained-handle sampler; no repeated full-thread enumeration. |

The real on-foot rig ran: session totals were 1,406 tracked poses/weapon outputs,
2,902 copies and 1,406 paired copies, with zero tracking or packing failures.
Body composition now reached 200 pairs and 790 instances, with no invalid,
missing, expired or failed instances. These are aggregate coverage counters,
not timed rig phases or validation of individual weapon gestures. Color-copy
submission wall time peaked at 4.7781 ms and zero-wait poll time at 0.0338 ms;
the earlier rejection correlation is gone in this run. Whole-session
request-to-delivery mean was 69.423 ms, maximum 124.758 ms, with zero request
timeouts. Neither wall-time counter measures GPU execution duration.

Cadence remains a distinct question. Every retained completed copy matches a
submission on the previous host tick; each new-pair interval spans exactly
four host ticks. The audit measured a median new-pair interval of 61.829 ms and
typical host ticks around 15.327 ms, despite the fixture's 11 ms predicted period.
The diagnostic ring zeroes stage durations below 20 ms, so it cannot attribute
that cadence in detail. These observations do not identify depth copying as
the cause of roughly 16 consumed pairs/s. Freeze R10 and next measure request,
fence and host cadence with a validated fixture clock before choosing another
optimization; no speculative graphics change is included in this checkpoint.

Native hooks disabled with zero camera restoration/GPU failures. The exact
resolution guard released and drained, settings.ini stayed unchanged, and the
owned game closed normally without termination. Parent acceptance is in
`performance436/onfoot-r10/completion.json`. Native totals include 302 published,
300 consumed and 2 discarded outside the measured interior; do not describe
that as zero discards across the whole session.

This establishes bounded on-foot presentation after the R9 failure. It is not a
controlled FPS speedup, full campaign/weapon acceptance or a physical-headset
test, and it does not identify the remote RTX 5080 tester's bottleneck. The main
thread remains busy and needs phase attribution alongside that cadence check.
Installed/public 0.1.12 remains unchanged.

## R11 measurement instrumentation and startup failures — October 22

Both normal architectures passed **298/298** suites. The native source closure
is `a3b1f1810418dfc4eb8084babc0e0ac4ab4a53055e35700a508bba4d47f3c393`;
the normal build receipt is
`86aa9ba4872a3f4b594b04189de2923703c300a14a2414f2af90113c221f145a`.
The fixed-capacity native journal records CPU observations of lease, export,
publication and exact-ticket release. Host aggregates include fast as well as
slow stages; full copy tickets permit exact native/host joins. Missing or
contended observations are explicit, not zero-time samples. Neither measures
GPU execution time. Production scheduling is unchanged from R10.

The private fixture uses rational 90 Hz QPC deadlines and a high-resolution
waitable timer, without busy spinning or changing the global timer policy.
Deterministic clock tests passed; fixture receipt
`4e17be3610b6acfd12a8be3c3b0ff6bcb2d03d30f70a6834da981af1736cbeef`.
An apparent rate difference from correcting the old fixture is not a production
or headset FPS improvement.

Game17/onfoot-r11 passed native resize but failed a single window observation
immediately afterward, before XR. Exact guard release and normal game close
passed. A private runner overlay moved stable renderer readback before a
bounded read-only window settle check. It sends no focus, show or input action.

Game18/onfoot04 passed that settle check and the optional save-menu inventory
(one valid `Levels/SP_01` entry; no checkpoint-body loadability claim). Continue
returned and its hook drained/restored. BC2 then exited with code `0xC0000005`
before a world pair or CPU window. The host recorded 230 menu frames and zero
projection frames. Its clock ran at approximately 90 Hz, with four skipped
slots, no clock failure and no spin. These are startup observations only.
The saved game crash report has EIP/EAX zero and a return-address pattern
matching earlier AMD-worker startup failures, including ones before VR
injection. No game18 module map exists, so that resemblance does not establish
the module or cause. Native cleanup and retained guard release could not run
after the process exited; the failure receipt remains failed. Evidence is
preserved in `performance436/onfoot04` and `onfoot-r11b`.

Next, obtain one bounded exception/module capture before deciding whether to
change code or resume the R11 timing comparison. Do not repeatedly run the same
failed performance fixture or classify debugger-instrumented timing as a
performance baseline. Public/installed 0.1.12 remains unchanged.

That single exception-observed run completed as game20/onfoot05. The existing
watcher attached before Continue for at most 30 seconds, captured zero
exceptions and detached while the game remained alive. The unchanged R11
runtime produced 319 presented pairs, with 320 GPU copy submissions and 319
recorded GPU completions. The separate `async_completed=320` counter counts
delivered tickets. There were 322 native publications and one host timeout
across the whole session.
On-foot coverage, exact native cleanup, resolution-guard release, unchanged
settings.ini and normal owned-game close passed. The raw capture total was
269,402,112 bytes; no exception dumps were created. This is bounded integration
evidence, not a clean performance comparison or a fix for the earlier crash.
`onfoot-r11c/completion.json` and `onfoot05/measurement-excluded.json` explicitly
exclude performance acceptance. Its bounded journals can still be inspected
to identify handoff delays without presenting them as headset FPS.

The offline diagnostic join retained 201 complete native pairs wholly inside
the 12-second observation window and 84 exact native/host ticket matches.
Mean request-to-restored time was 9.77 ms, export-to-observed-producer-fence
22.71 ms, publication-to-reaped-acknowledgment 20.31 ms, and release-to-next
admission 6.80 ms. Bounds from the host's input-return timestamp narrow
host-completion-to-native-reap to a mean 4.19–4.28 ms. Of 84 joined host copies,
81 completed on their first permitted next-tick poll and three on the second.
The native depth descriptors recorded eight samples for all 201 pairs.
These intervals include CPU/GPU scheduling and polling quantization; they are
not direct GPU execution measurements. The journal dropped no marks, but its
256-row capacity overwrote 66 earlier rows. Analysis files under `onfoot05`
retain the debugger/performance exclusion explicitly.

## R12 candidate: later native graphics service

The selected small change adds one zero-wait transport service before the
original native Present, after camera/context restoration. It can observe
already-complete exports or host acknowledgments before the next native Update.
Request admission stays at its original pre-visibility boundary. Exact renderer,
vtable, device, context, swapchain and owner thread must still match; disconnected,
fatal, test-Present, unrestored and nested/reentrant boundaries cannot service.
The callback lifetime is held during validation and service. A scope spanning
the full outer Present permits at most one service, including when native menu
or mirror work reenters Present. No simulation, camera write, buffer ownership,
host scheduling or weapon policy is changed.

The final `desktop_pacing.serviced_calls` scalar records successful services.
The new guard regression and staged-producer regression cover rejection and
exact old-ticket acknowledgment without early admission of the queued successor.
Both full normal architectures passed **299/299** suites. Frozen R12 receipt:
`performance436/r12/normal-runtime/build-receipt.json`, SHA-256
`2906d9b46c6706389903b1f2be16e77b4abbf349333ff0e21a2c4969fa121c7f`.
The source closure is
`c020c8b0f19bdfcf3e9fc678ea1ccbe25ce9dd79b3cf7e360fee32cafe9db717`.

Game21/onfoot06 passed without a debugger at 2784x3024 per eye. The unchanged
private fixture was rebound only after all 57 original source pins and 33
compiled dependencies matched R12; no timing test or fixture build was repeated.
The Present service executed 8,418 times. Eleven interior intervals covering
11.064 seconds published and consumed 206 pairs, with zero discards: weighted
18.6195 fresh pairs/s and median interval rate 18.877. The fixture actually ran
at 89.940 Hz. The measured main thread used 93.635% of one core; the process
averaged 1.926 cores. This is emulated-runtime integration evidence, not headset
FPS or function/GPU attribution.

The bounded native journal retained 256 of 359 lifetimes, with 103 older rows
overwritten and zero dropped/unmatched/invalid-clock marks. Of these, 221
complete lifetimes fall wholly inside the CPU interval. Their mean export to
observed producer fence is 21.999 ms, publication to reaped acknowledgment
20.579 ms, and full native lifetime 51.418 ms. Median release to next admission
is 0.278 ms (mean 2.709 ms). Eighty-four unique full-ticket host joins bound mean
publication to host submission at 6.002–6.141 ms, publication to completion at
17.231–17.298 ms, and completion to native acknowledgment reap at 2.858–2.925 ms.
Eighty-three copies completed on their first permitted next-tick poll; one on
the second. These observed waits remain the dominant scheduling spans; they do
not identify time executing on the GPU or time spent inside a driver call.

There were 358 GPU copy submissions and 357 recorded completions/presentations.
The final pending native request began before explicit shutdown and was
discarded afterward, outside the accepted interior intervals. The host's
`async_completed` counter is a delivery count, not GPU completion count.
No host timeouts, rejected pairs, copy retirement or measured copy errors were
reported. Native cleanup, exact guard release, unchanged settings.ini and normal
owned-game close passed. Eight raw captures total 269,402,112 bytes.

The offline result is `onfoot06/r12-measurement-analysis.json`, SHA-256
`cdbb8b2b615fdd3c75441a2fb92741470585957393434323c91515ee2ef54fa1`.
R10 used different fixture pacing and R11 included a debugger, so neither is a
controlled percentage-speedup baseline. R12 demonstrates the added service runs
and retains valid integration, while overall performance remains unresolved.
Next distinguish time inside producer publication/driver calls from the gap
between polls, and measure native draw versus rig work using bounded same-thread
cycle/QPC aggregates. Installed/public **0.1.12 remains unchanged**.

## R13: earlier host completion and failed CPU-attribution activation

R13 adds a zero-wait completion poll before `xrWaitFrame`, after known
cancellation. A completed source receives its exact acknowledgment once, while
the host retains its local color/depth images and original poses until they are
presented or dropped. That local ownership blocks another provider query/copy.
Deferred IPC feedback can be serviced without querying a new pair; cancellation
still precedes flushing it. Ready-image cleanup neither cancels a completed GPU
operation nor sends a contradictory negative acknowledgment. Exact host
submission/completion observations replace input-return bounds for this path.
Earlier completion is an opportunity, not an assumed performance gain.

The same candidate contains a default-off CPU recorder, enabled only by the
private child-process variable `FVR_BC2_PRIVATE_CPU_ATTRIBUTION=12`. It was designed
to start on the configured graphics thread after a fresh on-foot rig owner is
resolved, collect for 12 seconds with an 8,192-sample cap per stage, and report
after callbacks drain. Inclusive native drawing, rig/read/profile and graphics
transfer stages record QPC elapsed time and thread cycles separately; their
totals must not be summed as disjoint work or called GPU execution time.

The private recorder harness passed its C++ checks and 146 report assertions.
An empty bracket plus loop/bookkeeping averaged 733.6 ns in its 512-sample batch;
the raw clock-pair control averaged 668.0 ns. These are local harness overhead
measurements without baseline subtraction, not in-game profiler measurements.
R13 passed **299/299** normal suites on each architecture. The reviewed source
closure is `ff6442b32c64495ea055fb975cfff4be1e21c996f2afaab08e8465f5772bdad9`.
The frozen build receipt SHA-256 is
`ebef86ddff7aa300d9d9abeac0cb2c695d13c58a73c0225d52e62ba6a05e3941`.

Game23/onfoot07 completed without a debugger at 2784x3024 per eye, with
372 GPU submissions, completions and fresh presentations, no copy faults or
timeouts, and successful native cleanup, resolution-guard release, unchanged
settings.ini and normal owned-game close. Eleven interior intervals covering
11.068 seconds published and consumed 194 pairs without discards: **17.529
fresh pairs/s**. Main-thread utilization was 95.50% of one core; the process
averaged 1.983 cores. This is not physical-headset FPS or a demonstrated gain.

Eighty-four exact full-ticket joins measured mean publication-to-submit
6.199 ms, submit-return-to-observed-completion 11.247 ms, and completion-to-native
reap 3.116 ms. One of those 84 copies completed before `xrWaitFrame`; only two
of all 372 completed copies did so. The added poll therefore rarely found a
ready copy in this run. These remain CPU observations, not GPU execution times.

The native CPU recorder was enabled and drained but never entered collection.
It was armed for graphics setup thread 47476; its owner creation/start/end
clocks remained zero. All 1,244 verified rig activation attempts were counted
as foreign, matching 1,244 tracked poses/weapon outputs. The external sampler
measured thread 53540. This invalidates stage attribution, not the separately
validated copy timing. Do not interpret zero stage samples as zero cost or
infer that every stage shares the rig thread. The next measurement correction
must track actual verified callback thread lifetimes, with independent records
if stages have different owners.

Offline result: `onfoot07/r13-measurement-analysis.json`, SHA-256
`75b3f70e8f812dd22ff25a2a0d69b29adc17c6ecda99adbbf82f911292f50a67`.
Installed/public **0.1.12 remains unchanged**.

## R14: useful CPU attribution, no accepted performance gain

R14 changes only the private attribution header: an independent graphics record
and up to eight verified rig-thread lifetimes, with bounded samples and explicit
partial windows. No new native hook or runtime policy change. The deterministic
owner harness passed 45 checks in eight cases; both normal builds passed
299/299 tests. The frozen receipt SHA-256 is
`4869b638edfddc2831e42781b7ce39e85bc40c523dcb886da504d22a21f6340f`.

Game26/onfoot08 at 2784x3024 per eye is a **failed acceptance run**: the parent
reported a bounded controller-process wait timeout. The child wrote its passed
observation result within 2.503 seconds of the stop marker, but its actual OS
exit and the parent's wait-entry/return timestamps were not recorded. This is
not proof of a stale Process object, nor a reason to extend the six-second wait.
Add those timestamps on the next otherwise-needed run. Seven native request
timeouts and two rejected pairs remain in the whole-session report. Cleanup,
guard release and normal close passed. No performance improvement is claimed.

The external CPU interval is enclosed by 139 fresh same-actor on-foot samples,
with no rejects. Exact main/UI thread 49976 used 10.8125 CPU seconds over
12.003806 seconds (**90.0756% of one core**); the process averaged 1.75368 cores
over its own interval. The profiler gate overlaps that sampler for 10.843944
seconds, but its complete totals must not be prorated to that overlap.

Main-thread RigTotal: 379 samples, 180.1293 ms inclusive wall time and
830,381,900 cycles out of 50,237,487,370 observed owner cycles (**1.6529%**).
FreshRead accounts for 0.6229%; report-only ProfileCapture for 0.02768%.
Therefore disabling detailed report capture is not the principal demonstrated
CPU fix. Work outside these scopes includes other mod hooks as well as native
engine work; neither is yet attributed.

Graphics thread 37444 recorded ordinary draw 22.1473%, tracked draw 7.5603%,
extra-eye draw 7.1885%, graphics pump 0.9792%, and publication 0.6776% of its
observed cycles. These are inclusive CPU scopes, not disjoint costs or GPU
execution times. Two rig lanes have partial endpoint windows; no recorder
overflow, stage clock, reentrancy or cap errors were reported.

Diagnostic result: `onfoot08/r14-cpu-diagnostic-analysis.json`, SHA-256
`ef0a9ca81b766daa8fd9a7483a28744415c8247d07bad8eacaf9019df9633f2e`.
Next collect bounded sampled instruction pointers/stacks with exact process,
thread lifetime and QPC selection. The private WPR profile parses successfully
and requests 16 MiB of buffers. No symbols need downloading for module+RVA
attribution. Installed/public **0.1.12 remains unchanged**.

The private x64 decoder and two-second workload subsequently built, with
41/41 pure checks. Receipt:
`16ebd9d8dad32cfcbb769acd181b8d04e0266dee23a856953266647999ffed59`.
synthetic01 could not start WPR because the non-elevated token lacked profiling
privileges (0xc5585011). Exact own-instance status confirmed no recording.

The user approved a single elevated synthetic check. synthetic02 started and
its workload returned normally, but stop/merge hit the unchanged 30-second
bound. The owned stop process was terminated; an immediate cancel saw another
WPR control instance still draining. A later exact own-instance status reported
`WPR is not recording`, with no WPR process remaining. Preserve the original
failed completion and separate `postfailure-status.json` recovery observation.
No BC2 game or global sampling setting was changed.

Only the rundown ETL remains. It measured 2,359,296 bytes during the controller
check, but late writes continued until 13:06:47.454Z and its final size is
5,505,024 bytes. This exceeds the reported polling snapshot; do not call that
snapshot the final peak. Offline parsing counted 28,528
events, no sampled IPs/stacks, 8,604 identity-decode errors and 19,920 image-decode
errors. This is neither a successful format validation nor performance evidence.
Diagnose the metadata errors from this existing file before another capture.
The first observed disk snapshot was about 2.26 MiB, not proof of later or
between-poll peaks or a kernel-memory cap. No opaque trace collection loop or
longer deadline.

A diagnostic-only decoder revision reused the unchanged 41-check core proof
and parsed this same file. TDH property and schema calls both return 1168
(ERROR_NOT_FOUND), with and without pointer context. The first failures are
classic Thread opcode 4, version 3, flags 832, 96-byte payloads. This distinguishes
missing metadata from a known field value; no raw layout fallback has been
enabled. Final retained ETL SHA-256:
`e10cf6ed3827b1b65b0c33af88babbed572f4b6cf846efeda9ca04c3feb14a32`.

The next recorder design to investigate is the documented exact-instance flush
to raw collector files followed by cancellation, avoiding WPR's save/merge path.
It is prepared source-only, not validated. Confirm the preceding controller
exits before any cancellation; the failed immediate cancel overlapped a
draining stop.

### Subsequent cleanup correction and offline evidence

WPR's exact-instance "not recording" response did **not** prove kernel cleanup.
At 13:39 UTC, `logman query -ets` still listed the synthetic02 main and rundown
collectors. The rundown file remained open. A narrow elevated cleanup stopped
only those two exact names and verified both absent at 13:41:34 UTC, receipt
`synthetic02/kernel-cleanup.json`. No new recording or global settings change.
The closed file is now 9,699,328 bytes with SHA-256
`a9986e75ae8995ed8d96d79a9fe603f5e4978b9fb8bdf466db9b908172672685`.
The earlier 5.25 MiB/hash was an intermediate state, not a finalized trace.
Future cleanup must verify actual kernel enumeration, not WPR bookkeeping alone.

Decoder03 built with 48 targeted schema checks; its original 41-check
lifetime/window core was unchanged and reused. Build receipt:
`47e5a1569b5c1a4aee3fc2b486e0c8078ff8244dfec6293e49dcdf89cc310665`.
One pass of the closed trace completed with 40,723 events, no reported loss,
1,350 decoded process records and 11,160 decoded thread records. All 28,207
image records use version 3 and were explicitly unsupported; no sampled IPs
are present. This verifies part of the parser, not attribution or trace capture.
The exact-main-thread sampler extension separately passed 38 pure checks,
including two-thread endpoint separation and bounded reads. Parent/game
integration is still pending.

Source audit identified three measurement candidates outside `RigTotal`:
reload callback owner copying before target rejection (24,181 callbacks,
19,294 owner misses), repeated full owner/reflection validation in gameplay
callbacks, and gather-side tracking/inventory publication. Counts are from the
whole R14 session, not timed costs. The owner gate had only five contention
recoveries, maximum 300 ns; this does not establish lock waiting as the hot
thread's cause. Attribute these paths before changing lifetime or reload policy.

Decoder04 added only the observed Image version 3 layout from the pinned
Microsoft parser reference. Its consolidated 62 schema checks passed, with
the original 41-check lifetime/window core reused. Receipt:
`8a38b724e598b4919b611c07eff2e5b7d1da243fbb61ca46db791cadacd0f176`.
`synthetic02/closed-rundown-inventory04.json` decodes all 1,350 process,
11,160 thread and 28,207 image records with zero malformed/unsupported records
or reported loss. No sampled CPU events were present. This resolves the
retained-file schema problem; actual sampled-stack capture is still unverified.

The source-only R5 recorder now selects only its exact owned collector stem or
an owned underscore suffix, excluding terminal/suffixed rundown names. Its
parser passes; plan SHA-256 is
`435522179c1f1a858efd89b005871eff06101fe57e04f91e80c05c5bbaeafba3`.
The separately requested elevation for this replacement remains unanswered;
no replacement recording or game run has occurred.

A private controller-wait patch is prepared for the next useful integration
run. It records QPC/UTC around the unchanged six-second wait, retained-process
exit observations and a bounded completion-file snapshot. These observations
do not change timeout acceptance or treat a successful file as proof of process
exit. Patch SHA-256:
`b4ef75aa00ca25faf52643a8cc07dc60b137ae398cde509491f5105fe06a5b67`.

The source-only destruction review found no newly proven defect. The existing
left-draw override restores its exact writes, but that does not prove every
fracture consumer saw the correct eye. On the next visibly affected pair,
compare draw-time projection bindings with the distinct expected eye matrices
before expanding to instance-payload capture. Do not repeat unaffected captures
or disable culling without evidence.

The reviewed `r14-prep/onfoot-cpu15` parent/child now integrate the unchanged
38-check sampler and wait patch. A bounded read-only observer obtains the main
image's actual mapped base/size from retained process/thread handles before
the on-foot action window; it does not use the PE preferred base. Its nine
pure header/identity cases passed once. Root verified 101 dependency pins and
both PowerShell parsers, receipt
`74f4a07d6139d3e21e2032fbcabf24a1765f372c632621a6286c3d9e7d99016a`.
No new native fixture/runtime build, process observation, game run or profiling
occurred. Existing acceptance, recording limits and deadlines are unchanged.
This harness exports the identity/window for a future overlapping trace; it
does not yet orchestrate that recording. Do not run it solely to repeat the
existing CPU-utilization result while synthetic capture remains unverified.

### Approved replacement capture: synthetic03

The user approved the revised two-second elevated check. R5 completed at
14:29:46 UTC on October 22. The workload ran for 2.0000022 measured seconds;
raw flush took about 3.0 seconds, cancellation 131 ms, and offline decoding
93 ms. All owned controller processes exited normally. Kernel enumeration
confirmed no residual owned collectors, both in the runner and an independent
root readback. No game process, native adapter, global sampling setting or
installed/public package was changed.

The retained raw ETL is 8,388,608 bytes, SHA-256
`ea6159a024d4a2aed87de0df440f788750d4fed9d3ea28c1b0ef73e715b30be6`.
The capture folder retains about 26 MiB including temporary collector evidence,
under its 128 MiB admission. No loss or overflow was reported.

Exact PID 55100/main TID 9172, retained-handle births, binary hash and QPC
window match. The decoder accepted 1,974 IP samples; 1,959 (99.24%) fall in
the workload's BusyLoop, map entry RVA 0x5bd0, observed RVAs 0x5be3–0x5c1f.
There are 1,973 matching mapped user stacks, predominantly BusyLoop -> wmain ->
CRT startup. An independent source/map review confirms this attribution.

Limits: three accepted IPs remain unmapped. The 141 stack-decode failures are
counted before owner/window filtering, so their target membership is unknown.
Successful IP analysis is not a claim of complete stack coverage or full-window
retention. This is sufficient evidence that the revised capture can identify a
known CPU workload, not a game performance measurement or FPS improvement.
Preserve `synthetic03/completion.json`, `summary.json`, `identity.tsv`,
`raw-preserved.json` and `root-review.json`. Do not repeat the synthetic check
without a new reason; next integrate an overlapping BC2 recording.

### Actual BC2 capture: cpu17 / game33 / onfoot10

The October 22 capture completed at 23:22 UTC using the unchanged R14 runtime
and emulated 90 Hz fixture at 2784x3024 per eye. Combined acceptance, continuous
on-foot observation, native and profiler cleanup, resolution-guard release and
normal owned-game close passed. Installed/public **0.1.12 remains unchanged**.
This is an accepted diagnostic run, not a headset-performance or speedup claim.

The selected retained-handle window is QPC 553250121202–553340112943 at
10,000,000 Hz: **8.9991741 seconds**, PID 65044/TID 64952, with exact process
and thread births verified. Decoder05 accepted 8,533 IP samples; 3,277 map to
the main executable and 5,256 remain unmapped. It matched 5,199 stacks containing
main-image frames. The 1,369 stack-decode errors are counted before owner/window
filtering; their selected-thread membership is unknown. No event/buffer loss,
duplicate selected samples or module conflicts were reported. Full trace
retention and complete stack coverage remain unproven.

The raw collector lacks startup metadata because BC2 preceded recording. Its
same-instance rundown corroborates PID/TID and the main-image mapping. Only that
independently observed executable is supplemented; rundown-only DLLs remain
unmapped. Its DCEnd record matches base, size, exact mapped path and nonzero PE
checksum, but omits the timestamp. The reported module timestamp therefore
comes from the pinned PE, explicitly marked **not verified by ETW**. Nonzero
conflicting timestamps still fail. The narrow policy passed 49 checks; original
41 core and 62 schema checks were reused.

Bounded disassembly verifies the following associations. Counts below are lower
bounds from the retained top 32 stack rows, not whole-thread stage durations:

- The client-render chain `0x56F55E <- 0x56FB30 <- 0x574667` appears in 2,320
  samples (at least 27.19% of accepted samples). `0x56F55E` returns from
  `0x540D00`, which constructs `clientPreRendGrp` and `clientRender` dependencies,
  including UI/sound jobs. Missing intermediate x86 frames prevent identifying
  the exact child job responsible for the dominant wait.
- The secondary chain `0x835CC6 <- 0x56EB71` appears in 158 samples (1.85%).
  Its `0x7B6037 -> 0x508CC0` wait is specifically on `animMngrGroupJob`, with
  `animMngrPre`, `animMngrPost` and `animMngrWAnim` dependencies.
- Timer-polling chains through `0x5098A2` and `0x6E0ED3`/`0x6E5302` contribute
  at least 533 samples (6.25%). The imports resolve to QueryPerformanceCounter
  and SwitchToThread. The native wait loops check completion/time, help the job
  scheduler and yield or conditionally sleep. High CPU utilization consequently
  includes native dependency/pacing work; it does not establish equivalent
  useful rendering or rig computation, or quantify a safely removable fraction.

`pair-timing-selected.json` contains 164 complete native pair lifetimes wholly
inside the same selected window. Mean observed boundaries are 8.735 ms from
pair begin to camera restoration, 22.083 ms from export begin to first observed
producer-fence readiness, 0.012 ms to publication, and 20.659 ms from publication
to acknowledgement reaping; begin-to-release averages 51.492 ms. These are
elapsed observations, not disjoint CPU costs or GPU execution durations.

Only 30 late-window pairs have retained exact host-ticket joins. For that
subset, publication-to-copy-submit averages 8.135 ms, submit-to-observed-copy
completion 10.934 ms, and completion-to-reap 1.904 ms; none completed at the
pre-Wait service point. Do not generalize those host percentiles to all 164
pairs. Eight wholly selected adjacent telemetry intervals cover 8.0449775 s,
with 148 publications, 147 consumptions and zero discards: 18.272 consumed
pairs/s, **not headset FPS**. The fixture's whole-session measured cadence was
89.941 Hz, separate from this interval.

The next unresolved discriminator is actual GPU completion versus delayed CPU
observation/polling: producer-fence and host-copy times currently combine GPU
execution, queueing and service cadence. This capture does not prove which
component causes those intervals, nor which worker dependency the main render
wait is awaiting. Preserve this result before a targeted change or measurement.

Evidence under `performance436/` (SHA-256):

| Artifact | SHA-256 |
| --- | --- |
| `onfoot-cpu17/completion.json` | `3e8f2fbb0abe12bf4d73b0eec4f92fec911a6b97679ecc3f57ee2168cd18fc21` |
| `onfoot10/cpu-profile/decoded-final.json` | `d705d5293c9e511cc3e345f36aeca9556c57dd07d10ed2f6fd4abc6048f43058` |
| `onfoot10/cpu-profile/pair-timing-selected.json` | `733d183c47371c24ec3e789d633bae87bf578763b7b0b02caa60dadb07f53da2` |
| Raw ETL, 47,710,208 bytes | `bbd967d89ae5cfc5d96fc5643fa82db702ba04c0aaf1b03a9f25604ab69f225b` |
| Matching rundown ETL, 10,747,904 bytes | `d7083c38d5bdc608eb727eab220213346441c04bdf69cc8634ae51b2e5228809` |
| `r14-prep/cpu-etw/decoder05/build-timestamp/build-receipt.json` | `d2850c1395e7e59bdc3e9a7eb4047cf2a7fe24f8894da7fb469006068dc5adc3` |

### Ordinary main-view work between VR pairs — onfoot10 appendix

The same accepted R14 run confirms continued native main-view draw execution
when a new VR pair has no lease. `BeginTrackedFrame` disables only the extra
view before `TryBegin`; its `BridgeNoLease` return does not disable the main
view. `VisibilityHook`, `PrepareHook` and `OriginalDrawWithColorScope` still call
their native originals. `WorldHook` calls the original once, and the verified
native world-render contract consumes request phase 1 to 3. These are current
call-path observations, not authorization to skip those operations.

Whole-session counters are 9,057 world/update calls and 9,459 calls each for
visibility, preparation and draw: exactly 402 additional calls, matching the
402 accepted/rendered stereo pairs. `BridgeNoLease` occurred 8,651 times, but
this count includes the period before Continue and does not distinguish a
missing host request from an outstanding export or acknowledgement.

More specifically, the diagnostic CPU record for the exact graphics-thread
lifetime, TID 58468, spans QPC 553242022960 to 553361968332 at 10 MHz. Its draw
scopes validate the current world, request, main/extra view and native frame,
and surround the actual `drawOriginal` invocation:

| Original draw scope | Calls | Summed QPC duration | Share of this graphics thread's observed cycles |
| --- | ---: | ---: | ---: |
| Ordinary main view | 1,023 | 1,188.5404 ms | 21.4867% |
| Tracked main/left view | 217 | 208.7252 ms | 7.0956% |
| Additional/right view | 217 | 397.2313 ms | 7.0168% |

This roughly 12-second graphics record overlaps but is wider than the selected
8.999-second ETW window. It is not the sampled main/UI thread, TID 64952. QPC
durations include preemption and native calls; cycle shares are not GPU times
or a demonstrated removable fraction. The 164 complete pairs inside the ETW
window span native frames 13114 to 14032; 163 consecutive increments range
from 4 to 7 and average 5.63190184. Neither frame increments nor draw entries
prove that every intervening call submitted a full scene's geometry.

The evidence therefore supports ordinary main-view work continuing between
VR pairs, including transport waiting, but not a claim of five wasted GPU
frames per pair. Commands submitted after the export event on the same context
do not precede that event; possible interference includes work already queued
before a later pair and competition with host GPU copies. Draw/Present also
service export completion and acknowledgements. Any future gating must preserve
that progress, native request/job completion, view-item consumption, menu and
lifetime transitions, and simulation. No gating change has been made.

The existing one-pair `EyeGpuEvidence` records pipeline statistics, not GPU
timestamps. A future bounded timing observation must distinguish actual GPU
intervals from the existing CPU observation times before attributing the
22.083 ms producer-fence interval to rendering or copies.

Source: `src/games/bc2/NativeProbe.cpp` (`BeginTrackedFrame`, `VisibilityHook`,
`WorldHook`, `PrepareHook`, `OriginalDrawWithColorScope`),
`src/ipc/StagedFrameProducer.cpp`, and
`src/graphics/d3d11/D3D11SharedPair.cpp`.
Evidence: `performance436/onfoot10/native-trace.json`, SHA-256
`e7b64a06d6bad67756d3015587d813cc05e1805a1a08f25fca5d0fa55882eab8`.

### Separate mapped-buffer CPU lead

The graphics TID 58468 has 166 instruction samples in the retained histogram at
RVA `0x5989AD..0x5989DD` (VA `0x9989AD..0x9989DD`). Exact installed-image
disassembly and a second read-only review locate these inside executable `.text`
in `sub_9988B0`, ending at `0x998D66`. This is a six-vertex quad upload/draw helper;
no retail function name has been established.

The renderer's `+0x80` context getter and SDK vtable slots establish Map at
`+0x38`, Unmap at `+0x3C`, and Draw at `+0x34`. The helper maps its buffer using
WRITE_DISCARD, writes the quad, then reads ten DWORDs back from `mapped.pData`
to duplicate two vertices. It unmaps and draws six vertices with 20-byte stride.
Microsoft documents potentially expensive reads from write-mapped resources,
which may use write-combined memory:
[Map remarks](https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11devicecontext-map).

This is a concrete optimization lead, not proof of the actual allocation's
memory attributes or exact stall cost. Sampling can attribute delayed loads to
following store instructions. The bounded follow-up verified both upload
branches; all byte ranges below are inclusive offsets from `mapped.pData`:

| Mode | Complete construction | Mapped reads used for duplication |
| --- | --- | --- |
| 0 | Six 20-byte vertices, `0x00..0x77` (120 bytes); XY, packed white and UV. Four corners fully initialize `0x00..0x4F`. | Ten DWORDs at `0x9989A8..0x9989DA`: `0x00..0x13` to `0x50..0x63`, and `0x28..0x3B` to `0x64..0x77`. |
| 1 | Six 24-byte vertices, `0x00..0x8F` (144 bytes); XY, packed white and three float attributes. Four corners fully initialize `0x00..0x5F`. | Twelve DWORDs at `0x998CA7..0x998CEB`: `0x00..0x17` to `0x60..0x77`, and `0x30..0x47` to `0x78..0x8F`. |

Both branches form corners `(x,y)`, `(x+w,y)`, `(x+w,y+h)`, `(x,y+h)`,
then append vertices 0 and 2 for triangle-list `Draw(6,0)`. Mode 0 uses the
four standard zero/one UV corners. Mode 1 has six selector cases with verified
`+1/-1` constants; an unsigned selector above 5 initializes all three attributes
to zero. Its attribute semantics remain unnamed. Neither branch needs old
mapped contents. Modes other than 0/1 skip upload/draw but retain the outer
exit callback.

A candidate could preserve this construction in invocation-local ordinary CPU
scratch (at most 144 bytes), then only write the exact 120/144-byte result to
the mapping before Unmap. This is not a verified in-place patch: the existing
stack reserves only `0x14` bytes, many operands are ESP-relative, and X/Y
arguments are overwritten with rounded sums. Re-reading those arguments or
reconstructing them by subtraction would not preserve exact results. Any
implementation must prove scratch/reentrancy lifetime, x87 rounding, stack and
register ABI, callbacks, Map/Unmap, input layout, stride and draw equivalence.
The 166 samples establish interest in mode 0 only; mode 1 frequency and any
gain remain unmeasured. No native patch or hook has been implemented.

### Worker attribution from the same cpu17 recording

The separate offline `process-threads01` decoder passed 30 focused checks and
reused the accepted schema/main-image validation. It matched 17,509 samples
across 17 TID buckets using exact sampled QPC/TID and the StackWalk packet's
explicit PID. No new capture or game run was needed. Duplicate sample keys,
foreign-PID collisions, mapped-stack conflicts, capacity overflow and reported
event/buffer loss were zero. Samples without a matching ownership packet remain
unassigned; worker births are not inferred from ETW. The original main TID has
8,392 matched samples here, a stricter subset of the earlier 8,533 IP samples.

**A main-image frame is not necessarily an executable caller.** The decoder
maps the complete independently verified image, including data sections.
For worker 5892, 135 samples in its retained top stack rows contain only
`.rdata` addresses (`0x1424850`, `0x1416B70`, `0x14188D8`); seven for worker
65132 contain a `.data` address. These are not interpreted as executed work or
callsites. Unknown DLL/kernel frames, x86 frame omission and the eight retained
main frames also prevent treating displayed sequences as complete direct chains.

Bounded disassembly of the pinned executable gives these lower bounds from the
top 16 rows per histogram; the rows are not complete worker profiles:

| TID | Matched samples | Verified association |
| --- | ---: | --- |
| 5892 | 2,150 | 78 stacks through the EA Jobs worker loop; 28 instruction samples in `0x625C40`, a tree traversal with SIMD geometric tests. No dominant useful function is established. |
| 65132 | 929 | 88 worker-loop stacks and 104 at animation/getter return `0x8ADE95`. |
| 65364 | 650 | 96 worker-loop stacks and 14 at the same animation/getter return. |
| 65384 | 589 | 75 worker-loop stacks. |
| 59188 | 346 | 328 main-image instruction samples, including arithmetic at `0x13366xx`; subsystem and dependency remain unidentified. |

The loop at `0xA30D00` attempts work through backend virtual slot `+0x68`; its
no-work path calls `WaitForSingleObject`. A loop-root stack alone therefore
does not measure time waiting. Workers 65132/65364/65384 also have 15 retained
stacks through `0x984EB2 <- 0xA350DB <- 0xA30D1E`, confirming execution of an
indexed geometric-processing callback with atomic work distribution. Its exact
job-group relationship to the main-thread render wait is unproven.
`0x8ADE95` is the verified animation-to-weapon-world-getter return site; unmapped
mod frames may lie above it, so its 118 stacks are not all classified as native
animation or as rig cost. Another 2,318 matched samples in seven other buckets
have no mapped main-image IP or stack; they remain unidentified.

The separate exact-owner CPU records provide inclusive rig measurements over
approximately 12 seconds, wider than the selected 8.9991741-second ETW window:

| Owner TID | RigTotal samples | Summed QPC duration | Recorded cycle share |
| --- | ---: | ---: | ---: |
| 64952 | 627 | 301.6668 ms | 2.6120% |
| 65132 | 565 | 274.0667 ms | 21.4365% |
| 65364 | 42 | 21.4553 ms | Not used: closing endpoint overshoots the deadline by 1.059 s. |

Five unregistered RigTotal entries were omitted. Late partial records for 5892,
58468 and 65384 do not overlap the selected ETW window; their small/zero totals
cannot characterize those workers during sampling. These nested measurements
are not total mod cost. GraphicsPump's 49.2073 ms and ProducerPublish's
39.2778 ms likewise overlap and must not be added as independent costs. The
worker evidence establishes mixed scheduling and useful computation, without
identifying one dominant blocking child job or a removable CPU percentage.

Evidence under `performance436/`, SHA-256:

- `onfoot10/cpu-profile/process-threads.json`:
  `82b61b319bf63fd133693044c08f7a97222762f48d7c1fc621f5ce929911543f`.
- `r14-prep/cpu-etw/process-threads01/build/build-receipt.json`:
  `863c639c951c6c48e0d18308807fcd939d37a106bfce453a3481a0ddb8edb975`.

Installed/public 0.1.12 is unchanged. No speedup or headset-performance claim
follows from this additional offline analysis.

## R16 actual-game GPU attribution

The R16 runtime passes 300/300 full normal suites on each architecture. Focused
private GPU tests, actual D3D11 hardware timing smoke checks and scene-depth /
desktop-mirror checks in sharing modes 1/2/3 pass on both architectures.
Production timing remains off unless `FVR_PRIVATE_GPU_TIMING436=8` is selected.
It preallocates 193 queries for 96 interval slots and one disjoint epoch, caps
recording at eight pairs and 16 ordinary draws, and adds no production Flush,
wait, query reuse or callback allocation. The hardware-test Flush is confined
to the test harness and does not prove native polling cadence.

`game34/onfoot11/gpu-run01` completed at 00:53:07 UTC October 23 (October 22
local), with real graphics and emulated 90 Hz tracking at 2784x3024 per eye.
Its parent acceptance, GPU evidence, native cleanup, guard release and normal
owned-game close passed. The native fixture sent one 100 ms exit-vehicle pulse;
no desktop input/focus change was used. Settings remained unchanged. Eight
startup images consumed 269,402,112 bytes; no further image capture was added.

The run consumed 341 fresh pairs, published 344 and issued 345 requests. It
recorded two request timeouts and zero host-rejected pairs, host-copy errors,
host retirements or failed host frames. The timeout-associated discard
increments preceded this GPU epoch; early native timing records were already
overwritten, so exact failed request attribution is unavailable. Do not erase
these failures or claim a gameplay, crash, jeep/death-recovery or headset pass.

All 88 recorded GPU intervals are valid. Eight full tracked-pair identities
join the consumed native tickets and exact graphics owner (TID and birth),
with valid GPU order and no clock/query/owner/context/device/key failures.
GPU frequency is 100,000,000 ticks/s. The CPU recording epoch spans 2,005.8588 ms.
GPU marker issuance ends 168.9815 ms before the separate external CPU sampling
window starts. Graphics-owner telemetry overlaps, but the external CPU sample
window is not the GPU marker window. GPU execution itself has not been
calibrated against CPU QPC and cannot be placed on that CPU timeline.

| GPU interval | Count | Mean milliseconds |
| --- | ---: | ---: |
| Ordinary native draw | 16 | 9.940600 |
| One tracked-eye draw | 16 | 9.588565 |
| Both tracked draws, summed per pair | 8 pairs | 19.177130 |
| Stage-0 depth copies, summed per pair, **inside tracked draws** | 8 pairs | 2.128800 |
| Stage-1 depth copies, summed per pair, after tracked draws | 8 pairs | 0.040445 |
| Both eye-color captures, summed per pair | 8 pairs | 0.209935 |
| Two-eye export | 8 | 0.288095 |
| Left tracked draw begin through export end | 8 pairs | 19.715615 |

Stage-0 depth is nested: **do not add it again to draw time**. The original
offline summary v1 incorrectly labeled all depth copies nested. Its evidence
is retained; corrected `gpu-interval-summary-v2.json` distinguishes stages.
The actual game capture was not repeated for this analysis correction.

Only the first two gaps have complete ordinary-draw coverage before the
16-draw cap:

| Between tracked native frames | Gap from preceding export end to next tracked-left begin | Ordinary intervals inside gap | Fraction |
| --- | ---: | ---: | ---: |
| 11636 to 11646 | 90.61088 ms | 9 draws, 87.48128 ms | 96.55% |
| 11646 to 11652 | 49.47816 ms | 5 draws, 48.00796 ms | 97.03% |

These are inclusive same-context GPU timestamp intervals, potentially including
queue idle time, preemption or other-context contention. They are not exclusive
GPU busy time, a measured removable fraction or an FPS improvement. Later gaps
have incomplete coverage. Instrumentation and the short early-on-foot sample
also limit generalization. The two CPU issue-interval means (1.747475 ms for
ordinary draws and 2.164294 ms per tracked draw) are a different measurement.

### Optimization target and native dependency limits

The strongest new target is ordinary rendering between VR pairs. The evidence
does not justify removing depth/color copies as the dominant cost, nor does it
establish a safe main-view bypass. Independent native/code audits found:

- `DrawView` at `0xBB1DB0` owns subsystem/cache work and pending-view cleanup:
  it releases non-null entries in `view+[0x1670,0x1674]` at `0xBB3BBB`, then
  resets end to begin at `0xBB3BD3`. Returning before the original Draw skips it.
- `WorldRender` at `0xBB4D80` runs separate active-view Prepare and Draw loops,
  subsystem begin/end callbacks, then marks `request+0xC4=3` at `0xBB4EF5`.
  Prepare schedules/checks/waits a native job group at `0xBB1D6B..0xBB1D8C`.
  Disabling main only at World would occur after visibility and skip those
  dependencies. The existing extra-eye disable does not prove main-view safety.
- The native `world+0x80` branch at `0xBB2C01` occurs after substantial drawing
  and skips usual cleanup; its semantics are not an established raster switch.
- The mod Draw hook calls full `PumpFrameBridge` for cancellation restoration,
  body-ammo stop, connection, input observation and graphics close. Present
  calls only `PumpGraphics` and requires restored state. Preserve both services.

A pending-frame policy must retain exact channel/request/owner/device/tracking/
space/time identity through export, publication and acknowledgement, and clear
it on exact completion, cancellation, expiry, failure, peer loss or owner change.
Failed acquisition and `tracked.state==2` do not establish a pending frame.
Preserve ordinary rendering during menus, bootstrap, recovery, focus/size loss
or uncertain state. No ordinary-frame suppression was implemented in R16.

The mapped-buffer lead remains secondary. Exact Map return sites `0x998914`
and `0x998A49`, paired with Unmap returns `0x9989EE` and `0x998D07`, identify
the 120/144-byte branches. Redirecting their construction to bounded CPU scratch
could retain original x87 math, then copy only writes to the real mapping.
Scratch must outlive the Map interceptor and be bound to the exact invocation,
context, resource, subresource and branch. An unmatched Unmap after redirect
cannot simply fall back to native behavior. Resource length, HRESULT/pitches,
reentrancy and exception/retirement paths still require proof. No patch exists.

A separate build-efficiency audit found 407 x86 / 402 x64 build edges, including
265 x86 links. The native provenance guard is already order-only; the changing
capability header reaches data-only consumers through `Bc2SelectedMeshes1p.h`,
and 257 binaries link BC2Camera. Future cleanup can split stable binding types
from capability admission and narrow test library dependencies. It must retain
full source-closure validation and avoid differing inline capability definitions
across translation units. No build-graph change is part of the frozen R16.

### R16 receipts and retained limits

Paths below are relative to private `performance436/`; SHA-256:

- `r16/normal-runtime/build-receipt.json`:
  `a4c450542c2a2a64866fc5d821df23630f466d0a240d1a8e9e0f15181512b2b1`.
  Source closure: `7a10e5d9333e9b759997878b1f3941ee5ea23d90847635fe859b79863b2edb79`.
- `r16-prep/gpu01/rebind/build-receipt.json`:
  `3ab537a6b64a6f660004f274f5890b02d88cb251640cc0a9ab2d3ca3c5e6ef5b`.
- `gpu-run01/gpu-evidence.json`:
  `9ecadc24ee63d369b93503f67caf9e57c07d57812b98cfbccd84f2825a426514`.
- `gpu-run01/gpu-interval-summary-v2.json`:
  `e2398b7bd48e547d54ff7b22baa502d082120efa990bcacc0719bc4769a7950b`.
- `gpu-run01/independent-gpu-analysis.json`:
  `4fd4270f9be4bdc6c4a15280cfb731d7c15e195c1dfb20eefd24b0ea6412d39e`.

Selected-symbol NTFS compression preserved hashes and modification times;
24 verified stale incremental-link caches were removed. Total allocated space
reclaimed in this step was 691,592,620 bytes, without removing current runtime
symbols, compiler objects or useful evidence, or increasing the 40 GiB cap.
Installed/public 0.1.12 remains unchanged. R16 has not been headset-tested,
installed or published, and no before/after speedup has been measured.

## R19 same-process presentation comparison

On October 23 local (completed October 24 at 02:06:36 UTC), the frozen R19
candidate completed one actual-game, emulated-tracking crossover. The process,
scene, tracking fixture, quality settings and 2784x3024 eye dimensions were
retained across all four phases. Each phase lasted about ten seconds; analysis
discarded its first two seconds and used only complete adjacent telemetry
intervals entirely within the remainder. Zero-growth intervals were included;
missing samples were not bridged. Each retained window covers about 7.04 s.

| Phase | Queue limit | After-Present service | Fresh consumed pairs/s | Process CPU, equivalent cores | Main-thread fraction of one core |
| --- | ---: | --- | ---: | ---: | ---: |
| A1 | 3 | Off | 18.894292 | 1.904419 | 0.930664 |
| B | 3 | On | 17.759738 | 1.902624 | 0.922913 |
| C | 1 | On | **21.866547** | 1.923452 | 0.955314 |
| A2 | 3, restored | Off | 17.475766 | 1.953870 | 0.933652 |

The C intervals range from 21.807 to 21.918 pairs/s. Its mean is 15.73% above
A1 and 25.12% above A2 (23.12% above B). Baseline A2 is 7.51% below A1.
These are descriptive within-session differences, **not a guaranteed causal
speedup**, a headset/streaming FPS measurement, or evidence of a resolved CPU
bottleneck. Phase order is fixed and the full native world workload is not
measured. Service alone has no demonstrated gain. Main-thread CPU remains
nearly one busy core; limiting the native graphics queue is the useful
candidate from this comparison.

### Preserved scene and ownership evidence

The native camera observer reads the already-copied camera before VR adjustment;
it adds no native reads, writes or input. All phases retain the same camera
owner and tracking-space generation 2. There are no invalid/dropped camera
observations, and all observations fall inside their applied phase. Combined
native position spans are 2 mm, 4.6 mm and 2 mm at the configured world scale.
A1 includes a small initial basis adjustment (maximum component delta
0.000551939); subsequent within-phase changes are about 0.000001175. This
avoids R17's separate-launch, roughly 30-degree viewpoint confound. It does not
prove identical geometry, simulation or GPU work throughout the session.

All 256 retained native timing rows report 2784x3024, depth sample count 8,
sample quality 0 and depth mask 15. No resolution, MSAA, HBAO, VSync or other
quality setting was lowered for this comparison. Game settings are read-only
inputs to the experiment; local development does not use C: for new files.

The crossover opens only pre-created, session-specific request/acknowledgement
events. Queue changes occur on the verified graphics owner at a restored
Present boundary. Five requests were acknowledged, the phase controller
completed and closed, and queue 3 was restored. The separate normal queue
policy was observation-only and made no writes during this experiment.
The native original Present result and LastError, ordinary Update/Prepare/Draw,
camera restoration and persistent equipment state are preserved.

### Failures, cleanup and validation limits

The full session completed 965 fresh host pairs, published 968 native pairs
and discarded 3. All three discard increments occurred before A1; the last
was at least 4.236 s earlier. No later discard growth occurred. The host also
records six request timeouts without timestamps. Retained ring buffers cover
only the end of the run, so those six cannot be assigned to individual phases
or declared harmless. There were no host-rejected pairs, GPU errors, copy
failures, camera-restoration failures or feedback errors. All 13,453 desktop
Present calls returned S_OK. Native cleanup, resolution-guard release and
normal owned-game shutdown passed.

Full x86 and x64 builds passed 302/302 suites each. Focused presentation,
staged-producer and GPU tests passed, with native ABI checks on x86. The first
x64 recipe failed before compilation on an x86-only focused target; an x64-only
retry filtered that target and retained the original source freeze and failed
logs. Nine pure phase-analysis tests passed once. Successful existing pacing
proof was reused by its exact hash; no duplicate gameplay capture was made.

The benchmark used neutral input after one exit-vehicle pulse. It does not
accept knife damage, reloads, pump/bolt operation, holsters, jeep use, death or
mission restart. Physical runtime scheduling and perceived motion still need a
headset check. Public/installed 0.1.12 is unchanged, and queue 1 remains private
and process-scoped. No public rollout is implied by this result.

### R19 receipts

Paths are relative to private `performance436/`:

- `r19/normal-runtime/build-receipt.json`:
  `ca4885f755bc3fac830712d0bfd7f78f88428f6c8c72bc1f825ed98c96a2f352`.
  Native source closure:
  `9b63b2f547897b0eb54c885772c3125719cb073164009603441cd6ff0c318b8c`.
- `r19-prep/build/build-receipt.json` (private fixture):
  `601920079a1c2445db18c43bf1c5ec12fdaadafc70b6ef2e85b1b663a07972d8`.
- `crossover-r19/completion.json`:
  `f7863503dc203a004288121b8f5514f79a81c3b97ff8786b36a6d4dc71884841`.
- `crossover-r19/phase-analysis.json`:
  `8a9acd771dfbb14f2a109ca3ffe7f60d2ff53875cb62e08509ccd74fadab168f`.
- `crossover-r19/phase-failure-localization.json`:
  `f5e70fecf901ddd81a3d5e25832a7ba3a05911deb80b1b11640be117f3fad7f3`.
- `onfoot16/native-trace.json`:
  `47227cc5bc25cd7f5bc785d363914575179eef586b04e8a3b84a4afca1761373`.

Eight opt-in raw frames total 269,402,112 bytes. All CPU, phase, ownership and
failure analysis uses retained evidence, without another game run.

The private physical-headset route is now staged and offline-validated under
`performance436/headset-r19-prep`. It binds the frozen R19 binaries to current
startup/resolution scripts and the read-only installed asset/Python cache.
Independent source review and one offline ValidateOnly passed; no physical
runtime or game was started. Plan SHA:
`fa220002f198e179171aec406aeb73ada5ab573e633793dcc07350dabac9a4be`;
validation SHA:
`f8d676a6accc63b7c6f22a5f8774dbaea75b02eb123b7cf206dae453ddd1dceb`.
It leaves public 0.1.12 unchanged and provides a separate Stop path with native,
queue and resolution-guard cleanup evidence. A focused headset check is the
next acceptance step; repeating this unchanged emulated benchmark is not needed.

### Remaining optimization choices

The duplicate-color-copy proposal is still **unapplied and untested**. It could
remove 67,350,528 logical copy bytes per pair and that much private color pixel
storage at this resolution. The relevant previous measured export interval
was only 0.288095 ms; it does not explain the dominant ordinary-draw cost.
Independent review also found a new capture-time device-fault path that needs
explicit retirement and regression coverage before integration. Do not mix it
into the measured candidate or claim its theoretical traffic savings as FPS.

Static analysis of the sampled native timer path found deadline-based tick
pacing with cooperative job execution. Game.MaxFps/MaxVariableFps names exist,
but their live values and clock ownership have not been measured. The sampled
timer share is not a removable CPU fraction or proof of a fixed 60 Hz cap.
Ordinary Draw suppression and multiple in-flight shared pairs remain separate,
substantial ownership changes. The current single-flight depth/color/fence/ACK
contract must not be bypassed to manufacture a higher counter rate.

## R20 retained-evidence investigation

No production change or new game/profiler recording is part of these findings.
The R19 measured/headset candidate remains frozen. All new generated evidence
stays under the private G: `performance436/r20-investigation` directory.

### Ordinary-draw dependency now demonstrated

The actual Dx11 postprocess consumer, reached from DrawView through world+0x70
virtual+0x18, copies persistent exposure/luminance resources at 0x9B482F and
0x9B4862, can Map a selected history slot for CPU read at 0x9B3A8B, and advances
its eight-slot selector at 0x9B4DAF–0x9B4DC0. Consequently, suppressing GPU draw/
copy commands while preserving ordinary CPU cleanup is not equivalent: history
selection can advance over missing writes. A final-output-only bypass would
need its own dependency proof and cannot claim the whole ordinary Draw cost.

Evidence: `ordinary-render.json`, SHA
`de5bca125d27338c1d05e0497a8de10b60c56c0ce70d2ba65c32f8ea66aa3d46`.

### Quad upload: original-instruction equivalence, not performance acceptance

The pinned installed executable's helper at 0x9988B0 emits six vertices: 120
bytes for mode 0, 144 for mode 1. It first writes four vertices and then reads
10/12 DWORDs from the mapped buffer to duplicate two of them. The retained
graphics-thread histogram includes 166 samples in the mode-0 duplicate-copy
instructions. That association is not a timed removable CPU fraction.

`EmulateQuad.py` runs the original function in Unicorn 2.1.4 with explicit
external-call ABI shims. The comparison returns invocation-local CPU scratch
from the modeled Map and copies the exact initialized bytes at modeled Unmap.
972 pairs cover both formats, all six mode-1 selectors plus unsigned defaults,
four x87 rounding modes, three precision modes and nine coordinate cases.
All output bytes, intercepted stack arguments, callee-saved registers, stack
cleanup and FPCW/FPSW/MXCSR agree. Every output byte is written before reading,
all writes stay within the expected extent, and modeled mapped reads fall
from 10/12 to zero. No game or real GPU/driver call occurs.

This does not prove native hook ownership/lifetime/reentrancy, failed-Map
behavior, full x87 environment preservation, compiled write-only copy behavior,
or speedup. The report field `all_call_arguments_equal` means the intercepted
stack arguments; scope/getter ECX is not included. Independent review inspected
the native bytes and shims without repeating the matrix.

Script SHA:
`cc78d7db0600a9beb0c743e30bb150784a8523f07b01488dbc7fb38bbd3b3ae0`.
Aggregate generated vertex SHA:
`5b298feae10a5ce5b63d5de233d3bbde7e96421ac3ffeccb507697ebc1fd561f`.
Installed EXE SHA:
`3911fcc8914b0158c434cee36f19295316e715d4c9a0e70c857e8c39e3d54258`.
Result: `quad-emulation01.json`; no live candidate has been installed.

### Timeout interpretation

All 12 inspected transport sources match the frozen R19. Poll's Timeout result
includes queued expiry, missing active slot ID, and terminal/Cancelled/Complete
outcomes, sometimes before the deadline. R19 has 968 accepted/rendered/published
leases, 968 complete timing records without loss, and 965 consumed + 3 discarded
tickets. Thus at least three of its six timeouts never acquired a native lease;
more may have expired before acquisition. Six 150 ms render stalls are not
established, nor are the unclassified events accepted as harmless.

The audit found no cancellation/ACK ordering defect. The original request
deadline includes queued time behind a prior copy/ACK/reap. A proposed future
failure-only trace would retain the first 32 events per endpoint with IDs,
original deadlines, QPC, slot phases and reason. It changes no wire format,
deadline, scheduling or successful-frame logging and caps reports at 64 KiB.
It is not implemented. No extra game run is justified just to repeat aggregate
counters that cannot resolve this distinction.

Evidence: `timeout/evidence.json`, SHA
`a025541cfe7a7db02cc15f521f69474375b20320751b9b12e3b35a6f122e21d0`;
audit: `timeout/findings.md`, SHA
`6888db400af026e46188f1a346b975e69007e357ed39a535b24a02daebc689d5`.

## R20 shared adapter CPU changes

The retained trace exposed two repeated adapter checks that can avoid work
without changing input, ammunition, animation, camera or simulation policy.
`WeaponVisibilityCurrent` now skips the large metadata equality comparison
when both immutable selected-resource references point to the same snapshot.
Distinct snapshots still receive the complete comparison; owner, sequence,
expiry, coverage and live name checks remain unconditional. The compiled
native-operation table precomputes immutable receipt digest/flag validation.
Every runtime lookup still checks the complete binding and rig, rejects an
invalid matching receipt and requires exactly one matching entry. The generic
dynamic receipt validator remains available with unchanged semantics.

The frozen source closure is
`0be6f3eb6fdabf653c31e830b87ad2adff8cdc15284d8a231d62093c7d6011f4`.
Full normal x86 and x64 builds each passed 302/302 suites. Focused visibility,
capability and resource-enrollment checks also passed on both architectures,
including stale same-pointer metadata, distinct equal snapshots, the final
unused asset-path byte, duplicate/contradictory capabilities, all binding
components, 17 exact resource/body/visibility joins and 289 malformed bindings.
Independent semantic and normal-build linkage reviews found no blocker.
Frozen runtime receipt SHA-256:
`1bb439fc8040721389ecfe14907eb2afde885c5718bb4cf19633331666829358`.

### Isolated before/after costs

The x86 microbenchmark uses the extracted pre-change visibility function and
current normal BC2Camera library, with generic/compiled admission in separate
non-inlined translation units. Five alternating batches per case passed all
40 result checksums over 1.3 million timed calls. No allocation or logging is
inside the timed loops. Median elapsed costs were:

| Case | Previous ns/call | R20 ns/call |
| --- | ---: | ---: |
| Same snapshot pointer | 1805.54 | 30.64 |
| Distinct equal snapshots | 1724.48 | 1703.35 |
| Last asset-path byte differs, correctly rejected | 1704.46 | 1756.63 |
| Native receipt admission | 92.461 | 9.132 |

The same-pointer path saves about 1.775 microseconds per call in this fixture;
the immutable receipt path saves about 83 nanoseconds. The other two cases'
observed ranges overlap; their small differences establish neither gain nor
regression. These are hot-cache function costs, not frame time or FPS results.
Actual call frequency, snapshot identity hit rate, scheduling, driver waits
and the rest of the frame determine in-game benefit. No resolution reduction
or quality change is part of this patch.

Private evidence: `r20-investigation/visibility-bench/run01/summary.json`, SHA
`c8441102490bdeb5ddf4739300ffb2c4322503167d24c5cb660ae2856356a5ab`.
The separate R20 observation uses one 12-second actual-game measurement, the
unchanged 90 Hz emulated input fixture and a 15-second independent actor
witness. It does not repeat the four-phase R19 benchmark or start a new ETW
recording. Installed/public 0.1.12 and the frozen R19 headset route are unchanged.

### R20 one-run actual-game observation

`game41/onfoot17/observation-r20` completed at 03:15:55 UTC October 24
(October 23 local) with normal detach, queue restoration, resolution-guard
release and normal game close. No new profiler trace or additional game run
was required. The independent actor witness covered the full 12-second CPU
interval; analysis excludes its first two seconds. Counter coverage was
9.048 seconds, versus 7.043 seconds in retained R19 C. Intervals are reported
as observed and are not forced into equal lengths.

| Observation | R19 C | R20 C |
| --- | ---: | ---: |
| Fresh consumed stereo pairs/s | 21.8665 | 22.435 |
| Process CPU, equivalent cores | 1.923 | 1.766 |
| Main-thread CPU, equivalent cores | 0.955 | 0.950 |

The descriptive rate difference is 2.60%; historical A1-to-A2 baseline drift
was -7.51%, larger than that difference. First camera positions differ by
0.003523 native units and the maximum first-basis component difference is
0.00405036. Within-C basis variation was 0.00449716 in R20 versus roughly
1.17e-6 in R19. Neither identical view nor equal whole-world workload is proved.
Consequently, this run establishes a passed integration/performance smoke;
it does not establish the new source changes' causal FPS gain. The main thread
remains heavily occupied despite reduced observed process CPU.

Both runs use 2784x3024 per eye and 8x MSAA. R20 has 254 consumed native timing
rows inside its full CPU interval proving those dimensions/depth samples,
with no dropped or invalid camera observations. Its emulated input fixture
averaged 89.937 Hz with no pacing failures. All 411 host-submitted pairs
completed with zero rejects, request timeouts, feedback errors or recorded
GPU/camera-restoration/cleanup rejection errors. Native totals were 413
published, 411 consumed and two discarded; the measured interior included
203 consumed, 204 published and zero discarded. Do not mislabel both other
discards as shutdown-only. Native and host counters retain their own meanings.

The neutral-input check does not exercise reload/pump/holster interactions,
the opening jeep, death/restart, actual streaming or a physical headset.
Private analysis: `observation-r20/comparison.json`, SHA-256
`46e5a0662ed2c9baeef5e7c1895bd7db54cbe0fd5a67d582a7beae9a6e75f545`.

The separate `headset-r20-prep` route is staged and passed one offline
ValidateOnly without starting a game or headset. It advances only runtime
pins/labels from the reviewed R19 route; native-resolution startup, queue1,
cleanup and storage behavior are unchanged. Stage size is 19,707,904 bytes;
installed Python/asset cache are reused. Public 0.1.12 and R19 rollback remain
untouched. The next useful acceptance is a focused physical headset check.
Plan SHA `a836debe0c1ad667d89ea3c55abe25d41b3b62dcf49c67e3c531d084c4ec51dc`;
validation SHA `10626833cdd33db3f63c83748b5fe078dfd1f89a1fb4f22e4ce81070ed22eeea`.
## R21: ordinary rendering and VR on the same discrete GPU — October 24

The user challenged a CPU-only explanation and clarified that integrated
graphics was not a proposed route to 90 FPS. Read-only DXGI enumeration matched
LUID `(108849,0)` to the RX 9070 XT; the game/host transport and the new flat
observer use this same adapter. No adapter was changed.

One root-owned run (`game42`, `onfoot18`, `flat01`) completed cleanly at
04:09:57 UTC. It reused frozen R20 and the already-tested 120-second emulated
runtime/48-second actor witness. One verified 100 ms exit pulse placed the
same actor on foot. Twelve seconds of VR CPU observations preceded complete
native/host detach; a private 15-second Present observer then enclosed a second
12-second CPU measurement in the still-running game. Native resolution stayed
2784x3024, settings bytes remained unchanged, and both modes used interval 0
and queue 1. The observer modifies only forwarding cadence/queue depth, not
native Draw, simulation, images or world history. It records exact process,
render-thread, device/context/swapchain, adapter and cleanup identities.

Private build receipt:
`r21-flat-observer/build01/build-receipt.json`, SHA-256
`df0965359b9b062d421515711b0e182cdf78c28f3db5be1c1fd66b36f8a91a87`.
116 focused checks passed. The first DLL compilation refused a private global
name shadowing a template parameter under /WX; renaming that global allowed
the DLL/controller build. Passed tests were reused rather than rerun. A
fixture-metadata preflight separately caught the old receipt's roots-array
shape before any game action; all seven roots and 32 dependencies were then
validated. These were tool-preparation failures, not game failures. No normal
source/build/installed launcher was changed.

After a two-second settling exclusion in each CPU interval:

| Observation | VR | Ordinary rendering after detach |
| --- | ---: | ---: |
| Application progress | 163 fresh pairs / 9.0648 s = 17.982/s | 1409 successful non-TEST Presents / 9.0213 s = 156.186/s |
| Process CPU, equivalent cores | 1.698 | 1.692 |
| Main-thread CPU, equivalent core | 0.849 | 0.992 |
| GPU 3D-engine occupancy, weighted mean | 93.60% | 98.10% |
| GPU-counter covered interval | 8.0008 s | 8.0005 s |

CPU and progress bins cover slightly different enclosed interiors. GPU means
use 16 wholly enclosed counter intervals per mode, not sums across parallel
engines. VR 3D occupancy comprises game 79.84%, host 12.76%, other 0.99%; ordinary
comprises game 96.18%, other 1.92%. These are coarse WDDM engine counters, not
exclusive shader times or a CPU/GPU clock calibration.

The VR interior contains seven 22-23-pair/s bins, then 6/s and 1/s. Its final
GPU row drops to 39.42% while request 399 begins; source publication-to-reap is
observed at 1112 ms, and a later producer-fence observation at 802 ms. The host
records four aggregate timeouts. GPU sampling ends about 803 ms before the full
request399 interval ends, so it cannot explain the complete stall. Separate
other-process Copy/Video engines become more active, but those names were not
retained and no cause is assigned. Final GPU collection-to-closed footer is
2.311ms; a long sampler shutdown is not observed, but instrumentation effects
are not causally excluded. No GPU failure/camera-restore failure was reported.

All cleanup checks passed, including queue restoration, exact native teardown,
observer hook drain, resolution guard release and normal game close. The eight
required startup captures totaled269,402,112bytes under the existing512MiB
limit; no new flat images, ETW trace or headset capture were added. GPU logs
were capped at1MiB per arm and remained small. No C-drive output was requested.

Interpretation: ordinary rendering has substantially more application
throughput at this target size, while both modes make heavy use of the 3D
engine. This does not establish an8.7x removable cost, physical headset FPS,
or native90Hz feasibility at the current quality. Exact camera/FOV/world
workload equality and unique native world-frame counts were not proven. VR
depth has8samples in174 retained consumed rows within the selected interior.
The flat observer's Present-bound depth has1sample and explicitly is not a
world-MSAA probe; settings remained unchanged. Do not call that a quality
regression or claim direct measured8x flat-world evidence.

Next investigate bounded progress servicing after Present, before unnecessary
ordinary work accumulates. It may wait only for an already-restored pair's
publication/ACK and must return on exact reap, loss, closure or a short deadline.
Successor admission is performed by the next native Update; waiting for that
inside Present risks blocking progress. No native Draw/history work may simply
be skipped. A redundant host tail poll is not the priority: retained submission
to frame-end median is0.00545ms, and existing pre-Wait/post-input service already
polls. Additional useful overlap needs a measured scheduling change, not another
unconditional query. No proposed optimization was implemented in this run.

Evidence:

- `comparison-r21/frame-cpu-analysis.json`:
  `87fef022c7b82598aff659a52029254cd465590ec92c5cf5c0ac14a0e2cedeb6`.
- `comparison-r21/gpu-occupancy-analysis.json`:
  `34a61c606319b47cb828edde676165ef74717af00eac25df8e4226fd9631d7f8`.
- `comparison-r21/completion.json` retains process and all major source pins.
