# Current state: candidate 436 under validation / installed and published 0.1.12

For the consolidated October 23 priorities, emulated-test opportunities and
remaining headset polish, see [the pre-alpha work list](PREALPHA-BACKLOG.md).

## October 25–26 polish / R45 handling passes; mounted rendering remains unresolved

R45 build06 passed all305 suites on each normal architecture and the six selected
handling/cache/rig/visibility checks. Frozen private runtime receipt:
`40c26dd819e74945587a5334a6d3cce9c1cf36e680e9dad57a7856b7e393559c`;
native source `d12a43f8ddd80fb0ceb643e5f4961b41f6d62642b3fdb192bc607ccbfb3d0a8d`.
The x86 cache/enrollment consumer also checked all49 actual candidate cache
identities. Build05's failed logs are preserved; its unchanged native source
artifact paths were reused, not its failed validation. Public/installed0.1.12
is unchanged.

Actual SP01 game80 run02 received240 stereo pairs with0 receiver timeouts.
The unchanged strict mechanical analyzer now passes both shoulder draws:
requests2/committed2/cancelled0, confirmations29.47/31.12ms, no unintended stow.
All2,641 tracked/paired rig outputs have0 source, packing or partial-pose failures.
The independent audit finds119 settled hand samples matching their recorded
owners. Eight initial Garand samples remain attachment-pending and no authored
Garand grip is claimed. This establishes the reproduced draw-handoff repair,
not new reload coverage, tracking-loss recovery or headset acceptance.

The original visual analyzer still reports insufficient event correspondence;
an independent supplement explains its phase-join limitation (the exact GunHold
event precedes the body acknowledgement). Exact per-pose/two-Pack telemetry is
still absent. Root initially misread pair96/120 contact-sheet grouping; the
horizontal weapon is pair120 in both eyes. Pair48's close arm difference remains
unresolved; near-eye clipping/parallax is plausible, not established. No visual
or complete campaign acceptance is claimed. Evidence is in
`performance436/cross-draw-r45-build06-prep/run02`, including `mechanical.json`,
`independent-timeline-supplement.json`, `eye-timing-review.json`, and recorded
paired pixels. Run01 stopped before injection on a changing actor snapshot; a
read-only follow-up confirmed the original owner before run02 proceeded.
Game80 closed normally and run06 restored its three profile files exactly.

The mounted observations below identify the actual SP01 seat only by its typed
native owner and entry. Prior notes
calling this a fixed emplacement were not blueprint-verified; its screenshot
contains vehicle bodywork. A generic mounted-seat camera pass will not be called
a Kurogane/full-ride pass without exact identity evidence.

Game81/run07 first loaded to a black stock scene before any VR module attached.
A bounded read found no loader or front menu, inactive pause flags, and a valid
1920x1080 D3D11 renderer in a visible, foreground, responsive window. The native
pause menu rendered correctly; resuming restored the mounted-gun scene. The
cause remains unknown, and this does not establish a fix for the reported VR
death/restart failure. Evidence: `vanilla-r43/run07/stock-black-summary.json`.
R45 seat runs01/02 stopped before injection on wrapper identity/CLI errors;
run03 loaded the module but returned config code10 before hooks/report because
its command omitted the normal build's required resource/body mode. These
failures are retained. Game81 closed normally and its three profiles restored.

Fresh game82/run04 passes the mounted-seat numeric camera audit: 240 pairs,
zero receiver timeouts,14 actual post-setter stereo observations across6.5s,
correct20-degree/0.6m entry compensation and10cm lean/return. Maximum eye-position
error is0.019mm and IPD error0.0014mm. Native exit0, cleanup, and post-detach
same-seat/map checks pass. The initial40s stock load waiter timed out; gameplay
became visible afterward and the separate exact native preflight then passed.

**The mounted run fails visual acceptance.** Both receiver eyes contain large
black world/sky polygons absent from the stock screenshot. Paired review:
`vehicle-r45-prep/run04/paired-seat-review.png`. Saved native world/desktop
images also contain the defect, so it is not solely a receiver-copy artifact.
No gun/hand pose writes were enabled in that mounted interval. The exact draw
and cause remain unknown; do not equate it with the reported destruction-only
artifact yet. The follow-up draw-witness result is recorded below. Correct
camera matrices do not establish a playable jeep ride, VR transition or visual
pass. Public/installed0.1.12 remains unchanged.

The bounded source audit finds the defect in ordinary frame42543, before the
first stereo/weather frame42546. World color is copied from the render target
left bound after the native draw; that alone does not establish a final lit
pass. The desktop capture, however, copies the verified game swapchain's actual
backbuffer before Present and already contains the black polygons. Both this
run and the successful on-foot run have one ordinary main view/draw and three
active shadow children. Menu/mirror composition was inactive, and no concrete
mounted-only startup renderer write was identified. This narrows the fault;
it does not identify a material, destruction owner or proven fix. Evidence:
`vehicle-r45-prep/run04/startup-source-review.json`
(`0c5c69fe026f03554b91fcd7156c4b9ac8991e39fa377dfddcdbb9e0da7218c2`).

After clean detach, the desktop became fully black. One guarded Escape restored
actual normal gameplay immediately, not a visible pause menu; the screenshot
`vanilla-r43/run08/post-detach-pause.png` is named for the intended action.
Native UI state was not observed immediately before that input, so a pause/fade
cause is unproven. Game82 then closed normally and run08 restored the original
profile exactly. Its completion receipt covers profile lifecycle only; the
separate native receipts retain the mounted visual failure.

Game83's diagnostic-enabled mounted run01 used the same R45 normal binaries.
Reviewed receiver pairs0/7/239 and all three witness pairs show no recurrence of
the prior large black polygons. The post-detach image also shows normal gameplay
without a recovery input. Numeric camera checks and cleanup passed; game83
closed normally and run09 restored its profile exactly. This is an intermittent
result with the draw observer enabled, not evidence of a rendering fix.

The strict witness audit remains **failed** on global `wrong_owner=2302`.
`mounted-render-r45-prep/run01/witness-drain-diagnostic.json` separately confirms
three exact delivered pair joins, complete payloads and drained readbacks with
zero GPU copy errors. The rejection phase is unknown; the supplement does not
override the failure or establish ownership for all callbacks. The first window
mostly captured shadow draws and no main-color constant payloads. No headset,
destruction-association or full vehicle pass is claimed from the clean-looking
sample images.

Game84/run10 then crashed during ordinary native Resume, before any VR/helper
attachment. Both exception chances record the same AMD worker read of address
0x98 at `amdxx32.dll+0x686C0`, 5.53 seconds after the click. The driver hash and
worker-stack tail match the earlier R40 loading-fault family, but the faulting
instruction differs from game74's +0xAEBC2. Complete pre-click and crash-time
module inventories contain no VR/startup helper. This reproduces a stock-menu
loading failure without proving its upstream cause or a new VR regression.
Run10 restored the original profile. Evidence:
`vanilla-r43/run10/stock-loading-crash-analysis.json`.

Game85/run11 loaded to a black stock scene; ordinary Escape pause/resume restored
visible gameplay before the mod attached. Its mounted-render run02 used the same
normal R45 binaries with `FirstDrawOrdinal=256`. Two of three planned witness
pairs completed; the seven-second command budget ended before the third. The
receiver delivered240 pairs with1 timeout, so **the full camera/session audit
fails**. The inner13-row matrix check passed, but does not override that failure.
Native cleanup and the independent same-seat PostSeat check passed. Game85
closed normally and run11 restored its exact original profile; no game remains
open at this checkpoint.

The two retained witness pairs show no prior large black polygons. Their saved
draw window uses1920x1080 8x-MSAA color targets. Among the first64 draws per eye
with retained constant payloads, matched full view matrices belong to the correct
eye. This is finite partial evidence, not every-draw ownership or shader-semantic
proof. The vertex-buffer slot2 asymmetry was not traced to an executed mod
mutation or missing restoration; whether those native shaders consume slot2 is
unknown. Do not copy bindings between eyes or claim a rendering fix. See
`mounted-render-r45-prep/run02/camera-constant-diagnostic01.json` and
`slot2-restoration-audit.json`. No further unchanged rendering rerun is planned.

The shared campaign-bench planner now separates exact reload, support-reference,
body-display and holster-anchor metadata. It checks resource/GUID/content joins,
descriptor identities, support digests and finite rigid support/holster matrices.
Missing, incompatible and ambiguous features remain gaps; Type5 display metadata
does not grant magazine reload support. The supplied288-identity catalog has21
reload configurations (19 magazine,1 pump,1 bolt),164 support joins,18 body/holster
records and17 complete metadata joins. These are **not21 working weapons** or
native acceptance claims. All34 focused tests passed before the final holster
anchor correction; its two relevant regressions passed afterward without
repeating the suite. Native execution gates remain unchanged. Evidence:
`campaign-readiness-r45-review/validation.json` and
`campaign-readiness-r45-review/anchor02/source-preparation.json`.

`performance436/r45-session-checkpoint-20261026.json` pins these results. No
native rebuild, installed runtime update, release or headset acceptance occurred
in this follow-up; the frozen R45 build06 and public0.1.12 remain unchanged.

Two bounded lossless compression passes recovered 968,820,111 bytes in total
from idle build files and retained evidence. All content hashes remain equal;
no source, diagnostic evidence or user recordings were deleted. The 40 GiB
allocated-output limit and 15 GiB free-space floor remain unchanged.
The subsequent `r45-storage-vehicle04/run01/compression.json` completed all26
selected capture files and reclaimed another101,421,056 bytes, with0 content
changes and0 deletions. Raw evidence remains available with identical hashes.
`r45-storage-mounted01/run01/compression.json` then reclaimed98,013,184 bytes
across25 selected files, again with0 content changes and0 deletions.
The final `r45-storage-mounted02/run01/compression.json` reclaimed97,161,216
bytes across25 files. Those three capture-retention passes reclaimed296,595,456
bytes in total, with identical contents, no deletions and unchanged limits.

The user expanded the next combined candidate to include a campaign weapon
bench, ammo-counter flicker, reliable short knife jabs/audio, campaign vehicle
coverage and stuck destruction geometry. Root owns HUD/knife integration;
separate agents own bench tooling, vehicle coverage and destruction observation.
Installed/public 0.1.12 and the frozen R38 performance runtime are unchanged.

HUD source now selects the newest coherent same-owner ammo observation when
counts change between eye draws. Equal-sequence conflicts and owner/capacity
changes still reject. A zero-wait XR texture timeout may retain the previous
released quad only under its original fresh same-owner sample; final submission
rechecks both the new sample and displayed image expiry. Focus/menu/tracking
loss still clears presentation. New tests exercise firing/reload between eyes,
delayed images, expiry inside Layer and weapon replacement.

The shared jab recognizer no longer requires peak start speed at every subsequent
packet: a deliberate forward jab may decelerate before reaching required travel.
Start thresholds, settle/rearm, direction, displacement, native readiness,
tracking/ownership and maximum duration remain enforced. New trajectories cover
72/90/120 Hz, soft contact, reversal and expired travel. This is recognition
polish, not proof of native damage or a fix to stock knife audio/windup.
Both normal architectures passed all 305 suites and the selected HUD, jab,
vehicle, menu and draw-witness regressions. Explicit x64 HUD pixel readback and
draw-witness WARP checks also passed. Native/headset acceptance remains pending.
Frozen R39 normal runtime receipt:
`3fea6f1d7cb01bd1c824ba642d56d92bf38e53aa1a05449f8154f4076b6af8a8`.
The strict four-case host environment smoke passed without creating an XR
instance (`polish-r39/host-env-smoke02.json`); its two earlier harness attempts
are retained failures, not acceptance evidence.

New tester feedback reports an XM8 detaching from its hands. The shared rig
review found two concrete defects: RetargetWeapon wrote its caller-owned root
and shot/tool publications before optional visibility preparation could reject
the matching hand palette, and generic bindings could bypass the final tracking
guard. R40 source prepares every fallible output first, then rechecks exact
tracking/actor/item/space identity, native rig/output and the original deadline
before committing root, pose, shot and tool together. Regressions cover producer
invalidation during the pose mutex, replacement/recenter, same-owner newer
packets without lease renewal, and native conflicts. Both normal builds and all
305 suites pass; native validation is in progress. The tester's particular
trigger has not been established. A later
Pack boundary can still reject stale native data; do not weaken that protection.

R40 attempt01 passed all 305 x86 suites. Its x64 suite failed the existing
ThreeSourceExpiryAndClose assertion that B remained fresh after Sleep(120),
with a 200 ms original B deadline. The failed log has no B timeout snapshot, so
its cause is unproven. Attempt02 changes only this regression: exact B delivery
is still required while fresh; a timeout is accepted only with B's own exact
key/deadline and observed real expiry, followed by Discarded retirement. Native
timeouts, channel code, sleep and retry behavior are unchanged. Independent
review passed. Attempt02 then failed compilation because its new diagnostic
output lacked <iostream>; no tests ran in that attempt. Attempt03 corrected the
include and passed all 305 suites on both architectures and selected handling
regressions. Earlier failures are preserved. Frozen normal runtime receipt:
`837874d76e2fd56e6567137c6dde83e6e82e69c1d412f69cf950578b8083c2d3`.
Native source closure is unchanged between attempts:
`0cd4c2469662c9b0106b186e1e59871d79bb0ab62d5f996ad5f5701d8e2bfe69`.

R41 addresses a separate shared continuity defect found in follow-up review:
Pack used try_to_lock(frameMutex), so producer contention could revert only
hands after a root/shot commit. The prepared pose is now an atomic immutable
publication; Pack retains exact current identity, native source and original
expiry checks without reading mutable Tracking. Preview expiry is independent
of later input packets. Peer review passed; the focused regression runs the
production handoff while the producer holds its mutex and verifies subsequent
owner/recenter/expiry/source invalidation. Native source closure is
`ddf0b7fde7baf97246149b2189f2ad31bb1a36bb13c1cc0d98538ec281725c63`.
Build admission initially refused at 37.41 GiB allocated plus 3 GiB planned.
Three bounded lossless compression runs recovered 790,462,861 bytes without
deleting files or changing content hashes. Build01's focused x86 handling and
channel tests passed, then its wrapper rejected a missing static-menu target
before the full build. Build02 preserves/reuses those exact focused results and
native header paths, explicitly builds the menu target, and has passed the
installed-image check and all 305 normal suites on both x86 and x64. Frozen R41
normal runtime receipt is
`825f45f863fe88a67d197ef2913af9b5d8ba63782e9b21e4496db6345fc9a264`.
It is the rollback before the diagnostic successor. No R41 native/headset
acceptance is claimed.

Game74/onfoot46 captured an AMD-worker loading fault before world rendering.
Game75's newSP helper launch also failed without any VR adapter attached; its
original profile was restored. Game76's planned stock-menu control sent no
input because Windows refused foreground activation; it closed normally and
restored the profile. This is no loading-control result. Native handling and
destruction acceptance remain open; do not treat these failures as new weapon
or graphics acceptance, nor repeatedly rerun the unchanged failing route.

Game77 stock-menu control (PID 18656, creation FILETIME 134374234274932747)
loaded Levels/SP_01 through the visible Resume Game button in 39.2 seconds.
The 60-second exception observer recorded zero exceptions and detached with
the game running. Native map-name observations and a window capture verify the
first mission's mounted gun. This is not the failed SP_03 newSP loading case
and does not clear that separate crash or prove VR rendering.

The initial black menu was a confirmed missing-EA-movie stall: exact live
startup state was Movies/UIMALogos, enabled, index2/count3. The user's disabled
EA/Dolby files remain untouched. An instantaneous Escape did not advance it;
one ordinary 150ms Escape pulse did, followed by a visibly interactive menu.
No helper or VR module was injected for this loading control. The shared
wait_main_menu gate proves renderer/UI readiness, not visible buttons; do not
require completed movies before the VR startup helper is allowed to attach.

The user moved the game to the right monitor before authorizing this check.
Root then exited the mounted gun using its visible E prompt. On-foot inventory
is Nambu100 (selected, three copies idle2, 30/300) and an undrawn Garand
(three copies state0, 8/74). The old cross-draw preflight rejects that inactive
state; this is a harness admission issue, not evidence of a broken gun.
Game77 has since closed normally and its exact three original profile files
were restored. The stock control and later VR attempts have separate receipts;
the original stock completion must not be rewritten as a later VR result.

Game78 (PID 49408, creation FILETIME 134374428277763864) loaded SP_01 through
the same stock menu at temporary 1920x1080. Its original profile is backed up in
vanilla-r43/run04. R43 cross-draw run01 under cross-draw-r43c-prep delivered all
240 stereo pairs with zero receiver timeouts and clean native detach, but FAILED
the weapon-transition check: Nambu-to-Garand selection occurred, its body draw
never committed, and the return shoulder draw was not admitted. Tracked poses
stopped at 613 after input203, while input and stereo continued through 14s;
2,144 later rejections were rig_snapshot. Ammo presentation expired too. This
is a reproduced shared post-selection failure, not a successful handling test.
The read-only discriminator identified a collapsed jntWpn_6 leaf directly under
the weapon root in all three native palettes. The old reader preserved only
jntWpn_7; it rejected this legitimate hidden part and therefore the whole rig.
Three owner/identity-bracketed samples agreed. Game78 then closed normally and
run04's exact original profile was restored.

R44 source now preserves structurally verified hidden jntWpn_ leaves, retaining
the exact observed collapse signature in all three palettes, valid inverse bind,
direct-root leaf topology, arm-branch exclusion and native ownership checks.
Hidden leaves remain unavailable as grips/attachments or arbitrary write targets;
the separate SPAS owned-shell unhide permission is unchanged. Actual old/new
production reader replay against the same captured first-observed bytes rejected
with R43 and accepted with R44, preserving jntWpn_7 and jntWpn_6. This external
memory reconstruction is explicitly non-atomic and does not prove live handling.
Its first compile failed on a runtime-library mismatch; the corrected compile
executed both replays, then its receipt serializer failed. Actual result files
and a separate pinned review are retained without rerunning successful checks.
Both normal R44 architectures now pass all305 suites, the focused rig/shell/
reload-palette checks, and the installed-image menu check. Frozen normal receipt:
`3d306fddf81697d8d823b97ee3656a494d5aeada358df6b5e622765ccd277a30`.
Native source closure:
`a73be28bec0cb6388230fc35ff4f1dabec5fe7a33e1fc86ddc8f9db1af8a0352`.
The R44 live cross-draw run received240 pairs without receiver timeouts and
observed actual remote probe exit0 with verified native cleanup. All1,697
tracked/paired poses completed with zero rig-snapshot, packing, fallback or
partial-commit failures; Garand had35 retained hand samples and Nambu continued
through input881. This validates the reproduced hidden-leaf rejection fix, not
full handling. Both native selections occurred, but requests1/2 cancelled before
their visible acknowledgements; request3 then stowed Nambu. The unchanged strict
analyzer rejects missing fresh left-shoulder contact. Final Recovery began only
after the input stream ended and is not evidence of an in-play deadlock.
Independent timeline: cross-draw-r44-prep/run01/independent-timeline-analysis.json
(`81d374bff5c6583f024ea3ba3689a0901a061e4c6466707f6df2fb4dd924a8a7`).
The shared failed-Apply path cancelled a pending ordinary draw when its own
successful native selection triggered the neutral input rearm barrier. R45 now
retains only the exact issued-target transaction through that barrier, under
the original policy acknowledgement deadline. The composition regression passes
24 historical-failure and24 corrected cases, with both shoulders, independent
native/physical epochs, movement and slow cadence. Live acceptance is pending.
Build05 compiled completely and passed303/305 suites; the two failures were
old enrollment assumptions that every body profile had a manual-magazine route.
The tests now explicitly preserve17 resource routes and require the new Type5
body-only row to have none. Build06 reuses unchanged, verified native header
artifact paths to avoid unnecessary recompilation, but requires fresh full
x86/x64 results and a new full-source manifest. No failed validation is reused.
Game79 (PID52340, creation FILETIME134374456324324262) closed normally and its
three saved profile files were restored exactly under vanilla-r43/run05.
Ordinary Escape after the completed-but-blank stock startup showed the main
menu; no VR module was attached during that recovery.

Reviewed Type5 campaign data is integrated into four catalog/generated headers
for the next build only. Existing48 records remain unchanged. It adds the exact
Beach/Jap_Type5/Type5.dbx display/visibility description, with admission flags
still disabled; it grants no reload/support-grip/native mutation capability.
Source integration receipt is type5-coverage-r44-prep/enrollment/integration.json
(`bf4bbf2d1631fcc0f834ec26b210c615ef529ddcada2f819927be07ab37495eb`).
The candidate geometry cache was generated through the existing Type5
derivation: 49 profiles, 16,750,541 bytes, SHA
`dade888c8bc205491a6e5a421055adf2085e1c864efede36b36f32415aa698d3`.
All 48 prior serialized records are byte-identical, and all 49 indexed geometry
hashes pass. The cache remains 26,675 bytes below the existing 16 MiB limit.
The production x86 C++ loader accepts all49 records/279,908 triangles and its
seven magazine chest-clearance checks pass. Native display acceptance remains
pending. Installed caches and public 0.1.12 remain unchanged.

The initial r43-prep attempt stopped before injection on an unsupported receiver
CLI flag. r43b run01 attached but produced no pairs because the native 1024x768
renderer did not match the 1920x1080 receiver. r43c adds a checked live renderer
size/identity gate; it does not weaken ownership or final-selection assertions.
Its bounded capture exception was reviewed after pruning a hash-verified
duplicate generated body cache. Actual raw capture use was 248,832,000 bytes;
no user recordings were touched. Further identical runs are not justified until
the now-reproduced pending-draw cancellation is corrected; R44 addressed the
preceding rig failure.

An independent report audit found that NativeTrace returned probe_exit=0 on
game-process exit without observing the remote probe's return. All three
inspected installed 0.1.12 headset final reports (October 19–21) were empty;
their periodic telemetry lacked rig miss/rejection counters. They cannot
corroborate a particular detachment cause. The R42 diagnostic successor
distinguishes actual game exit from remote return and adds existing atomic rig
counters to the existing 1 Hz telemetry. All ten reviewed staged files are now
merged into the checkout; frozen R41 remains intact. The staged PowerShell
validation passed 37 native-failure and 86 portable-session checks, including
missing final reports, process crashes, and a contradictory game-exit report
while the held original process is still alive. R42's focused C++ checks,
installed-image check and all 305 normal x86 suites passed. Its x64 build and
freeze are intentionally deferred: a newly identified input-handoff correction
will be combined into R43 before the next two-architecture validation, avoiding
an immediately superseded x64 build. Frozen R41 remains the complete rollback.
See report-retention-r41-prep receipt 48b457dc... and the pinned
history report ff112179... for scope and validation status. Independent counter
samples do not establish exact root/hand/pair correspondence.

R43 separates complete immutable Tracking publication from pose
scratch ownership. The prior producer could drop a full input update on mutex
contention while publishing a newer independent guard; a root getter can also
skip retargeting while an earlier hand pose remained eligible. The narrow
correction removes producer/scratch contention and retains original expiry and
native/current-owner checks. It does not yet establish exact root-to-Pack
snapshot identity or solve concurrent Retarget scratch contention. The
regression and production publication wiring were independently reviewed and
merged. Both normal architectures passed all 305 suites, focused probe/rig
checks passed, and the installed-image check passed. The modeled old
producer-lock split reproduced; the production input, commit and pose-handoff
helpers matched the new root/palette under contention. This is a deterministic
regression, not a native gameplay observation.
Frozen R43 normal receipt:
`3d015540e35f641fa078921a7b12beeab106e8a0c804534602be8f1a2ba72317`.
Native source closure:
`1e2eac9a69752e1d14bf6bd1720a0e0ef51fc8ade2a4462e1a68b317a26aa882`.
R43 includes R42 diagnostics; R42 itself has only x86 validation and was not
frozen as a complete runtime. Installed/public 0.1.12 remains unchanged.

The peer-reviewed stable native-operation binding type extraction is integrated
for R45, preserving the exact type layout and admission algorithms. The unchanged
pure CompleteOperationBinding validator also lives beside the stable type so
direct consumers do not inherit generated admission-table dependencies. Its initial receipt
is build-efficiency-r44-prep/integration-r45.json
(`df5f185c49866dd215bd3a810457f2b70b3274d7f622506f5663eb20ddb0b4be`).
The separate native-root-pointer identity change remains staged and unbuilt.
The build audit counted
110 compilations/259 links in R42 and estimates 136 incidental generated-header
dependencies can be removed; this is not a measured timing gain. The root
candidate does not itself add a fresh root check at Pack. Exact native static
root ownership findings are in rig-root-native-r42-audit/findings01.json
(81133b25...), and the build audit is build-efficiency-r43-audit/analysis.json
(875fc5c6...).

Knife audio investigation decoded all five exact 48 kHz stereo resources using
the installed FFmpeg, with complete packet/sample validation and an independent
per-channel comparison of one variation. The bounded recovery output totals
1,278,136 bytes. Ordinary sounds contain strong regions around 0.31-0.38 seconds;
the kill sound has separate regions around 0.39 and 0.83 seconds. These are
acoustic observations, not identified impact/damage events. Playback, trim and
kill cues are unchanged; findings are in knife-audio-decode01/analysis.json
(939590e4...).

The vehicle auditor has 11 explicit authored scenarios / 14 human entry edges;
its seven Python regressions pass. This is an inventory, not all-vehicle
acceptance. Attached-soldier passengers and UAV mapping remain production gaps.
See [campaign coverage](CAMPAIGN-VEHICLE-COVERAGE.md).

The first same-runtime control (game72/onfoot44) failed before native attachment:
PowerShell cleared the three-source flag to an empty value, which the frozen host
rejected. Geometry admission and cleanup passed; no native trace/measurement
exists. Preserve that failure. A source fix treats empty as disabled, like absent
or zero, while keeping invalid nonempty values rejected. The frozen executable
reproduced the parse failure with an empty flag and passed parsing with zero;
both probes deliberately used a nonexistent loader and created no XR instance.
The corrected same-runtime control2 (game73/onfoot45) launched once and crashed
during loading, before any world draw, stereo pair or CPU measurement. Its
389 menu surfaces and zero foreign/reentrant Present omissions do not prove
thread safety. The retained game XML reports EIP zero; no new-run module map
establishes the caller. Do not repeat the unchanged benchmark. A bounded,
default-off graphics call/thread observation is being prepared before another
native run. No percentage gain has been accepted. R38 standalone remains 88.183674 fresh
pairs/s under its recorded conditions, not headset FPS.

Lossless compression this segment reclaimed 1,092,193,009 bytes from completed
raw captures and inactive rebuildable files, across four retained receipts.
Contents were hash-verified unchanged; no evidence/user recordings were deleted.

The campaign bench catalogs exact mounted configurations and now has a
single-magazine episode in preparation, avoiding a mandatory SPAS companion on
maps where one is absent. Catalog/configuration coverage is not game acceptance.
The destruction no-fire three-pose survey fixture compiled in its bounded
recovery directory; its session wrapper and native scene evidence remain pending.
Knife timing/sound observation is staged separately, default off; production
windup, damage and audio behavior have not been altered by that staging.

## R38 game check passed: depth admission and bounded timeout evidence

R37b isolated a shared optional-depth admission gap: a transient staged-producer
try-lock failure at left-eye scope entry can omit depth although later color
preparation succeeds. R38 retains read-only clear observation on explicit Busy
only and permits one exact-owner retry before the destructive world-depth clear.
Persistent Busy, changed ownership and invalid preparation remain absent;
native Draw, restoration, request deadlines and source retirement are unchanged.
The shared path covers both two- and three-source modes. Source review passed.
Existing policy and actual-bridge GPU cases now cover transient/persistent
contention without added sleeps or artificial GPU completion. Attempt01 caught
a private-API access error during compilation, before tests or game execution.
Attempt02 routes preparation status through the existing public depth-begin call;
it performs no additional graphics-lock acquisition. All four focused x86 cases
(depth policy, actual GPU depth pairs, async channel and pipelined channel) and
all 304 full x86 suites passed. The same four focused x64 cases and all 304 full
x64 suites passed. The private benchmark AA helper passed its existing 55 checks
against the actual changed dependency closure; fixture binaries were reused.
Bounded local timeout snapshots now retain up to 16 failed request identities,
slot phases and timestamps, with explicit unavailable/overflow states. Existing
timeout decisions, first-timeout evidence and IPC wire layouts are unchanged.
Attempt02 source freeze:
`50511d01243945b481830a5b179f88a05da150f84c5da8ad87a1fc1e0b9e48ad`.
Native source closure:
`2510eae6fc2e281d1f3302c58ffb9805f82af578251723525429fcda23870d00`.

R38 game70/onfoot42 passed its complete session, early/full window geometry,
guard release and normal game close, with no settings write. All four host
error counters are zero. All nine measured intervals contain 798 consumed pairs
over 9.0492941 seconds: 88.183674 fresh stereo pairs/s, not physical-headset FPS.
All 762 retained interior native rows and 245 exact host joins have full actual
1x depth; none has the interior depth omission observed in R37b. A startup
request with absent depth was discarded before measurement, and two later
discards are after the CPU interval; the raw report retains them explicitly.
This run does not prove the cause of the earlier intermittent startup timeouts.
The R37b first timeout was entirely in benchmark phase0/queue3/post-service-off;
ordinary experiment-disabled startup enables post-Present service immediately.

Frozen R38 runtime:
`fc7e66bf3bcb96a6dcd1458e9ecca5a1011fbd0b27d92b3a7d0b4aafcad12ff2`.
Parent completion:
`2e9cec9b86764752b4ebf2ef75a4e9455ff97ce523e05dc8a6b5fca89de329d2`.
Raw depth/timing report:
`8a466de969c65fe2912bfb006d7805c1e6fa09ea58078c16f29f30e77188b8ff`.

Percentage comparison is still blocked: the historical accepted R33c host used
adapter LUID108849, while this session used110367. Archived hardware records do
not establish unique matching adapters across those observations. Prepare one
fresh frozen-R33c baseline on this session's adapter; do not silently relax the
identity gate or advertise a gain from unmatched runs. The historical R33c
producer unconditionally wrote performance_qualified=false even for successful
reports; its schema-specific accepted-observation fields and raw reconstruction
are the authoritative acceptance evidence. This schema mismatch was corrected
in the new comparator without changing historical evidence or measurement gates.
The one fresh frozen-R33c baseline (game71/onfoot43) matched the current adapter
and geometry, with zero host errors and clean close, but reproduced an interior
depth omission: request864/slot1/frame17208. It remains rejected; raw 539 pairs
over 9.05058 seconds is not an accepted gain baseline. Refusal report:
`38ab96a42f39451bc432d4b4145707861c1300cffa742590a9d9a432c37501b0`.
Next control uses the same corrected R38 binaries with two sources and export
overlap disabled. Post-delivery refill is three-source-only in this build, so
the comparison covers the combined delivery policy, not buffer count alone or
a claim against the installed public release. Do not repeat the known old bug.

Private native-headset preparation is separate from the installed launcher.
Its AA helper generalizes the two fixed benchmark-size entry guards, retaining
exact native-size/owner/cleanup checks and the existing per-source allocation
ceiling. Source review and all 105 helper checks passed. Private launch binding
is staged at `performance436/headset-r38-prep/candidate`, and ValidateOnly passed
with the existing asset cache and SteamVR selection; no game or XR session was
opened by staging/validation. No physical-headset
performance or full-campaign claim is made. Standalone candidate qualification
passed every existing per-arm gate without claiming a cross-run gain:
`f74e2bf5cf05a84f11db62bec88712e5e511054d4a4183197d77f867a4dcac73`.
Dynamic helper build:
`c60ccb616c56c8cb9c86f2f47961c3d685c512783725888f0b8cd8864125d898`.

## R37 validation: explicit three-source delivery candidate

R36 identified source reservations lasting about 30.15 ms across 166 exact
native/host ticket joins, including about 11 ms until the next post-input copy
completion observation. Both sources were occupied 80.91% of the retained span.
The next candidate combines three physical sources with successful post-delivery
request refill, retaining two undelivered immutable requests, one host copy and
one unreleased camera render. Neither change alone provides this overlap.
This is implemented behind explicit private v10 selection. Existing v8/v9
layouts/defaults, gameplay, ordinary Draw and headset-selected pacing remain.
An extra source costs 202,051,584 logical GPU bytes at the measured resolution;
three-source allocation gets a separate explicit cap and the existing available
adapter-budget/headroom proof. x86 focused IPC, staged producer, asynchronous
delivery and host diagnostics passed. The actual GPU test passed its new A/B/C
ownership, original color/depth and retirement cases, then failed an existing
single-source desktop-mirror delivery assertion. Full builds did not run.
The initial failure is unclassified; source review found no default-v8 IPC change or
leaked three-source flag. A 150 ms request expiry during synchronous readback
is a hypothesis only. Attempt02 adds failure-only phase/ownership diagnostics
without changing deadlines, assertions or production code. The changed GPU
follow-up passed all three desktop-mirror modes, and the full x86 build passed
all 304 suites. x64 focused GPU/actual-host checks and all 304 suites also passed.
This does not establish the original mirror failure's cause. Attempt02 runtime:
`fb0243756a495d7b6696205bdf170a8478b287d8a1e3ff936ca8dba22fb9a990`;
source freeze: `439c4f669bdc5b3b031f77c2ffa5d17c48e8cdfafc8770ac7057a17ab22ab6c0`.

R37 game68/onfoot40 completed with clean detach, guard release, normal game
close and no settings write, but failed acceptance: 16 host request timeouts,
two retained discarded pairs inside the CPU interval and a window-position
mismatch throughout observation. The user confirmed moving the window before
measurement. Its size stayed unchanged; the performance effect is unproven.
Nine raw counter intervals give 764/9.048369 = 84.4351 fresh consumed pairs/s,
UNACCEPTED diagnostic data, not an optimization claim. Request1302 spans
266.3582 ms between admission and left color capture (265.9572 ms to first
depth copy); this includes native update/draw/driver/scheduling time, not just
GPU work. First timeout was before CPU measurement; individual times for the
remaining 15 are unavailable. Retained host copy rows do not cover the stall.
Raw diagnostic: `8d0eee44584a89c1c4d0887e978829625b24a77b5f01a7b609d038a0b3e25876`.
R33c remains the accepted baseline. R37b game69/onfoot41 reused the exact runtime,
AA helper and fixture binaries without builds or repeated deterministic tests.
Its early placement check and all 1,500 observer samples passed, with no movement,
mismatch or unstable reads. It still failed the unchanged whole-session gate:
two host request timeouts, zero rejected pairs, menu errors or feedback errors.
The first timeout was again ordinal 11, about 165.58 ms after submission; the
shared startup path is under investigation. This does not prove the earlier
window movement caused its additional stalls. Mod teardown, guard release and
normal game close passed, with no settings write. Completion receipt:
`48dc90453a496862406a763f817d150413de8ed2d6159933cbe0167c90bc9dbe`.
No accepted performance comparison or headset-ready claim follows this run.
All nine enclosed R37b intervals report 797/9.0467423 = 88.0980 fresh consumed
pairs/s with zero interval discards, UNACCEPTED diagnostic data. Two of 760
retained interior native rows have no depth copies: request1298/slot1/frame18057
and request1686/slot2/frame18654. Color-only delivery is allowed by the protocol,
so zero host rejections do not establish complete depth. The final source
descriptors concern later tickets and cannot fill this evidence gap. The first
timeout is 11.632 seconds before CPU measurement; the second has no individual
timestamp. Raw report:
`f77f37afd312a64b5cf962dfd506e5a49e19090aa576b6a475e91c2fdc527ac3`.
Installed/public 0.1.12 is unchanged.

Storage: lossless compression preserved all hashes while reclaiming 535,732,224
bytes. Another 160,200,542 bytes of reviewed incremental-linker caches were
removed; runtime symbols, binaries, compiler objects and diagnostic evidence
were preserved. On October 25, 12 reviewed superseded R2/R4/R6/R7/R8/R9/R10/R12/
R13/R15/R16/R17 runtime PDBs were retired, reclaiming 330,108,928 allocated bytes.
Their binaries, sources and historical receipts remain; those old symbol assets
are no longer available. Active crash, CPU-profile, current and baseline symbols
were preserved. Post-x86 lossless compression reclaimed another 212,684,293 bytes
with identical hashes. Post-x64/current-runtime lossless compression reclaimed
another 582,346,383 bytes, preserving every content hash and deleting nothing.
The eight R37b captures were also compressed losslessly, reclaiming 248,213,504
bytes without deleting evidence or changing hashes.
R38 post-x86 and post-x64 compression reclaimed 208,771,987 and 233,451,130
bytes. Compressing the eight R38 raw captures and frozen runtime recovered
another 348,118,016 bytes, preserving all hashes, current symbols and evidence.
The unchanged 40 GiB ceiling and 3 GiB reservation apply.

## R36 diagnostic completed: steady-phase GPU timing, no performance claim

R36 adds a separate strict opt-in `FVR_PRIVATE_GPU_PHASE_C_DELAY_MS=3000`.
It retains the existing eight-pair GPU recorder, but delays activation until
three seconds after the first verified phase-3 Present service. Default
first-eight behavior, rendering, queue policy, gameplay and query limits are
unchanged. Exact owner checks precede latch mutation; invalid prerequisites,
phase departure and clock failures cannot silently collect an earlier window.
The report includes latch, threshold and actual activation timestamps.

Source and peer review passed. Focused diagnostic tests and all 304 suites on
both x86 and x64 passed. Runtime receipt:
`89e012bc9f1c5b898534400f7bf8072487d21f0cff2a2c1068d6d0f3d5d67ae3`.
The existing private CPU ownership window still gates GPU admission. All 88
selected GPU intervals qualified: their CPU marker-issuance endpoints lie inside
both phase 3 and the actual external CPU interior. Eight tracked pairs and
16 ordinary draws were captured together, with no disjoint/query/owner errors.
GPU intervals are nested and are not exclusive GPU occupancy measurements.
The bounded sample spans 24 consecutive native frames: eight tracked pairs and
16 ordinary draws. Mean first tracked draw through export end is 6.48015 ms per
pair; ordinary draws average 2.577495 ms each. Export copies average only
0.22468 ms per pair. These timestamp envelopes are not a sustained throughput
benchmark or a promise of 90 FPS. Their exact native journal rows were already
overwritten, so sampled GPU keys cannot be joined to restoration/fence times.
Safe ordinary-draw removal remains unproved: Draw owns child cleanup and GPU
history updates. Focus remains on frame delivery and source-release scheduling.
GPU analysis: `27cad349f73ec7b2aaeb1ec0478f5248a89647e2f837b00ff7aa0311a7ec14f3`.

Source receipt: `4762d9ff167d9770ed4efc8b39e237077c6f25b93efbdfe3661e84267249218d`.
Patch: `8c535538be845cfb768421da2c720141d1f21fed90033b4650eeef76450c6409`.
Source freeze: `225b9648abdaf15578f8fbeee418a035ebc4557840d38ace4a8c364064c5e634`;
native closure: `9b96f819265f68f0c089fe22a3a375f79c5dc26cc852437fa86b792051054988`.
The first 3 GiB build admission was refused at 37.67 GiB allocated generated
usage. Lossless compression reclaimed 926,279,752 bytes across 919 generated
files, preserving every content hash; no deletion or raised limit. x86 then
completed. x64 was refused before compilation at 37.10 GiB plus the original
3 GiB reservation; lossless compression of 13 newly completed x86 outputs
reclaimed another 197,417,984 bytes. The unchanged admission then passed.
First compression receipt: `d4eaf5582ffb310020c7775781b07d71795b970d4ea0f8c13380d1cf397e3723`.
R36 game67/onfoot39 completed once. The gameplay scenario passed with zero host
timeouts and clean mod teardown, resolution-guard release, normal game close,
and unchanged original settings. The window observer recorded 1,500 samples
with no placement change or mismatch; the CPU-window coverage passed. This
proves sampled equality, not continuous stability between observations.
The GPU and geometry diagnostic qualifications passed. The inherited strict
performance gate correctly rejected the instrumented run ("Unexpected native
profiling enabled"); it is not an accepted performance comparison. Original
AA362/55-check and fixture17/witness27/timing2 dependencies
were verified byte-identical and metadata-rebound without helper rebuilds or
repeated tests. Plan: `30e6638b5524cbba4fdcc5e4229e78923b8b5126fe09e7d4e28a84234c6587b9`;
fixture: `043f30c5e2de7af8e45c5dcfe76c66faa70d62d45a994ff0a256e07c9c6d5863`.
No installed/public update has occurred.
Native trace: `a93d6a45774e302381f5d26acaa0e92f86505880e89b0d77a82a757804747247`;
diagnostic qualification: `82ae1dd97feaf2ec677421a575b2951d66936fb85d8e8933cf45824184dd53b3`.
Native protocol evidence confirms export overlap enabled with 1,299 capture
releases from 1,299 attempts, no contention or declined release. The completion
receipt's `export_overlap_native_game=false` describes direct environment
injection; the host negotiated this feature through the shared channel.
Raw enclosed intervals measured 539 consumed / 9.0482391 seconds = 59.5696
pairs/s, with zero steady-window discards. This is diagnostic only; R33c remains
the accepted baseline. Exact joins and coverage are recorded in cadence analysis
`98ae4450af451a29782247957897ed186d4a207f698b0d2c035c344e7b0246ee` and
[R36 history](history/2026-10-24-performance436-r36.md).

## R35 rejected after game66: retain R33c performance baseline

R33c remains the accepted performance baseline below. R35 is frozen; focused
checks and all 304 suites passed on both architectures. Its single game66/onfoot38
run completed with clean mod teardown, resolution-guard release, normal game
close and exact original settings, but failed acceptance: 20 host request
timeouts, one menu copy error and four discarded frames inside the CPU window.
It is not an accepted optimization, installed update or headset-test candidate.

All 377 retained interior rows (373 consumed and four discarded) have full 1x
depth at 2784x3024. Real overlap occurred in 161 of 370 exact adjacent consumed
request pairs, without a retained camera/source identity violation. The new
admission mechanism therefore ran, but the measured rendering/export intervals
were slower and included stalls. The raw eight enclosed counter intervals give
298 consumed / 8.0747487 seconds = 36.90517 pairs/s with four discards. This is
an unaccepted diagnostic rate, not a replacement for the accepted baseline.

The first timeout preceded the first observed tracked-eye draw. The menu error
was a loading-panel source-copy query timeout before the CPU window. Remaining
timeout timestamps are not retained. Inside the window, request1170 took
174.741 ms from Begin to observed fence, exceeding its immutable 150 ms lifetime.
Those are CPU observations, not exclusive GPU execution durations. Native
window position also changed after initial placement; its timing/cause and
performance impact are not established. Source epochs changed during negative
retirement, adding recreation/import work. Do not attribute the entire regression
to overlap or rerun unchanged. Next work must localize these stalls and control
the changing presentation conditions. No new headset test is requested.

Completion: `decf324b3b79d10c5dd28a3c717d4e5c2a9307e82fefd33f530248f08877516f`.
Diagnostic summary: `56afa93f942a19bcd8106afe5bd537057a690f8197eff2e85aba419dd66dbf26`.
Timeout/menu audit: `96aa789f8239a6ba00be0260e0dd715100dd1350fe5d55ed43f88a43c1e164eb`.
Conditions comparison: `625f935cf432abb882f41d0e9f16c3aed0ac3268709eaadbfccd169153a3dc3b`.
The first retained shifted-window snapshot is 16.833 seconds before CPU
measurement; render dimensions and saved settings match R33c. Sparse snapshots
do not establish continuous placement. The user subsequently confirmed moving
the window during this run; its performance effect is still unproven. This is a
presentation-condition confound, not a demonstrated compositor mode switch:
the actual swapchain is legacy DISCARD and reported no occluded Presents.

A read-only window observer under performance436/r36-window-observer was used
by the subsequent R36 diagnostic above. It samples at 100 ms for at most 150 seconds,
caps its JSONL at 256 KiB, and records exact process/window identity plus geometry
and QPC coverage. It cannot move/focus the game or send input. Python/PowerShell
syntax checks passed; the owned process completed and was joined before cleanup.
Source receipt: `3234f58c12ec151c0248153d5116d6bbd216064c3d4df5be9c373dba188e6a07`.
R36 supplies the missing steady phase-C GPU alignment; R32's early eight-pair
timestamps alone did not explain R35's measured-window stalls.

The candidate remains private and default-off in source; installed/public
0.1.12 is unchanged. Build directories contain R36 artifacts. Use the retained
frozen R33 attempt03 runtime for the accepted baseline, not these outputs.

Existing R33 traces show about 4.9 ms between
camera restoration and publication, during which the single camera admission
still waits for the prior source's real export fence. The candidate releases
only that camera admission after exact restoration and ordered capture/mirror
reads on the original immediate context. It does not release source ownership,
publish unfenced pixels, invent an ACK or add a third source allocation.

`FVR_PRIVATE_EXPORT_OVERLAP=1` requires private two-source lookahead. A negotiated
v9 feature uses two formerly reserved words for exact released request IDs;
default behavior remains separate. Focused policy, mapped channel, staged and
real-GPU regression source covers cancellation, FIFO, exact retirement and
preservation of A's pixels/depth after B overwrites the shared capture inputs.
Peer source reviews and focused policy/channel/staged/GPU/host checks passed.
The GPU regression exercised actual pre-publication release and preserved the
original A color/depth after B overwrote capture inputs. The fresh AA helper
passed its original 55 checks; fixture sources
are unchanged and only rebound to the new runtime. Runtime receipt:
`13d985b524b5b9aee73274791309d7a1cba68d3d0a4b52a969dd975839d49c22`.
The initial build reservation
was refused before compilation; lossless compression recovered 639,536,628
allocated bytes with zero deleted files, and the original reservation then passed.
Compressing newly completed x86 outputs recovered a further 280,726,350 bytes
before the x64 reservation; no storage limit was raised and no evidence deleted.
No gain or native90 result is claimed. Production pacing remains xrWaitFrame
at the headset-selected rate. Installed/public 0.1.12 is unchanged.

## R33c retained: 59.78 fresh pairs/s; R34 rejected and source restored

The accepted game64/onfoot36 comparison used the frozen R33 attempt03 runtime
at the same 2784x3024 per eye and 1x AA. All nine enclosed counter intervals
give 541 newly consumed pairs / 9.0493833 seconds = 59.78308 fresh stereo pairs/s,
versus R31b's 47.86325 (+24.9039%). The historical R24 comparison is 2.00367x,
not added percentages. This measures fresh source throughput in emulation,
not physical headset FPS or a verified native90 result. No intervals were removed;
0.9577306 seconds of the interior CPU window lie outside the counter coverage.
The run had no host timeouts, rejected pairs or feedback errors and passed exact
native cleanup, resolution release, normal close and saved-settings checks.
All 507 retained consumed native rows inside the CPU window have full 1x depth
in both source slots; the bounded native trace does not cover the entire run.
Analysis SHA-256: `ac4056ec7ed2919c279d15abbc4f5a66ffb18ac8dc3fc954585be31d4c830ca0`.
Independent raw audit: `fa8d9a6959824d74aa613eaffcb89f9757cdf12ed6f33237c7a77128423e7a30`.

R34's metadata-only refill passed focused checks and all 304 suites on both
architectures. Its single game65/onfoot37 run completed and cleaned up normally,
but the strict full-depth quality gate rejected one consumed depth-absent pair
(slot0/request1078) among 508 retained CPU-window rows. Both slots were used,
254 rows each; all other retained rows have full 1x depth. There were no host
timeouts, rejected pairs or feedback errors. A first depth-admission try-lock
miss fits the aggregate counters, but no request-keyed cause is recorded.

All nine raw intervals give 543/9.0483099 = 60.0112 pairs/s, only +0.38% versus
R33c, and this is an unaccepted diagnostic rate. The retained original prediction
lag worsened from 22.70 to 27.28 ms; this is not physical motion-to-photon latency.
The experiment is not being retained. The two provider files and focused test
were archived, then restored byte-for-byte to the accepted R33 source manifest.
Restoration receipt: `ac88dc43f580ade73b7df3271f3387912a26149f0a0b8a084828d4dddb897733`.
At restoration time the build directories contained R34 artifacts (now R35);
use the frozen R33 runtime for this baseline. No repeated build/test is needed
to validate an exact source restoration. R34 evidence and symbols remain intact.
Independent diagnostic: `d9849bcb5e0807697b041813cf0aa3d8278e0ce665328ff2cd042e48e5878c98`.
The R35 investigation above uses the existing traces to address admission delay.
The missing depth admission remains a separate unresolved observation; the
strict full-depth measurement gate remains in force. No unchanged repeat is planned.
Installed/public 0.1.12 is unchanged; no headset test is requested yet.

## R33: opt-in request lookahead and retained loading investigation

R33 adds one request-only service after current XR input, cancellation and source
feedback. It may retain two immutable undelivered poses while keeping two native
source sets, one native camera render, one delivered host lease and one host copy.
A successor can start at the ordinary native Update while its predecessor is
Ready but not yet delivered. No simulation or native graphics callback is added.

`FVR_PRIVATE_SOURCE_LOOKAHEAD=1` requires the existing private two-source mode.
The v9 mapping negotiates a known feature bit; old endpoints reject it, default
v9/v8 behavior stays separate. Exact original pose/deadline and oldest terminal
reporting remain bound together. Cancellation clears pending metadata before a
deferred source ACK can unlock reuse. GPU fences and graphics retirement still
own every physical source lifetime. Production remains paced by xrWaitFrame.

Source review, focused x86 policy/mapped-provider/staged/real-GPU checks and all
304 x86 suites passed in attempt01. The x64 focused run stopped at the retained
legacy ordered-composition test's final nonblocking staging Map, after its target
event drain passed. Its original assertion omitted HRESULT, so the failure cause
remains unclassified. Attempt02 preserves that failure and adds test-only error
reporting plus one bounded 2-second retry budget, solely for WAS_STILL_DRAWING;
the final drain, exact pixel comparisons and production GPU code are unchanged.
Attempt02 then passed the entire focused GPU test (no transient Map retry was
observed), but the actual-host test process hit Windows exception 0xc00000fd
(stack overflow). The new bounded 101-frame test trace is being moved to owned
heap storage; production code and its memory limits remain unchanged. Attempt03
passed the focused GPU/actual-host checks and all 304 suites on both architectures.
Its frozen runtime receipt is `3f663acab98e4339fdcf929b6cbd64a12c84a7dadd1755a94ce2763da90f9449`;
the source freeze is `64574475593f638b466a769023d8494749ddacbe0569e6941a26b06f1c42a444`.
The unchanged AA helper was freshly linked against the changed dependency closure;
its original 55 checks passed. The menu fixture is reused byte-for-byte. One
game61 launch stopped before VR attachment: native resolution and independent
renderer readback passed at 2784x3024, but the visible window failed the left-monitor
containment gate. The old helper lost rejected geometry; the exact cause is not
established. No onfoot34 session or R33 measurement exists. The resolution guard
released cleanly, and an additional normal-close request ended the owned game
without termination. The failed receipt is retained. The private R33b harness
retains every observed rectangle and allows one conditional position-only move,
preserving size/style/focus and all native/render invariants. Game62/onfoot35
passed these checks without needing a move, then exited with 0xc0000005 during
ordinary Resume loading. All 14 native snapshots contain zero world calls and
pairs; the host submitted zero requests despite servicing lookahead. This is not
a performance result. The saved game crash XML shows execution at zero and a
stack resembling the earlier loading failure; without the current module/thread
map this does not establish driver blame or the upstream cause. Its SHA-256 is
`156f581144ef32d96bd45f77a084e64e5b15a565b951463077e89f4377d27705`.
The process exited, so no clean native detach/guard release/normal close is claimed;
saved settings.ini still matches the baseline. Native menu/graphics/depth/Present
source is unchanged from R31b. One bounded watched Resume (game63/resume-crash03)
then completed with no exceptions and exact native cleanup, guard release and
normal close. It ended 30 seconds after Resume; accepted R31b did not reach its
first tracked eye until 39.796 seconds. Early world progression closely matches
that baseline, so the absence of pairs in this diagnostic is not evidence of a
loading stall. The crash remains intermittent and unresolved. The subsequent
R33c performance measurement succeeded with the same frozen binaries, as above;
it does not establish that the intermittent crash is fixed. Source closure:
`964638aea90cd65c060dcbeb78d8839494975110f02140fcd6cb1561bf1f47de`.
Installed/public 0.1.12 is unchanged.

## R31b: accepted emulated comparison, 47.86 fresh stereo pairs/s; not native90

The one game59/onfoot32 run passed ordinary-menu loading, on-foot measurement,
native cleanup, resolution release and normal close, with unchanged saved text
settings. Both source slots carried complete native1x depth at2784x3024 per eye
through every retained consumed row inside the measured CPU window. There were
zero host timeouts, rejected pairs or feedback errors. Accepted offline analysis:
`performance436/observation-r31b/analysis.json`, SHA-256
`9c1bb8c3329a0a1eae4643e1323094afce53d6dd2247b18b276989fc74fbc7cd`.

433 newly consumed pairs /9.0466068 covered seconds =47.86325 pairs/s, versus
retained R24's29.83684: +60.4166% in these separate emulated runs at unchanged
resolution/AA. All nine enclosed counter intervals were retained (45.71–49.74
pairs/s); no stalls were removed. This is not physical display FPS, not a claim
that earlier headset gameplay ran below30, and not a native90 result. No installed
or public update. R32 completed the bounded actual1x GPU diagnostic below;
retained timing analysis is now examining earlier native request admission.

The first rejected depth eye had only one matching world-depth clear, no invalid
clear, no bridge capture attempt and the correct context/thread. Optional depth
allowed this startup frame to present and the ordinary boat exit to proceed.
Full on-foot depth subsequently worked in both slots. This supports the specific
R31 compatibility correction; it does not prove the intermittent loading crash fixed.

## R32: native 1x GPU diagnostic, scheduling investigation continues

The unchanged R31b runtime completed game60/onfoot33, with exact native cleanup,
guard release, normal close and unchanged saved text settings. Eight early GPU
stereo pairs and sixteen ordinary draws produced 88 valid timestamp intervals.
Two tracked draw scopes averaged 6.82204 ms; the native span from first tracked
draw start through export end averaged 7.224485 ms (6.853–7.69472 ms). Ordinary
draws averaged 3.15369 ms. These are native GPU measurements, excluding host
composition/runtime work. All samples were enqueued in phase2 before the external
CPU measurement; they are not sustained phase-C or native90 performance evidence.

Independent raw analysis: `performance436/observation-r32-gpu/independent-gpu-analysis.json`,
SHA-256 `e8564f86471427216bb2481a29cda23c9ffc0af33c23cfc9781a3e20c193cd9a`.
The original helper's `four_nested_depth_copies` label is inaccurate: only stage0
is nested in DrawOriginal; stage1 is outside. The independent analysis corrects
the interpretation without changing the original receipt. Stage1's mostly zero
timestamp intervals do not prove its work free. R32 is not another FPS benchmark.

R31b timing shows most two-tick gaps occur because the next native publication
arrives after that host tick's provider check. A second immediate post-EndFrame
check would miss 99/100 such publications too. The next design work compares
earlier bounded request admission with native graphics-owner service points;
no extra simulation, fabricated completion or stale pose relabeling is allowed.
Production remains runtime-paced by xrWaitFrame, not locked to the 90Hz fixture.
Installed/published 0.1.12 is unchanged; no headset test is requested yet.

## Previous R31 first run: failed all-depth requirement, evidence preserved

R31 attempt04 passed all 304 suites on both architectures, including the focused
real-GPU and actual-host two-slot regressions. Its frozen runtime receipt is
`87185daa5c01416637cd33c942410c32fb2589cefea4ded03a30c05861c61498`.
The fresh AA helper passed its original 55 checks. The one game57/onfoot31 run
reached world rendering without the prior loading crash, but published no pairs:
4,229 camera pairs began, both color eyes were captured, and all 8,458 depth-eye
scopes were rejected. Zero depth copies reached the bridge; its newly mandatory
full-depth gate then rejected color publication too. This is an integration failure, not
a performance result. Do not repeat the unchanged run or relax depth validation.
The native report shows hooks disabled, restored camera/list/reference state and
quiescent graphics, but the generic successful-scenario cleanup validator rejected
probe exit12 / controller exit2. Separate verification of the exact restored
graph, callbacks and graphics quiescence then permitted guard release and normal
close; saved settings exactly match their original bytes. Original failed receipts
remain unchanged. No new headset test is ready.

Performance wording: R24 measured 270 newly consumed stereo pairs across
9.0492144 seconds of enclosed telemetry in one emulated 90Hz configuration at
2784x3024 per eye, native 1x MSAA. This is not a measurement of the user's earlier
headset sessions, physical headset refresh or desktop FPS. A separate flat test
measured about156 successful Presents/s. Do not equate these different counters
or claim native90 from either. Fixture timing has no explicit30Hz throttle, but
80 of82 retained adjacent exact-ticket pairs took three90Hz ticks. The real
SteamVR0.1.10 session20261020-125259-682-d41037e4 had87.916 host-loop frames/s
and fresh-pair interval median30.512/s, p9042/s at1920x1080 per eye; its failed
ending/timeouts prevent treating it as an accepted benchmark. Neither observation
measures physical display refresh or establishes the external tester's FPS.

Source comparison found a real R31 compatibility change: the working bridge
allows a color pair when optional depth is absent, but the new pipeline requires
all four depth attachments before any publication. Restore explicit optional
depth behavior across bridge, IPC and host; provided depth must still be complete,
exact-ticket, native1x and free from aliasing. Never reuse old depth for a new
color-only frame. Keep full-depth proof mandatory for the measured on-foot
comparison, separately from scene presentation capability.

R31b attempt01 integrates that optional-depth correction across bridge, staged
producer, mapped channel and host, plus a bounded first-rejected-depth-eye record.
The existing GPU/actual-host regressions now cover full, absent, partial-omitted
and restored depth, with explicit advertised-presence consistency. Root and
cross-package source review and the final GPU-lifetime review passed. The focused
x86 GPU sequence (full/absent/partial-omitted/full depth), actual x64 host variants
and all 304 suites on each architecture passed. Frozen private runtime receipt:
`90756cb0660096242f404d0a52041d4dd98260004f924f6657af886c8df689c2`.
The unchanged fixture dependencies were verified for metadata reuse; the freshly
linked AA helper passed55 checks. The one game59/onfoot32 outcome is above. The refresh
audit confirms production follows xrWaitFrame timing; only the comparison fixture
is fixed to90Hz. See performance436/r31-prep/metric-audit.json for pinned evidence.

## R30b loading crash captured; R31 integrated privately for validation

R30 adds one nonblocking completion check after WaitFrame and current-input
cancellation, before requesting another source. It acknowledges only real query
success; busy copies retain their lease. A prior-copy record preserves the exact
old ticket if a successor is admitted. Native first-failure diagnostics now
survive ring wrap, including the actual restored-publication result. These are
scheduling/diagnostic changes, with no gameplay or equipment policy edits.

Both full builds passed all 304 suites. The one game55/onfoot29 measurement
completed with guard release and normal game close, but failed strict acceptance
with three channel timeouts. All 671 source completions occurred at the new
post-input service point; final GPU drain passed. The first timeout matches
retained native request87/tracking4545: restoration/capture predicates passed,
but the final graphics preparation try-lock was busy. R30b attempt02 retains an
exact preparation grant across the already-owned depth/color capture steps and
final publication. The first final-publication-only draft was superseded before
any game run. The revised GPU contention regression and all 304 suites passed
on both architectures; frozen runtime SHA-256 is
`606e267cb07539717e051dcabb9a9f7c71c47be1a30ca6935f2392bc2495aed7`.
The one game56/onfoot30 attempt crashed during loading with `0xc0000005`, before
any world frame or measured interval. Its native report is empty; no performance
result or clean native shutdown is claimed. The first 12 crash-stack addresses
match the earlier R28 loading crash after one ASLR shift. One bounded watched
Resume (game58) reproduced it before any world frame, capturing both exception
chances and the actual AMD module mapping. An AMD worker dispatches through a
queued record whose first word is 0x18, then faults executing address 0x18.
This establishes malformed or incorrectly positioned driver work, not who
caused it. Sampled process virtual usage was about 1.02 GB. The capture completed;
the game crashed, so native cleanup and normal close are not claimed.
BC2's device has SINGLETHREADED creation flags. Source audit found that menu
capture and desktop work at Present did not share the later bridge-service
thread/reentrancy guard. No retained evidence proves a wrong-thread call caused
this crash. A reviewed narrow guard now covers all added post-bootstrap Present
graphics work and records foreign/nested omissions in the existing flushed
session telemetry. This is integrated for validation; no unchanged benchmark
is being repeated.
Real Submit gating, GPU fences and ACK remain unchanged. R29's offline recovery
confirmed that its single channel timeout still fails strict acceptance even
though ordinary-menu validation succeeds. Do not repeat that unchanged run or
promote it into an accepted speedup. R24 remains the accepted 29.8368 fresh
stereo pairs/s at 2784x3024 per eye and native 1x MSAA. 90 FPS is a target, not
an achieved result; installed/published 0.1.12 remains unchanged.

In parallel, R31 preparation covers two bounded native source slots with one
host copy/presentation path, so rendering a successor could overlap an older
source transfer. All five reviewed packages are integrated privately (26 files),
including actual-host A/B/A reuse and per-slot recreation coverage. Attempt01
was source-only; attempt02 combined the Present guard and stopped on a signed
QPC diagnostic field compile mismatch. Attempt03 corrected that field and found
a test helper return-type mismatch; attempt04 corrected the helper and passed
both builds. The first live result is recorded above. Review caught and fixed a fault path that stopped new
admissions but could still publish previously staged work. The contract retains
one native camera render, exact slot/ticket
ownership, real GPU completion and cancellation before stale acknowledgements.
Additional resources require actual 1x depth validation and bounded admission.
No performance claim follows from that design alone.

## R29 in validation — GPU-ordered host composition, no new FPS claim

The private `FVR_PRIVATE_GPU_ORDERED_PRESENT=1` candidate is implemented and
defaults off. It queues local depth/color copy and composition on the same host
immediate context, preserving the source lease until actual GPU query completion.
It permits composition in the delivery frame and removes the conditional
two-host-frame scheduling ceiling. A separate shutdown query covers later XR
image writes. Source review and focused real-GPU/actual-host tests passed.
Attempt02 passed all 304 suites in both full builds. The first GPU-test attempt
exposed a swapped generation-field test fixture; only that fixture was corrected,
and its failure remains recorded. Frozen runtime receipt SHA-256:
`3be4899ff37c3171dd664c7e52738b0bd298b553efb6c84528d3a386820257b2`.
The one game54/onfoot28 run completed with native cleanup, guard release and
normal game close. Its original measurement receipt failed because an older
analyzer still expected the replaced Continue-injector file. Offline recovery
uses the retained ordinary-menu evidence; the original failure is preserved.
Independently, the host reported one short channel timeout before the measured
CPU interval, so this is not an accepted performance result even after fixing
that analyzer mismatch. No gameplay code changed from R28.

Diagnostic timing shows early admission working (exact acknowledgement to next
Begin about 0.004 ms median), but host source-copy completion is still observed
one host frame later. Native Begin-to-publication remains about 10.7 ms. The next
private change targets completion after WaitFrame and after current-frame input
cancellation, before provider admission; no spin, early ACK or second delivery.
The accepted result remains R24's 29.84 fresh stereo pairs/s, not 90 FPS.

The one bounded R28 watched Resume produced no exceptions. Its 30-second stop
occurred during loading after stereo owner initialization but before any pair
publication. The original scenario rejection is preserved. Separate read-only
evidence proved graph/reference/callback cleanup; exact resolution release and
normal game close passed. BC2 saved MSAA=0 on exit; the only settings change was
reconstructed and restored to the exact original MSAA=3 file hash. This run is
not a performance measurement and does not resolve the prior intermittent crash.

## R28 implemented — early native frame admission, no new FPS claim

The private `FVR_PRIVATE_EARLY_FRAME_ADMISSION=1` candidate admits the next
camera/visibility request after the exact host completion acknowledgement.
It records the old lease separately and delays its GPU ownership callback until
the graphics thread, before any successor depth/color resource mutation. The
feature defaults off and is restricted to the legacy fenced sharing path.
Gameplay, weapon and interaction policy are unchanged.

Independent source review and the x86 real-GPU ownership check passed, including
distinct eye pixels, visibility-thread admission, discard recovery and cancellation
before the successor's first capture. The first focused attempt exposed a test
sequencing error in the new timeout diagnostic test; its failure is preserved.
The corrected test admits a replacement request through the normal host API only
after the cancelled producer publication releases its old slot. No production
change was needed for that correction. Attempt02 passed all 304 suites in both
full builds. Frozen runtime receipt SHA-256:
`6ef0e3f06126a0f3c3c849e88ded259bd1d7cfe295780535ddfc5b056fa46888`.

The one game52/onfoot27 run exited during loading with access violation
`0xc0000005`, before any world callbacks, frame admission or stereo publication.
One menu press/release completed; no measured interval began. The host reported
zero menu, frame-timeout or rejected-pair errors. This does not localize the native
crash. Windows had no matching application crash event; the game's own XML was
subsequently found and preserved as `observation-r28/game-crash.xml`. It reports
EIP `0x78417f80`, but lacks loaded module bases, so module attribution is pending.
Normal game-close/guard-release acceptance failed because the game had already
exited. Completion SHA-256:
`531849eeee7cfc1735a86342747fa10dcde3336b2d70a4f87c672dda16979e48`.
Strict analysis preserved failure and emitted no FPS result:
`fa7d965079e91164a48ed3c5418e308cdb17d1573869511df742a7e58fb986f3`.
The bounded exception-context follow-up is recorded above; it did not reproduce
the exception and is not a performance result.

First-error diagnostics now retain menu failure and asynchronous request timeout
identity. Timing records distinguish logical acknowledgement from graphics
release, so overlapping camera preparation does not masquerade as early GPU reuse.
The next measurement keeps the R24 native resolution/1x settings and disables
the R27 host copy service. Do not repeat the accepted R24 baseline.

The source review also identified a separate conditional ceiling: with the host
copy service disabled, the legacy asynchronous host path needs at least two host
frames per fresh pair. At a genuine 90 Hz host cadence that path is limited to
45 fresh pairs/s, independent of GPU speed. R29 GPU-ordered composition is now
implemented privately but has not been validated or measured. Latest accepted result remains
29.8368 fresh pairs/s. Public/installed 0.1.12 is unchanged.

## R27 built and exercised — no accepted performance gain

R27 distinguishes a signaled timer, a bounded timeout and an API failure. A
timeout ends only its current service episode, before another poll or ACK,
retaining the copy and future timer availability. Genuine failures still
disable the private service. Focused coverage includes timeout/rearm, owner
loss and clock failure. Diagnostics retain one first rejected-frame snapshot
even after the rolling frame log wraps. No gameplay code changed.

The private menu fixture now follows the existing verified/serialized native
task-migration contract; it does not require a fixed worker TID. Both full
builds passed 304/304 suites, reusing identical native provenance paths to avoid
unrelated recompilation. Frozen runtime receipt SHA-256:
`80d1aee9d0361a863f9fa98db70a5f2786d7d1859d28c2ccf3185350b9208a3c`.

The single game51/onfoot26 run kept servicing available throughout (730
episodes, 90 observed completions, 640 expirations, zero timer API failures),
but failed acceptance with two frame timeouts, two rejected pairs and one menu
error. The first retained rejection is a completed, cancelled Ready copy,
sequence 3, while menu ownership is active; it is not a failed GPU read or a
proven shutdown event. Other errors still need attribution. Cleanup, exact
guard release and normal game close passed. Completion SHA-256:
`02e18ee9e7172cdf36c08a7f6e55817159ceb61941ca815b070279664209216d`.
The strict offline analyzer preserved the rejection and issued no FPS result:
`019546de2324d3b81ccc6f9b0c78de87033735935f4184ad30e7c53aff804c3d`.

No public update exists. Latest accepted measurement remains **29.8368 fresh
stereo pairs/s** in the R24 native-resolution/1x emulated scene. This is not a
headset or campaign-wide guarantee, and 90 native FPS has not been achieved.
Do not repeat the baseline or unchanged R27 run: first attribute the existing
menu/transition and timeout evidence. See [R27 history](history/2026-10-24-performance436-r27.md).

## R26 rejected — private timer timeout handling needed correction

The one R26 game50/onfoot25 run reached its measured interval, then failed
acceptance. A normal `WAIT_TIMEOUT` (258) permanently disabled the private
host-copy service before the CPU interval: 157 episodes, 26 completions,
130 expirations and one admission loss. The candidate is not a qualified
performance comparison and has no accepted FPS gain. Cleanup, exact guard
release and normal close passed. Completion SHA-256:
`03f5d90cbcfe8dccc02f218154679d51abd7d21541cf0c0f6ff1867cfe737cd2`.

The first reported rejection, 3125 native menu worker-thread changes, exposed
an incorrect private fixture assertion: Bc2MenuBinding.h explicitly permits
serialized task migration. Exact callsite/controller/InputNode validation and
serialization passed, with one down/up, no input rejection/release failure or
held button. A worker TID is a last-observed callback, not permanent ownership.
The failed receipt remains unchanged; R27 will correct the fixture assertion
and distinguish ordinary bounded timer expiry from actual wait API failure.

The R25 ordinary-menu run (`game49/onfoot24`) reached gameplay with one verified
pointer press/release and permanent menu-automation retirement. Loading took
52.5 seconds, exceeding the inherited 90-second fixture's 40-second remaining
work allowance. It stopped before the exit pulse or CPU/performance interval;
native cleanup, exact resolution-guard release and normal close passed.
Completion SHA-256:
`6302b5adf0d32ca2d54cbe040f7409f80eb60dedef683420d4b8b30e1dfa251`.

Its startup-only host counters nevertheless exposed a concrete wait defect:
24 service episodes, only one observed completion, 23 expirations/overshoots;
mean episode elapsed 8.858 ms, maximum 17.5321 ms, against a requested 2 ms
budget. These are **not FPS measurements**. The coarse `Sleep(1)` implementation
will not receive another full performance run unchanged.

R26 replaces that wait with an opt-in, Host-owned high-resolution auto-reset
timer. Creation/arm/wait failure disables private servicing without completing
or retiring its pending GPU copy. No coarse fallback, spin loop or global timer
setting is introduced. The existing three-poll/two-wait limit and QPC checks
remain; scheduler/driver overshoot is still reported, not claimed impossible.
Independent source review and both 304-suite full builds passed. Native/game
integration source digest remains exactly R25's `100d1f9f...63683a2`.
Frozen runtime receipt SHA-256:
`eb7b5365c12edb1933d298f0d8011d33f50e19293b373f255c35a16a58887c10`.
One API-only timer smoke check completed all 16 waits (1.0169–2.763 ms observed);
this is not an FPS result or a hard deadline guarantee.

A separate private fixture raises only total loading lifetime to 120 seconds;
the 12-second CPU interval, two-second settling exclusion, 15-second actor
witness and 90 Hz emulated pacing remain unchanged. R24's
29.8368 fresh pairs/s at 2784x3024/1x remains the latest accepted measurement.
Installed/public **0.1.12 is unchanged**.

## R25 built — no accepted performance interval

The private host-copy service is implemented and defaults off. When explicitly
enabled it polls only the newly submitted, exact legacy copy for at most three
polls/two scheduler sleeps with a requested deadline of at most 2 ms. Only an
actual GPU query success acknowledges the source; local Ready images remain
owned through presentation, and expiry retains the existing asynchronous path.
Diagnostics record elapsed time and scheduler/driver overshoot.

Both normal builds passed **304/304 suites**, including focused ownership,
cancellation and deadline regressions. Runtime receipt SHA-256:
`b94594e110ec4f27b739b8c3e5318cdb3e37ac93850fca2f8ae2932cffb37067`.
The first attempt (`game47`, `observation-r25`) stopped before host launch or
measurement: Windows Defender quarantined the older Continue injector during
preflight. The AA helper drained, resolution guard released, owned game closed
normally, and saved text settings remained unchanged. No new FPS result exists.
Completion SHA-256:
`0f55baa07fbae8f8e07731462fd2001c0fa832a7678e263a1dd9f0f99b54c9da`.

The replacement fixture uses ordinary emulated VR pointer/trigger input through
the existing host menu path; its 132 focused checks passed. One settled native-
resolution menu image was captured without clicks. The private harness then
incorrectly demanded gameplay cleanup from a menu-only session. A separate
recovery verified the native `main_menu_completed` no-world path, released the
exact resolution guard and closed game48 normally; the failed original receipt
and capture are preserved. Native menu callback and HWND/Present ownership are
separate, observed threads, not assumed equal. Recovery completion SHA-256:
`dba12a0f86c50b86e404274ff8575104f4253aa6eabd7c5acc0ef3399d10f8b1`.

The Measure attempt used the reviewed Resume Game UV (0.88, 0.378), reused the
compiled fixture and its tests, and retired menu automation at gameplay. Its
loading allowance ended before the measured interval as recorded above. No
security exclusions, replacement injector, recapture or repeated baseline were
used. No R25 performance result is accepted.
Installed/public **0.1.12 remains unchanged**. See
[the R25 record](history/2026-10-24-performance436-r25.md).

## R24 passed — native-resolution single-sample rendering measured

The original native AA options setter selected the advertised single-sample
mode once on the verified UI thread. Its dirty state settled through normal
engine application before Continue. All retained depth-bearing rows then proved
**2784x3024/1x**, unlike R23. The helper made no Save call; both text settings
files remained byte-identical after normal close. 55 focused checks passed.
The original R22 normal binaries and accepted 8x baseline were reused.

The retained-baseline comparison measured **22.4273 -> 29.8368 fresh stereo
pairs/s (+33.0379%)** over enclosed nine-second intervals after the same
two-second settling exclusion. All interior stalls remain included. This is
an intentional AA quality/workload tradeoff, not a same-quality optimization
or proof of native 90 FPS. Separate process/save/world state prevents claiming
an exact isolated causal gain. Camera differences were small but no equality
tolerance was predeclared. Candidate menu errors, host timeouts and rejected
pairs were all zero; guard release and normal close passed.

Completion SHA-256:
`f46fa417a2bbea8f93f823427ea8e54951e7784d0a053b3e6b6b0eabd2508bbc`.
Analysis: `performance436/r24-prep/analysis.json`, SHA-256
`f460a919b45721da0e85578e88809479d8a19ff2790921be0cf79341ffc03e09`.
Helper build: `r24-msaa/build01/build-receipt.json`, SHA-256
`e76fb31aab0886c8ee6f3e66b4615a0e260f4025578558b5d1871f41af400d0a`.

83 exact ticket joins show copy completion was only observed on the following
host frame, median 10.972 ms after submission. 80/82 consecutive submissions
were three host frames apart, two were four. Their native successors missed
the intervening delivery poll. This is refresh-quantized handoff latency, not
a proven hard 30 FPS engine limit or measured GPU copy execution time.
R25 now implements the private, default-off, at-most-2-ms host copy-completion
service. Its builds passed; the first setup attempt was blocked before any
performance measurement, as recorded above. Native R22 bounded servicing
remains disabled.
Installed/public 0.1.12 is unchanged; headset visual acceptance is still needed.

## R23 rejected — pre-world AA scalar changes did not reach actual targets

The private one-shot helper changed both Render and WorldRender sample counts
from 8 to 1 on the verified UI owner. Immediate readback, hook restoration and
callback cleanup passed; the original options index remained 3 (advertised 8x).
Its 27 focused checks and private x86 compilation passed. Production binaries
were reused from R22, with bounded Present progress disabled.

One actual-game emulated run (`game45/onfoot21/observation-r23`) then found
**all 198 consumed pairs inside the CPU window still used 2784x3024/8x depth**.
All 210 retained depth-bearing rows were also 8x. The descriptor gate correctly
rejected the experiment. No lower-AA performance gain can be inferred; the
later native operation or cached settings responsible are not yet established.
The request's `world_msaa_samples=1` is intent, not an observed descriptor.
Native cleanup, resolution guard release and normal game close passed; both
text settings files remained unchanged. Completion SHA-256:
`f50b0b2c4e66ccfece74315bd2c6bbaab2dddccc1866a2bd86df05b68c9fba7e`.

The subsequent R24 used the already identified original AA options setter, as the resolution
helper already does for dimensions, so the engine's later application receives
the intended value. This is an in-memory process-lifetime experiment; no Save
call or settings-file edit is allowed. Actual depth descriptors and final text
settings hashes remained mandatory. R24's actual result is recorded above.

## R22 measured, not promoted — bounded servicing of one restored pair

The R21 follow-up is now implemented, privately opt-in through
`FVR_PRIVATE_PRESENT_PROGRESS_MS` (0/off by default; accepted range 1–16 ms).
Frozen native source digest:
`2a1b89db2b14252facf59bfe7529d6a48291a9a57eac13881faf2e2015abffd5`.
After exactly successful, outer graphics-owner Present in gameplay, the bridge
may service only its existing restored mode-3 pair. A local lease token ends
on exact consumption/discard, cancellation, failure or close. A queued successor
cannot extend the wait: admission still requires the next native Update.
The loop retains callback/image lifetimes, sleeps outside IPC locks, rechecks
ownership/restoration/menu state, and has QPC and iteration limits. Sleep(1)
can overshoot; this is not a hard real-time delay guarantee. Normal rendering,
simulation, equipment mechanics and public/installed 0.1.12 remain unchanged.

Focused deterministic checks passed, followed by **303/303 suites on each of
x86 and x64**. Frozen runtime receipt SHA-256:
`c958303f66da8517bbaf8b44eff801dd32431c3d763fa80853856e8896d72a54`.
The comparison used the same frozen build with 0 and 8 ms budgets, fresh owned
game sessions at 2784x3024/8x MSAA, fixed emulated input and 12-second CPU
collection. Both games detached, restored the queue, released the resolution guard, and closed
normally. Baseline passed; candidate failed combined acceptance on one real
menu error, despite zero native GPU/camera-restoration failures. Candidate also
reported one host timeout. Do not weaken the gate or label the candidate passed.

The explicitly diagnostic analysis keeps that failure and all interior stalls.
Fresh consumption was 22.4273 vs 23.6372 pairs/s (+5.3947% descriptive), world
callbacks 95.0123 vs 83.0616/s, process CPU 1.8766 vs 1.7630 equivalent cores,
main thread 0.9328 vs 0.9172. Camera basis differed (max 0.052575), and save
bytes changed: no causal FPS speedup is established. 84 exact-ticket timing
joins per arm show mean host-copy-observed-complete to native reap 2.670 vs
1.081 ms; copy submission to completion observation stayed near 11 ms. These
are CPU observations, not exclusive GPU time. Candidate recorded 822 service
episodes, 122 ended leases and 700 budget expiries; maximum observed episode
was 9.9906 ms, illustrating scheduler overshoot. No clock/poll-limit failures.
Evidence: `r22-prep/diagnostic-analysis.json`, SHA-256
`d9219d9e443b1b3111f4b2cf0fd3c0feaafb29218fd04ab94b4cc5c83ece117c`.
The source/log bound places the menu error before phase 3 enabled pacing; its
exact failed API remains unknown, so overall acceptance remains failed.
`private_present_progress.completed` means the exact lease ended, including
discard; successful consumption must be checked in the existing bridge report.
The new concrete D3D bridge flag has source review but no dedicated fake-device
test; existing real IPC regressions cover its underlying terminal transitions.
R20 remains the rollback. Private artifacts: `performance436/r22`, `r22-prep`.

Next GPU experiment: 8x MSAA is not required by the VR renderer. Static evidence
now identifies the separate WorldRender count/quality fields actually consumed
by DX11 world-target creation; a Render-only change is insufficient. A private
one-shot pre-world override is being prepared, retaining native dimensions and
upstream saved options. No AA write or lower-AA measurement has occurred yet.
See `r22-msaa-research/static-aa-evidence.json` (SHA-256
`7bb9c88ac3e5ee31df26ac82be0d559c79209ecfe6bb68820382185ba630f21f`).

## R21 completed — ordinary rendering versus VR on the same GPU

The user requested a direct investigation of the large performance gap, with
CPU and GPU both considered. A current read-only DXGI enumeration matches
R20's adapter LUID `(108849,0)` to the RX 9070 XT hardware adapter. Integrated
graphics and the software adapter have different LUIDs. Successful producer/
consumer checks also exclude silent cross-adapter sharing in that run. This
is local same-boot evidence, not proof about another player's hardware.

One bounded private comparison completed at 04:09:57 UTC October 24:
`game42/onfoot18` R20 VR, clean detach, then `flat01` ordinary rendering in the
same process, actor and 2784x3024 target size. Both used interval 0/queue 1.
The private Present observer passed 116 focused policy checks; full R20 builds
and existing emulation binaries were reused, without repeated tests. Native
cleanup, observer drain/queue restoration, resolution guard release and normal
game close passed. R20 production source and the public/installed build were
not changed.

After the same two-second settling exclusion, ordinary rendering accepted
**156.186 non-TEST application Presents/s** across 9.0213 s. VR consumed
**17.982 fresh stereo pairs/s** across 9.0648 s: seven one-second bins were
22-23/s, followed by a real late stall (6/s and 1/s bins). Do not discard the
stall or describe this as an 8.7x removable overhead or optimization gain.
Successful Presents are not proof of unique world frames/physical scanout;
exact camera/FOV/whole-world work was not proven equal between the two modes.
Main-thread use was 0.849 core in VR versus 0.992 flat; whole-process use was
1.698 versus 1.692 cores, over their enclosed CPU samples. A busy main thread
alone therefore does not establish the performance bottleneck.

External GPU Engine counters covered about eight seconds of each interior.
Weighted 3D-engine occupancy was **93.60% VR** (median 97.69%) and **98.10%
flat**. VR's final sampled row dropped to 39.42% as the late stall began;
sampling ended before the complete stall. Other-process Copy/Video activity
rose, but process names and causality are unproven. Request 399's observed
publish-to-reap delay was 1112 ms; a later request's producer-fence observation
took 802 ms. Four aggregate host timeouts occurred. No GPU failure or camera
restoration failure was reported. These are elapsed observations, not exclusive
GPU execution times, and do not establish a hardware or driver fault.

Evidence: `comparison-r21/frame-cpu-analysis.json`, SHA-256
`87fef022c7b82598aff659a52029254cd465590ec92c5cf5c0ac14a0e2cedeb6`;
`comparison-r21/gpu-occupancy-analysis.json`, SHA-256
`34a61c606319b47cb828edde676165ef74717af00eac25df8e4226fd9631d7f8`.
Do not repeat this unchanged run merely to improve the number.

The next concrete scheduling candidate is a bounded, non-busy service interval
after successful Present, allowing an already-restored pair's export/host ACK
to finish before more ordinary draws are queued. Return immediately when that
exact pair is reaped; successor admission needs the next native Update and
must never be waited for inside Present. This is source investigation only:
no such pacing change has been implemented or measured. Simply adding another
host poll has little expected value: pre-WaitFrame and post-input polls already
exist, and submission-to-frame-end was only 0.00545 ms median in retained rows.
Do not skip native Draw/cleanup/history or replay simulation.

MSAA remains at 8 samples for this comparison. The only established lower-MSAA
route edits the existing Documents settings file on C:, which is excluded by
the user's current no-C-writes instruction. No new settings setter or profile
redirection is verified. Keep native dimensions and current quality unchanged
in the completed flat/VR comparison. Flat Present-bound depth was sample1;
that is not world-MSAA evidence. 174 consumed VR timing rows in the selected
interior independently report eight samples. New artifacts belong under
`performance436/r21-flat-observer` and `r21-flat-prep`; keep storage bounded.

## R20 CPU candidate — full builds and one game observation passed

Further decoding of the retained CPU trace identified repeated work in the
adapter. Two narrow source changes are now frozen as
`0be6f3eb6fdabf653c31e830b87ad2adff8cdc15284d8a231d62093c7d6011f4`:
compiled capability receipts validate their immutable strings/flags at compile
time; visibility checks skip deep metadata self-comparison when both references
point at the same snapshot. Full live binding/rig equality, duplicate receipt
rejection, ownership, sequence, expiry and coverage checks remain. Independent
source reviews passed. Full normal builds passed **302/302 suites on each of
x86 and x64**, plus the focused visibility, capability and resource-enrollment
checks, including 17 resource/body/visibility joins and 289 malformed bindings.
The separate frozen runtime receipt is
`performance436/r20/normal-runtime/build-receipt.json`, SHA-256
`1bb439fc8040721389ecfe14907eb2afde885c5718bb4cf19633331666829358`.
The bounded x86 microbenchmark passed all 40 result checks across five
alternating batches per case. Median same-snapshot checking fell from 1805.54
to 30.64 ns/call and native receipt admission from 92.461 to 9.132 ns/call;
distinct-snapshot paths remain essentially unchanged within observed variation.
These are hot-cache function costs, not FPS predictions.

One actual-game observation (`game41/onfoot17/observation-r20`) completed at
03:15:55 UTC October 24 (October 23 local). With the same **2784x3024 per eye,
8x MSAA**, queue 1 and post-Present servicing, measured fresh-pair consumption
was **22.435/s**, versus retained R19 C's **21.867/s**. Process CPU averaged
1.766 versus 1.923 equivalent cores; the main thread remained about 0.95 core.
The separate-run rate difference is **2.60%**, smaller than R19's 7.51% baseline
drift. Camera position/basis differed slightly and whole-world workload was
not proven equal: this does **not** establish a causal end-to-end speedup.
411 pairs completed with zero host rejects, timeouts or feedback errors and
no reported GPU/restoration errors. The measured interior had no discarded
pairs; two native discards occurred elsewhere in the session. Native detach,
queue restoration, resolution-guard release and normal game close all passed.
No new ETW recording, extra capture or repeated game run was needed.

The private R20 comparison is `performance436/observation-r20/comparison.json`,
SHA-256 `46e5a0662ed2c9baeef5e7c1895bd7db54cbe0fd5a67d582a7beae9a6e75f545`.
This is neutral-input performance/cleanup evidence, not weapon-mechanics,
jeep/death recovery, streaming or headset acceptance. Public/installed 0.1.12
and the frozen R19 headset route are unchanged.

### Next focused headset check — private R20

`performance436/headset-r20-prep/Launch.ps1` is staged and passed one offline
`-ValidateOnly`: correct frozen R20 source, assets ready and SteamVR selected;
no game, OpenXR session or headset check was started. The private stage is
19,707,904 bytes and reuses the installed Python/asset cache read-only. The
ordinary desktop launcher remains unchanged. Root should launch this route
when the user is ready, with the headset connected and any previous BC2 closed
normally. It starts at the VR main menu at the headset's recommended resolution,
with queue 1 limited to that owned game process. `Stop.ps1` stops only the
owned mod, checks cleanup and leaves BC2 and SteamVR open.

Check menu pointer/Continue, head turns and movement, then shoot, manual
reload/pump and holster with known working equipment. This checks physical
presentation and regressions, not full campaign or jeep/death acceptance.
Do not repeat the unchanged builds, microbenchmark, game observation or offline
launcher validation. No claim of a solved main-thread bottleneck is warranted.

Private route plan SHA-256:
`a836debe0c1ad667d89ea3c55abe25d41b3b62dcf49c67e3c531d084c4ec51dc`;
offline validation receipt:
`10626833cdd33db3f63c83748b5fe078dfd1f89a1fb4f22e4ce81070ed22eeea`.
Retain `headset-r19-prep` as the previous rollback route. Both routes bound the
eight mandatory startup captures to 512 MiB at the actual headset dimensions;
neither silently lowers native resolution to satisfy storage.

The offline exporter passed 23 new reporting-boundary checks and reused the
unchanged decoder's prior proofs. Only the original ETL was decoded. Local PDB
resolution attributes 152 top-histogram graphics samples to repeated native
operation admission/digest checking and 49 to WeaponVisibilityCurrent, under
the source-supported retained-loader inference. The module base was observed
only at rundown, so these are not independently lifetime-proven ETW module
samples or promised removable CPU percentages. No second histogram pass was
needed. Saved captures were losslessly compressed to reclaim 500,883,456 bytes;
all content hashes agree and no files were deleted.

Three findings narrow the next work:

- Ordinary Draw cannot simply become a no-op while CPU cleanup proceeds.
  The native postprocess copies persistent exposure/luminance history, can
  read a selected history slot, and advances its eight-slot selector. Skipping
  GPU commands could leave that selector pointing at unwritten history.
- The native quad helper reads back 10 or 12 DWORDs from its write-mapped
  vertex buffer. Executing its original x86 instructions in an offline emulator
  with invocation-local scratch instead produced identical 120/144-byte output
  in 972 comparisons. This is a promising bounded optimization, not an installed
  hook, real-driver validation or measured speedup. Intercepted stack arguments,
  callee-saved registers, stack cleanup and FPU control/status agree; the test
  does not establish full x87 environment or compiled-copy behavior.
- R19's six host timeouts are not proof of six 150 ms stalls. At least three
  did not acquire a native frame lease; the aggregate also classifies some
  terminal/cancelled requests as timeouts. No ownership defect is proved.
  Request IDs/reasons were not retained, so exact attribution needs bounded
  failure-only diagnostics in a future otherwise-needed run.

Private evidence is in `performance436/r20-investigation/`: original-byte
quad emulator and result, `ordinary-render.json`, and `timeout/` audit.
Do not rerun the unchanged R19 builds, crossover or launcher validation to
answer these new questions. The planned R20 real-game followup uses only one
short candidate measurement; it must retain the separate-run comparison limit.

## October 23 R19 same-game comparison — measured candidate, not installed

R19 retains R18's post-Present service and adds an opt-in, existing-event-only
private comparison: baseline queue 3, after-service at queue 3, after-service
at queue 1, then baseline queue 3 again. Changes occur at the verified graphics
owner's restored Present boundary. Queue 1 is restored before the reversal and
before shutdown; no new native frame admission, input, simulation replay or
render-size changes are introduced. Camera evidence comes from the native
camera copy already read by ApplyTrackedView. The completed stationary
session includes CPU/counter measurements and a reversal to expose drift.
The frozen source `9b63b2f547897b0eb54c885772c3125719cb073164009603441cd6ff0c318b8c`
passed full x86 and x64 builds, **302/302 suites on each**, and focused
presentation/staged-producer/GPU checks (native ABI checks on x86). The first
x64 attempt stopped before compilation because the private script requested
the x86-only NativeHookAbiTests; the successful x64-only retry filtered that
focused target, preserved the source freeze and retained both attempts' logs.
The new phase-analysis tests passed 9/9.

The actual-game `game40/onfoot16/crossover-r19` session completed at 02:06:36 UTC
October 24 (October 23 local). At unchanged **2784x3024 per eye**, with the same
game process and emulated 90 Hz tracking, fresh stereo-pair consumption was:

| Phase | Native queue limit | Post-Present service | Fresh pairs/s |
| --- | ---: | --- | ---: |
| A1 baseline | 3 | Off | 18.8943 |
| B service only | 3 | On | 17.7597 |
| C combined candidate | 1 | On | **21.8665** |
| A2 baseline restored | 3 | Off | 17.4758 |

C was **15.73–25.12% above the two baselines**. This is a useful candidate
signal, not a proven general speedup or headset FPS: fixed phase order,
7.51% baseline drift and unmeasured whole-world workload limit the comparison.
Camera observations retained the same owner/space with millimetre-scale idle
position changes, no invalid/dropped observations and no restart/viewpoint
change. Main-thread CPU remained 92.29–95.53% of one core across phases.
Post-Present service alone showed no clear improvement. Resolution and quality
were not reduced; native depth reports 8x MSAA.

965 fresh pairs completed, with zero host-rejected pairs and no GPU, copy,
camera-restoration or feedback errors. All three native discards predated A1;
six host request timeouts are aggregate-only and cannot be assigned to phases.
Queue 3 was restored, native cleanup and resolution-guard release passed, and
the owned game closed normally. This neutral-input benchmark is not gameplay,
jeep/death-recovery, streaming or physical-headset acceptance.

Frozen runtime receipt: `performance436/r19/normal-runtime/build-receipt.json`,
SHA `ca4885f755bc3fac830712d0bfd7f78f88428f6c8c72bc1f825ed98c96a2f352`.
See [measurement and limits](PERFORMANCE-436.md#r19-same-process-presentation-comparison).
Queue 1 remains a private process-scoped opt-in; installed/public **0.1.12 is
unchanged**. A separate headset test route is staged, independently reviewed
and offline-validated. The old installed local-checkpoint route still selects
425 and must not be mistaken for R19.

### Previous headset candidate / rollback — private R19

For the preserved rollback, use `performance436/headset-r19-prep/Launch.ps1`
in PowerShell 7.5+ after the
headset is connected and any existing BC2 is closed normally. This route starts
a fresh game at the VR main menu, uses the headset's recommended resolution,
and applies queue 1 only to that new game process. It reuses the verified
installed assets/Python read-only; no asset preparation or download is needed.
Its separate `Stop.ps1` ends only the owned mod session, checks native/queue/
resolution cleanup, and leaves BC2 and SteamVR open. Root is responsible for
running these commands when the user is ready; the ordinary desktop shortcut
has not been changed.

Offline validation already passed once: correct R19 source, assets ready,
SteamVR selected, no game or headset started. Do not repeat the full builds,
same-game benchmark or offline launch validation without a new failure or
changed dependency. Physical acceptance is still pending. The short next check
is menu pointer/Continue, movement and head-turn smoothness, then shoot,
reload/pump and holster to spot regressions. Six earlier aggregate request
timeouts remain unresolved; retain session diagnostics if image loss recurs.
This check does not establish a full campaign or jeep/death-restart pass.

Private route plan SHA:
`fa220002f198e179171aec406aeb73ada5ab573e633793dcc07350dabac9a4be`;
validation SHA:
`f8d676a6accc63b7c6f22a5f8774dbaea75b02eb123b7cf206dae453ddd1dceb`.
Staged binaries/loader total 19,707,904 bytes. Frozen native R19 requires eight
startup images; the route bounds their actual headset-size bytes before game
launch rather than adding captures or silently lowering resolution.

Next rendering decisions: retain the measured candidate for a headset check;
do not bypass ordinary native draws or tick pacing without dependency proof.
An unapplied direct-color-capture proposal removes duplicate copies but targets
only the previously measured ~0.288 ms export interval, not the dominant native
draw cost. No gain is established for that proposal. Native timer analysis found
cooperative tick pacing, not a verified removable frame cap. CPU, jeep/restart
and destruction-rendering acceptance remain open.

## R18 post-Present completion service

The frozen R18 candidate adds one zero-wait graphics service opportunity after
an exact S_OK desktop Present. It revalidates the graphics owner, device,
context, swapchain and restored native state after the original call. The
existing pre-Present service, nesting/lifetime guards, native return value and
LastError are preserved. Neither service admits a camera frame or replays
simulation. The legacy service counter remains before-only; an independent
counter records after-service calls. Its x86 full build passed 301/301 suites.
The remaining R18 build/run was superseded by R19's same-game comparison,
which requires fresh full x86 and x64 builds of its own final source.
Source closure: `f653104a8a725c6b168f4576ac6a2e69f37e96e4f3cf2461ca38e38b8fd36a2b`.

R17's exploratory queue-one run completed cleanly at 21.7842 fresh pairs/s
versus 19.0047 in the baseline. This is **not a causal speedup measurement**:
the retained pre-exit camera orientations differ about 30 degrees and the
ordinary GPU draw costs differ too. The sole cancelled host pair occurred
after the measurement and stop request; no request timeout or GPU error was
recorded. A same-game crossover has now completed above, removing restart/viewpoint
confounds. Do not repeat the already prepared unmatched R18 game run merely
to collect another incomparable rate. Installed/public 0.1.12 is unchanged.

## October 23 R17 queue-depth comparison

The private candidate adds an opt-in DXGI device queue-limit experiment, with
the same build used for observation-only (`FVR_PRIVATE_RENDER_QUEUE436=0`) and
limit-one (`=1`) arms. It does not skip native Update/Prepare/Draw, change
resolution or alter gameplay. It records the original limit and restores only
its exact owned value. With the environment unset, the experiment is inactive.
Full x86 and x64 builds passed 301/301 suites, including focused policy/WARP
checks and all three depth/mirror modes. The frozen R17 runtime receipt is
`4670497c034cac13320a1056c6f52eedd06ad8ac57db2938a130f6ab29706306`.
The observation-only actual-game baseline completed cleanly at 19.0047 fresh
stereo pairs/s over its selected 9.0504-second counter interval, at 2784x3024
per eye; zero request timeouts were recorded. This is emulated tracking, not
headset FPS. The ordinary native queue limit was 3.
The initial matching candidate was refused before launch because BC2 rewrote
GameSettings.bin; text graphics/gameplay settings remained byte-identical.
The subsequent explicitly unmatched exploratory run hit a test-harness startup
deadline race before adapter injection and closed cleanly. A private child now
starts its 10-second host-readiness deadline after process launch returns,
retaining the overall 90-second bound; the corrected queue-on3/onfoot15 run
then completed. See the exploratory result and comparison limitation above.
No checkpoint equivalence or percentage gain is claimed.
Source closure: `2b6a15003b667e4c164b9ec4ef698ad6d71e5945eaf16e1b4a8ac302deebb671`.
No speedup, rollout or headset readiness is established yet.

The native scheduling audit also found an active-view gate before visibility
enqueue, but native rebuild can add pending child views before that gate.
Previous-frame queue emptiness alone cannot authorize suppression. The audit
is retained privately in `performance436/r17-prep/native-gate-audit.json`;
no main-view suppression is implemented.

## Candidate 436 — October 22 local / October 23 UTC, R16 GPU capture complete

R16 passed **300/300 suites on x86 and x64**, focused hardware GPU timing
checks, and depth/mirror integration in all three sharing modes. Its bounded
actual-game run (`game34/onfoot11/gpu-run01`) consumed 341 fresh stereo pairs
at 2784x3024 per eye using emulated tracking. Native cleanup, guard release and
normal owned-game close passed. Two request timeouts remain recorded; host
rejected pairs were zero. This was a diagnostic run, not gameplay or headset
acceptance. Installed/public **0.1.12 remains unchanged**.

The accepted GPU evidence contains eight exact-owner stereo pairs and 16
ordinary draws. Ordinary draw intervals averaged **9.941 ms**; the two tracked
draws together averaged **19.177 ms**. Ordinary rendering accounts for 96.55%
and 97.03% of the first two fully covered gaps between stereo pairs. These are
inclusive same-context GPU intervals, not exclusive GPU busy time or a measured
FPS gain. Later gaps exceed the ordinary-draw sampling cap. No safe suppression
has yet been established: native Draw owns pending-view cleanup and the mod's
Draw hook also services cancellation/connection/shutdown. Do not early-return
from Draw on this evidence. See [the R16 analysis](PERFORMANCE-436.md#r16-actual-game-gpu-attribution).

Frozen R16 receipt: `performance436/r16/normal-runtime/build-receipt.json`,
SHA-256 `a4c450542c2a2a64866fc5d821df23630f466d0a240d1a8e9e0f15181512b2b1`;
source closure `7a10e5d9333e9b759997878b1f3941ee5ea23d90847635fe859b79863b2edb79`.
The next optimization target is the ordinary rendering between VR pairs, after
proving its scheduling and ownership dependencies. Jeep, death/restart and
destruction-rendering acceptance remain unresolved.

### Retained CPU attribution and R15 implementation

The actual BC2 CPU capture is complete. `cpu17/game33/onfoot10` passed its
bounded on-foot/cleanup checks on the unchanged R14 runtime. Offline decoding
of that same trace now correlates 8,533 main-thread samples with the independently
observed executable. At least 2,320 retained sampled stacks (27.19%) follow the
native client-render job dependency path; a smaller chain waits on the animation
group, and at least 6.25% follow native timer-polling paths. This establishes
substantial native waiting/helping activity, not a removable CPU percentage.
The exact slow render child job remains unresolved; 5,256 instruction pointers
remain unmapped and stack coverage is incomplete. A second offline summary of
the same trace now retains 17,509 exact PID/TID/QPC-matched samples across 17
thread buckets, including the known graphics owner. No additional game capture
was needed. See [evidence and limits](PERFORMANCE-436.md).

The same captured window contains 164 complete pair lifetimes: mean export to
observed producer-fence readiness is 22.083 ms, and publication to acknowledgement
reaping is 20.659 ms. These combine GPU queueing/work and CPU observation cadence;
they are not measured GPU execution durations. No physical-headset FPS or
before/after improvement has been measured.

R15 moves the desktop-only mirror copy after VR export/fence submission in all
three graphics sharing modes. Same-context ordering, retained left-eye pixels,
failure behavior and resource ownership are preserved by design and independent
review. Focused GPU coverage passes in all three sharing modes on both
architectures; both full normal builds pass 299/299 suites. The preserved R15
receipt is `e8dc076cf91b5379eef4809938d29a42a5e3bcf69582b3e85c40d086da046735`
under `performance436/r15/normal-runtime`. No gameplay
mechanic was changed by this revision; installed/public **0.1.12 is unchanged**.

R16 measures a bounded set of native GPU draw/depth/color copy intervals. It is
opt-in, uses existing graphics ownership/service points, adds no production
flush or wait, and leaves gameplay and MSAA quality unchanged. The capture
above completed successfully. There is no verified in-memory MSAA
override; the old INI-edit route would write C: and is not being used.

Storage retention: superseded raw frames from idle08/onfoot02/05/06/07 were
reviewed as compact contact sheets and pruned. All logs, timings and small visual
records remain under `performance436/retention-20261022`; the exact prune manifest
records file hashes. Current onfoot10 CPU/raw evidence and unresolved failure
frames remain intact. Reviewed compiler/test intermediates from older recovery
work reclaimed another 1.446 GiB of allocated storage, allowing the builds within
the existing 40 GiB limit. Current runtime symbols, source and user recordings
were preserved; the exact compiler prune plan and result remain beside the
raw-frame manifest.

R16 build admission also reclaimed 691,592,620 allocated bytes (659.6 MiB)
through byte-preserving compression of selected symbol files and removal of
24 verified stale incremental-link caches. Hashes and modification times were
checked; runtime symbols, compiler objects and evidence were preserved. Plans
and results are retained under `performance436`. The 40 GiB generated-data cap
was not raised. Reclaimed storage is unrelated to rendering performance.

## Retained R14 evidence

The first actual-game CPU capture attempt, `onfoot-cpu16` / game32 / onfoot09,
failed before recording: the private helper reparsed a JSON UTC DateTime as
local time, adding four hours and rejecting valid storage admission. Actual
receipt age was 73.541 seconds; the erroneous age was -14326.420 seconds.
No on-foot action was issued. Native cleanup, guard release and normal owned
game close passed. There are no CPU trace samples from that attempt.

Private `r14-prep/onfoot-cpu17` corrects only that timestamp conversion, with
six offline admission checks passing. Its receipt is
`4a4cdc675c4118555c2d120b121d0117ce9215a84dfbd8ccf8e22933df482b59`.
Root ran the corrected capture as game33/onfoot10 at 23:20 UTC. Combined
acceptance, on-foot coverage, native cleanup, profiler cleanup, guard release
and normal game close passed. The run recorded 400 completed pairs, zero
request timeouts and zero rejected pairs. The main thread consumed 94.67% of
one core over its measured 12-second interval; no physical-headset performance
claim follows from this emulated run. All gameplay binaries are unchanged.

The raw CPU trace is 47,710,208 bytes. Raw-only decoding rejected attribution
because the game preceded recording. Correlation with the same-instance rundown
and retained-handle identity now passes for the pinned main executable only;
unknown DLL/kernel addresses remain unresolved. The rundown omits its timestamp,
so that field is explicitly sourced from the independently pinned PE; base, size,
path and nonzero checksum agree. The narrow policy passed 49 checks. No
performance gain is claimed.

R14 passed **299/299** normal tests on each architecture. The private CPU
recorder now follows verified thread lifetimes independently instead of
requiring rig work to run on the graphics thread. Its actual-game diagnostic
measured the main/UI thread at **90.08% of one core**, but the instrumented
rig-update scope accounted for only **1.65% of that thread's observed cycles**.
Most hot-thread work is outside the present scopes, including potentially
other mod hooks. This is useful attribution, **not a demonstrated speedup**.

Game26/onfoot08 failed parent acceptance because its controller process did
not finish within the bounded wait. The controller wrote a successful result,
and its CPU interval has fresh on-foot coverage; these permit diagnostic use
only. Seven request timeouts and two rejected pairs remain recorded. Native
cleanup, resolution-guard release and normal owned-game close passed. Do not
promote this run to accepted performance evidence or erase its failure.

Game27/pause01 passed one controller pause gesture and captured the actual
2784x3024 released menu quad. The reviewed image shows RESTART; **no menu click
or checkpoint restart was attempted**. Game28/death05 stopped because it could
not verify an independent positive health baseline. No grenade was sent;
death, both-eye fade and recovery remain unverified. Both sessions cleaned up
and closed the owned game normally. Offline investigation found that the health
reader stopped at its legacy pristine-code check of the damage function, which
the enabled knife feature hooks. It took zero health samples. This is an
instrumentation conflict, not evidence of zero health or a death/restart fix.
The correction is confined to the private read-only health diagnostic.

Game30/death06 verified that correction, then rejected its initial health
baseline on the existing positivity/equality/static-bound check. No grenade
was sent; cleanup and normal close passed. The rejected values were not saved,
so their exact failing clause is unknown. A narrow reporting correction now
preserves those values without weakening validation; its six pure checks pass.
Do not repeat the same game test without a useful evidence change.

The offline CPU decoder built and passed 41 pure checks. A user-approved
two-second elevated synthetic workload ran, but WPR stop/merge exceeded its
30-second bound. WPR's subsequent "not recording" status was insufficient:
actual kernel enumeration later found both owned collectors still running.
Root stopped only those exact collectors and verified their absence at
13:41:34 UTC (`synthetic02/kernel-cleanup.json`). No new recording was started.
The now-closed rundown file is 9.25 MiB; earlier sizes were incomplete snapshots.

Decoder04 passed 62 targeted schema checks, including the observed Image
version 3 format. One offline pass of the closed trace read 40,723 events and
decoded all 1,350 process, 11,160 thread and 28,207 module records without
unsupported/malformed records or reported loss. No workload CPU samples are
present in that older rundown. The subsequent synthetic capture below supplies
separate sample-attribution evidence.
The read-only sampler extension passed 38 pure checks; actual game integration
is still pending. These are diagnostic improvements, not performance results.

The source-only `r14-prep/onfoot-cpu15` harness now integrates that unchanged
sampler, a retained-handle observation of the actual mapped executable, and
the controller-wait timestamps. Root reviewed both script diffs, verified 101
dependency pins and syntax-parsed both scripts. The new PE-identity helpers
passed nine pure tests; the earlier 38 sampler checks were reused. No process
observation or game run has exercised this integration. It does not start ETW;
recording overlap still needs integration after the synthetic check succeeds.
Harness receipt: `74f4a07d6139d3e21e2032fbcabf24a1765f372c632621a6286c3d9e7d99016a`.

The user-approved replacement `cpu-etw/RunSyntheticFlushR5.ps1` passed as
`synthetic03` at 14:29 UTC: flush/preserve/cancel saved an 8 MiB raw trace,
and actual kernel enumeration confirmed both cleanup and no residual owned
collectors. No controller was forcibly terminated. Plan:
`435522179c1f1a858efd89b005871eff06101fe57e04f91e80c05c5bbaeafba3`;
decoder04 receipt:
`8a38b724e598b4919b611c07eff2e5b7d1da243fbb61ca46db791cadacd0f176`.

The exact workload PID/main TID yielded 1,974 accepted IP samples over
2.0000022 seconds. Of these, 1,959 (99.24%) map to its known BusyLoop, and
1,973 have matching user stacks. Reported event/buffer loss is zero. The 141
global stack-decode errors cannot be assigned to a target or window; complete
stack coverage and whole-window retention remain unproven. Independent review
confirmed the binary/map attribution. No BC2 game or gameplay was touched,
and no FPS improvement is implied. Final raw trace SHA-256:
`ea6159a024d4a2aed87de0df440f788750d4fed9d3ea28c1b0ef73e715b30be6`.
The capture folder retains about 26 MiB, within its 128 MiB admission. The
profiler check is complete; actual BC2 recording orchestration is still pending.

The first-Restart-click fixture built and passed its targeted policy checks,
but game29 was stopped before any helper or VR attachment: available commit
was 3.30 GiB (6 GiB required), and available physical memory was 1.60 GiB
(2 GiB required). No pause, click or restart occurred. The owned vanilla game
had no main window for normal close; after checking its exact identity and
absence of injected helpers, root terminated only that process. This was
forced cleanup, not a successful restart test. Do not repeat game tests under
the same memory conditions; continue the retained-trace work offline.

Next: bounded sampled CPU stacks for work outside the current hooks, rejected
health-baseline evidence on the next useful game run, and one reference-grounded Restart click to inspect
its resulting panel (confirmation is not established). The opening jeep's exact scripted checkpoint and the
one-eye destruction artifact remain unresolved. **Installed/public 0.1.12 is
unchanged; R14 is not in the launcher.** See
[the R14 checkpoint](history/2026-10-22-performance436-r14.md).

## Earlier candidate 436 evidence

R13 passed all **299/299** suites on both architectures and game23/onfoot07
without a debugger. At 2784x3024 per eye, 372 fresh pairs were presented;
native cleanup, guard release, unchanged settings.ini and normal game close
passed. The interior rate was **17.53 fresh pairs/s**, not headset FPS or a
performance improvement. Only two whole-session copies completed at the new
pre-wait poll. The private CPU profiler never started: it expected graphics
setup thread 47476, while verified rig updates arrived on another thread
(the external sampler measured thread 53540). Its empty stage results cannot
attribute CPU cost. Correct thread ownership before another attribution run.
The public launcher remains unchanged. See the performance and jeep notes.

October 22 R12 passed **299/299** normal suites on both architectures and the
actual-game on-foot check without a debugger (game21/onfoot06). It adds one
guarded frame-transfer service at the original Present boundary; no new frame
is admitted there. The service ran 8,418 times. The requested 2784x3024 per-eye
resolution survived Continue, 357 fresh pairs were presented, and native detach,
resolution-guard release, unchanged settings.ini and normal owned-game close
passed. Eleven interior intervals published/consumed 206 pairs with no discards:
**18.62 fresh pairs/s**, not physical-headset FPS. The fixture measured 89.94 Hz.
Main-thread utilization remains 93.6% of one core; producer-fence observation
(22.0 ms mean) and publish-through-acknowledgment (20.6 ms) remain substantial.
This is a validated candidate, not a claim that performance is solved. R10 used
a different fixture clock and R11 had a debugger, so no percentage speedup is
claimed against them. Installed/public **0.1.12 is unchanged**.

Existing R11 evidence also joins a same-process Continue menu entry labelled
`Levels/SP_01` to a generic campaign gunner seat: 16 camera rows over 8.125 seconds have both-eye
application, zero PBL aim-camera views, and successful ordinary X exit. This
exercises the corrected `NativeSeat` route, but the retained evidence does not
identify the vehicle blueprint or `jeepRideStart` checkpoint. Death/fade/restart
acceptance remains outstanding.

October 22 R11 instrumentation passed **298/298** normal tests on each
architecture. It adds bounded native ticket timing and uncensored host-stage
aggregates, without another scheduling change. The private fixture now uses a
validated rational 90 Hz clock instead of `Sleep(11)`. Actual-game acceptance
is still pending: game17 stopped before XR on an immediate window observation;
game18 passed the corrected read-only settle check but exited with an access
violation after Continue, before its first world pair. Its crash report has a
null execute site and the same return-address pattern as earlier startup
failures; that is not yet module attribution or proof of cause. No R11
performance improvement is claimed. R10 below remains the accepted on-foot
baseline. Installed/public 0.1.12 remains unchanged.

Game20/onfoot05 subsequently completed with the same R11 binaries under a
bounded exception watcher: zero captured exceptions, 319 presented pairs,
successful on-foot coverage, native detach, guard release and normal close.
The watcher detached normally. This did not reproduce or explain game18's
startup crash; the debugger run is explicitly excluded from performance
acceptance. Its retained timing journal is available for diagnostic analysis.

The current unpublished candidate addresses the reproduced half-refresh request
schedule, batches fresh rig reads, restores held left-stick sprint with Y
click/release toggle crouch, and separates campaign turret cameras from the
boat-specific head-aim proof. Both-stick recenter and X+Y menu remain available.
The emulated approach fixture now uses the same Y crouch gesture. Equipment,
knife damage and manual-ammunition behavior are not being redesigned here.

R10 passed the bounded actual-game on-foot check (game16/onfoot02) at
2784x3024 per eye with emulated tracking. All **300 submitted GPU copies
completed and all 300 pairs were presented**, with zero host pair rejections,
copy retirements or timeouts. Eleven measured interior intervals published 177
pairs and consumed 176, with no discards; median consumption was 15.93 pairs/s.
These are emulated-runtime pair measurements, **not physical-headset FPS**.
The exact gameplay/UI thread still used 95.66% of one CPU core; the process
averaged 1.94 cores. Function attribution and headset performance remain open.
The retained frame sequence shows four host ticks per new pair and completion
on the first poll after submission. Checkpoint R10 here; the next investigation
is request/fence cadence and the emulated fixture's clock, not an assumed depth
copy bottleneck or another speculative optimization.

The host now retains the exact ticket, source resources, poses and body/depth
metadata until asynchronous copy completion. It polls without the old 20 ms
spin/failure rule and acknowledges completion separately from eligibility for
presentation. Cancellation remains pending through mutex contention and is
applied before old feedback can promote a queued request. Maximum observed
pending-copy age was 42.934 ms, with no copy errors. The earlier game15 failure
remains recorded as the reproducer; its receipts were not revised.

One verified X exit reached the same infantry actor. The new elapsed-span
witness recorded 139 unchanged observations over 14.930 s, enclosing the full 12 s
CPU interval with maximum gap 110.373 ms. The host ring retained 84 distinct fresh
completed world pairs inside that interval. Native cleanup, exact resolution
guard release, unchanged settings.ini and normal owned-game close all passed.
Both R10 normal builds passed **297/297** suites. This is bounded integration
acceptance, not full campaign, weapon-interaction or headset acceptance.
Installed and published 0.1.12 remain unchanged. See
[measurement and limits](PERFORMANCE-436.md).

Native resolution preparation now follows verified native display setters and
the original Options Apply callback before attaching VR. A real game confirmed
1920x1080 -> 1280x1440 for both renderer and swapchain, with the temporary hook
restored and independent readback passed. The larger 2784x3024 allocation also
passed in two fresh processes. The first combined menu/world run failed: native
targets reverted to 1920x1080, the private launcher's saved
hidden-window style was restored, and exact camera restoration rejected a
separate derived-camera change. R4 corrected upstream options synchronization,
visible private startup, and the verified lazy camera scale-cache calculation.
Its next combined check caught another 1920x1080 reset **before Continue**.
A subsequent run delivered 181 world pairs (362 eye captures) at 2784x3024
with zero exact camera restoration failures, then native dimensions changed
to 1024x768 and source validation stopped VR. Hooks detached cleanly and both
owned games closed normally. A native-only menu minimize/restore check retained
2784x3024 in all 120 observations. Game08 subsequently reset to 1024x768 before
VR adapter injection or any minimize action, so minimizing is not necessary
for this failure. Game09's bounded native observer identified the actual
resolution-menu handler: setter return `0x960fe5` requested 1024x768 and Save
return `0x960ffe` wrote it successfully, with the same options owner and a
non-minimized window. Game10 captured its numeric mode index0, matching the
native list's first eligible 1024x768 mode. The game was foreground, and Windows
last-input age predated process launch. The immediate MainMenu sender is only a
generic forwarding branch; the default-selection cause is not yet established.
Observer hooks disabled and drained; both games closed normally. Explicit
VR-session ownership of resolution is implemented, with independent
setup/retained-guard cleanup and supervisor-death handling. R7 and R8 each passed
297/297 suites on x86 and x64. Game11 stopped before configure because Windows
had updated the loaded DLL's ImageBase header; all executable bytes matched.
R8 normalizes that exact field and preserves the rest of the identity check.
Game12 then reached native setup but rejected the UI manager's primary vtable.
Static inspection identifies primary 0x1440830; the earlier 0x14407f0 belongs to
a secondary interface. R9 corrected this ownership check and added staged
failure codes. Both normal builds passed **297/297** suites. Game13 then passed
the combined actual-game check: native renderer/swapchain and eye targets stayed
2784x3024 across Continue and minimize/restore, with 304 fresh stereo pairs,
unchanged settings.ini, exact native cleanup, guard release and normal game close.
The guard blocked two resolution-change events with zero owner mismatches.
Game14 separately proved automatic guard disarm after its supervisor exited,
idempotent release and normal close, with no VR adapter or input. These are
emulated-runtime integration checks, not headset FPS or campaign acceptance.
R10 subsequently established on-foot presentation and CPU coverage above;
timing attribution and physical-headset performance remain outstanding. See
[resolution evidence and limits](NATIVE-RESOLUTION-436.md).

R10's native operation source closure is
`3a482e89c7035bfbb431e1b9ff5575673d997247defe3442603519ce4714730b`.
The full source manifest and paired normal build are recorded under
`performance436/r10`; its runtime receipt is
`bbf1ae3c5c0cfd0ac513b229b291e6bf7941a05909a21eec9e6161470340b7c1`.
R9's prior normal runtime remains preserved as a baseline, with receipt
`f22c3c5bbb14e237c15263539d5bf9af8d7e60845b83bf1c081e850f10895c3d`.
Installed/public 0.1.12 is unchanged.
It keeps cancelled, unwritten camera requests blocked through restoration and
allows BC2 to discard a size-mismatched unsubmitted pair without retiring the
channel. It does not resize, scale images, clear unrelated failures or accept
the wrong source dimensions. Focused x86 owner-recovery and staged-producer
tests passed on r5; the GPU suite then failed an existing depth-view check before
the new cases. A bounded private x86 diagnostic reproduced an unexpected
mutation of the caller's frame ticket during a by-value metadata comparison.
R6 replaces it with const-reference field comparison and verifies input
immutability in the GPU regression. Both r6 normal builds passed **296/296**
suites. The real GPU depth/bridge tests passed on x86 and x64, including input
immutability, 1/2/4/8-sample depth, exact ownership, discarded-frame and transient
size recovery. R5 did not complete its full builds. No claim about this separate
bug causing the native resolution change is established. The r6 combined check
stopped before Continue because resolution had already reset; it did not execute
its minimize/restore stage. Installed/public 0.1.12 is unchanged.

The prior r4 source is
`5b4bd00ac544581aee190e0dc8701c0e7e88644f650ea031f2d9fc29912b1b22`.
`bc2vr-recovery/performance436/r4/source-freeze.json` records its complete
source/test manifest. The r4 x86 and x64 builds each passed **296/296** suites;
x86 also passed real-image signature checks. The revised real-GPU depth-sharing
test passed on x64. These results do not establish end-to-end acceptance:
idle03 remained in the menu and idle04 failed after the world pairs above.
The preceding r3 compile caught a
missing test-only iostream include, corrected before the new freeze.

Both earlier r2 builds passed **296/296** suites, and real-GPU scene-depth/menu
tests passed. These did not catch the subsequent actual-game integration
failures above and do not validate the corrective r4 source. The initial x86 run passed
294/296; two old binding assumptions, including the emulated crouch generator,
were corrected before the successful full builds.
An earlier four-job build hit system commit exhaustion; subsequent builds use
one job with the diagnostic game closed. No installed/public files changed.

The jeep camera defect is established in code and covered by ownership-policy
regressions; actual opening-mission seat/headset acceptance remains pending.
The separate X-entry report and left-eye-only destroyed-building artifact are
unresolved. See [jeep/rendering investigation](JEEP-TURRET-436.md). A new reported
Steam installation was rejected before VR attachment by the executable allowlist.
The tester got running by switching to Origin/EA; the original Steam executable
is still unverified, not fixed by that workaround. This does not establish
Steam-wide incompatibility. See [executable admission](EXECUTABLE-ADMISSION-436.md).

The research figures below describe the baseline. Candidate synthetic scheduling
delivers 119 pairs over 120 ticks versus 60 before, and fresh rig batching cuts
the measured 147-bone read shape from 2,484 calls to about 191. Neither is a
measured headset FPS or CPU-time improvement. Physical-headset performance and
the external tester's configuration remain unmeasured.

## Performance investigation — October 21

The user relayed successful headset progression through the opening knife guards
to the scientist, working crouch, loading text and manual reload, with no VR
dropout in that run. Poor performance on an RTX 5080 and cutscene-facing errors
remain reported. On October 22 the user supplied the tester's Ryzen 9 9900X,
Quest 3 at 90 Hz, and Virtual Desktop VDXR configuration. Their in-game graphics
settings, Virtual Desktop quality/render dimensions and timing diagnostics remain
unknown. This is human feedback, not a measured local 0.1.12 benchmark; the
reported specifications alone do not establish a CPU or GPU bottleneck.

Research 435 reproduced a scheduling limit using the actual RemoteFrameProvider
and FrameChannel: an instantaneous synthetic producer still delivers only 60
fresh pairs over 120 logical XR ticks. The provider returns a delivered pair
without starting the next request until another headset tick. This limits fresh
world images to half refresh before render costs. The shipped launcher also
overrides runtime-recommended eye dimensions with desktop game dimensions;
native-resolution support needs coordinated native render-target configuration.
The rig reader performs 2,484 small reads for the measured 147-bone snapshot,
making fresh-read batching a strong CPU candidate, with cost still unmeasured.

The old local 0.1.10 telemetry is consistent with the scheduling concern: median
30.51 fresh pairs/s in positive near-one-second intervals, and 64.3% reused world
presentations. These are not the external tester's numbers. No CPU-bound claim
or FPS improvement is established. The actual-code reproduction passed once;
the reusable bounded log analyzer ran once. No game/headset session was started,
no runtime source or installed package changed, and no new release was published.
See [the evidence, limits and implementation order](PERFORMANCE-435.md).
Private evidence is in `bc2vr-recovery/performance435` (about 445 KiB, no captures).

## Released hotfix — October 20, 22:42 UTC

[BC2VR 0.1.12](https://github.com/justiceofrip/BC2VR/releases/tag/v0.1.12)
is published in the separate installer repository (release 418953565, public
documentation commit `c99f02485ae3b4a3545701d5501d149b7cdea84d`). All three
GitHub asset sizes and SHA-256 digests match the tested local package. Local
`BC2VR-Launcher` is current 0.1.12 / previous 0.1.11; settings are byte-identical.

Combined normal 434 source is
`2e693ca5bbe9e8e333e582675fc43dd74a001b241b896db42b8f3c672274c738`.
Both complete builds passed 294/294 suites. The normal runtime receipt hash is
`d7214c076cbca9544cb9ad7ddba472a96a03f9aa5b92fa7d0f969c88aee34c3b`.
It includes the 431/433 selected-knife recognition/delivery corrections and
the 434 fix replacing a world-origin distance rejection with current
head-to-hand reach validation. Translated LOCAL origins and physical walking
no longer invalidate otherwise valid blade motion. Currentness, finite poses,
reach, custody, native ownership and the original bounded delivery remain
checked. It also includes passive opening narrative surfaces, explicit QPC
menu timing for crouch input, left-stick click/release toggle crouch, Y sprint,
and the released 0.1.11 managed-cache correction.

Actual-game acceptance now passed for the opening encounter with emulated
controllers: after a single Entire Mission start, native navigation reached
the two live guards. `knife430/approach43404` delivered one measured blade jab
through original native acquisition/attack/damage (serial 1, input 485, target
1041955984, health 12.5 -> 0). Two gestures completed, with no pending attack.
The same uninterrupted encounter subsequently displayed **Reach Trenches** in
`knife430/objectives434/after-02/objectives-after-click.png`; there was no
checkpoint reload, teleport, direct health edit or objective skip. Earlier
approach misses were short of the target while crouched. The first objective
capture rejected a map as a dialog; its corrected private classifier passed
saved modal/dropdown negatives before the successful capture. All native
cleanup checks passed. The root-owned diagnostic game was closed normally.

The same 434 candidate captured all three opening story cards from native
passive-menu surfaces in `knife430/intro/candidate434-01`. Root reviewed the
actual text pixels. This proves the game-to-menu-raster path, not a physical
headset-compositor capture. Subsequent user-relayed headset acceptance is recorded
above. The full campaign, death/reload crashes, and the other limitations recorded
below remain open; the external successful run does not resolve all of them.

Packaging passed 12 checks. Actual fresh install, 0.1.11 -> 0.1.12 update,
settings/user-data preservation, installed Worker validation and rollback
passed 18 checks. The local installed runtime then validated successfully.
Private evidence and pinned acceptance are in `bc2vr-recovery/release012`;
acceptance SHA is
`2dd62364c66af3d97a37203640dd731768b34b0f621b0e818fad0ac74d5f1b21`.
No further full-suite repetition was performed after the accepted normal builds.
All generated work stayed on G: under the existing storage cap; idle compiler
artifacts were compressed losslessly with hashes unchanged, not deleted.
See [release record](history/2026-10-20-release012-knife-and-opening-text.md).

## Current corrections — October 20

The supplied `launcher-last(1).log` identifies a separate startup failure:
the tester installed `BC2VR-Launcher` below the game folder. The preparation
tool rejected its own `data/runtime/cache/<recipe>` because it rejected every
output below the game installation. It failed before game or XR startup.
The caller now passes the managed cache root, and the tool binds it to the
executing installed package. Dedicated nested and separate launcher layouts
are supported; stock `Dist`/`Package`, redirected paths and linked outputs
remain rejected. Twenty-one relevant Python tests passed, including the actual
Entry-to-tool invocation and unchanged stock-file sentinels. This correction
is integrated and packaged as a launcher-only 0.1.11 overlay using the exact
0.1.10 native runtime; the 433 gameplay candidate is excluded. The staged
payload changes exactly two executable-text files plus two metadata files.
All 84 other members and all Setup helpers match 0.1.10 byte-for-byte.
Fresh installation, updating from 0.1.10, installed Worker validation and
rollback passed 16 checks. Local 0.1.11 is installed with settings unchanged
and 0.1.10 retained for rollback. The separate BC2VR repository now publishes
[v0.1.11](https://github.com/justiceofrip/BC2VR/releases/tag/v0.1.11), release
418599317, from documentation commit
`0dc3d0547a4cd751e312f6a4f93bd32b3e02fb31`. All three public asset sizes and
GitHub SHA-256 digests match the tested local files. See the
[0.1.11 release record](history/2026-10-20-release011-nested-cache-hotfix.md).

Left thumbstick click/release now toggles crouch; Y holds sprint. A two-stick
recenter chord cancels the pending click and preserves the chosen posture.
Posture belongs to the actor lifetime, separately from input epochs, and
survives same-actor menu/tracking/recenter gaps. Invalid input sends no actions.
The diagnostic recipes were updated to use the new mapping. Four focused
suites and the native receiver syntax check passed. Combined 432 passed both
full builds, 294/294 suites each. An actual emulated crouch approach reached the
fallen-tree passage; its later attack diagnostic cancelled after a movement
publication gap, so that run is not a completed combined test.

The frozen 431 knife/loading candidate passed both complete normal builds,
294/294 suites each. Its first actual-game motion check delivered exactly two
separate blade gestures through native acquisition and attack, both completed
misses, with no duplicate native attack and pending count zero. This fixes the
reproduced recognition/delivery failures described below, but does not yet prove
enemy damage or first-encounter progression. Further bounded navigation reached
the opening guards. Two approaches cancelled on expired input-menu evidence;
their clean native teardown is preserved separately from test success.

The actual-game 431 Entire Mission check now captured nine fresh passive-menu
surfaces. Image review confirms all three opening narrative cards are present
on this path. The mission restarted once, fresh world frames resumed, and the
native hook/view cleanup passed. This verifies the game-to-menu-raster path;
it is not a physical headset-compositor capture.

An additional delivered stroke in `approach43107` exposed one transient native
read failure resetting recognition mid-jab. The historical report does not
identify the failed read predicate. The firing reader compares complete
snapshots containing changing state/timers. Candidate 433 now classifies these
failures and preserves pre-intent recognition over one ownership-bracketed,
ready-but-incoherent packet, including repeated Gathers of that same packet.
It still requires a fully current native read before emitting an attack, keeps
the original sample deadline and 75 ms gap bound, and rejects a second distinct
missing packet. It does not retry an emitted attack or change pending outcomes.
The regression uses the delivered stroke timing and a changed-between-reads
timer: the old reset emits zero; the candidate emits exactly one after verified
recovery. Four added groups and 15 existing groups passed, as did x86 native
syntax validation. These changes are integrated; both full 433 builds passed,
while first-guard progression acceptance remains pending.

The subsequent 432 emulated approach caught a real crouch integration failure:
the new `ControllerActions` menu test used the input/XR clock while menu leases
use QPC nanoseconds. The recorded left click was present, but both native runs
had zero button actions; the second approach could not pass the fallen tree.
Candidate 433 now supplies an explicit menu clock at all six BC2 action calls.
The recorded generations 17–23 reproduce the old missing crouch; the separate
QPC clock produces exactly one toggle. The approach harness also incorrectly
counted the intentional 80 ms uncrouch click as a movement-publication gap.
Movement and uncrouch release receipts are now separate, retaining the full
500 ms neutral tail and rejecting gaps over 100 ms. Both focused suites passed.
The six-file correction is integrated; actual native crouch and traversal
passed the bounded check described below. Combined 433 source is frozen as
`bdd1006f36909942f55c184a36a995fe73b8782609e8174a3f18c5666260652e`;
both complete normal builds passed 294/294 suites. Its normal runtime receipt
SHA-256 is `be9b4894713448895c84bc7efb37e4272c52e569f8d5f48fc66d7d99505a3b50`.

The actual 433 crouch approach passed: all 427 controller publications arrived,
there were no menu pauses, and native eye-minus-body height went from 1.548 m
to 0.943 m, then back to 1.547 m after the second click. The actor moved 3.61 m
past the fallen tree. Both trace and receiver completed cleanly. This verifies
native crouch/traversal, not physical-headset comfort.

The next actual approach delivered one physical blade stroke to the original
native acquire/attack and damage callbacks. Serial 1/input 627 targeted
1231476320; the callback recorded health 12.5 -> 0, with pending and unknown zero.
The diagnostic cancelled on a Gameplay lease timeout about 114.6 ms after the
hit. No menu epoch/loading transition is recorded, so this is not classified
as an expected scripted menu takeover. A subsequent neutral observation
rendered normally and detached cleanly. First-guard identity/objective progression
and the reason for the publication lapse still require evidence; a damage
callback alone is not a claim that the mission hardlock is fixed.

The next approach, `progress43303`, stopped before movement. Saved frames show
a 162.357 ms native frame gap crossing the 100 ms menu lease; the receiver
cancelled before frames resumed. It was not a game crash. Trace 2/probe 12 also
reflect failed stream minima (published 1/consumed 0). Independent review
verified all 43 non-exit cleanup predicates, including exact graph/reference
restoration and disabled hooks. The old cleanup helper still requires zero
exits and cannot admit another run from this receipt unchanged. No further
injection was attempted. A private startup-only recipe correction under
`knife-startup434` now passes focused tests reproducing the recorded 162.357 ms
gap. It uses the existing opt-in pause bound and 500 ms fresh-neutral rearm,
and rejects changed epochs, exhausted timeouts and already-begun crouch clicks.
The candidate is not integrated or built into a production runtime. See the
[433 checkpoint](history/2026-10-20-checkpoint433-crouch-and-native-jab.md).
Combined 432 native source is frozen as
`508cbfe96436037d6138b6a66fbe4cb7ef93e9e1c35af378104ccc9c6cda2b56`.
It adds the launcher and crouch corrections to 431. Both full builds passed
294/294 suites. Its frozen normal-runtime receipt SHA-256 is
`33fe14e923fad2f683fd5c5c56d18a3c14de2ae9cd173e42cd47a21ef887d918`.
Installed launcher is 0.1.11, with native gameplay unchanged from 0.1.10.
The root-owned diagnostic game closed normally before installer checks; no
forced termination or SteamVR change occurred. No new headset session started. Physical
headset acceptance, first-guard objective progression, and the reported crash
remain unresolved. Private evidence is under `bc2vr-recovery/knife430`,
`knife431`, `nested-install432`, `crouch432`, `checkpoint432`,
`knife-native-gap-candidate` and `checkpoint433`.

## Opening knife fails human acceptance — October 20

The user tested packaged **0.1.10**, source
`bc035d08a511df6d0d57cbf7bb78689a8d7842977cbc6bee953ac5940621c133`,
and reported that stabbing still does not get through the first encounter.
Session `20261020-125259-682-d41037e4` ended unexpectedly before the
authorized mod-stop action could run. Its recorded probe exit was `0xC0000096`;
the cause has not been established. BC2 had already exited, and no game or
SteamVR process was stopped by the investigation.

The final native report is empty. The 329 periodic telemetry snapshots survived:
the selected-knife adapter was installed/enabled, but `prepared`, `pending`,
`published`, `acquired` and `unknown` stayed zero. The final snapshot records 71
read/hand gaps and 66 geometry gaps. Both native hook entries were unassociated
with a VR intent. This establishes failure before attack preparation, not an
incorrect damage amount or an old installed build. The current logs do not
identify the recognition rejection reason. Bounded text evidence and hashes
(about 2.1 MiB) are preserved privately in `bc2vr-recovery/knife430/human010`.

The previous `navigation42803` result established a native damage callback,
not independently verified guard kills or objective progression. Its record's
`cause: 1` means the VR **Stroke** gesture enum, not a native damage cause.
The health sample was taken inside the damage callback, before the enclosing
native attack returned; it does not establish the eventual target outcome.
Human opening-encounter acceptance is failed and remains a release blocker.

The [recognition correction](KNIFE-RECOGNITION430.md) reproduces and fixes a
same-controller-packet reset caused by independently recalculated render
geometry. The old code fails the real calibration-path regression; the changed
code preserves one physical stroke across repeated gathers. Periodic rejection
diagnostics are implemented. Full 431/432 builds and actual native misses passed;
the remaining 433 recognition and opening-encounter work is described above.
None of these candidates is installed or published.

Also retained from this run: the first mission's opening text appears on the
desktop while VR stays black. SP01's authored `SingelplayerFirstLevel` game mode
selects `Menu/loading_sp01.swf`, whose installed UI resource contains the three
`SP01_LOAD_LINE_*` text keys. It is distinct from the plane cutscene and the
mission-end video. The original XR raster path required an entered interactive
menu. The passive loading-screen correction now has the actual native-raster
evidence described above; physical headset presentation remains unaccepted.
The requested all-weapons sandbox
investigation has produced [a bounded catalog and range plan](TEST-RANGE-PLAN430.md),
not a playable all-weapons level.

## Tester startup correction — October 20

[BC2VR 0.1.10](https://github.com/justiceofrip/BC2VR/releases/tag/v0.1.10)
was published at 12:37 UTC on October 20. The installer, runtime ZIP and update
manifest all match the tested asset sizes and SHA-256 digests returned by GitHub.
Source checkpoint: `aaad4c7` on `checkpoint/427-knife-investigation`.

The supplied 0.1.9 EA/VirtualDesktopXR log establishes two local software bugs:
the host disabled pointing for the tester's 1024×768 raster, and the launcher
stopped a healthy main-menu-only session after 35 seconds because its readiness
check required world-render input telemetry. The supplied archive does not
contain the later failed retries. See [the diagnosis and implementation](MENU-STARTUP-HOTFIX429.md).

The candidate uses the original verified native pixel-to-APT mapper, supports
full-raster pointing at different aspects, and publishes a separate expiring
menu connection handshake. Explicit restart guidance replaces misleading
retry status; it does not force-close games or delete installation data.
The previous 0.1.9 knife and 0.1.8 selected-drive corrections remain included.

Native source is frozen as
`bc035d08a511df6d0d57cbf7bb78689a8d7842977cbc6bee953ac5940621c133`.
Both full normal builds passed 293/293 C++ suites each. The runtime receipt is
`6d6184ebad27f567f40d3051d8b79825e1e04dbf35e316f97980be82dca9b093`.
The 45-second actual-game menu check passed with world input zero, fresh menu
readiness beyond 35 seconds, correct off-center native mapping and clean hook
drain. The owned game was closed normally afterward. Package validation passed
21 checks covering all 87 payload files, fresh install, 0.1.9 update, installed
validation and rollback. Private evidence is in `bc2vr-recovery/startup429`.

The local launcher now uses packaged 0.1.10, with 0.1.9 retained for rollback.
Settings are byte-for-byte unchanged and installed validation reports the
existing asset cache ready. Installation started no game or headset session.
Keep **Use local checkpoint** unchecked to run the installed package; the
optional older local profile remains preserved.

The new actual OpenXR-menu presenter regression passed across four raster sizes,
off-center input, panel/cursor/beam layers, trigger and focus recovery. Native
mapper/getter instruction emulation passed 72 cases. Changing this workstation's
window to 1024×768 did not change the internal 1920×1080 renderer, so that resize
is explicitly inconclusive for actual-game 4:3 acceptance. The bounded live check
used the actual 1920×1080 renderer; it sent hover only, without clicks or OS input.
Actual Virtual Desktop headset acceptance and death/reload crash recovery remain
unresolved.

## Knife hotfix published — October 19, 16:57 UTC

[BC2VR 0.1.9](https://github.com/justiceofrip/BC2VR/releases/tag/v0.1.9)
publishes the tested r14 runtime, installer and updater manifest. All three
uploaded assets match their tested sizes and SHA-256 digests. Source and the
bounded native-hit checkpoint are at `72166d082e8114f05bf60132544aec73233deac4`.
The release retains the death/reload-crash, full-campaign and human-headset
acceptance limits. This is a narrow knife hotfix, not campaign acceptance.

## Verified opening interaction — October 19

The main r14 candidate has now delivered an actual physical knife hit in the
opening mission. In `navigation42803`, emulated tracked-controller stroke input
449 reached the ordinary native path, acquired target `895084672`, and produced
one native damage callback for a stroke intent. The verified native health field changed
from 12.5 to 12.375. The later distinct stroke, input 602, completed a clean miss
and rearmed; the final pending count was zero. Native and receiver helpers exited
zero, and retained hook/reference/view cleanup passed. This is native damage
evidence beyond the earlier client animations and targetless attacks.

The independent audit joins the delivered inputs, fresh menu/tracking authority,
repeated typed owner reads, native acquisition/attack and damage timestamps to
the same game06 lifetime. No trigger-button attack was synthesized. It also
measures the native blade axis within 0.517 degrees of body forward. The retained
post-attack image was independently reviewed and visibly reads “Two down. About
a thousand to go.” while the player faces wooden stairs with the knife held.
That is an observed narrative response after the hit. The runtime target has not
been independently joined to an authored guard GUID; two separate kill events,
objective completion and a scripted camera turn are not established.

Private evidence remains under `overnight426/knife/navigation42803`, with
`navigation42803-independent-hit-audit.json` beside it. The audit SHA-256 is
`ab2991d3d0ea31e5a55a0ff70ab3da846d6c8227ac1d76dd94823d013ceb430c`.
Its native report, input journal, before/after identities and cleanup receipt
remain pinned; raw game evidence is not distributed with source/packages.

This was a bounded game test with emulated controller input, not a human headset
test. Whole-campaign playability, actual headset handling, death/reload recovery
and long-session stability remain unverified. The older 0.1.8 is unchanged. The local
0.1.9 candidate is installed and its installed-runtime Validate passed with assets
ready. Settings remained byte-for-byte unchanged, and 0.1.7 is retained for
rollback. Installation/validation started no game or headset session. The exact
installer bundle passed 20 smoke checks covering all 87 payload files. The
root-owned game06 was closed normally with exit zero after testing. The optional
old local-checkpoint profile remains in settings; leave **Use local checkpoint**
unchecked to run packaged 0.1.9. No further session was launched.

## Built and frozen main candidate

Both r14 full architectures passed **293/293 C++ suites**, following focused
selected-knife ownership/geometry and receiver-fixture regressions. The shared
`build/x86` and `build/x64` trees again use this main repository rather than the
isolated 0.1.8 hotfix worktree. Normal feature flags and input protocol 4 were
verified from retained configuration and compiled target definitions.

The frozen runtime is `overnight426/normal-runtime-r14`. Its build receipt is
`7443155a14404ecf99cf592df7bc36bd7d32ebdcbb70435a3e3293be05780960`;
the full source snapshot is
`44f637ccdabb8991665610fd60e4d26b60a0f92e8792a811ec2ae326153aa1fa`.
Native source remains unchanged from r13:
`17f0b0647fa09f063d7becb2acff27ea0bc69b50b0f511a9c4dc3fe0edd3ddf7`.
Frozen r13 and published r4/0.1.8 binaries remain separate.

R14 changes only the private receiver's prepared-approach recipe and its tests:
it rotates the measured wrist before movement, holds that orientation through
the approach, and schedules the first physical jab after 500 ms of released
movement. The option requires motion and measured rotation, excludes turn/resume
combinations, and retains the menu, tracking, lifetime and delivery gates.
It does not add a production knife mechanism or assign native damage.

The production candidate includes the earlier exact ordinary acquisition caller
correction (`0x6E1FCA`) and consistent normalization of established transient
owner context bits. Inventory selection and typed lifetime checks remain exact.
Those changes previously passed repeated native miss/rearm checks; the new hit
supplies the missing bounded native damage evidence. Earlier failed runs remain
failed and retained. See [selected-knife evidence](SELECTED-KNIFE-NATIVE-426.md)
and [the checkpoint428 record](history/2026-10-19-checkpoint428.md).

## Public startup hotfix

[BC2VR 0.1.8](https://github.com/justiceofrip/BC2VR/releases/tag/v0.1.8) was
published on October 19 at 15:18 UTC from `hotfix/0.1.8-startup`, implementation
commit `475dc87f13a94546c0c6f639efedc51ad393a86c`, over published 0.1.7 gameplay.
It fixes the G:-only native audio-cache check that rejected the EA tester's
valid X: cache, retains exact content/callback validation, and surfaces bounded
native startup failures. It does not contain the main branch's knife work.
The public campaign warning therefore remains applicable to that release.

Both hotfix architectures passed 293/293 suites; focused launcher/setup checks
passed 135, and exact package validation passed 44 checks plus 51 launcher checks.
The unchanged asset recipe/cache was validated with the staged runtime. All three
uploaded assets match their tested sizes and SHA-256 digests. EA/VDXR headset
rendering is still unverified; runtime initialization is not rendering acceptance.
The separate hotfix source branch is published for fork review.

The earlier [0.1.7 recovery/resource hotfix](LAUNCHER-HOTFIX017.md) and
[0.1.6 installer/runtime hotfix](LAUNCHER-HOTFIX016.md) retain their own package,
settings, update/rollback and cold-cache evidence. Versions 0.1.5–0.1.7 used
normal-r4 native gameplay, source
`9196965b6f424cbfa33e98650fc230de273fc444de24c720218dbfa5f703b05b`,
with 291 tests per architecture. Its preserved profile is
`profiles/checkpoint426`; those results do not establish the new candidate's
gameplay. Build instructions remain in [BUILDING.md](../BUILDING.md).

## Other retained functionality and limits

The shared Nambu100 grip/support pipeline accepts its authored absent optional
persistence record while preserving exact weapon/data/configuration/mesh/rig
and lifetime checks. An earlier actual-game run verified partial-magazine return,
replacement from reserve and a subsequent shot, with native copies agreeing
and counts changing from 30/114 to 29/84. Detached-magazine pixels and headset
placement remain unverified. See [opening coverage](OPENING-WEAPONS-426.md).

Nambu's earlier pre-spread axis matched the authored barrel, but its stock shot
origin was displaced from the displayed muzzle. The translated-muzzle descriptor
and downstream argument observer remain Candidate; final native acceptance and
impact accuracy are pending. Matching other weapons' configuration fields does
not enroll them. See [aim evidence](NAMBU-AIM-426.md).

Manual recenter remains both thumbstick clicks held for one second. The vertical
look/reference-yaw correction and protocol 4 tracking-basis distinction have
combined support/recenter regression coverage; headset acceptance remains open.
The checkpoint425 opening-cloud correction was accepted by the user in a headset
and is retained in [the 425 handoff](../handoff/checkpoint425/README.md).

The reported death/reload access violation is unresolved. Failed session
`20261019-114521-913-2e4b9243` has an empty final report and an invalid instruction
target `0xC` in the retained game crash XML. Actor loss precedes the crash, but the
producer/corruption cause and mod involvement remain unknown. A prior no-VR fault
does not exonerate the mod. Bounded `checkpoint42701` evidence shows a scene
replacement, later stereo rendering and clean cleanup despite an incomplete
wrapper poll. `navigation42705` shows actor loss before its scheduled jabs and
later checkpoint return, but does not verify post-death controller/stereo recovery.
Neither result establishes a death/reload-crash fix.

Manual LMG reloads, wider weapon/vehicle coverage, spent-shell ejection, some
magnified scopes, full-body IK and multiplayer remain unfinished or unverified.
The next acceptance work is ordinary progression beyond the observed opening
response, real headset handling and bounded death/reload testing.

## Distribution and storage

The launcher retains manifest-checked updates/rollback and manual sanitized
feedback; nothing uploads automatically. Python is bundled. First asset
preparation fetches a pinned upstream FFmpeg decoder and builds private assets
from the player's own installation; validated warm caches require no download.
No game executable/assets, saves, raw traces or credentials belong in a package.

The G: rule is local development policy. Public installers/caches use the
player's selected valid local drive, including C:. All work here stays on G:
under [the storage budget](STORAGE-BUDGET.md). R14 reused the two existing build
trees; headroom was recovered by lossless, hash-verified compression of idle
generated files. Source, runtime symbols and evidence were preserved. Older
checkpoint results retain their scope in [history](history/2026-10-18-checkpoint425.md).
