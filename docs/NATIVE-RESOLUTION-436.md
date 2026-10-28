# Native render resolution — checkpoint 436

Recorded October 21, 2026. R9 passed an actual-game session with emulated OpenXR
tracking at **2784 x 3024 per eye**, across main menu, Continue and a controlled
minimize/restore. Actual renderer/swapchain descriptors, 304 fresh stereo pairs,
unchanged settings.ini, hook cleanup and normal game close were verified.
Automatic resolution-guard cleanup after supervisor exit passed separately.
R10 subsequently passed an actual-game on-foot run at the same dimensions:
300 completed and presented pairs, no host rejections, exact guard release and
normal game close. Both R10 normal builds passed 297/297 suites. Its measured
on-foot intervals consumed 176 pairs with median 15.93 pairs/s; these are
emulated-runtime measurements, not physical-headset FPS. See
[performance evidence](PERFORMANCE-436.md) for the full timing scope.
Earlier failed transitions and their causes remain documented below. This is
native-resolution integration evidence, not measured headset performance.
Installed and published **0.1.12 remain unchanged**.

## Implemented startup sequence

The existing OpenXR preflight now reports recommended and maximum eye dimensions.
The launcher plans the common supported runtime recommendation, checks the
16,384-pixel dimension and 32-million-pixel per-eye limits, and carries that plan
to the owned session. “Native headset resolution” here means the **runtime's
recommended render size**, including its current render-scale choice; it does not
mean the headset panel's pixel count. No desktop-size override or upscale is
accepted as proof that the game rendered at that size.

After a fresh game's original main menu is stable, the x86 resolution helper
verifies the executable code, process creation identity, renderer, settings,
registry aliases, upstream graphics-options owner and original UI update slot.
On that UI update thread it calls the original upstream size/fullscreen setters,
the typed `DX11DisplaySettings.WindowSize` and `FullScreen` setters, then the
renderer notification used by native Options Apply. The original call
site at `0x7de53c` dispatches renderer vtable slot `+0x90`; this invokes the native
settings comparison and resize path. The helper does not write renderer size or
reset fields directly, and neither launcher nor helper writes `settings.ini`.

Success requires the upstream dirty flag to drain through native update, then
three consecutive updates with matching upstream/registry settings, native
renderer dimensions and `IDXGISwapChain::GetDesc` dimensions. The temporary UI
hook must be restored and callbacks drained. A separate read-only menu probe then
checks the original renderer again before any VR adapter is attached. Host
readiness must match both the realized game size and the preflight recommendation.
Menu surfaces retain their independent captured raster/aspect and pointer mapping.

Failure prevents VR attachment. The helper attempts rollback through the same
native setters and Apply notification only while ownership and the attempted
values remain identifiable. It does not overwrite intervening user settings or
terminate a thread inside an engine call. The helper DLL remains loaded until
that game exits; a failed setup requires a fresh game.

## Actual-game evidence

Private evidence root: `G:\Unity\VRMMO\bc2vr-recovery\performance436`.

| Evidence | Result |
| --- | --- |
| `game01/resolution-native.json` | Initial setters-only attempt changed settings but not the renderer. Timed out, confirmed rollback to 1920x1080, restored hook and drained callbacks; no VR adapter attached. |
| `game02/process.json`, `menu-before.json` | Fresh owned BC2 process 83004; original main menu stable at 1920x1080. |
| `resize02-build.log`, `resize02-binding.log` | Focused corrected helper build and exact-image binding check. |
| `game02/resolution-native.json` | Corrected original Apply call: 1280x1440 confirmed in 4 UI updates, no rollback, hook restored, callbacks drained. Its pass predicate checks both renderer and swapchain. |
| `game02/menu-after.json` | Independent read-only probe: 1280x1440 over 3 stable samples, 0.203 seconds, with the same menu and renderer owners. |
| `game03/resize03`, `game04/resize03` | Fresh-process 2784x3024 native renderer/swapchain confirmation plus independent readback. Game03 closed normally after a pre-attachment argument rejection; that rejected VR attempt provides no integration acceptance. |
| `idle02/failure-summary.json` | Normal runtime reached menu and published four world pairs, then targets reverted to 1920x1080. Camera restoration separately failed once; native probe exit12, hooks_disabled=false. |
| `game04/close-after-idle.json` | Normal close failed. Root verified path/creation and terminated only its owned diagnostic process after read-only inspection. Window enumeration was sandbox-constrained, so absence of a top-level window was not proven. |
| `game05/resize-r4/resolution-native.json` | Upstream options and native renderer/swapchain reached 2784x3024 in four UI updates; hook restored and callbacks drained. |
| `idle03/completion.json`, `native-trace.json` | Before Continue, options and renderer reverted to 1920x1080. No world calls or published eye pairs. Probe exit0, hooks disabled, zero camera restoration failures. |
| `game05/close-after-idle.json` | Elevated read-only enumeration confirmed visible responsive window on left monitor. Exact-owned CloseMainWindow succeeded; no forced termination. |

The first failed attempt exposed a missing commit step: registry setters alone
do not schedule the renderer's resize. It was corrected by following native
Options Apply, rather than forcing a reset flag.

**Game02 did not demonstrate a clean session close.** The process remained
present at roughly 70 MB with `MainWindowHandle=0`; a normal window-close request
could not complete. It had been launched with `WindowStyle Hidden` and then
shown without activation (`StartMenuGame.ps1`, `game02/window.json`). Native
Apply may recreate or hide its window using that startup style, but this cause
is not established and the observation is not proof of a crash. After waiting,
the root agent terminated only its owned test process. Stable end-to-end startup
and cleanup remain unverified.

The game04 read-only investigation found a second source of resolution:
graphics-options singleton `*(0x1570ea0)` retains a float2 at `+0xa0` and fullscreen
at `+0xac`. These still held 1920x1080 even after the downstream registry/renderer
resize. Native Apply (`0x7de030`) consumes these options on update/lifecycle
paths. Current and saved renderer settings were both back at 1920x1080 when
inspected. R3 is synchronizing this upstream source through verified original
setters `0x7bc0a0` and `0x7bc0c0`, with rollback and without the separate save
routine. No exact dynamic call stack was captured for the overwrite.

The read-only game04 snapshot identified options object `0x07f500d0`, vtable
`0x142e570`, current renderer settings `0x07f902c0`, and saved settings
`0x07f90300`. All three sources contained 1920x1080, fullscreen false; the
options dirty byte at `+0xc0` was clear. Disassembly verifies the size setter as
`__thiscall(options, const float2*)` and fullscreen setter as
`__thiscall(options, bool)`, both `ret 4`. Original callers at `0x960fcf` and
`0x56ade9` verify those argument forms. Constructor `0x7e44d9` verifies the
options vtable, and `0x5d1685` verifies the singleton passed to native Apply.
All 25 literal binding signatures and 18 code spans matched the installed image
in a read-only source-to-image check; this is not native execution acceptance.

R3 now checks upstream identity and values before mutation and after each group
of setters. It waits for the original options update to clear the dirty flag,
then requires three matching real renderer/swapchain observations. A failed
setter sequence can restore old or attempted values in either source, including
a fully applied upstream change with unchanged downstream settings. A third
value or replaced owner denies rollback; failed ownership proof never authorizes
a call through an old pointer. Restoration uses the same original setters and
commit notification. No call to the separate UI save routine `0x7dd1f0` was added.
The actual-game check must still verify the settings file remains unchanged.

The private launch also saved window style `0x04c80000`, without WS_VISIBLE.
Native resize restores that style. R3 corrects the private harness to start the
requested interactive game visibly. Public Session.ps1 already starts the game
normally, so this finding does not establish a production hidden-window defect.

GPU failure stage2 was source validation, consistent with1920x1080 targets
against2784x3024 XR requirements. Operation5 was stale from a successful earlier
sharing fallback and must not be interpreted as that failure's operation.
The separate exact camera-restore mismatch is being investigated; guards are
not relaxed to make the run pass.

## Offline coverage and remaining acceptance

The real launcher coordinator passed 39 mocked session checks, 24 restart checks
and 26 setup checks. They cover distinct desktop/headset sizes, setup failure,
wrong process or dimensions, unremoved hooks, pending callbacks, timeout,
unchanged native renderer, changed runtime recommendation and stop during setup.
Logs are in `G:\Unity\VRMMO\bc2vr-recovery\performance435\launcher-tests`.
These exercise launcher policy with mocked process boundaries, not GPU behavior.

The combined 436 source freeze is
`2083da4dd5f4e7f0fbea36f474883f13ed880159f9660967eee8608c3f32dfeb`;
`performance436/r2/source-freeze.json` identifies its source manifests. Both
complete r2 builds passed296/296 suites; real-GPU scene-depth and menu tests also
passed. They did not cover the later game04 integration failures. R3 edits need
their own fresh validation. An initial four-job build while BC2 was open hit
system commit exhaustion (`0xc000012d`); subsequent full builds used one job
with the game closed. This build failure is not a game performance measurement.

R4 source `5b4bd00ac544581aee190e0dc8701c0e7e88644f650ea031f2d9fc29912b1b22`
passed both complete 296-suite builds and real-image signature checks. Its revised
scene-depth GPU test passed. The camera cache correction follows verified native
bit4 scale-cache writes; no restoration comparison was relaxed. Idle03 stopped
before Continue; the subsequent world observations below narrow the remaining
failure without establishing end-to-end acceptance.

Idle03 exposes a second reset despite synchronized upstream options. Its first
native Present observed a visible 1920x1080 swapchain. No game resize/mode call,
controller click or mirror write was made by the VR adapter. It did advance two
missing startup logos after resizing. Native resolution-selection UI code can
write one of the enumerated display sizes; static inspection alone does not
prove it caused this reset. A bounded pre-/post-adapter read-only observation is
planned to distinguish late native startup from adapter-triggered progression.
Settings.ini remained byte-identical. Both installed/public 0.1.12 are unchanged.

### Follow-up: world rendering and minimize/restore

Game06 / idle04 refuted the simple missing-logo-cleanup explanation: all 120
pre-attachment observations and all 120 observations spanning startup cleanup
retained 2784x3024. Continue then reached world rendering. The host consumed
181 fresh stereo pairs, with 362 captured eyes, no rejected host pairs and zero
exact camera restoration failures. Later, the native swapchain, upstream
options and current/saved renderer settings all changed to 1024x768. The game
also saved that size to its settings file; this was not a launcher/helper file
write. The GPU source rejection preceded fixture shutdown by approximately
5.47 seconds, so cleanup did not cause the change. Probe exit12 retained the
correct stage2/operation0 diagnostic; hooks disabled successfully and the game
closed normally. See `idle04/failure-summary.json` and `game06/after-idle04.json`.

The user reported minimizing BC2 rather than changing its graphics options.
This is a lead, not an established cause. Game07's native-only menu cycle used
explicit minimize and nonactivating restore, verified both window states, and
retained 2784x3024 in all 120 read-only observations. It did not include the VR
adapter or world rendering. See `game07/window-cycle/acceptance.json`.

R5 adds cancellation ordering and an opt-in unsubmitted-pair discard for a
dimension-only mismatch. Cancellation remains visible until exact camera
restoration completes. The next pair still requires the original XR dimensions;
device, format, array, mip and sample mismatches remain fatal. Published texture
ownership and acknowledgement are unchanged. No live resolution renegotiation,
forced settings reapplication or image stretching was added. Focused owner and
staged-producer tests passed, but the x86 GPU test failed an existing depth-view
check before reaching its new cases; r5 full builds and combined acceptance
remain pending. A private x86 diagnostic reproduced caller ticket mutation
inside `SameResourceSet`, rather than a padding mismatch. R6 changes that
comparison to const-reference field comparisons and adds immutable-input
checks. Both full r6 normal builds passed 296/296 suites, and the real GPU suite
passed on both architectures, including 1/2/4/8-sample depth and transient-size
recovery. This is separate from the native resolution change below.
Installed/public 0.1.12 remains unchanged.

### Follow-up: native resolution-menu event captured

Game08 / idle05 reached the requested size, but its first read-only observation
before VR adapter injection already found 1024x768. The guarded run stopped before
Continue; it never executed its controlled minimize/restore stage. Native cleanup
passed with zero camera restoration failures. The owned game closed normally.
This demonstrates that minimizing and VR adapter injection are not necessary to
trigger the early resolution reset; it does not establish the cause of every
earlier reset.

Game09 attached a separate bounded observer to the verified original options
SetSize and Save functions after the resolution helper detached. It performed
no native settings calls itself, changed no arguments or returns, and captured
no images. No VR adapter, Continue action or minimize request ran. At process
age approximately 14.66 seconds, the original UI `changeResolution` handler
called SetSize (return address `0x960fe5`) with 1024x768. The same options owner
changed from 2784x3024 / dirty0 to 1024x768 / dirty1. Save immediately followed
(return `0x960ffe`) and returned true. Both callback observations showed the
same valid, non-minimized window. The next sampled renderer/options/current
and saved settings agreed on 1024x768. Settings-file content stayed identical
to its existing 1024x768 version, but native Save advanced its timestamp.

The observer recorded exactly one setter and one Save, with no overflow. Hooks
disabled, callbacks drained, and the game exited by normal close. Evidence:
`options-observer/session01/{observer.json,timeline.jsonl,analysis.json,completion.json}`.
This identifies the native handler, not which UI sender or default selection
generated its event. In particular, no fixed startup delay, guessed confirmation
call, Save call or forced repeated reapplication is justified by this result.

A bounded static review of `Menu/main_root.res` and `Menu/settings.res` found
APT scripts, not standard SWF bytecode. The available-resolution rows and current
resolution label are populated separately. The list action callback dispatches
`changeResolution`; an automatic missing-mode fallback was not proved. Native
mode enumeration uses the renderer-owned vector at `+0x20`, with 32-byte rows
and the same filter/order in the getter and change handler. There is a verified
engine-owned append path, but no windowed-only flag, and the change handler
saves even a same-size selection. Appending a custom mode alone is therefore not
an accepted correction. `options-observer/StaticResolutionUi436.json` records
resource hashes, exact code offsets and these limits.

Game10 / observer session02 added numeric event context without changing native
arguments or return values. It captured one `changeResolution` event with index0,
one setter and one successful Save. The renderer's 132-row native mode vector
contained 93 UI-eligible rows; eligible index0 was 1024x768 at 48Hz, and the XR
target was absent. The window was visible and foreground. Windows' last-input
age was 84.922 seconds at process age14.580 seconds, predating launch. This
argues against a recent physical click, subject to that API's input coverage.
The immediate sender at return address `0x77d8a3` is a MainMenu forwarding
branch: it forwards the original event and payload unchanged. It proves neither
a missing-mode fallback nor a timer. Hooks disabled and drained and the owned
game closed normally. No further identical event-capture run is needed.

The R9 implementation uses explicit VR-session resolution ownership: reject
only this resolution-change event while the exact session, renderer, options
and UI manager remain owned. It must arm before initial resize, survive a menu
replacement, and release after VR cleanup or supervisor death. The temporary
setup hook and retained event guard need separate proof fields. Fake display
modes, guessed confirmation calls and repeated forced resizing are not used.
This does not intercept other graphics Save or fullscreen paths, so it cannot
claim those paths will never persist XR dimensions. Installed/public 0.1.12 is
unchanged.

## R9 acceptance and cleanup

The loaded-image verifier normalizes only the PE ImageBase field to the actual
mapped base, alongside HIGHLOW relocation normalization; all other headers and
executable bytes remain checked. Windows changed that field in game11, causing
the earlier false rejection before native setup. Game12 then exposed a separate
ownership error: UIManager's primary vtable is 0x1440830, while 0x14407f0 belongs
to a secondary interface at +0x82afc. Constructor/destructor writes and the
primary update-slot relationship now constrain this check, with negative tests.

The request carries exact game and supervisor process creation identities plus
a session token. Setup is one-shot. The retained event guard is armed before
the original native resize calls and survives replacement of the main-menu
object. It validates the manager, renderer, options, registry and settings
owners, without treating current dimensions or the menu object as lease identity.
Only the exact changeResolution event is suppressed while that lease is valid;
unrelated events call the original dispatcher. Temporary UI-hook cleanup and
retained guard cleanup have separate proof fields. The original trampoline and
module remain mapped until game exit, including for late callbacks.

Evidence under performance436:

| Evidence | Result |
| --- | --- |
| r9/normal-runtime/build-receipt.json | Frozen normal x86 and x64 builds each passed 297/297 suites; receipt SHA-256 f22c3c5bbb14e237c15263539d5bf9af8d7e60845b83bf1c081e850f10895c3d. |
| game13/resize-r7/resolution-native.json | 2784x3024 reached in four UI updates; temporary hook restored/drained, guard active. Independent readback passed. |
| idle08/acceptance.json | 304 fresh pairs, native dimensions retained across Continue and minimize/restore, a fresh pair after restoration, unchanged settings.ini. One timeout occurred; this is not a zero-stall performance result. |
| combined-r9/resolution-release.json | Two resolution events blocked, nine passed, zero owner mismatches; exact lease released, hook disabled and drained. |
| combined-r9/completion.json | Normal native runtime cleanup, guard release and normal owned-game close passed. No installed/public changes. |
| r9/owner-death/acceptance.json | Separate owner process configured the guard then exited normally; native watcher disarmed it with owner-loss reason2. Two exact releases returned terminal cleanup proof, then the game closed normally. No input, VR adapter or raw captures. |
| onfoot01/analysis.json and onfoot-r9/completion.json | Game15's same-actor on-foot CPU interval is valid; raw combined harness failed its 140-sample requirement with 139 observations. On-foot presentation also failed independently: zero consumed pairs during the measured intervals. Native cleanup, exact guard release and normal close passed. |
| onfoot02/measurement-acceptance.json and onfoot-r10/completion.json | R10/game16 passed the revised elapsed-span witness and enclosed 12-second CPU interval. All 300 host copies completed and pairs presented; measured interior consumed 176 with 0 discarded. Native dimensions, unchanged settings.ini, exact cleanup/guard release and normal close passed. |

The game13 emulated fixture remained in a vehicle role, so valid neutral hands
and tracked-frame counts there do not establish on-foot rig workload or equipment behavior.
The initial raw images include transition fades and are not steady-scene visual
or physical-headset acceptance. The test uses real native rendering and GPU work,
but does not include headset compositor/streaming costs.

Game15 subsequently exercised the same actor after one verified vehicle exit.
Its full 12-second CPU interval fits inside 139 unchanged on-foot observations;
the monitor's separate 140-sample requirement was not met and remains a failed
harness result. Tracked rig publication ran, but the host rejected all pairs
during that measured interval after slow color copies. The original resolution
setup and exact lease release still passed, with no forced process termination.
This is a graphics-consumer failure, not evidence to weaken native resolution,
ownership or cleanup checks. The R9 failure is preserved; it was followed by the
separate R10 acceptance below. Installed/public 0.1.12 remains unchanged.

## R10 on-foot integration acceptance

R10 retains exact GPU source ownership while copy completion is pending and
polls without the former 20 ms spin/failure rule. Sticky shared-channel cancellation
survives lock contention and precedes deferred old-ticket acknowledgement.
Presentation eligibility remains separate from proven copy completion; no
dimension or ownership guard was weakened to pass the run.

Game16/onfoot02 used one verified vehicle exit and real native rendering/GPU
work with emulated tracking. All 139 actor/route observations over 14.929714 s
agreed, enclosing the full 12-second targeted CPU observation. The elapsed-span
monitor replaces the previous 140-sample heuristic in a new private fixture;
game15's failed receipts remain unchanged. The host completed and presented
all 300 copies/pairs with 0 rejection or retirement. Its retained diagnostics
prove 84 fresh completed world pairs within the measured interval, and native
counter intervals there consumed 176 of 177 published pairs with 0 discards.

The exact lease blocked two resolution-change events and recorded zero owner
mismatches. Native hooks restored and drained; the guard released, disabled
and drained; settings.ini stayed unchanged; the owned process closed normally.
No installed/public files changed. Source closure is
`3a482e89c7035bfbb431e1b9ff5575673d997247defe3442603519ce4714730b`,
and the R10 paired normal runtime receipt is
`bbf1ae3c5c0cfd0ac513b229b291e6bf7941a05909a21eec9e6161470340b7c1`.
Both architectures passed 297/297 suites. This bounded run did not repeat the
already accepted R9 minimize/restore or supervisor-death scenarios.

Still required:

- Timing attribution sufficient to distinguish game, mod and transfer costs
  now that bounded on-foot presentation passes; first check request/fence cadence
  and the emulated fixture clock. The four-tick pair interval does not establish
  a depth-copy bottleneck.
- A physical-headset check and comparable timing samples before claiming better
  FPS or identifying the external tester's CPU/GPU bottleneck.

Changing native video options after startup can restore a different game size.
The source/XR mismatch guards must continue rejecting mismatched images; there
is no accepted live-resolution renegotiation. The independent turret and
one-eye artifact investigations remain in [JEEP-TURRET-436.md](JEEP-TURRET-436.md).
