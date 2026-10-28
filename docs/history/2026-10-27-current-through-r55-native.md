# Current state — shared ammo-entry fix / public 0.1.15

Updated October 27, 2026. This is the working index; older next-step instructions
are historical. The complete preceding checkpoint is preserved in
[the archived CURRENT](history/2026-10-26-current-through-r45.md).

Newest R55 AEK empty-magazine episode: scene119/run01 completed mechanically.
The server recorded exactly30 shots30/210->0/210, all three copies stayed0/210
for5.0459109seconds, then two actual authority-joined operations removed the
empty magazine and refilled30/180. One postfire shot left29/180 in all copies,
also stable after detach. Original checker exit1 is preserved: it incorrectly
treated two native client prediction rollback/replay pairs as reloads. The
reviewed saved-evidence successor proves their exact snapshot source, bounded
server-shot match/replay, and per-shot convergence; it does not relax the empty
dwell, refill, ownership or cleanup checks. No repeated game run was needed.
Corrected audit SHA256
`6fc76bd07f4a84fec9b06a7e8c27926a1a703098b0e649944f9eb01b1c784489`;
independent audit `8284ee65319f3581ed45293c2c4700cd3c7aa0a659853f7c0b4cef96e25f76c4`.
Twenty record-lock drops remain explicit; maximum retained dwell gap14.337ms.
Native/receiver helpers exited0 and retired; scene119 closed normally and
restored all three profiles. This covers one-handed empty firing/manual refill,
not two-handed empty firing, last-reserve behavior or headset presentation.
The shared normal integration is staged for builds; public0.1.15 is unchanged.

Latest mechanical qualification: R54 CombinedPump03/run01 PASSED the actual-game
emulated F2000/SPAS sequence. Independent audit
`ammo-retirement-r52-integration-prep/combined-pump03/run01/independent-audit02.json`,
SHA256 `a7b65d1551a505830faf759cde871d16f40005ea55f713c226dfbf4f47447cf9`.
Six magazine operations join real native authority invocations and before/after
observations on all three firing copies: original return30/210, replacement
30/180, post-holster return30/180. SPAS fired8->7, completed the manual pump on
all three native paths, and loaded one shell7/24->8/23 through actual callbacks.
Support returned; both shoulders stowed/drew the expected items. Final F2000
28/180 matches all three copies after two bounded shots. No active magazine
cancellation or missing evidence. This qualifies this mechanical episode only,
not all weapons, headset feel, visual quality, audio or campaign completion.
Both helpers retired normally; scene115 closed normally and restored all three
profiles. Its completed77.5MB trace exceeded the older64MiB Finish read cap;
reviewed FinishRetired115 raised only that read bound to96MiB and preserved the
first attempt's provisioning receipt. No force close or omitted cleanup checks.

Latest R52 native result: composition01/run01, game111, reached the F2000
magazine sequence after the ordinary PBL exit. The original magazine return
completed correctly across all three native copies: 29/210 -> 0/210 -> 29/210.
The second removal also completed; replacement insertion then cancelled with
View/Expired and same_owner=false during Guided/Stroke. Three native transfers
completed and no fourth refill was called. After cancellation the stock game
reloaded to 30/180; 0/210 in the last service view was stale, not final native
ammo. The complete reload/holster episode FAILED, despite the successful partial
sequence. Exact failed ownership subcondition is not present in the old trace;
bounded shared cancellation diagnostics are being added without relaxing policy.
Independent audit: `ammo-retirement-r52-integration-prep/composition01/run01/independent-audit01.json`,
SHA256 `28f8c0fe0ec52973e23d607d9633cd17ac1ba3197fd07f0fa55d8af96837f257`.
Both helpers exited0, hooks disabled/drained/inflight0; game111 closed normally
and all three original profiles restored exactly. Game110 had exited during
plain loading before any mod attached; its preserved negative result is separate.

R53 bounded cancellation telemetry compiled with R51/R52 in the existing
CombinedPump episode (original return, discard/replacement, then SPAS and rifle
return). Native behavior/deadlines are unchanged. Build receipt
`ammo-retirement-r52-integration-prep/combined-pump02/build01/run01/build-receipt.json`,
SHA256 `e7efab38ba9efcc7c1789dfcf39ef29541f480a6cfb4426503dcd01cc8d717b5`.
Actual source closure `499bb360a963e71592641bfe94f803d75215c01d5aae0a65791d4f6144cb34f2`;
54 affected objects compiled for diagnostic and normal restoration, both complete.
Scene113 verified the original XM8 in slot1, then provisioned F2000 there with
one native call and client/server convergence, preserving SPAS slot0. The R53
episode FAILED earlier, during ApproachMagazine before any squeeze/grab or ammo
operation. At143336033602700ns, Right GunHold token2 expired1.8899ms after its
deadline although current tracked/focused input604 was observed19.8898ms before
that deadline. The fixture failed6.3422ms later; a new gun claim appeared after.
This did not reproduce the prior Guided cancellation: first_active_view_cancel
is null. No magazine/pump/shell/holster acceptance. All three rifle ammo copies
remain30/210; support hold/release succeeded. Shared expiry/renewal ordering and
idle diagnostic rearm are under review; no lease has been relaxed. Audit
`ammo-retirement-r52-integration-prep/combined-pump02/run01/independent-audit01.json`,
SHA256 `8e14c144c9d659d542490f1fe996059a82c0daba508df42f43bf7ded138d631e`.
Both helpers exited0 with native hooks drained; game113 closed normally and
all three original profiles restored exactly. No unchanged rerun is planned.

R54 source02 corrects shared GunHold update/renew ordering using an atomic
current-input operation: only a newer, still-fresh original packet overlapping
the old hold can renew the exact equipped-gun token before expiry. Existing
Update/Renew behavior, true source gaps, invalidation and dependent-hand proof
deadlines remain unchanged. Plain lifecycle results remain separate from the
renewal result for sight recovery. The existing HandInteraction suite passed,
including the recorded old-order failure, new-order success and31 invalid
continuity/proof cases. Source receipt `608f564a303a112d8b85f7904a40321fb589e6689842e47d1fe4c3cf60423247`;
focused result `gun-hold-renewal-r54-prep/focused01/validation.json`,
SHA256 `30a1569cba7939b36ffb2d97d95ba40033868d0ed8699e44bd3f82e67feeab53`.
Private CombinedPump03 built with actual source closure
`3396c1adf7fb371df27b98338881e0dd441b4c134ff75caeeaf436c245321b64`.
Build receipt `ammo-retirement-r52-integration-prep/combined-pump03/build01/run01/build-receipt.json`,
SHA256 `a217c7126432bdc54e6a338fe1ae8b7b1e93e15c4f5209f73d65b2e93965c7d4`.
All69 affected objects compiled in each diagnostic/restoration phase; normal
source/config/output restored exactly. Independent build audit `e37fa2e9...`.
Fresh scene115 reached SP03, exited the PBL normally and selected XM8 slot1.
One verified native provision replaced that rifle with F2000, preserving SPAS
slot0; client/server ownership converged and all provisioning hooks retired.
CombinedPump03/run01 completed with independent mechanical acceptance above.

Destruction scene112 closed normally with all three original profiles restored.
Its plain SP02 traversal never reached changed masonry, so no native observer
was attached. Six brick/corner LOD geometry fingerprints now provide exact
small VB/IB comparisons for a future captured draw. Count/stride alone are
ambiguous and do not identify the reported artifact. No rendering fix claimed.

Private observer04 is built and captured in the game. Receipt
`diagnostics-r50-edge-observer04-prep/build01/run01/build-receipt.json`,
SHA256 `cc09047a9212ba02668a01d270112ec2cae9528e5fdb8c907856f89494da5eec`.
Actual source closure `9d8a939ee5920f56588c81b4b3bab124a9e2751aacc6ce99ec8f36132b826779`;
five diagnostic and four normal restoration objects compiled, with exact normal
source/config/output restoration. Its bounded offline matrix classifier passed
eight focused cases; those synthetic results are not native rendering evidence.
Scene114 reached the village and root resumed input ownership. After the
ordinary binocular cutscene, an aimed launcher shot opened a clear hole in the
upper-left house facade. `root-before04-burst.png` and `root-after04-burst.png`
prove newly changed masonry; `root-damaged05-burst.png` centers the opening.
Observer run01 stopped before any receiver/trace/injection: its reused AA reader
incorrectly required the pre-world main menu. A gameplay read-only successor
retains the same native signature/AA checks and admits the initialized level.
Run02 completed with one paired witness, no failed copies, both helpers exiting0
and hooks retired. The left image visibly places brick fragments beside the
damaged house while the right and flat reference retain them around the hole.
This reproduces the stereo artifact; head-angle persistence was not tested.
Scene114 closed normally and all three original profiles restored exactly.

Existing projection evidence contains21 exact edge-caller0x5D7F28 rows; all
match right-primary non-depth projection terms, none left or secondary. Worker
thread57896 is distinct from outer Draw63576, so the original observer's TLS
admission rejected all55761 observations and produced no rows. The useful values
were recovered from the existing projection journal, without another matrix
capture. Numeric audit SHA256
`f34ade3fb47ec00d5e391df020b11f3a21124b53bde3863d44f5b8760511bde0`.
The context override takes a one-time derived-state cut. Phase06/run01 now
records all 21 edge setters completing before both left Prepare/Draw cuts in
selected frame144328, with no active edge-setter callbacks at either cut. All 21 legacy
rows share the cut thread in this frame; this supports the narrow caller
enrollment candidate, not a general worker-scheduling guarantee. Phase audit
SHA256 `a94861c7c1052bbfd8c61ca6475fc66cda964e5bba3e19ad7f38511814e39e25`.
The later paired witness144346 reproduces a detached brick ring/strip in the
left eye while the right perimeter stays attached to the hole. Both images
match exact receiver pair3 bytes. These are different frames; ordinal502 shape
mismatched and captured no payload, so no exact masonry VB/IB association follows.
Cleanup passed; scene117 closed normally with all three profiles restored.

Correction02 enrolls the signature-verified edge caller through the existing
derived-projection path, preserving native depth terms and exact restoration.
Profile discovery, including the installed executable, and projection-math
focused checks passed: receipt
`4ca6015ce1e314bff9711a45bb1f6aaeb1410fbd57f102f53cd88c05bb60ea95`.
Correction07 built successfully with six diagnostic/five normal-restoration
object proofs and exact normal source/config/artifact restoration; build receipt
`82886c3607cd4a6dc237477f893cda7aa5413586c06019e3fe08ad5b9bf2a9ad`.
Correction07/run01 now removes the detached left-eye brick ring/strip in the
matched damaged-house scene. Root reviewed both full-size delivered eyes; an
independent review confirms the attached hole perimeter in pair3 and pair239,
3.781seconds later. Each witness image equals its delivered receiver image.
Runtime audit SHA256
`a62794104015d7e761b7851e07a4f6b986736b7ab593866eab28e9c280dda16c`
records11568 derived writes, including exactly21 added edge contexts across
two left scopes and241 frames;482 scopes succeeded with zero patch/restoration,
derived-capture or GPU failures. In selected frame84136 the worker's21 setters
complete before both outer-thread cuts. All callbacks drained; both helpers
exited0. Scene118 closed normally and restored all three profiles. This is a
visual fix for this sampled scene, not all destruction assets or headset acceptance.
The combined normal source union is staged separately and is not built yet.

R55 single-magazine02 SCAR_sp_s mechanical qualification completed on
2026-10-27. An actual preparation shot changed 30/210 to 29/210; four native
operations then produced 29/210 -> 0/210 -> 29/210 -> 0/210 -> 30/180, followed by a
post-reload shot to 29/180. All three native ammo copies agree, both magazine
cycles completed, and fresh support returned. Independent audit SHA256
`e20b4d6bf7e9b1dcfaff7433de099a247335e9806f3b679f24123dee104eb500`
pins the trace, command/authority joins and cleanup. Seventeen record-lock drops
remain disclosed; this is bounded transaction evidence, not an exhaustive
callback record. Scene116 closed normally and all three profiles restored
exactly. Empty/autoreload suppression, visual alignment, headset performance and
other weapons are not qualified by this run. Public 0.1.15 remains unchanged.

Actual build receipt SHA256
`6ece63b7430630f7ad5d1231de791d49fbaf1c53274ec6a96db0c6d2b4cc2805`
binds source closure
`5e8438fa60ae6cb547b7a8bc02a336e456394c782b629202d5e737bff5235943`,
69 affected object compilations in both directions, and exact normal source and
artifact restoration. The successor removes only the inherited diagnostic boat
episode; shared fixes remain. Never build superseded single-magazine01.
Eighteen of nineteen registered configurations have provisioning selectors;
the append-only MP443 selector is source-only. Later representative routes reuse
this magazine episode rather than repeating SPAS for every rifle. See
`campaign-magazine-batch-r55-prep/coverage-plan01.json` for configuration,
geometry and empty-reload coverage gaps.

The earlier storage admission block is resolved. Verified lossless compression,
four duplicate PDB removals with retained canonical copies, and authorized
superseded raw cleanup reclaimed 1,273,467,227 bytes in total (about 1.27GB), with
the 40GiB limit unchanged. Receipts under `performance436` are
`storage-r53-symbols/completed-compression08.json`,
`storage-r55-duplicate-pdb01/allocation-summary-correction.json`,
`storage-r55-superseded-raw01/root-cleanup-result.json`, and
`campaign-magazine-batch-r55-prep/single-magazine02/completed-compression01.json`.
Retained logs/screenshots, protected evidence and user recordings are unchanged.

Static edge-renderer tracing identified kind3's separate projection caller
0x5D7F28 during Draw, outside the two existing gather-time derived callers.
It copies a primary camera and adjusts near/far before writing a separate state
block. Correct eye association remains unproven. Default edge culling is off;
the default dirty-object instance upload uses WRITE_DISCARD, so a speculative
triple-buffer/cull-ring fix is unsupported. A bounded existing-hook witness is
built for the exact edge projection path, without guessed stack writes.

Latest native attempt: R49+R51 build run06 / game106 / native run04 equipped
F2000_sp through the exact native provisioning path (one give call, server/client
converged), admitted the PBL seat and committed ordinary vehicle Use. The fixture
then failed104 on the normal seated-to-foot rigEpoch advance, before any reload
or holster operation. The same soldier/weak/player survived; generation changed
from1 to2 at the existing controlled/entry transition. This is another diagnostic
assumption, not successful weapon validation. All three ammo copies remain30/210.
Both helpers exited0; hooks disabled and reload flow drained/inflight0. Game106
closed normally and all three original profiles were restored exactly. Independent
audit SHA256 `8c2535ee32062e7f01dad1f366a838d16b22afec882716454de96f45dee4f627`.
The fixture now observes the exact native seat-to-foot boundary; production
actor checks remain unchanged. Its focused regression passed using the actual
route reader and native epoch helper (receipt `6ab3afad...`). R49+R51 run07 built
and restored normal source/config/output; receipt SHA256
`4a00affbcd3bdb8819adb520d36b6337429f212dfb96a1b00251cab367cf367a`.
Its actual reload/holster sequence remains pending in prepared scene108.

R52 shared actor-retirement source02 passed the existing ammunition service
and physical magazine resource suites, including an old queued request that
could block the replacement actor before service dispatch, entered uncertainty,
and persistent actor/address reuse. Focused receipt SHA256
`70a5c1904ce9522473553ef3e2d3cd8aecac6043c779009ed33ea49a90f00f52`.
The private R49+R51+R52 composition built successfully in
`ammo-retirement-r52-integration-prep/composition01/build01/run01`, receipt
`4a7a4d383b6bd61d85f5ca05d3a4f5e0e092f6908a1e280a23bd1659f06cd36e`.
All 54 affected native objects compiled in both apply and normal-restoration
phases; normal source/config/output restored exactly. Native acceptance remains
partial only as described above. The superseded run07 episode was not repeated.

SP04 scene107 reached gameplay and exploded barrels, but the targeted shelter
remained visibly intact: no qualifying destroyed-wall evidence and no native
observer attached. It closed normally with all three profile files restored.
Plain scene109 selected Cold War through the ordinary mission menu and reached
gameplay; three coherent reads identify `Levels/sp_02`. The initial map checker
omitted that capitalization, so its rejection and the separate evidence review
are both preserved. No damaged masonry was reached and no native observer was
attached. The game closed normally; its SP02 profile is preserved under
`scene109/game109/profile-after`, and all three original profiles were restored.
Installed SP02 assets contain destructible brick houses matching the reported
class of artifact. Changed masonry and paired native render evidence remain
required. No destruction fix is claimed.

Storage: removed four byte-identical obsolete diagnostic symbol copies while
retaining current/R50 copies, plus 1,032 rebuildable test intermediates and 19
incremental-link caches. Reclaimed 777,383,900 allocated bytes. Runtime symbols,
test executables/results, source, captures and recordings were preserved.
Receipts: `storage-r50-symbols/duplicate-symbol-removal05.json` and
`test-cache-removal06.json`; original 40 GiB limit remains unchanged.

Earlier R49 build run05 / game105 / native run03 detached
cleanly but performed zero reload/holster operations. It exposed a second
diagnostic error: Seat compared a raw soldier pointer with the route reader's
packed weak/soldier actor identity. The actual production reader is unchanged.
Its corrected fixture now passes a focused test using that actual reader with
nonzero weak identity, including mismatches. Receipt SHA256
`90fc53a705b998ddd606a19f20837ad5b7a7174904a0f73e2e4ff7ab9b5208e7`.
Game105 closed normally and all three original profiles were restored.

A separate shared production defect was found in the ammo service: Dispatch
marked a native call issued before a second clock check could reject it without
calling the engine. Cancellation then retained an uncertain transaction and
blocked later requests. The private R51 change makes the final clock/Dispatch/
callback entry contiguous; real entered-but-uncertain calls remain protected.
The existing service suite plus new expiry/recovery cases passed, receipt
`build/x64/scratch/ammo-entry-r51/run01/validation.json`, SHA256
`136416e91386c6ff4d9691571420d92e60c26ab70e1bf25d1cdc4571bd84cb2e`.
This is not native acceptance. Combined R49 actor fixture + R51 ammo source
`58e203f4...` built as run06 with all five affected objects actually recompiled
in both diagnostic and normal-restoration phases. Build receipt SHA256
`c6ea44be2c2d6bef36b0e4e03d31d663660d11c914601b24822050ddbc2e0f6c`.
Normal source/config/output restored exactly. Run04 above did not reach the ammo
operations, so the shared service fix still lacks actual-game acceptance.

Earlier corrected R49 build run04 / game104 / native run02
completed and detached normally, but performed zero reload/holster operations.
Its first controller sample failed the fixture's seat/input admission before the
exit action. All native hooks drained, both helper exits were zero, and BC2
remained live until ordinary close. All three original profile files were then
restored exactly. The independent run02 audit is retained. This is a failed
sequence, not new weapon validation.

The diagnostic now has a fixed three-second initial input-readiness window;
wrong native seat/actor still fails immediately, and original 100ms input leases
are unchanged. Its focused startup/terminal regression passed once, receipt
`build/x64/scratch/boat-admission01/validation.json`, SHA256
`4780ed4890b982757bb3eabf83d231e11929afc410755cd55e48747647308908`.
Fresh native compilation and another actual sequence remain pending.

Earlier game103 loaded the retained SP_03 boat checkpoint. Root
verified the driver seat, exited normally, selected the existing XM8 (22/191,
capacity30; keyboard1 is its ordinary binding), then re-entered. R49 run01 did
not execute its reload sequence. Its report writer raised0xc0000005 while
reading a corrupt, unused shell-probe row count; the14MiB raw report is truncated
and is not accepted evidence. An independent prefix audit records zero native
ammo operations, hooks disabled, and reload flow drained with zero in-flight
operations. Receiver exited0 naturally. BC2 subsequently exited; all three
original profile files were restored exactly without a forced process kill.

The build logs identify a concrete stale-object defect in the private overlay
wrapper: Copy-Item preserved old source timestamps. Both diagnostic and restore
builds skipped changed BC2Camera ResourceInventoryProbe/InventoryReloadProbe
translation units while rebuilding Gameplay/NativeProbe, which changed the
shared object layout. Build run03 is therefore invalid for further execution.
Source installation/restoration now invalidates exactly the four affected
objects and verifies actual compiler commands in both phases. Corrected build
run04 passed both proofs and exact normal restoration; RunBuilt rejects older
unproven builds before process access. This finding does not establish the cause of past public headset
failures. Preserve raw run01 and its independent-audit01.json; do not clamp the
serializer count or repair its JSON to manufacture a passing run.

R50 isolated camera helper checks passed (13 vehicle-runtime cases). The selected
destruction-payload callback integration also passed on WARP: distinct paired
buffer contents, both predicate polarities, cancellation and graphics-owner
cleanup; evidence4,211,349bytes. Neither is an actual destroyed-wall fix or a
native restart acceptance. A source-reviewed composed private overlay combines
these two optional observations; its native build uses the repaired wrapper.
See performance436/restart-camera-r50-prep/focused01/run01 and
performance436/destruction-r50-prep/integration-build01.

The composed R50 diagnostic built and restored normal sources/config/output
exactly, receipt2606676eb822b15b9ea22c7c758d62a097940bb8765f9e5f25ac54ba3389f25b.
A separate graphics audit found mod-owned copies inherit native predication.
However the actual AMD hardware raw control executed both polarity copies;
it did NOT reproduce stale pixels. Preserve predicated-copy-r50-prep/build01
and build02 failures/observations. The proposed scoped copy guard is unbuilt
and excluded from the next native build; no destruction fix is claimed.

The campaign provisioning harness was rebuilt from the existing23 exact variants
without adding offsets or enabling weapons. Its focused x86/x64 checks passed.
SP_03 catalog planning identifies F2000_sp and SCAR_sp_s for subsequent exact
loaded-resource checks. Planning/provisioning metadata is not reload validation.
No new all-weapons, all-vehicles, destruction or headset acceptance is claimed.

Public0.1.15 is published at https://github.com/justiceofrip/BC2VR/releases/tag/v0.1.15
(release424351084, October26 10:28EDT). The supplied Report.txt proves a supported
EA game stayed live through menu/activation, but its initially recorded image
path was null; no XR host/native adapter had started. The launcher now resolves
missing image paths through the retained Windows handle, verifies PID/birth,
and uses the same proof for existing-game retries. A path unavailable only after
verified exit is allowed for diagnostic recording, never for live actions.
The saved experimental Steam option admits only reported22e96001... after its
expected PE layout, then existing native checks. Actual Steam rendering and
generic equipment-operation support remain unverified. Native434 binaries and
the default3911... admission are unchanged. This is not a performance release.

Validation:50 actual offscreen UI/settings checks,83 startup checks,13 packaged
Steam-session checks,50 baseline-session checks,33 real hidden-process checks,
10 Python menu checks, plus actual isolated install/update/rollback. Tests caught
and corrected scalar argument splatting and exited-helper path availability;
failed attempts are retained. Final packaged files match tested hashes.
GitHub asset digests verified; public014 updater selects015. Product commit
13d7574bd682ae883a935a9adae52a59409b576f. Validation receipt:
`bc2vr-recovery/release015/validation.json`, SHA256
`a19ca8ac790fb2a5e0e8de495724c44398983510b3520f2e8b0b6576e5872c0b`.
Final packages are in release015/stage03. No local desktop install was updated.
The later instruction to continue while asleep authorizes resuming guarded
emulated checks; the earlier game102 idle cancellation below is historical.

The game98 continuous fast-path restart failed after a successful native world
rebuild: the first replacement owner triggered permanent pipeline closure.
All154 native tickets belonged to the old world; controller publication then
returned Closed for606 consecutive host frames. There were no new-world stereo
tickets or post-restart images. All three helper processes and the native probe
exited0, references/view order restored exactly, and no exceptions were observed.
The ten host timeouts occurred before the rebuild, including six during loading;
they are retained failures, not erased by the identified closure cause.

R47 source adds an explicit, same-device world-owner transition after verified
native retirement/rebuild. Camera admission waits while the graphics owner
drains old source leases; the live host/input connection stays open. Only then
are source sets retired and lazily recreated with fresh resource epochs. Normal
unexpected owner/device/requirements mismatches remain failures. Cold startup,
shutdown during a transition, foreign graphics callbacks, and native address
reuse with reset frame counters have focused regression coverage in source.
The targeted mapped-channel and update/graphics-handoff regressions passed.
Real-GPU two-source and three-source checks also passed, proving fresh resources,
new frame1 pixels, and live input after transition. Their first attempt used the
wrong request API for negotiated lookahead; only those two test calls changed.
The failed attempt is retained. Build02 passed both normal305-suite builds and
is frozen as `unreleased-checkpoint436-r47-world-transition02`, receipt
`19dc0b1088111c456b412bb89ece7fbc4b582b22bdc81071f1feccbb69285968`.
Game100 exercised R47 through one real Last Checkpoint restart using the normal
host and emulated controllers. Native recovery and source transition completed
once without failure. The old world produced170 pairs; the replacement produced
65, with62 consumed. Both retained post-restart stereo pairs join exact host
frames1107/1111 to distinct new-world tickets. Root reviewed both pairs: both
eyes contain the mounted scene. This proves return of color, not complete
mounted-camera/depth correctness or physical-headset acceptance.

Controller publication continued:1115 successes, one transient Busy, zero
Closed. Native GPU/camera failures, rejected pairs, menu errors, feedback errors
and observed exceptions were all zero. All three helpers exited normally; native
hooks, callbacks,8 references and7 views restored exactly. BC2 then closed
normally and the three original profile files were restored byte-for-byte.

The overall protocol remains failed: six150ms world-request timeouts occurred
after checkpoint confirmation and before the rebuild, with no held native
request/source. The unchanged runner also uses a single-source ACK field in its
post-pair join; the independent audit proves pipeline retirement through its
actual Reaped and graphics-release timestamps instead. Neither issue was hidden
or used to rewrite the failed result. See `restart-fast-run-r47-awake-prep/run01`
and its29-check `independent-game100-audit.json` (SHA256
`df7280b72898568d287c49f9b2bea7698b866498963f2e9dbd7797dedfa911f7`).
R48 adds fresh native Gameplay admission: the previous host treated unknown or
expired menu ownership as "not a menu" and incorrectly admitted world requests.
The new gate preserves original deadlines and keeps input/retirement active.
R48's four focused targets passed, then both full305-suite builds passed. The
frozen runtime receipt is `0d8e6d8c2510ac7d8ae19b9d7c5887e9a64a7d773fbff7c1e52583fe077355f0`
under `loading-admission-r48/build01/normal-runtime`; the native closure is
unchanged from R47. Game101 crashed during plain Resume before any VR/AA helper
injection: execute AV at0x18, first saved return `amdxx32.dll+0x10b613`, matching
game97's AMD worker call site. Its exception records and exit are retained;
the original profiles restored exactly. That is not an R48 native-test result.
Game102 subsequently ran the exact R48 runtime/fixture after the user's renewed
overnight authorization. The frozen public014 menu helper was rebound to its
byte-identical archived path because015 changed the working file. Plain Resume
and ordinary E exit reached the reviewed on-foot baseline without exceptions.
The emulated native-menu Last Checkpoint action rebuilt the world once;219 pairs
were consumed, including44 from the replacement owner. Root reviewed post frames
1254/1258: both eyes contain the mounted checkpoint scene. No actual death,
complete mounted-camera, headset or performance acceptance is claimed.

The strict restart protocol still fails one async timeout. Exact evidence shows
request184/frame68425/tracking1213 was the first replacement-owner request,
1.07006s after rebuild. Native world_return cancelled it because complete_pair
was false, before either eye capture. The next request185 reused the slot; the
host saw slot_replaced at age10.3991ms, not an expired150ms deadline. Request185
published9.182ms later. The six earlier loading expiries were not reproduced,
but this new first-frame cancellation must not be erased or called a pass.
Investigate missing first post-rebuild eye passes; do not add blind waits or
extend leases. Independent audit02 has SHA256
`59a3933f63ea5a4cb61962b2bfd2d2d9c576e34f7c6a44af01fe9bffa6e59644`
under `restart-fast-run-r48-retry-prep/run01`.

Source follow-up b24f8c178bed500e7c92424a259c8129f531669f256c5eced6a15249cbc632ae
finds one known-seat camera Wait and45 successful camera plans, matching the
one cancelled plus45 captured replacement-owner leases. BeginTrackedFrame takes
the lease before updateOriginal; ApplyTrackedView only writes/sets state1 after
AdjustViewBase succeeds. A seat Wait deliberately leaves state3 with no camera
writes. This is the strongest candidate, not proven per-request causation: no
frame-stamped Wait reason exists, so stale input, owner/space/focus/mode and a
skipped visibility callback cannot yet be separated. The smallest next evidence
is the first ApplyTrackedView return reason with exact lease and seat deadline.
Keep the freshness guard; this10ms cancellation is not a measured CPU/GPU stall.

Inputs:1260Ok/1Busy/0Closed; zero menu/feedback/GPU/camera errors and observed
exceptions. Host/trace/watcher all exited0; native hooks/callbacks/current view
order and8references/7views restored exactly. Finish closed BC2 normally and
restored all three original profiles byte-for-byte. No rendering release or
headset test ran. The earlier pre-live checkpoint receipt remains historical:
`loading-admission-r48/checkpoint-pre-live.json`.

The plain-game crashes in games84/97/101 were observed with a debugger attached.
The watcher does not write game memory/registers or swallow access violations,
but debug-event suspension and synchronous capture can alter timing. These are
pre-VR crashes under observation, not untouched no-observer controls. The next
discriminating check if it repeats is one plain Resume without the debugger,
using owned-process exit and existing Windows crash evidence.

Two new tester launch reports are tracked separately in
[executable admission](EXECUTABLE-ADMISSION-436.md). The Steam tester's exact
`22e96001...` executable remains unverified. The non-Steam Pimax tester now fails
after optional window activation; public0.1.12's message cannot distinguish real
exit from an inaccessible/mismatched process. A separate launcher-only diagnostic
patch passed44 startup,47 feedback and125 portable-session checks. It preserves
admission rejection, reports observed exit codes, and collects activation logs.
This is not a proven fix for either tester's underlying startup failure. Native
code is unchanged; the completed native suites were not repeated for these
interpreted scripts.
The R48 retry now binds its launcher argument reference to the byte-identical
preserved Session.ps1; ValidateOnly passed, with no game/input action. See
`launcher-launch-triage-r49/validation.json` and
`restart-fast-run-r48-retry-prep/session-preimage-rebind01.json`.

Public0.1.13 was published October26 at07:58EDT in the separate
[BC2VR release repository](https://github.com/justiceofrip/BC2VR/releases/tag/v0.1.13).
It ports only the three diagnostic scripts onto the byte-verified public0.1.12
payload; it does not ship R48 or the development resolution/performance changes.
The extracted public package passed50 session,44 startup and47 feedback checks,
plus fresh-install/update/rollback and settings/cache-preservation checks.
Public0.1.12's unchanged updater discovered0.1.13 with pre-alpha updates enabled.
All published asset hashes match the validated files. Source/cache recipe and
native binaries remain the checkpoint434 public baseline. The local desktop
installation was not updated. Receipts: `release013/github-published.json`,
`release013/updater-discovery.json`, and `release013/stage01/stage-receipt.json`.

The next EA/Pimax report (`launcher-last(5).log`) selects0.1.13's unverified
process branch; the user confirms BC2 runs only on the monitor. Offline R50
checks reproduced rejection after a missing initial executable path, while real
hidden32/64-bit child processes passed identity capture and lookup. This is a
possible cause, not confirmation for that tester. All19 final checks passed;
the fixture's initial Handle-getter assumption was corrected and recorded.
No production/release changes or game/input actions occurred. Await the tester's
session.json and activation.json; see `EXECUTABLE-ADMISSION-436.md` and
`performance436/launcher-identity-r50/result.json`. R48 still waits for PC idle.

Public0.1.14 was published October26 at08:56EDT to fix the diagnostics Save
button itself. Its GetNewClosure callback could not resolve file-local
Begin-Action, closing the dialog without dispatching the export. The callback
now marks the dialog; the original launcher scope dispatches Feedback after
ShowDialog returns. Actual-source click tests reproduce the old failure and
pass the corrected Save/dismiss behavior. The exact release passed47 feedback
checks including the real worker, plus isolated install/update/rollback.
All79 runtime files remain byte-identical to0.1.13; only Launcher.ps1 and version
metadata changed. Uploaded hashes and discovery by the unchanged0.1.13 updater
were verified. No local desktop installation, game, headset or input changed.
The latest tester screenshot shows a later native-input-readiness timeout;
the fixed export will collect its evidence, not itself fix that timeout.
Receipts: `release014/validation.json`, `github-published.json`,
`updater-discovery.json`. See `history/2026-10-26-launcher014.md`.

Game99 was stopped before gameplay input or VR injection: native menu readiness
and read-only AA1x proof passed, but desktop captures remained black. The
desktop clock was initially stale while the display was asleep, then advanced
after a display wake; a normal minimize/restore did not recover the game image.
This is a plain-game startup observation, not an R47 native-run failure.
Normal close and exact three-profile restoration passed. Game100, launched with
an awake display, reproduced the initial black screen. Its bounded read-only
startup probe found the removed EA movie `Movies/UIMALogos` still selected; one
ordinary Escape revealed the menu. The native readiness check proves ownership
and dimensions, not visible menu readiness. Game98 had also required this step.
A reusable private missing-intro helper is prepared under `startup-gate-r47-prep`;
syntax/default dry-run passed, and live game101 use reached the visible menu.
Game100 additionally exposed a
capture-method difference: CopyFromScreen returned black during gameplay while
the existing bounded ImageGrab burst showed the current scene. Do not diagnose
rendering health from those black CopyFromScreen captures. Sleep causality is
not established.

Game98 used native MSAA=0 from startup, verified read-only; no AA helper was
injected. Actual descriptors showed90 single-sample depth rows and64 canonical
color-only rows. BC2 subsequently closed normally from its real window and all
three profiles were restored. A sandbox-restricted process query misleadingly
returned window handle0; its attempted termination was denied and never occurred.
The separate `normal-close-correction.json` records the successful ordinary close.
Evidence: `performance436/restart-fast-run-r46-native-aa-prep/run01`, including
`independent-game98-audit.json` (19 evidence checks).

R46 passed both normal builds and is frozen. Games90/91 completed the matched
emulated comparison with zero menu errors or stereo timeouts; both closed
normally and restored all three profile files exactly. No headset test ran.

Game92 also completed one real Last Checkpoint restart through the native menu
with neutral emulated controllers. The current world/request rebuilt once,
240 stereo pairs were delivered, and retained images show both eyes returning
to the mounted scene after both went black during loading. Native hooks, view
order and references restored exactly; all four helpers exited normally and
the exception observer recorded zero exceptions. The game closed normally and
all three profiles were restored exactly.

This is bounded lifecycle evidence, **not a clean presentation pass**: the
ordinary single-source receiver recorded 238 async timeouts and 233 native
discards. Source/timing review found a diagnostic cadence mismatch: this
receiver waits about229ms between polls but queues successor requests with a
150ms lifetime. Losses persist before and after loading;226/239 observed polling
gates already exceed the prior request's lifetime. Exact per-timeout attribution
is unavailable, so these totals are neither a game FPS result nor proof of a
restart-specific rendering stall. See [timeout analysis](G:/Unity/VRMMO/bc2vr-recovery/performance436/checkpoint-restart-r46-prep/run02/timeout-localization.json).
This run does not test the private fast three-source OpenXR path, physical
headset reconnects, actual death, weapon handling or a full jeep ride. Apparent
mounted-gun differences between eyes remain under review. See
[restart result](G:/Unity/VRMMO/bc2vr-recovery/performance436/checkpoint-restart-r46-prep/run02/runner-result.json),
[independent review](G:/Unity/VRMMO/bc2vr-recovery/performance436/checkpoint-restart-r46-prep/run02/independent-review.json),
and [paired images](G:/Unity/VRMMO/bc2vr-recovery/performance436/checkpoint-restart-r46-prep/run02/restart-paired-review.png).
The subsequent [mounted camera audit](G:/Unity/VRMMO/bc2vr-recovery/performance436/checkpoint-restart-r46-prep/run02/mounted-visual-analysis.json)
found matching eye rotations/projections and63.996mm horizontal separation in
pair239. Pair72 also matches the actual post-setter matrices. Strong parallax
from the nearby turret is plausible; exact turret mesh constants are absent,
so this neither confirms a gun-specific defect nor grants visual acceptance.

The continuous three-source successor in game93 stopped **before menu input**:
its wrapper omitted the private pipeline's existing single-sample AA helper.
Native depth was actually 8x (`R24G8_TYPELESS`), so `CapturePipelineDepth`
correctly rejected it at stage22/E_INVALIDARG; color was single-sample. There
were zero exported pairs and no checkpoint action. Public0.1.12 uses the
original single-source route, while the staged private headset launcher already
contains the helper. This is a failed test setup, not a restart pass or evidence
of a public regression. The failed result is preserved. An independent28-check
audit verified hook, callback, view and reference cleanup despite aggregate
probe exit12; game93 then closed normally and all three profiles were restored.
The next continuous test must carry the existing AA helper/readiness and verify
actual single-sample native descriptors. No production guard or deadline was
relaxed. Evidence: `performance436/restart-fast-run-r46-prep/run01`.

Game94 included the existing AA helper and verified actual native depth1x:
274 retained depth rows,273 consumed pairs, and zero GPU/menu/async/feedback
errors. It stopped before checkpoint input because the new controller fixture
sampled its frame clock before command acceptance, then rejected that older
time when trying to emit the menu pulse. The separate clock-corrected fixture
now samples time after reading the command; only that small helper was rebuilt,
with focused checks passing. The normal R46 runtime is unchanged. Game94's
native/XR/watcher processes all exited0 with verified cleanup; normal close and
exact original profile restoration completed. This is renderer startup evidence,
not restart acceptance.

Games95/96 both opened the pause menu and sent two balanced pointer clicks,
then correctly refused to approve an image that still showed the checkpoint
dropdown instead of its confirmation. Game96's later native capture already
contains the exact Last Checkpoint confirmation, with no additional input:
the early image was submitted 261.73 ms after arming the second click, while
the confirmation was captured at 686.95 ms. The click worked; the fixture's
75 ms wait after its own release did not acknowledge the native menu transition.
The isolated hover fixture also passed its focused checks but did not cure that
capture timing. No production input or rendering change is justified by these
failures. Both games detached and closed normally, restoring all three profiles.
Game95's premature `checkpoint-action.json` is explicitly corrected by its
independent audit: there was no third confirmation command or restart.
Game97 added a bounded 600 ms transition wait before each next fresh capture,
but crashed during the ordinary Resume load before the native/XR session began.
The captured fault is a null execute from AMD's queued worker (`call edi`,
EDI=0), distinct from game84's read-at-0x98. Only the already-retired AA startup
helper was present; its involvement is neither established nor excluded.
All three original profiles were restored after process exit. See
`restart-fast-run-r46-settle-prep/game97/pre-vr-loading-crash-analysis.json`.
The successor will initialize the game's normal MSAA=0 option before launch
and verify it read-only, eliminating the 8-to-1 startup transition. The prior
AA helper must not be used or falsely reported as applied on that route.
The bounded menu wait, original image deadlines and native GPU descriptor checks
remain unchanged. No continuous fast-path restart acceptance yet.

## R46 menu fix and matched performance result

The completed game88/onfoot49 emulated run reached gameplay and measured all
nine intervals, but failed acceptance with 14 menu-copy errors. The first was
the legacy copy's fixed 20 ms GPU-fence wait: the reported call lasted 36.72 ms
while its original source lease still had 37.84 ms left. Native cleanup, normal
game close, exact profile restoration and geometry checks passed. No second
comparison arm ran and no performance improvement is accepted.

The reviewed source change in OpenXrMenu uses the existing asynchronous
SubmitCopy/PollCopy route into owned scratch textures. Exact source tickets,
original deadlines, reference space, epoch/focus cancellation and negative
acknowledgement remain enforced. A pending copy does not acquire an XR image,
renew image age or acknowledge completion. Real expiry and GPU failures still
count as errors. The existing GPU test adds pending-copy, pixel, cancellation,
expiry and contended-feedback cases. The three affected host suites passed,
then both x64 and x86 normal builds passed all 305 suites.
The native BC2 dependency closure remains exactly `d12a43f8ddd80fb0ceb643e5f4961b41f6d62642b3fdb192bc607ccbfb3d0a8d`.
See [reviewed patch](G:/Unity/VRMMO/bc2vr-recovery/performance436/menu-copy-r46-prep/source-preparation.json)
and [build preparation](G:/Unity/VRMMO/bc2vr-recovery/performance436/polish-r46-menu-copy/build01/source-freeze.json).

The successor game90/onfoot51 and game91/onfoot52 checks both passed. They
completed 1,252/1,952 stereo pairs respectively, with zero menu errors, rejected
async requests, async timeouts or feedback errors. The original failing run is
preserved. See the qualified comparison below; this is measured loading/transfer
coverage, not a full-campaign, all-runtime or physical-headset acceptance.

The earlier game86 attempt stopped during startup identity observation before
any VR attachment. Its exact process later matched; the failed conjunct was
not logged, so the cause is unproved. A reviewed successor records each field,
waits only for missing startup information within the original 15-second bound,
and retains fatal handling of actual identity mismatch. Game88 passed this
readiness gate. Both games closed normally and original profiles are intact.

## Runtime identities

- Public and installed: [0.1.12](https://github.com/justiceofrip/BC2VR/releases/tag/v0.1.12), unchanged. Local rollback is 0.1.11.
- Public normal434 native source: `2e693ca5bbe9e8e333e582675fc43dd74a001b241b896db42b8f3c672274c738`.
- Public runtime receipt: `d7214c076cbca9544cb9ad7ddba472a96a03f9aa5b92fa7d0f969c88aee34c3b`.
- Private candidate: `unreleased-checkpoint436-r46-menu-copy01`; [frozen runtime receipt](G:/Unity/VRMMO/bc2vr-recovery/performance436/polish-r46-menu-copy/build01/normal-runtime/build-receipt.json).
- R46 receipt SHA-256: `edec49147b8b91a5bf92597384380b130af5a27fe3d3d8e271ddf0c3fed60a86`; source freeze: `39a2abbc82b0f7e6366806ed72b313a9717628653526f1bb4a0e48d39c66ba3b`.
- R46 full source manifest: `6fa50c4fbe208c63413730f0b33f793ce0e66d4f90a81f160b21ac8c3ecedb7d`. Native binaries/closure remain unchanged; host-menu code and tests changed.
- Previous private candidate: `unreleased-checkpoint436-r45-body-handoff06`; [retained runtime receipt](G:/Unity/VRMMO/bc2vr-recovery/performance436/polish-r45-body-handoff/build06/normal-runtime/build-receipt.json).
- R45 receipt SHA-256: `40c26dd819e74945587a5334a6d3cce9c1cf36e680e9dad57a7856b7e393559c`.
- R45 native closure: `d12a43f8ddd80fb0ceb643e5f4961b41f6d62642b3fdb192bc607ccbfb3d0a8d`.
- R45 source freeze: `dbb7431a79a89328a0189367baa9104b324ac49e48182e9e7ac7d3f32946bc1d`; full manifest: `b2b3fce204053c1439d9739832809f06b0c1379cec943d5938ea7324ced7af03`.

Both R45 normal architectures passed **305/305**, plus six selected handling,
cache, rig and visibility suites. The x86 cache consumer checked all 49 actual
candidate identities. Build05's failed validation remains preserved; build06
reused unchanged source-artifact paths, not failed test results. Later bench
Python changes do not alter these frozen native binaries. Nothing here updates
the public release or desktop launcher automatically.

## Validated progress and limits

The released baseline has human confirmation of opening knife guards through
the scientist, narrative text, manual magazine reload and no VR dropout during
that run. This is not full campaign or all-weapon acceptance. See
[the release record](history/2026-10-20-release012-knife-and-opening-text.md).

R45's actual SP01 shoulder regression passed: 240 stereo pairs, zero receiver
timeouts; two requests, two committed draws, zero cancellations and no unintended
stow. Confirmations took 29.47/31.12 ms. All 2,641 tracked/paired rig outputs had
zero source, packing or partial-pose failures; 119 settled hand samples matched
their recorded owners. The repair retains a verified pending draw across the
native selection's neutral input rearm. Dispatch still uses the original hand
lease; acknowledgement uses the original 1.5-second policy deadline, never renewed.

This proves the reproduced shared draw handoff, not additional reload coverage,
tracking-loss recovery or headset comfort. Eight initial Garand samples remained
attachment-pending. The strict visual analyzer still lacks event correspondence;
the supplement explains the phase join but does not grant visual acceptance.
Pair48's close arm difference remains unresolved. Evidence:
[mechanical result](G:/Unity/VRMMO/bc2vr-recovery/performance436/cross-draw-r45-build06-prep/run02/mechanical.json),
[timeline supplement](G:/Unity/VRMMO/bc2vr-recovery/performance436/cross-draw-r45-build06-prep/run02/independent-timeline-supplement.json),
[eye review](G:/Unity/VRMMO/bc2vr-recovery/performance436/cross-draw-r45-build06-prep/run02/eye-timing-review.json).

Other implemented private changes include coherent same-owner ammo HUD selection,
bounded jab deceleration, atomic root/pose/shot publication, immutable pose and
tracking handoffs, and preservation of structurally verified collapsed native
weapon leaves. Deterministic coverage is included in the builds above. These are
not claims that HUD flicker, knife audio, every animation or every gun is fixed.

## Weapon and campaign test bench

The planner now validates exact configuration joins independently for reload,
support grip, body display and holster anchors. It checks resource identity,
descriptor/support digests and finite rigid transforms. The supplied catalog has
288 identities: 21 reload metadata joins (19 magazine, one pump, one bolt), 164
support joins, 18 body/holster records and 17 complete four-feature metadata joins.
Those are metadata counts, **not counts of working weapons**. Type5's new 49th
cache row grants display/holster geometry only, not native reload or hand calibration.

All 34 focused planner tests passed; after the final rigid-anchor correction,
only the two affected malformed/missing-anchor and full-corpus tests were rerun,
and both passed. Native Observe/Equip/Restore gates are unchanged. A retained VR
module can require a fresh process before a bench episode. See
[bench design](CAMPAIGN-BENCH-436.md) and
[latest bench receipt](G:/Unity/VRMMO/bc2vr-recovery/performance436/campaign-readiness-r45-review/anchor02/source-preparation.json).
LMG manual reloads, broader bolt/pistol chambering, scopes and underbarrel coverage
remain incomplete; use [the pipeline](WEAPON-SYSTEM-PIPELINE.md), not weapon-name guesses.

Knife audio is research only: stock Attack/AttackKill are whole nonlooping
approximately 1.03/1.55-second recordings. A separate body-impact graph has five
authored 0.515–0.622-second variations. Live cue-to-jab correlation and duplicate
impact prevention are unproved. Existing owned damage evidence can support a
future confirmed-hit event; no sound, damage timing or kill cue has been changed.
[Audio investigation](KNIFE-RESPONSE-AND-AUDIO.md) describes the mechanism; its older
next-step notes do not override this checkpoint. Latest [asset evidence](G:/Unity/VRMMO/bc2vr-recovery/performance436/knife-audio-route-r45/findings.json) remains research only.

## Performance — matched R46 result, physical headset still pending

One fresh two-source control and one fresh three-source/overlap/refill run used
the same R46 binary, adapter, profiles, scene bounds, 2784x3024 pixels per eye,
actual 1x world/depth samples, queue1 and post-Present service. All nine adjacent
counter intervals were retained, with zero interior discards:

| Policy | Fresh pairs | Enclosed time | Fresh stereo pairs/s |
| --- | ---: | ---: | ---: |
| Two sources, no overlap/refill | 404 | 9.0635697 s | 44.574049 |
| Three sources, overlap/refill | 806 | 9.0461958 s | 89.098226 |

The descriptive combined-policy increase is **99.8881%**. It is not an isolated
buffer-count effect, a public0.1.12 comparison or physical-headset FPS. Both runs
passed exact owner/ticket/depth/cleanup/geometry gates; 507/762 retained depth
rows passed. The retained prediction-age metric fell from31.96 to22.50ms, not a
measured motion-to-photon latency. Logical source allocations increased by
202,051,584bytes; this is not measured resident VRAM.

[Comparison](G:/Unity/VRMMO/bc2vr-recovery/performance436/r46-matched-analysis-prep/comparison.json)
SHA `13332ce438b514627f19a2ec2e6dc23be74539c4c24719faa4e44e35c897a254`.
Both final desktop captures show the ordinary matching SP01 scene. Early saved
frames are black, and no saved image lies inside the first CPU window; numeric
qualification does not grant full stereo visual acceptance. The neutral fixture
uses the same installed48-record cache and does not exercise reload/pump/knife
or holster mechanics. Existing R45 handling evidence remains separate.

The public launcher still selects its existing single-source path. The
three-source policy remains private. Its [R46 candidate launcher](G:/Unity/VRMMO/bc2vr-recovery/performance436/headset-r46-prep/candidate/Launch.ps1)
is staged separately:8 binaries/20,557,312bytes. Setup validation passed with
assets ready and SteamVR selected, without starting BC2 or XR. All61 affected
offline policy checks passed. [Staging review](G:/Unity/VRMMO/bc2vr-recovery/performance436/headset-r46-prep/root-staging-review.json).
This uses exact recommended dimensions, refusing sizes over the retained AA
helper's8,418,816pixel/eye bound or v10's separate768MiB logical-image cap; no
silent downscaling. Physical-headset/lifecycle acceptance remains pending. Do
not treat the old R38 candidate or ordinary desktop shortcut as this build.

R38's accepted standalone emulated observation delivered **88.183674 fresh stereo
pairs/s**: 798 pairs over 9.0492941 measured seconds, 2784×3024 per eye, actual 1x
rendering, 762 native depth records and 245 exact host joins. The four host error
counters were zero in the accepted run. Startup/post-measurement exclusions remain
recorded. This is neither physical-headset FPS nor a demonstrated percentage gain.

Before R46 no valid matched baseline existed: the older baseline had missing
depth; a later control crashed loading. The R45 mounted30Hz fixtures are not performance tests.
The reviewed `r45-matched-performance2` control produced 538 fresh pairs over
9.0504257 seconds (59.44 raw pairs/s), but its 14 loading/menu errors reject the
whole arm. All 507 retained CPU-window depth rows passed. This is diagnostic
data, not an accepted baseline or comparison with R38. The actual host used the
installed 48-record cache, not the new 49-record Type5 cache; neutral controllers
exercised no visible ammo HUD. Black saved frames preceded measurement and the
normal final desktop frame followed it by 255 ms; none establish stereo visual
correctness inside the measured interval. See the
[offline diagnostic](G:/Unity/VRMMO/bc2vr-recovery/performance436/r45-matched-analysis2/run88-diagnostic.json).
See [performance](PERFORMANCE-436.md), [native resolution](NATIVE-RESOLUTION-436.md)
and [R38 receipt](G:/Unity/VRMMO/bc2vr-recovery/performance436/r38-attempt02/normal-runtime/build-receipt.json).
The staged R38 private headset launcher is a historical candidate, not current R46
or the public launcher. Actual headset-selected refresh/resolution acceptance is pending.

## Loading, mounted rendering and destruction — unresolved

Stock game81 and game85 loaded black before VR; ordinary Escape pause/resume
restored gameplay. Stock game84 crashed 5.53 seconds after Resume with no VR or
startup helper in the module map: `amdxx32.dll+0x686C0`, read address `0x98`.
This identifies an observed stock failure, not a proven driver root cause or a
fix for VR death/restart. [Stock crash evidence](G:/Unity/VRMMO/bc2vr-recovery/performance436/vanilla-r43/run10/stock-loading-crash-analysis.json).

The tested SP01 mounted seat has typed owner/entry proof but no verified blueprint
identity; do not label it a Kurogane/full jeep ride. Game82's numeric camera audit
passed 240 pairs with zero timeouts and correct yaw/lean/entry compensation, but
**visual acceptance failed**: huge black polygons in both eyes, already present
in the native desktop before stereo/weather writes. No gun/hand palette writes
were active. The affected draw and cause are unknown; it is not yet tied to the
reported destruction-only artifact. [Paired failure](G:/Unity/VRMMO/bc2vr-recovery/performance436/vehicle-r45-prep/run04/paired-seat-review.png).

Game83's same-binary draw-witness run looked normal, not proof of a fix. Its strict
audit failed global wrong-owner evidence despite three completed/drained pairs;
the first 256 draws were mostly shadows. Game85's later ordinal256 window retained
two of three planned pairs before the 7-second command budget; its 240-pair stream
had one timeout, so overall camera/session acceptance failed. Cleanup/PostSeat
passed. Retained main-size draws contained correct own-eye view matrices; coverage
was partial and shader semantics are unknown. Slot2 asymmetry was not traced to
an executed mod binding/restoration omission. Do not rerun just to obtain pair3.

Mounted depth scopes reject the one-clear route while on-foot expects two; the
current host accepts absent depth with color fallback, so that alone does not
explain black VR. Do not blindly relax depth gates. Latest evidence:
[constants](G:/Unity/VRMMO/bc2vr-recovery/performance436/mounted-render-r45-prep/run02/camera-constant-diagnostic01.json),
[slot2 audit](G:/Unity/VRMMO/bc2vr-recovery/performance436/mounted-render-r45-prep/run02/slot2-restoration-audit.json),
[depth audit](G:/Unity/VRMMO/bc2vr-recovery/performance436/mounted-render-r45-prep/mounted-depth-audit.json).
All games are closed and the three user profile files were restored exactly.
[Session checkpoint](G:/Unity/VRMMO/bc2vr-recovery/performance436/r45-session-checkpoint-20261026.json).

The new [destruction audit](G:/Unity/VRMMO/bc2vr-recovery/performance436/destruction-cache-r45-review/findings.md)
confirms visibility, Prepare and Draw run for each eye. It identifies no proven
fracture-specific cache fault. Retained mounted projections are equal between
eyes, so those GPU observations cannot exclude mixed-eye projection consumers.
Portable/native camera math already covers asymmetric projections. The existing
frozen generic receiver supports `--asymmetric` for a targeted future observation
without rebuilding, but cannot combine it with vehicle control fixtures. An
actually affected scene and suspect draw remain prerequisites; do not repeat the
normal-looking mounted survey just to add captures.

The isolated [selected-draw payload witness](DESTRUCTION-PAYLOAD-WITNESS436.md)
now compiles and passes its x64 plan/WARP checks. It adds bounded IA/buffer-SRV/
indirect payload capture, with explicit predicated-copy refusal. It is unlinked
from NativeProbe and the normal runtime. Root validation SHA256
`035a3d5735ec19a26ca4fc5f8fe126d824fe42198a11557d4ccb48c4716467ad`
is in build/x64/scratch/destruction-payload01/validation.json. Actual affected
draw association is still needed; no destruction fix is claimed.

Agent preparation for one continuous PBL exit→same-rifle magazine cycle→support
release→same-item holster round trip lives under performance436/
boat-exit-reload-r49-prep. It is a private diagnostic overlay, not linked normal
code or live validation. Three result-checker tests passed. Peer review also
caught a terminal observer that would expire after successful completion; the
diagnostic now freezes only its terminal evidence, leaving ordinary consumers
and active deadlines unchanged. Root compiled the two changed driver objects
against existing x64 policy libraries and ran both affected regression targets
once, including the new terminal/late-callback negative control: both passed.
No native module or Bc2Gameplay build ran. Validation receipt SHA256
`ef777b3d01b2520d9d173cc90402343bff9bc8da2be83343a0cd673ce5c3ce2a`
is in build/x64/scratch/boat-composition01. Final reviewed preparation SHA256
is `ae97ef160d60f336d2aaf06cdbd73fe445649a1e2e7cdd0f4e0303aff7ebf647`;
the full diagnostic build and actual PBL fixture remain pending. Do not run
this sequence in the unrelated mounted SP01 checkpoint. The initial sandbox
storage scan could not read an old restricted test directory; the same guard
then passed outside the sandbox before compilation, without an accounting bypass.

## Next work and operating constraints

1. Preserve the qualified R46 comparison and staged separate candidate without changing public defaults. Resolve loading/jeep/death-restart gaps before requesting another headset test. Separate numeric, visual and physical-headset acceptance.
2. Use the shared bench to expand exact configuration coverage and catch reload/holster/pickup/reinsert/pump/bolt/pistol failures. Preserve accepted shared behavior; do not treat metadata enrollment as native validation.
3. Continue HUD flicker, confirmed-hit knife audio, exact campaign vehicle and paired destruction investigations using retained evidence first. Require a discriminating question before another native run.
4. Deliver a combined checkpoint with a specific human test list. Cutscene orientation, weapon/scope polish and missing vehicle reticles remain on [the backlog](PREALPHA-BACKLOG.md); physical grenades/underbarrel reloads are deferred. Full body/MP/other Frostbite ports are later scope.

Root owns builds, native tests, XR and ordinary game input. Scene117's phase
capture completed and the owned game closed normally with all three profiles
restored. Agents currently prepare disjoint offline source, audits and successor
recipes; none owns desktop input. Keep the game on the left monitor, honor focus/idle guards and do
not touch BF2142. Preserve exact runtime/owner/lifetime pins and typed cleanup;
do not reuse a retained module where the contract requires a fresh process.
No second-eye simulation replay. Restore temporary writes exactly; persistent
magazine, holster and owed cycle state survives until completion/lifetime change.

Use G: for generated files and process-local TEMP/TMP/TMPDIR. Player installations
may use any valid selected drive. [Storage policy](STORAGE-BUDGET.md): 40 GiB
allocated, 15 GiB free, 3 GiB normal-build reserve, default 512 MiB raw/run. The
reviewed 752,713,728-byte mounted exception is specific, not a new default.
Call the storage guard; preserve source, current symbols, receipts and recordings.
Prefer exact-manifest lossless compression; never bypass a failed guard. Run
relevant regressions and both normal architectures for native changes; do not
repeat passed suites for docs or unchanged binaries. See [architecture](ARCHITECTURE.md)
and [AGENTS](../AGENTS.md). No full-campaign, all-weapons or new release acceptance yet.
