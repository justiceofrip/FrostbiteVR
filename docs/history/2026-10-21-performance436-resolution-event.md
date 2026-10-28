# Candidate 436: resolution reset traced to native UI selection

Installed/public 0.1.12 remains unchanged. The scheduling and fresh rig-read
candidate remains unpublished. R6 source is
`d77eabd6de37eb0487226fcfc201fdc1ce09734ae41fd9cc759c156a2a544878`;
both normal builds passed 296/296 suites, including real GPU checks on both
architectures. A separate x86 frame-ticket mutation found by the GPU suite was
removed by const-reference field comparison, with immutable-input regressions.

Game06 delivered 181 stereo pairs / 362 eyes at 2784x3024 with zero exact camera
restoration failures, then native settings changed to 1024x768. Game07 retained
2784x3024 through a native-only menu minimize/restore cycle. Game08 then reset
before VR attachment and before the controlled minimize stage could start.
The user's minimizing is therefore not required to trigger the reset.

Game09's bounded options observer captured the original `changeResolution` UI
handler calling SetSize with 1024x768 (return0x960fe5), followed immediately by
successful Save (return0x960ffe). The same valid window was not minimized at
either call. The observer performed no native settings calls or argument/return
modification. Hooks disabled and drained; the diagnostic game closed normally.
The event's originating UI sender is not yet identified. A guessed startup delay
or repeated forced resize is not the current solution.

Evidence: `bc2vr-recovery/performance436/options-observer/session01`, plus
`idle04`, `idle05`, and `game07/window-cycle`. Full details and remaining gates
are in `docs/NATIVE-RESOLUTION-436.md`. No physical-headset performance or actual
opening-mission jeep acceptance is established. Existing generated-file and
capture budgets remain in force; lossless compression reclaimed intermediate
storage without deleting user recordings or diagnostic evidence.
## Later bounded event-context observation

Game10 / observer session02 captured `changeResolution` index0 at process
age14.580s. Eligible native mode0 was 1024x768 at48Hz; the requested2784x3024
was absent from the hardware list. The game was visible/foreground and Windows
last-input age84.922s predated launch. MainMenu return0x77d8a3 forwards an
unchanged event/payload, so the upstream producer remains unknown. Hooks
disabled/drained and normal close completed; no headset or native VR adapter ran.

Decision: implement explicit VR-session resolution-event ownership instead of
adding a fake hardware mode or guessing a confirmation timer. Setup and release
carry exact game/supervisor creation identities and a session token, with an
owner-death lease. The guard only rejects this event and does not claim to solve
all graphics Save/fullscreen paths. This implementation is not native-accepted
yet. Installed/public0.1.12 remains unchanged.

## R9 native-resolution integration accepted

Both normal architectures passed 297/297 suites. Game13 / idle08 retained
2784x3024 across menu, Continue and controlled minimize/restore, delivering 304
fresh stereo pairs. Two unwanted changeResolution events were blocked, with zero
owner mismatches. settings.ini was unchanged; native hooks, retained guard and
normal owned-game close all completed cleanly. Game14 separately proved native
supervisor-death disarm and idempotent exact release without a VR adapter/input.
No further identical resolution or minimize test is needed absent a change.

The two preceding setup failures improved verification: normalize Windows'
mapped PE ImageBase without weakening other image checks, and distinguish the
actual primary UIManager vtable from its secondary interface. R9 full-runtime
receipt SHA-256 is f22c3c5bbb14e237c15263539d5bf9af8d7e60845b83bf1c081e850f10895c3d;
the unchanged core native-operation closure alone does not identify these files.

CPU sampling covered the same game13 run without additional inputs or captures.
It remained vehicle-gated, so on-foot rig work and physical-headset performance
are not established. Continue with distinct workload/timing coverage rather than
claiming FPS gains from synthetic scheduling or successful image dimensions.
Installed/public 0.1.12 remains unchanged.

## Game15 on-foot measurement and copy-completion failure

One verified X exit reached the same infantry actor. All 139 read-only monitor
samples agreed on actor, weak identity, control and route over 14.917 seconds,
enclosing the complete 12-second CPU measurement. The harness required 140
samples and reported failure; preserve that raw result and use the independently
enclosed interval without repeating the run. Native cleanup, exact guard release
and normal owned-game close passed.

The exact gameplay/UI thread used 0.94495 CPU core; process utilization averaged
1.80219 cores. Targeted observer overhead was 15.625 ms CPU. Real rig publication
ran (1,754 tracked poses and 3,618 copies), but output did not: eleven measured
near-one-second intervals published 112 pairs, consumed zero and discarded 112.
Every retained rejected-pair row had slow ColorCopy, 29.13–45.54 ms. All 160
received scene-depth pairs were copied and the host rejected 160 pairs overall.
This is not usable on-foot FPS or physical-headset acceptance.

Decision: implement asynchronous consumer-copy completion with exact ticket and
GPU lifetime ownership, rather than treating a pending copy as failed feedback
after the current 20 ms spin budget. That correction is not implemented or
validated by these receipts. Do not repeat the same monitor test merely to get
one more sample. Analysis and source hashes are in
`performance436/onfoot01/analysis.json` (SHA-256
`3f606a4abeaf334465c0683843243fbb781b73a8bd9cce39565e852ac33690e4`).
Installed/public 0.1.12 remains unchanged; candidate 436 is not headset-ready.

## R10 completed asynchronous copying and on-foot presentation

R10 implements exact pending-copy ownership with zero-wait completion polling.
The host retains the ticket, resources and original tracking/body/depth metadata
until completion. GPU completion and eligibility to present are separate; a
stale completed image can release its source without being displayed. Shared
channel cancellation persists through mutex contention and is applied before
deferred feedback can promote queued work. Both normal architectures passed
297/297 suites. Source closure is
`3a482e89c7035bfbb431e1b9ff5575673d997247defe3442603519ce4714730b`;
normal runtime receipt is
`bbf1ae3c5c0cfd0ac513b229b291e6bf7941a05909a21eec9e6161470340b7c1`.

Game16/onfoot02 passed a distinct actual-game check at 2784x3024 per eye using
emulated tracking. All 300 host copies completed and all 300 pairs were
presented, with zero rejections, retirements or request timeouts. Maximum copy
pending age was 42.934 ms. Eleven interior intervals totaling 11.051964 seconds
published 177 pairs, consumed 176 and discarded none; median consumption was
15.9328 pairs/s. The host ring retained 84 fresh completed world pairs wholly
inside the CPU measurement. These are emulated-runtime pair rates, not headset
FPS or a controlled speedup over the failed R9 run.

One verified X exit reached the same actor. The new monitor accepts elapsed
coverage rather than exactly 140 samples: 139 unchanged observations over
14.929714 seconds enclosed the full 12-second measurement, with a maximum
110.3732 ms gap. The exact gameplay/UI thread used 0.95658 CPU core; the process
averaged 1.93861 cores. Rig publication and body composition were active, but
these aggregate counters do not attribute CPU cost or validate weapon gestures.
The targeted sampler used 15.625 ms CPU. VR stop followed CPU completion by
71.052 ms, preserving useful host rows while the independent witness finished.

Native cleanup, exact guard release, unchanged settings.ini and normal owned
game close all passed. Prior failed receipts were preserved. Evidence is in
`performance436/onfoot02/measurement-acceptance.json` and
`performance436/onfoot-r10/completion.json`. Installed/public 0.1.12 is unchanged.
Next work is attribution of the still-busy main thread and physical-headset
validation; this run does not establish complete campaign or headset acceptance.

Follow-up cadence audit: retained copies completed on the first tick after
submission, and successive new pairs were exactly four host ticks apart. Typical
host ticks were about 15.327 ms despite the fixture's 11 ms prediction period.
Sub-20 ms stage timings are suppressed in the current ring. Therefore do not
attribute the remaining pair rate to depth GPU cost. Checkpoint R10; investigate
request/fence cadence and validate the fixture clock before another optimization.
