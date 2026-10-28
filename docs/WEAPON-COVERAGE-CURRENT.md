# Current 436 R56: representative shared-mechanic validation

The October 28, 2026 developer checkpoint includes actual-game emulated checks
for F2000/SPAS combined magazine/pump/shell/holster handling, SCAR magazine
handling, MP443 magazine handling, AEK empty-magazine suppression and refill,
and five M95 manual bolt cycles. These are scoped episodes on the shared
integration, not approval of every weapon using the same magazine family.
The final R56 Nambu100 check is tracked in [current state](CURRENT.md).

See the [436 R56 handoff](../handoff/checkpoint436-r56/README.md) for counts,
build provenance, and important limits. M95 magazine refill is still stock;
manual belt-fed LMG loading, broader chamber/slide mechanics, combined normal
tool-consumer validation, and complete campaign coverage remain unfinished.
No new physical-headset acceptance is claimed by these emulated checks.

# Historical 416: unchanged weapon coverage; latest human run was 414/source 415

The latest headset feedback is the 2026-10-17 run of runtime 414 / source 415:
holsters were reliable, the SPAS pump felt great, and pump audio was described as
"PERFECT". That feedback does not accept every weapon or the full campaign.
Checkpoint 416 adds no native weapon-operation or configuration rows. Its audio,
menu, mirror and boat-reticle work must not be counted as expanded weapon support.

Manual LMG reloads remain unfinished. Broader weapon-family coverage and full
campaign reliability remain unaccepted; registrations and measured geometry are
prerequisites, not a count of finished guns. See [current state](CURRENT.md) and
the [416 handoff](../handoff/checkpoint416/README.md) for current validation limits.

All entries below are retained historical records. Their original uses of
"current" or "latest" refer to their dated checkpoint, not the present state.

# Historical412: shared presentation changes, unchanged weapon admissions

The latest normal410 headset feedback reports much better pumping and reliable
manual SPAS shell insertion. It does not accept every weapon or campaign level.
412 lowers the shared ammo anchor and moves the read-only counter beside the
gun hand; the verified SPAS loading-shell presentation now survives reload
retirement. No native operation/configuration rows are added. Spent-shell ejection
and immediate pump timing remain outstanding/deferred respectively. New412
headset acceptance is not claimed; see [the current handoff](../handoff/checkpoint412/README.md).

The pipeline retains measured geometry and shared ownership/ammunition services.
Registrations are prerequisites, not a count of fully working guns. Prior
actual-game coverage and outstanding classes are preserved below and in the
feature-specific notes.

# Historical394: tracked pump wrist and combined sequence verified in game

Both full builds pass252 suites. The raised-gun controller sequence completes
three shots/pumps, two shell insertions, shoulder return and rifle use. All357
extended-hand paired publications contain the authored hand with zero recorded
wrist error, no reach rejection and no mechanical-only fallback. Both inactive
shotgun copies retain7/22 through31 coherent post-detach samples with no rejected
reads. The shared arm policy extends segment translations; its appearance still
needs headset acceptance. Maximum observed extension scale is1.4634.

The separate default-off grenade diagnostic sends no native edge: its gesture
is rejected by an unidentified admission condition while input remains fresh.
All three original and grenade ammunition copies remain unchanged. Retain that
failed result;395 adds targeted evidence before another attempt. It is not an
enabled player grenade binding. The393 checkpoint fade fix is retained; actual
death and whole-campaign acceptance remain open. Human390 is still the latest
failed headset run. See [394 evidence and limits](archive/TRACKED-WRIST-394.md).

# Current392: targeted recovery evidence, unchanged weapon admissions

The expired original pump input is now exercised in the actual game: rejection
preserves the pending mechanism, fresh input resumes it, and the combined run
completes three pumps and two manual shells. Both inactive shotgun copies remain
7/22 after detach. This adds evidence for the existing shared services, not new
weapons. Both normal architectures have248 passing distinct suites, with the
original failed and corrected test results retained. No new headset or campaign
acceptance. See [392](archive/NATIVE-RECOVERY-392.md).

# Previous391: failed human390 and targeted corrections

The October13 headset390 test failed. Local391 corrects expired pump/bolt input
cancelling persistent debt, verified mutable native publication flags rejecting
shell reloads, and shoulder draw-release immediately stowing again. Both full
builds pass246 checks. One actual-game run completes eight pumps, one shell,
rifle reload and shoulder return; inactive shotgun ammunition stays1/23.
No new weapon admissions or campaign/headset acceptance. Death rendering and
the reported interruptions remain under targeted investigation. See
[391 evidence and limits](archive/HEADSET-RECOVERY-391.md). Older candidate statements
below describe their historical scope, not current human acceptance.

# Historical390: continuous weapon swaps and checkpoint recovery verified in game

The new second-SPAS-visit diagnostic first failed389, then passed390 after a
shared native handoff fix. A completed reload's retirement now survives family
round trips; fresh native ownership and callback checks remain required. Actual
SCAR/SPAS testing completes magazine handling, two shotgun shot/pump visits,
shell loading, support returns, repeated shoulder draws and subsequent rifle use
in one persistent session. No unintended hand drop or rendering error is logged.

A separate actual-game VR-menu checkpoint restart during unfinished pump debt
recovers the new player, boat exit, rifle magazine handling and a fresh pump/shell
sequence. Five M95 shots/bolt cycles through empty also pass independent native
and paired-render audits; its magazine refill remains stock fallback. Both full
builds pass246 suites. Normal390 is a local headset candidate, not headset accepted.

388's eight-pump empty-tube proof and unwanted stock shell-fill fix are retained.
The latest human375 failure, failed389 reproduction and historical intermittent
graphics crashes remain documented. No claim of all weapons, death recovery,
sustained campaign stability or new native admissions. See
[390 evidence and player checklist](archive/NATIVE-HANDOFF-390.md).

# October12 status correction

The October12 headset375 test failed.388 now passes two actual-game emulated
sequences after a shared native shell-stop correction, including eight pumps,
an empty-tube shell, shoulder return and rifle fire. A second F2000 run verifies
cross-family magazine use and side-view pump presentation. These checks do not
add native weapon admissions or establish human/campaign acceptance. Historical
375 candidate language below is superseded; see [388](archive/SHELL-STOP-388.md).

# Current evidence index — October 11, 2026

The next local player candidate is375;374,369,361 and357 are preserved. Optional partial prop
materials are available as an explicit preview and are not enabled by default. The
historical entries below retain the scope of their original tests.

372 revalidated59 contacts and prepared ten additional bounded references.
374 selects those contacts (69 variants/38 meshes), corrects the emulated
player's ammo-dependent support mapping and passes MG3/TOZ native grip tests.
This does not increase native ammunition admission. See [374](archive/SUPPORT-PIPELINE-374.md).

Offline376/377 measures other fore-ends and recovers10 previously unreadable
clips across four weapon families. Four models now produce18 exact mechanism
records through the shared tool;870/NeoStead have sampled hand-contact candidates.
These are data preparation results, not new native reload or pump admission.
See [377](archive/ANIMATION-WRAPPER-377.md).

The378 shared-source correction adds eight SPAS configuration records while
preserving all18 prior mechanism records. These are offline data, not new native
admissions. See [378](archive/SHARED-ANIMATION-SOURCES-378.md).

Offline379 processes427 authored action records across39 models through the
shared batch. Three PP2000 reload variants reject decoded orientations. This is
data preparation, not enabled gameplay. See [379](archive/AUTHORED-ACTION-PIPELINE-379.md).

Offline380 retains five valid parts per PP2000 reload variant and explicitly
rejects the failing part/dependent child. No usable reload contact is inferred.
See [380](archive/PARTIAL-ACTION-DATA-380.md).

| System | Shared implementation and current evidence | Still unverified or incomplete |
| --- | --- | --- |
| Weapon placement and support | Measured contact data covers69 variants/38 meshes. MG3 and TOZ sustained support pass374. SCAR sustained support and MP443 one-/two-hand placement have actual-game evidence.339 corrects the SCAR controller basis. |69 contacts are not69 accepted reloads. Human acceptance of the revised SCAR alignment is pending. |
| Detachable magazines |18 compiled prerequisite configurations across10 asset names use the shared magazine transaction path.358 completes three rifle-magazine cycles, shoulder swaps and subsequent fire. | Complete fleet admission and headset acceptance; a data row alone does not prove native magazine authority. |
| Pump shotguns | 375 completes SPAS shot, physical pump and shell insertion, including held-support tracking-loss recovery. Geometry/fixed parts are now explicit family data.357 uses the authored fore-end hand shape; sampled stock/ring positions remain attached. Support returns in127–131ms in375. | Human feel and all-frame visual acceptance; additional pump weapons need their own measured part/contact data and verified native operation binding. |
| Bolt actions |293 completes five M95 shots and physical bolt cycles, including the final round. The pump/bolt ownership and prediction rules are shared. | M95's final magazine refill in that test is a native fallback. Other bolt rifles and manual sniper ammunition loading are not accepted by this evidence. |
| Pistols |309 MP443 evidence includes sustained support, magazine transfer and fire.305 verifies pistol-only hip stow/draw. | Pistol slide/chamber manipulation, a third native carried slot, and remaining pistol admission. |
| Underbarrels | The previously tested sight interaction and native underbarrel selection remain separate from rifle magazine custody. | Manual launcher/underslung-shotgun ammunition handling and full attachment/sight coverage. Do not imply all attachments share the same geometry or animation. |
| Belt-fed LMGs | Shared interaction/reload prerequisites and prior research are retained. | Complete player cover/belt/charging sequence and actual-game acceptance. No fleet claim. |
| Body equipment |357 includes paired world/first-person depth for body props.359 adds a bounded optional material sidecar and software preview tools. | 369 verifies30/30 stowed and200/200 held both-eye ammo samples after367 publication changes;239 depth copies and237 compositions in240 pairs. Complete materials, remaining omissions and headset acceptance are open. |
| Knife | Private336 records two directed client/server attacks and prediction handoffs. | Normal player chest custody, tracked mesh/hand integration, scene cleanup and downstream hit/damage. Private helper success is not an enabled player knife. |
| Menus and campaign transitions |370 verifies restart/replacement-menu recovery using normal369:240 consumed pairs,197 after the replacement menu,zero observed exceptions. Two frame discards remain unclassified. Vehicle control/aiming has prior scoped evidence. | Intermittent startup/restart crashes and sustained campaign completion remain unresolved. |

Relevant evidence: [shared admission and depth353](archive/DEPTH-AND-ADMISSION-353.md),
[authored pump357/358](archive/AUTHORED-PUMP-HAND-357.md),
[SCAR basis and fixed shotgun assembly339](archive/VISUAL-ALIGNMENT-339.md),
[M95 final-round293](EMPTY-PREDICTION-293.md),
[pistol support309](archive/SUSTAINED-SUPPORT-309.md),
[hip carry305](archive/HIP-CARRY-305.md), [knife336](archive/KNIFE-TIMING-336.md),
[materials359](archive/MATERIAL-PIPELINE-359.md), [graphics setup369 and menu370](archive/GRAPHICS-BOOTSTRAP-369.md).

Enrollment should reuse the shared transaction/mechanism implementation and add
measured adapter data plus the required native evidence. It should not copy an
XM8/SPAS-specific controller state machine into another weapon. Automated checks
cover ownership, prediction and failure transitions; headset checks remain for
alignment, visibility and feel. Neither can substitute for the other.

# Historical local312: shared mesh-layout correction

The shared asset decoder now distinguishes position/skin layouts at equal vertex
strides. It recovers six installed mesh entries across the323-archive inventory
and preserves every previously derived skin/index signature. All58 prior support
contacts remain unchanged; M1911 adds its own measured support contact and
pistol-only hip classification (59 variants/33 meshes total). This does not admit
its manual reload, chambering or body-render asset enrollment.

Both full builds pass243 suites each;100 Python checks and11 focused C++ checks
pass. Native game evidence remains311 combined rifle/holster/SPAS and309 pistol;
no new312 native or headset acceptance is claimed. The311 player checkpoint is
preserved. See MESH-LAYOUT-312.md and CONTROLLER-GAP-311.md. Knife draw/hold, third
native slot and the other listed campaign/equipment gaps remain open.

# Local311: native controller-gap handling and combined acceptance

Two real-game failures exposed expired controller evidence cancelling an owed
pump cycle, first while held and then between native shot receipts.311 preserves
freshly verified native observation across that bounded input gap without
renewing hand leases or manufacturing completion. Pump and bolt regressions
cover all first-reporting branches, neutral resumption and invalid evidence.

Both normal builds pass243 suites. The combined actual-game emulated sequence
passes sustained rifle support, three magazine cycles across shoulder swaps,
SPAS pump/shell insertion,102.94ms support regrip and later rifle firing. Native
ammunition receipts independently agree across server and both clients. The
successful run did not trigger gap suspension; those boundaries have separate
deterministic regression evidence. See CONTROLLER-GAP-311.md.

58 measured grip variants are not58 completed reloads:18 magazine prerequisite
configurations span10 asset names. Pistol support/reload/fire evidence remains
prior309 with unchanged pistol components; hip carry305 and menu/scene304 retain
their original acceptance scope. No new headset or sustained campaign acceptance.
Chest knife, third native slot, pistol chambering, belt-fed loading and broader
manual-action enrollment remain open. Do not reuse native slot2 for a pistol:
it participates in underbarrel switching. Physical grenades remain deferred.

# Local305: pistol-only hip carry

The exact MP443_sp carry profile now uses a dedicated right hip anchor. Rifles,
SMGs and shotguns retain their shoulder slots; the hip is not an extra long-gun
slot. BC2 labels this pistol wcSmg, so reviewed asset identity determines carry
location independently of native firing classification. Contacts and both body
render paths share the same anchor lookup.

Both normal builds pass all 243 suites; five focused x64 suites pass. Native
receiver-only emulation on the ordinary production DLL stowed and drew the
pistol with two commits and no cancellations, then fired one round (17/85 to
16/85, all three native branches). It consumed 240 stereo pairs and shut down
cleanly with exact original view order restored. No OS input or headset was
needed. Fresh body metadata is not proof of visible headset prop pixels.

See HIP-CARRY-305.md and local hip305-native02/hip-carry-audit.json. Three carried
items are covered by CPU fixtures; this does not add or verify a third native
weapon slot. Hip comfort, pistol slide/chamber and sustained campaign acceptance
remain pending. Earlier reload, pump, bolt and scene evidence keeps its original
checkpoint identity. Checkpoint304 remains intact.

The next equipment request is a chest knife grabbed with an empty hand, held
without attacking on grab, with deliberate motion attacks. Read-only native
survey finds KNV-1, wcKnife, slot7 and its mesh; draw/hold and hit behavior still
need binding proof. Physical grenades are deferred by user request.

# Local298: body catalog capacity and reload-to-fire check

The native and host body renderers had a 16-asset catalog limit while the cache
loader accepted64. Our46-record cache parsed correctly but its entire catalog
was rejected by both renderers. Both now share the same64-asset limit; the eight
simultaneous display slots and all per-asset identity/geometry checks remain.
Queue rejection has its own status instead of being mislabeled geometry failure.

Both architectures parsed and queued the full installed cache:46 parts and
269,940 triangles. Software D3D11 tests drew the final asset of a64-entry catalog
into both eyes, checked depth and atomic expiry, and rejected duplicate/oversized
catalogs. Five focused suites and all243 normal suites pass on both architectures.

The actual campaign test accepted all46 native catalog entries, removed the
MP443_sp magazine, loaded a chest replacement and then fired one ordinary
controller-trigger shot:17/85 ->0/85 ->17/68 ->16/68. The independent audit
matches both transfers and the subsequent shot to the server and both client
branches. All240 stereo pairs were consumed, with40 pixel checks. There was no
magazine presentation fallback, conflicting hand claim, GPU error or camera
restore failure. Hooks drained and BC2 remained alive. No OS input was used.

The earlier297 run failed before any controller input or stereo pair. The receiver
could observe a successful handshake, then re-read its nonblocking connection
status and mistake mutex contention for timeout. Setup now retains the first
successful observation; subsequent input/frame operations still validate the
live peer. A deterministic regression covers success followed by contention,
real timeout and a backwards clock. The exact mutex race in297 was not captured;
the original failure remains saved. Its no-frame shutdown retained hooks and
required closing that exact game process; that cleanup case remains unresolved.

Prior296 separately verifies17 single-fire shots through empty, a5.071-second
empty hold and manual refill. Prior294 verifies VR-menu pointer checkpoint
recovery with a removed rifle magazine. Prior290 verifies eight SPAS pumps,
shell insertion and F2000 magazine return; prior293 verifies five M95 bolts
through empty with native magazine-refill fallback. These are native emulation
results, not whole-roster, headset placement or sustained campaign acceptance.

Open work includes pistol slide/chamber and hip routing, belt-fed native loading,
broader exact enrollment, M95 manual magazine loading, sustained campaign/death
recovery, and headset visual checks. Ammo metadata delivery is separately verified
from existing296 records; that does not establish compositor visibility. No
headset session was launched and nothing was published.

Evidence: `pistol298-native01/singlefire-magazine-audit.json`.
Source: `9dfb6bbbe24086197ff2e41e9cf96317c210c86269e8edb95782e0129e42cfe5`.

# Local296: pistol fires through empty and manually refills

The exact MP443_sp fired 17 distinct ordinary controller-trigger presses in
the actual campaign (17/85 -> 0/85), stayed empty for 5.071 seconds, then removed
its empty magazine and loaded a chest replacement (0/85 -> 17/68). Independent
raw original-Update records confirm every shot on all three engine branches,
the empty dwell and both magazine operations. All three branches applied and
restored empty-reload suppression. There was no automatic refill, presentation
fallback, conflicting hand claim, GPU failure or camera-restore failure. All
240 published stereo pairs were consumed, with 40 pixel checks. Hooks drained
and BC2 remained alive. No operating-system input or headset was used.

This is additional native acceptance for the SingleFire magazine family enabled
in295. It adds an input-only diagnostic mode, disabled in normal builds; normal
gameplay behavior is unchanged. The driver never fabricates shots or ammo. It
retains one bounded trigger pulse across temporary body publication suspension,
then waits for fresh coherent native ammo before its next press. Negative tests
reject missing shots, automatic refill, reserve changes and changed identities.
Both architectures pass all 243 suites and four focused suites.

The first audit assumed sequential server/client callbacks. The retained native
records show a valid client Update overlapping the server Update: client exit
is after the server exit, with exact matching counts on both reads. The corrected
independent audit requires that actual ordering and uses the genuine helper-owning
server Update as its server vote. It does not change runtime admission, deadlines
or engine state. Both the original rejected audit and corrected result are saved.

Prior295 verifies returning the full original pistol magazine and discard/chest
replacement. Prior294 verifies the VR pause pointer and checkpoint recovery with
a removed rifle magazine; prior290 verifies eight SPAS pumps through empty plus
shell loading and F2000 magazine return; prior293 verifies five M95 bolt cycles
through empty (native magazine-refill fallback remains). These are separate
native proofs, not fleet, headset or full-campaign acceptance.

Still open: a pistol shot after the newly inserted magazine, slide/chamber
interaction, hip-holster routing, belt-fed native loading, broader exact weapon
enrollment, sustained campaign/death recovery and headset placement/visual checks.
Eighteen compiled magazine prerequisites do not mean eighteen accepted weapons.
No headset session was launched and nothing was published.

Evidence: `pistol296-native01/singlefire-magazine-audit.json`.
Source: `68b333be94a91a2039371f48c50eb95fd579684c0dfc09bf3c1e518d76929fab`.

# Local295: pistol magazine uses the shared reload pipeline

The exact MP443_sp now uses the existing magazine resource service, sampled
reload geometry, hand arbitration, visibility and body ammunition rendering.
An actual-game controller run drew it, removed/returned its original full
magazine (17/85 -> 0/85 -> 17/85), then discarded it and loaded a chest
replacement (17/85 -> 0/85 -> 17/68). All four operations were independently
confirmed by both client branches and the server, within their original
deadlines. All 240 stereo pairs were consumed, 40 pixel-checked; the interaction
reported no presentation fallback or conflicting hand claim. Hooks drained and
BC2 remained alive. No operating-system input or headset was used.

This extends the pipeline to the separately reviewed SingleFire, zero-bolt
magazine family. The exact asset descriptor and existing instruction proof
authorize MP443_sp only; it does not infer support for every pistol. The
authored sampled magazine path is reused. Right-hand static grip remains live
calibrated because its authored spline is not an accepted static binding.
Slide/chamber interaction, empty-magazine firing, pistol hip-holster routing
and headset hand placement remain unverified or unfinished.

The private magazine-only controller driver skips unrelated SPAS steps. It
cannot change ammo or acknowledge a consumer and is disabled in normal builds.
All 243 regression suites pass on x86 and x64; seven focused suites passed on
each architecture. Generic magazine test fixtures now use each exact capacity
and sampled path, instead of assuming a straight 30-round rifle magazine.
Installed-asset regeneration reproduced the 46-record private body cache
exactly (16,473,040 bytes); installed game geometry must not be distributed.

Prior294 proves native VR-menu checkpoint recovery with a removed rifle
magazine. Prior290 proves eight SPAS pumps through empty, shell insertion and
F2000 magazine return; prior293 proves five M95 bolts through empty, retaining
native magazine-refill fallback. These are separate tests, not a full campaign
or new headset acceptance. Eighteen compiled magazine prerequisites do not mean
eighteen accepted weapons. Sustained campaign/death recovery, belt-fed loading,
broader exact enrollment and remaining visual/interaction acceptance are open.

Evidence: `pistol295-native01/singlefire-magazine-audit.json`.
Source: `e03fbc41dde389e960942f5773cafa24ee322bab16914d1d681d3f2bd038df1d`.

# Local294: checkpoint restart during a removed rifle magazine verified

An actual-game controller run removed the original 22-round XM8 magazine,
opened the VR pause menu with its pointer and restarted the checkpoint. The
replacement player recovered stereo presentation, exited the boat, drew the
rifle, removed and returned its original magazine (22/191 to 0/191 to 22/191),
then fired and physically pumped the SPAS and inserted a shell (7/24 to 8/23).
Support returned in 134.72 ms. All three native branches independently confirmed
each magazine operation. All 240 published stereo pairs were consumed, including
166 after replacement, with no fallback, menu error, exception or ammunition loss.

This adds a private diagnostic mode for a real removed-magazine interruption;
normal player behavior is unchanged. The test retains the actual detached-item
witness and original bounded deadline rather than completing a reload on behalf
of the player. The new mode is disabled in both normal builds. Both architectures
pass all 243 suites; four relevant focused suites also pass on each architecture.

Prior293 verifies five native M95 bolt cycles through empty (magazine refill
remains native fallback); prior290 verifies eight SPAS pumps, shell insertion,
holsters and F2000 magazine return. Prior279 verifies an unfinished-pump menu
restart. These are separate preserved proofs, not fleet or headset acceptance.

Still open: sustained campaign/death recovery, retiring unfinished pump debt with
the rifle selected first, native pistol/slide and belt-fed loading, broader weapon
enrollment, M95 manual magazine loading, and visual headset acceptance. Seventeen
compiled magazine prerequisites do not mean seventeen accepted guns. No new
headset test was launched and nothing was published.

Evidence: `scene294-recovery-01/recovery-audit.json`.
Source: `e319710e5c7d60da8e9d0927f113788d729b54b378c2bda600289f4be91f5a4d`.

# Local293: final-round bolt and pump prediction recovery verified

Five ordinary-controller M95 shots and physical bolt cycles passed in the actual
game, including the final round. Ammunition went 5/45 to 0/45; the stock native
magazine reload then produced 5/40. That final refill is an explicit native
fallback, not a manual M95 magazine reload. Each bolt closure reached native
Ready in 38-62 ms, with original guarded clocks, all three branch receipts,
custody return and paired presentation for all four physical motion phases.
There were no presentation fallbacks or evidence rejections. Hooks drained;
the game remained alive. Full callback coverage and headset acceptance are false.

The shared prediction validator now recognizes the engine restoring the final
predicted shot and replaying it. Only that client's tentative shot vote is
withdrawn; the owed physical cycle, original deadlines and ammunition authority
are retained. Exact failed292 callbacks and connected pump/bolt regressions
cover both clients and malformed histories. No native count, state or timer
write was added. Earlier291 recorder rejection and292 native failure remain saved.

Both normal x86 and x64 builds pass all 243 suites. A local normal player build,
source archive and checksum-verified headset package are being saved; private
input drivers are disabled there. Nothing is published or launched in the headset.
Prior290's eight-shot SPAS/pump/empty-shell/shoulder/F2000 sequence and279's
native-menu pointer/checkpoint recovery remain separate preserved evidence.

Campaign playthrough/death recovery, rifle-reload interruption across actor
replacement, native pistol/slide and belt-fed loading, M95 manual magazine
loading, broader family enrollment and visual headset acceptance remain open.
Seventeen compiled magazine prerequisites are not seventeen accepted weapons.

Evidence: `pipeline-runtime-20261005/root-monitor/m95-empty293-01/concurrent-timing-review.json`.
Trace: `8271f6b4b477e511b139f603d59aa97bafb24dc468205a80b8ba20adb9a196c3`.
Source: `3e317aa671ebd5c596f33eeb28291651705d0592f8598080e063901d5f7a6071`.
See [final-shot prediction](EMPTY-PREDICTION-293.md) and
[last-round bolt/fallback ownership](archive/LAST-ROUND-BOLT-291.md).

# Local290: combined empty-tube, pump and rifle sequence passed

The actual-game controller-emulation run completed eight SPAS shots and physical
pumps (8/24 to 0/24), inserted a shell (1/23), returned support grip, swapped
shoulder weapons, removed/reinserted the full F2000 magazine (30/210 to 0/210
to 30/210), and fired afterward. Support returned 30-48 ms after pumps and
132 ms after shell insertion. The independent ammunition/callback audit passed.
All pump and magazine presentation samples had zero fallbacks. Hooks drained;
the game stayed alive. This is a bounded native result, not headset or complete
campaign acceptance. Nine recorder lock drops mean full callback coverage is
false; all required shot, hold and completion proofs were present.

The shared fixes preserve controller custody independently of older render
poses, protect in-flight completion reads, inhibit the last-shot automatic
reload, and recognize the directed server -> clientB -> clientA completion
chain. The successful run used local tails; forward routes are covered by exact
failed287/289 fixtures and connected regressions, not claimed as live290 hits.

Both normal player builds passed 243 suites. Normal290, headset290 and a local
source checkpoint are being frozen from the same source receipt. Diagnostic
drivers are disabled in the player build. No publication or headset host launch.
Earlier279 native menu/pointer checkpoint recovery and280 loaded M95 bolt
evidence remain preserved without redundant repeats. Last-round M95, native
pistol and belt-fed reloads, broader enrollment, sustained campaign/death
recovery and headset visual acceptance remain unfinished. Seventeen compiled
magazine prerequisites do not mean seventeen accepted weapons.

Evidence: `pipeline-runtime-20261005/root-monitor/resource-empty290-01/empty-tube-receipt-review.json`.
Source: `fb11a03db956d779a22dc1b50f557ab49cc4ccb2ecf48f5b25dad2cff66d1bc7`.
Trace: `4661e1a2b9a5ebb9ede4ff293bc085b08afbccbe75d411aa9b45b123015f8277`.
See [prediction chain](archive/PREDICTION-CHAIN-290.md) and
[controller custody](archive/CONTROLLER-CUSTODY-289.md).

# Local280: bolt clock overlaps the physical stroke

Both normal builds pass243 suites. Actual ordinary-controller M95 testing
completed two ordered bolt strokes, both custody/support returns and subsequent
firing. Loaded ammunition fell5 to3 with45 reserve unchanged. Native Ready
followed physical completion by51.6ms and40.7ms, compared with2.27/2.28seconds
in277. All four motion phases had paired rendering evidence; no presentation
fallback was recorded. No native timer, state or ammunition edits were added.

The shared gesture contract now distinguishes a frozen native boundary from
three independently guarded native Updates. BC2 can advance its original clock
while firing and reload remain suppressed. Neither native idle nor hand motion
alone completes the operation. Tracking interruption preserves the owed cycle
and requires neutral/regrip recovery. Only the verified M95 adapter enables this
new route; SPAS retains its existing route.

Normal280 and a local headset package are prepared. The279 native-menu checkpoint
restart followed by XM8/SPAS use remains the preceding campaign recovery evidence;
it was not redundantly rerun for280. Last-round M95 behavior, native pistol and
belt-fed reloading, wider weapon enrollment, sustained campaign/death recovery and
headset visual acceptance remain incomplete. No GitHub publication.
See [280 evidence](archive/CONCURRENT-BOLT-280.md).

# Local279: native checkpoint, menu and reload recovery pass

The bounded actual-game test opened the native pause menu, clicked checkpoint
restart, recovered stereo rendering in the new world, and retired the old pump
obligation for the replacement actor. Ordinary controller input then exited the
boat, drew XM8, removed and returned its magazine with 22/191 ammunition
unchanged, and completed a new SPAS pump and shell insertion. Support returned
136 ms after the shell completed. Magazine presentation had zero fallbacks.

Both normal builds pass all 243 suites. The diagnostic now preserves visual
failures without manufacturing a mechanical cancellation: it records mechanical
and presentation results separately, requiring both for overall acceptance.
The earlier278 visual failure remains recorded. Player behavior is unchanged
from278, whose native F2000/SPAS sequence passed through an input pause.

Normal279 player binaries and a checksum-verified local headset package are
prepared. This is a bounded campaign-recovery result, not a campaign playthrough,
death-recovery proof, headset approval or whole-roster acceptance. Seventeen
exact magazine prerequisites are enabled; pistols, belt-fed native reloading,
wider enrollment and last-round bolt handling remain incomplete. M95 has a
measured roughly2.3-second post-stroke native wait. No GitHub publication.
See [279 evidence and limits](archive/DIAGNOSTIC-CONTINUITY-279.md).

# Local278: combined reload/pump test passes through an input pause

Input rearming now has its own generation, separate from persistent equipment.
The body inventory retains item identities across scheduling pauses only after
fresh coherent native reads confirm the same owner, container and item data.
The tube and magazine policies also no longer cancel a submitted insertion
because another native copy's old Update context expired. Current input, native
identity, transfer evidence and original deadlines remain required.

Both normal builds pass all 243 suites. Actual BC2 controller emulation passed
three F2000 magazine cycles, shoulder swaps, interrupted SPAS pump recovery,
shell insertion and subsequent rifle firing. An injected 172 ms pause preserved
the physical magazine identity. Support returned 32 ms after pump completion
and 134 ms after shell completion. Separate regressions reproduce the old
inventory reset and shell/magazine scheduling failures before their fixes.

Native menu pointer interaction and checkpoint stereo recovery also completed,
but the combined post-restart reload check stopped on one expired magazine
render target. It did not complete reinsertion; that full check remains failed.
The renderer missed the target deadline by 3.25 ms, and the diagnostic then
neutralized input. Next: retain the visual failure while continuing independent
mechanical checks. No headset, full campaign or whole-roster acceptance claimed.
No new weapons or GitHub publication. See [278 evidence](archive/INPUT-EQUIPMENT-LIFETIME-278.md).

# Local277: shared timestamp correction; actual M95 cycles pass

The native reload reader now validates a newly published owner snapshot at gate
acquisition, retaining original deadlines and revision checks. A threaded test
fails with the prior timing rule and passes with the fix. Both normal builds
pass all243 suites. Actual M95 controller emulation completed two bolt cycles
with firing-grip return,5 to3 loaded and45 reserve unchanged. The timing race
did not occur in this run; its regression proof is deterministic.

Candidate276 also passed checkpoint restart during owed pump, replacement-player
shoulder draw, XM8 magazine return, SPAS pump and shell insertion. This is a
bounded campaign recovery check, not a campaign playthrough or headset approval.
The M95 still waits about2.3seconds for native completion after its physical
stroke. Native input pauses still share an equipment-generation counter; that
earlier failure is the next target. No new weapon enrollment or GitHub update.
See [277 evidence and limits](archive/OWNER-TIMING-277.md).

# Local276: native tracking-origin recovery and physical pump complete

Both normal builds pass all 243 test groups. Actual BC2 controller emulation
changed tracking origin during an owed SPAS pump, retained the same cycle and
7/24 ammunition, completed a fresh physical stroke, and restored support grip
32 ms after native completion. The focused run had no pump-control rejections
or native owner-read misses. Two preceding rifle magazine cycles also passed.
The focused origin branch is not the full combined-sequence test.

The native menu opened, accepted pointer input, closed and resumed stereo world
rendering in a separate finite check. Headset laser visibility, animation and
hand placement remain unverified. Earlier intermittent reload/input-epoch
failures and startup exits remain open. No new weapon enrollment or publication.
See [276 evidence and limits](archive/TRACKING-RECOVERY-276.md).

# Local275: pump failure reproduced; expanded game test passes

Headset274 failed. Logs show physical grip cancellation at narrow rail limits,
followed by a tracking-origin change cancelling native pump ownership. Candidate275
adds bounded grip retention around the measured rail and an explicit drained
tracking-space handoff that preserves the owed pump and ammunition.

Both full builds pass all243 suites. Restoring the old rail limits fails the new
measured-motion and expanded controller regressions. The actual F2000/SPAS test
now completes three magazine cycles, holsters, an interrupted pump with22cm
rearward/8cm lateral controller motion, a shell, support return in130ms, and
rifle firing. See [275 evidence and limits](archive/PUMP-HEADSET-275.md).

This is a development checkpoint, not headset acceptance. Actual-runtime
tracking-origin rebase and stock-animation/hand-placement checks remain. Earlier
intermittent273/274 magazine and input-epoch failures are retained as unresolved.
No new weapon enrollment or publication; normal275 binaries and symbols are
frozen separately from the private diagnostic DLL.

# Local274: native input-epoch failure isolated; full sequence still fails

Both architectures pass243 suites. Added shell-custody evidence before native
reload admission and first-per-cause magazine cancellation records. Actual-game
emulation verifies two rifle magazine cycles, interrupted pump recovery, one
shell and prompt support return, then fails on the final rifle warmup after a
native input pause advances the equipment generation for the unchanged rifle.
See [274 evidence and next work](archive/CUSTODY-DIAGNOSTICS-274.md).

This is a development checkpoint, not a headset-ready promotion. The earlier273
failures remain unresolved;274 did not reproduce them. Headset272 is unchanged.
Next: distinguish input observation recovery from persistent equipment identity,
then verify the full combined sequence. Native pistol/LMG enrollment, chambering,
last-round bolt behavior, campaign recovery and headset visual acceptance remain.
No new native weapons are enabled, and no GitHub update was made.

# Local273: measured paths integrated; native regression failures retained

The shared magazine consumer now supports measured curved paths, rebased from
the saved agent payload without replacing later recovery/ownership fixes.
Measured M9/MP443 removal, original return and replacement pass the shared-policy
tests with simulated native receipts. This adds geometry support, not native
pistol enrollment or slide/chamber completion. The normal registry remains
seventeen exact magazine prerequisites. See [273 integration](archive/SAMPLED-MAGAZINES-273.md).

Both full normal builds pass243 suites, and101 affected Python tests pass.
Two native F2000/SPAS emulated runs failed: the first at an idle magazine-view
cancellation before any ammunition command; the second after two magazine cycles
and interrupted pump recovery, at shell pickup before starting native reload.
These are unresolved failures, not a passing compatibility regression. Normal273
binaries/source are frozen for development; headset272 remains staged. Prior272
passed its combined sequence;271 passed one campaign checkpoint/actor replacement. Sustained campaign, rifle-first actor
retirement, broader enrollment, pistol slides, belt-fed loading, last-round bolt
handling and headset visual acceptance remain outstanding. No GitHub update.

Historical records below retain their original dates and limits.

# Latest local267: shared recovery, pending native acceptance

Both full normal builds pass240 suites. Family-neutral actor retirement
addresses rifle-first recovery after genuine player replacement, across
the enrolled magazine profiles. This is a shared lifecycle fix, not new
weapon acceptance. See [267 evidence and limits](archive/RECOVERY-267.md).
Frozen264 remains the staged headset candidate and source of the latest
combined game checks. Current maturity is pre-alpha; sustained campaign,
death/restart and broader equipment coverage remain unverified. No public
release or GitHub update was made. Historical evidence follows.

# Latest local264 evidence (published checkpoint remains245)

SCAR/SPAS and F2000/SPAS passed the same frozen264 controller-driven game
sequence, including natural aiming during an interrupted pump, three magazine
cycles, shell insertion, support return at about134ms, and final rifle firing.
Both normal player builds pass240 suites. Shared timing corrections and the
off-body empty-hand recovery fix are included; headset acceptance is pending.
See [264 evidence](archive/RECOVERY-264.md). Seventeen compiled magazine configuration
prerequisites remain distinct from weapon acceptance. Earlier258 M95 evidence
is retained, not rerun. Pistol slides, belt-fed loading, broader enrollment and
scene-reload recovery remain unfinished.

# Historical local252 evidence

F2000/SPAS and SCAR/SPAS each completed the same combined input-only actual-game
run: three magazine cycles across holsters, interrupted physical pump recovery,
manual shell insertion, immediate support and rifle firing. The shared fixes
required no per-rifle geometry change. [Evidence and limits](archive/RECOVERY-252.md).
This validates those sequences in two fresh campaigns; it does not turn17
compiled configuration prerequisites into17 headset-accepted weapons. Ordinary
M95, pistols, belt-fed loading and scene-reload recovery remain outstanding.

# Historical checkpoint245 coverage

See [the current handoff](archive/CHECKPOINT-245-HANDOFF.md):17 compiled magazine configuration prerequisites are not17 accepted weapons. The latest headset test failed; M95, pistols and belt-fed mechanisms remain unaccepted. Older entries below retain their original dates and limits.

# Current weapon coverage — October 8, 2026

## Active integration: checkpoints242–243

The full normal242 build passed229 C++ suites per architecture, with17 exact
magazine prerequisites: scoped XM8 and AEK campaign baselines,12 multiplayer
configurations across M416, XM8 Compact, MG36 and XM8 LMG, two campaign SCAR
configurations, and F2000_sp. These are configuration prerequisites, not17
headset approvals. Multiplayer data does not establish campaign compatibility.
Current243 adds ordinary controller-input testing and first-failure retention.
See [current evidence and limits](archive/CHECKPOINT-243-INTEGRATION.md).

| System | Implemented and observed | Remaining acceptance |
| --- | --- | --- |
| Magazine rifles/SMGs | Shared original return, discard/replacement and conservation. SCAR241 completed three magazine cycles and holster/shotgun transitions. | Current ordinary/headset acceptance across enrolled variants. |
| SPAS shell loading and pump | Actual234 eight physical pumps; SCAR/SPAS241 combined shot, pump, shell, immediate support and return. | Ordinary combined input; tracking interruption recovery and native timing polish. |
| Bolt action | M95242 completed two native physical cycles and custody/support returns with conserved reserve. Ordinary family dispatch/player wiring merged. | Ordinary input-only native run; last round, M95 holsters, interruption recovery and stock timing overlap. |
| Multi-part magazines | F2000 exact assembly and runtime enrollment merged242; actual removal/renderer succeeded. | Original-return View cancellation under investigation. |
| Pistols | MP443 measured hand contact; MP443/M9 reveal a common curved-path requirement. | Sampled rail, exact native admission and slide/chamber route. |
| Belt-fed LMGs | Prepared mechanism/contact data and portable feed policy. | Player cover/feed coordinator and native completion adapter; not completed by magazine-fed LMG profiles. |
| Launchers/attachments | XM8 sight/mode route; native automatic loading retained by request. | AEK horizontal sight and physical loading remain separate. |

Older sections below are historical, including their backend and registry counts.

## Historical227 and225 coverage

Checkpoint227 integrates five permanent family-batch regression suites and
passes212 full suites per architecture. Twelve exact configurations across
M416, XM8 Compact, MG36 and XM8 LMG exercise the common resource consumer with
mock native receipts. Optional reviewed private profiles are preserved under
`profiles/resource-batch227`; ordinary builds still enable only the two prior
magazine registrations. Physical pump and bolt custody implementation does not
yet constitute enabled native pump/sniper coverage. See [status](history/STATUS.md).

Checkpoint225 adds **zero newly enabled weapons**. Its private resource-backed
XM8 consumer now passes actual BC2 simulated-controller removal/original return
and discard/chest replacement, with native command and render-publication
receipts. Headset appearance and additional configurations remain unverified.
Normal builds still use the previous backend. See [current evidence and limits](archive/RESOURCE-PLAYER-223.md).

The detailed extraction audit below records prepared data and earlier consumer
tests. It is not acceptance of that whole batch against the new resource backend.
The225 followup candidate tests12 exact zero-bolt variants across M416, XM8
Compact, MG36 and XM8 LMG through the current shared consumer. Native responses
in that batch are mocked; proposed family-based registration review is separate.

**The current checkpoint integrates two detachable-magazine profiles: scoped
XM8 and AEK971_sp. It does not enable manual magazine reloads for the whole gun
roster.** SPAS shell loading and the scoped-XM8 launcher sight/mode are separate
integrated paths. Wider asset extraction, generated descriptors and offline
consumer tests are preserved, but most are not enrolled in the current runtime.

The follow-up extraction now prepares **21 exact configurations across seven
additional weapon models**. All 21 pass the shared consumer's partial/empty
reload and identity-denial checks on x86 and x64 with mocked native responses.
Their production registrations remain disabled. See the completed variant batch
below; it resolves the 14 geometry gaps found in the initial audit.

The often quoted **35 configurations, 20 two-hand grip records and 10 magazine
contact records are prepared data counts**, including SP/optics variants. They
are neither distinct gun counts nor completed manual-reload coverage. The
35-configuration visibility/holster path is also independent of magazine
registration. Sharing its native operation proof does not admit ammunition,
chambering, pump, bolt or belt-feed operations.

This is a source/data audit of the local 206 composition, followed by a separate
offline coverage-tool correction. It performs no builds, process inspection or
game actions. Final build/native acceptance
belongs to [checkpoint 206](archive/RELOAD-RECOVERY-206.md). Its shared hand-geometry fix
repairs existing interactions; it adds no weapon profiles. No all-gun percentage
is meaningful until exact configurations and independent capabilities are joined.

## Historical206 configuration audit

[Build-Checkpoint.ps1](../Build-Checkpoint.ps1) now selects operation receipt225
and calibration headers202, with private native candidates OFF. The following
paragraph and cache counts describe the earlier206 audit.

The206 build selected the calibration headers
from `profiles/checkpoint202` and the operation receipt from `profiles/checkpoint206`.
The inspected `build/x86-checkpoint206/CMakeCache.txt` and
`build/x64/CMakeCache.txt` agree with these inputs. Cache inspection establishes
configuration. The subsequent serial 206d builds completed successfully with
186 tests passing on each architecture; their frozen evidence is under
`<local-recovery>/recovery206d-build-logs`, with receipt
`pipeline-runtime-20261005/normal-recovery206d-build-receipt.json`. Successful builds do not establish the
identity of a loaded DLL or a fresh native/headset acceptance result.

| Current source/configuration | Count and meaning |
| --- | --- |
| [Native magazine registry](../src/games/bc2/Bc2MagazineNativeRegistry.cpp) | Two enabled builtins: exact scoped XM8 and AEK971_sp descriptors. `BC2_REVIEWED_MAGAZINE_REGISTRY_HEADER` is empty in the checkpoint script and both inspected caches. No wider generated magazine registry is selected. |
| [Magazine geometry lookup](../src/games/bc2/Bc2MagazineGeometryProfile.cpp) | Builtin XM8 calibration plus one selected experimental AEK profile. Native registration, geometry and live owner/configuration evidence remain separate requirements. |
| [Authored grip header](../profiles/checkpoint202/Bc2AuthoredGrips.generated.h) | Two rows: AEK971_sp and XM8_sp_s. Accepted-baseline replacement is OFF, so a row's presence does not mean it overrides the accepted XM8 handling. |
| [Authored support header](../profiles/checkpoint202/Bc2AuthoredSupports.generated.h) | One SPAS12_sp support-contact row. Support-only data is not a new full weapon grip or pump implementation. |
| [Visibility descriptors](../src/games/bc2/Bc2VisibilityDescriptorData.h) | 46 geometry rows, of which 35 have exact configuration paths; 11 lack those backlinks. The selected reviewed operation class can qualify compatible exact rows under live binding checks. Legacy XM8/SPAS handling has its own accepted route. This is not magazine coverage. |
| [Body equipment profiles](../src/games/bc2/Bc2BodyEquipmentProfiles.h) | 19 render-only rows, including aliases. Body-model/cache presence does not establish reload or native hide/show authority. |
| [Sight adapters](../src/games/bc2/Bc2SightAdapter.h) | XM8 sight has `nativeSightAccepted=true`; measured AEK/GP30 subtree profile remains false. The corresponding AEK/GP30 mode family also remains unaccepted. |

The three entries in [Bc2WeaponProfiles.cpp](../src/games/bc2/Bc2WeaponProfiles.cpp)
describe evidence for aim/support/muzzle handling of SPAS, scoped XM8 and its
launcher. That table is not the complete runtime capability catalog: authored
grips, magazine profiles, visibility, body rendering and sights have separate
registries.

The freshly built x86 206d capability output,
`test-temp/compiled-capabilities206.json`, directly confirms **two enabled,
resolved, magazine-ready registrations**, `chamber_known=false`, and 46 visibility
rows with **two `production_stow_admitted=true` and 44 false**. The two true rows
are scoped XM8 and SPAS. This probe leaves its synthetic snapshot's
`operationBinding` empty, so it exercises their legacy admission and does not
establish the configured-operation-class route for the other exact descriptors.
Report that route as untested by this output; do not convert either the old 35
qualification count or the current 44 false results into an all-weapon claim.
The generated `test-temp/weapon-coverage206.json` joins the supplied 35-package
index to this compiled report and finds two compiled magazine prerequisite rows.
Both reports are also preserved in `recovery206d-build-logs`. The report's
`production_stow_profile_not_admitted` wording inherits the missing-binding
limitation above; it is not evidence that the live operation-class route fails.

## Prepared exact-configuration data

`<local-recovery>` below means the local sibling directory
`G:\Unity\VRMMO\bc2vr-recovery`. These private artifacts are evidence, not
distributed game assets or instructions to select an old build.

Direct inspection confirmed:

- `pipeline-audit-20261005/assets/integrated-package-index.json`: **35 rows and
  11 exclusions**, with zero rows marked runtime-enabled. SHA-256:
  `2d997615d5c48422d0284eb76c79796e6d9b6e0cabee372ad731e5d8216b7b9c`.
- `pipeline-runtime-20261005/component-closure-verified.json`: the same package
  set, with **20 two-hand grip and 10 detachable-magazine contact evidence
  records**. All remain metadata prerequisites without native admission.
  SHA-256: `55fff72b8c007effef6fdc6a75ade995c2839d9bf84888e6be6c5c9c57f6b4fc`.

The closure records their exact inputs: the package index, configured visibility
evidence, `runtime-authored-grips/bindings.json`,
`runtime-authored-magazines/geometry.json`, and the SPAS support-only metadata.
The base index alone contains visibility; its name does not mean the later
component records or a runtime consumer were integrated into each row.

| Base asset grouping in this package set | Exact configurations | Joined two-hand grips | Joined magazine contacts |
| --- | ---: | ---: | ---: |
| AEK971 | 5 | 5 | 5 |
| XM8 | 5 | 5 | 5 |
| F2000 | 5 | 5 | 0 |
| SCAR | 5 | 5 | 0 |
| AKS74u | 5 | 0 | 0 |
| AN94 | 5 | 0 | 0 |
| UZI | 3 | 0 | 0 |
| MP443 | 2 | 0 | 0 |
| **Total** | **35** | **20** | **10** |

These are eight asset groupings, not eight fully supported guns. Zero means
absent from this particular exact package closure; separate extraction batches
may already contain useful data. For example, the wider magazine batch below
contains AKS74u/UZI contacts outside this closure. Do not add counts from the two
overlapping sets. A magazine contact also does not supply the visible ammo cache,
native transfer proof or chamber state.

## Where the wider magazine work is, and why it is still disabled

The reusable registry and consumer code is present in the main source. Missing
runtime enrollment is more specific than an unimplemented registry:

1. `<local-recovery>/magazine-registry-followup-120602-candidate/private/InstalledDisabledMagazineRegistry.h`
   contains **63 generated rows, zero enabled**. Its adjacent
   `disabled-registry-receipt.json` records no review input and no native test
   claim. Header SHA-256:
   `1e683e6ce829299d95808e8ddf3d4fdb9908275fbf1511fb5340b746d4889cfa`.
2. `<local-recovery>/automatic-magazine-stock-bolt-20261003-candidate/private/`
   contains `DisabledRegistry.h`, `FixtureRegistry.h` and `MeasuredGeometry.h`.
   Its parent directory's `coverage.json` records **21 descriptor paths across seven asset names**,
   tested on both architectures with real shared consumers but mocked native
   receipts; separate disabled-registry tests reject all 21. New runtime
   admissions are zero. Coverage SHA-256:
   `f55ae31fe1e1116d5ad2e2fefb58da5824f9cb97469b4321bbf5240435ca5137`.
3. `FixtureRegistry.h` deliberately fabricates admission for standalone tests.
   It is explicitly marked **never enable in game**. It is not a reviewed native
   registry and cannot be used to promote that batch.
4. The saved `MeasuredGeometry.h` predates the current exact-path join: it has
   no `configurationPath` entries, and its seven underlying saved JSON profiles
   also lack the `weapon` configuration backlink. Current `MagazineGeometryMatchesConfiguration`
   permits pathless compatibility only for the two immutable XM8/AEK builtins.
   These wider geometry rows therefore need regeneration and exact configuration
   component joins before reuse with today's consumer. Merely selecting the old
   headers cannot complete the integration.
5. The current exporter accepts a reviewed enablement document only for
   `bc2.automatic-magazine-zero-bolt.v1`. It explicitly refuses enabling a
   stock-bolt candidate using that review. Missing live configuration, native
   owner/cycle, render and empty-control evidence remains separate from the
   configuration-path incompatibility.

The seven-name batch divides into these reusable groups:

| Prepared definitions | Exact paths in the batch | Reuse and remaining boundary |
| --- | ---: | --- |
| M416, XM8 Compact | 6 | Automatic magazine, zero-bolt descriptor shape matches the existing reviewed class in principle. Regenerate exact component joins, review the selected native configuration/three-copy lifecycle, then enroll data in the shared consumer. |
| MG36, XM8 LMG | 6 | Prepared detachable-drum contacts and the same zero-bolt dispatch shape. They are magazine-feed candidates; their inclusion does not implement belt-fed LMGs. |
| 9A91, AKS74u, UZI | 9 | Automatic magazine route with nonzero authored bolt fields. Static proof shows ordinary AutomaticFire2 bypasses stock bolt states, while preserving those fields. The broader admission contract and native evidence still need review; no manual charging-handle implementation follows from this proof. |

The wider descriptor audit also marks exact AEK/XM8, F2000, SCAR and other
automatic-magazine definitions as dispatch-compatible where their values match.
That is eligibility for shared review, not live enrollment or a guarantee that
their complete configuration components are joined. Its AN94 and M16/M16k
definitions instead report unreviewed `fltSingleFire` dispatch. Those require a
shared native dispatch-family review; they must not be forced into the automatic
class because their category or visual magazine resembles it. Handguns and
several self-loading rifle definitions have the same unresolved distinction.

See [the stock-bolt evidence and limits](AUTOMATIC-STOCK-BOLT-DISPATCH.md) and
[the exporter](../tools/bc2_magazine_registry_header.py). The review unit is the
native mechanism/configuration family. Reusing compatible authored components
and testing representative families avoids a separate controller routine or a
fresh manual calibration campaign for every gun name.

## Completed offline tooling correction and exact outputs

The current [authored geometry exporter](../tools/bc2_authored_magazine_geometry.py)
already writes `g.configurationPath` from validated `weapon` metadata, checks
profile digests, and rejects duplicate asset/configuration pairs. It does not
need a new per-weapon exporter. The corrected
[bc2_magazine_pipeline_coverage.py](../tools/bc2_magazine_pipeline_coverage.py) now:

1. Joins descriptors to geometry through exact configuration resource/hash/GUID
   and the original `grip_binding_digest` backlink, with selected mesh/LOD and
   rig, skeleton and static-pose evidence checked. It reuses the existing
   component-closure identity/hash helpers; full package resource closure remains
   a separate prerequisite.
2. Recovers a legacy profile's missing `weapon` only from its verified original
   backlink. Preserves the old profile digest as source provenance and computes
   a new derived digest; never stamp three variant paths onto one name match.
3. Reports missing or ambiguous exact joins as unavailable. It accepts original
   source metadata with `--bindings`; legacy `--header` input is provenance only
   and its C++ content is ignored.
4. Passes the validated metadata to the existing `cpp_header` emitter and keeps the
   generated registry disabled. Reports descriptor-eligible paths, exact geometry
   joins, and zero new native admissions separately. Selected-build enrollment
   and native acceptance are explicitly not observed by this offline tool.

The first target is the **12 zero-bolt paths** for M416, XM8 Compact, MG36 and
XM8 LMG. The historical name-joined fixture does not establish that all 12 can be
regenerated from the four saved geometry rows alone. This is a data/tooling task,
not authorization to enable native support.

The corrected tool was run against the saved seven-profile batch and its original
source bindings, writing only a new private directory:
`<local-recovery>/weapon-pipeline206-exact`. Its `coverage.json`, disabled registry,
private mock fixture and regenerated `MeasuredGeometry.h` contain **seven exact
joins, zero enabled production rows**. The immutable input files were preserved.

Root also compiled the generated geometry and disabled registry against the
completed x86 and x64 core libraries. Both standalone checks validate all seven
one-to-one configuration matches, insertion configurations and rigid hand/part
poses, with native registrations still disabled. This is data/compiled-policy
validation; it does not execute the native reload adapter or accept hand feel.

| Result against historical 21-path fixture | Exact joins recovered | Other variants still missing exact geometry |
| --- | ---: | ---: |
| Zero-bolt: M416, XM8 Compact, MG36, XM8 LMG | 4 | 8 |
| Nonzero stock bolt: 9A91, AKS74u, UZI | 3 | 6 |
| **Total** | **7** | **14** |

The seven joined paths are the base paths for M416, XM8C, MG36, XM8 LMG, AKS74u
and UZI, plus the 9A91 Kobra path. The original binding files are
`arsmg-paired-contact-batch-candidate/all/grip-bindings.json` (66 records) and
`lmg-authored-mechanisms-candidate/all/reference-bindings.json` (30 records).
Having more source bindings does not authorize cloning a measured magazine role
onto every configuration using the same asset name. The subsequent extraction
below resolves the 14 variants through their own assets and configuration
bindings. Independent native family proof remains separate.

## Completed exact variant extraction and shared-consumer batch

`<local-recovery>/weapon-pipeline206-variant-extraction` contains fresh extraction
of all 14 previously unresolved variants using the existing paired-grasp
extractor and each variant's original configuration, mesh and animation data.
The serial extraction took about 97 seconds. No hand positioning or controller
state machine was written separately for these weapon names.

The combined output now contains 21 unique exact paths: **12 zero-bolt and nine
stock-bolt candidates**, with zero requested extraction gaps and zero enabled
production registrations. The seven models are M416, XM8 Compact, MG36, XM8 LMG,
9A91, AKS74u and UZI, each with three configurations. The manifest records source
and output hashes and exact reproduction inputs. Independent validation checks
35 file hashes, 14 new profile digests/original bindings and the emitted path set.

Root compiled the existing standalone magazine-consumer batch against the final
21 headers and the current 206d libraries, sequentially on x86 and x64. Both
architectures pass all 21 disabled-registry checks and all 21 mock-native consumer
rows. Each consumer row runs partial and empty magazine reloads, checks transferred
counts, and rejects mismatched path/capacity/bolt timing. `consumer-audit.json`
joins the exact output paths and retains source/library hashes.

These checks execute the shared C++ consumer and measured geometry; native calls
are mocked. They do not test game hooks, live configured meshes, GPU rendering,
headset feel, full package closure or production admission. The current runtime
still enables only scoped XM8 and AEK971_sp magazine profiles. The remaining
integration unit is the native mechanism family and its package composition,
not a new per-weapon interaction implementation.

The new `manifest.json` lists all five data-input hashes, tool/output hashes,
the seven exact and 14 unresolved paths, and reproduction arguments. SHA-256:
`daa24de33004267e3593fc63b9faefff9da550c7b8bf7f454f15d0bdcdb1e9d4`.
These are content hashes, not signatures. The baseline regression failed with
two name-joined fixtures where zero were justified. After correction, 89 focused
Python tests passed, including corrupt/missing bindings, configuration/mesh/LOD/
rig/pose changes, ambiguity, exact variant separation, preserved source metadata,
disabled output and CLI input-overwrite rejection. No generated header was built
or selected by a native checkpoint. `focused-python-tests.log` in the new
artifact records the completed test-output transcript.

A complementary report change should distinguish legacy stow acceptance from
configured-operation-class acceptance and label absent synthetic operation
binding as untested. This prevents a report from implying either 35 complete
guns or 44 definitively unsupported configurations.

## Mechanism readiness and the next integration step

| Mechanism/category | Integrated or prepared now | What brings the next family online |
| --- | --- | --- |
| Detachable rifles/SMGs | One persistent removal/original-return/replacement consumer; XM8 and AEK profiles selected. Wider batches above are disabled. | Finish the shared recovery regression, regenerate exact package/header joins, supply an independently reviewed native family admission, and exercise partial/empty reload plus equip/pickup/holster transitions through the same consumer. |
| Tube/shell loading | SPAS has actual shell-supply, insertion, native hold/transfer and presentation consumers. | Bind other exact shell-feed configurations to that shared path where semantics match. Prove their native entry/transfer/cleanup; a shotgun label does not select tube loading or pumping. |
| Pump action | Portable `WeaponCycle` pump recognition and a bounded SPAS `PumpPart` presentation planner exist. They are not an ordinary gameplay pump consumer. | Wire the shared after-shot/after-feed action requirement, exact mechanism claim/part pose, fire-readiness gate, native completion and cancellation. Use actual chamber knowledge rather than loaded-count guesses. |
| Bolt-action rifles | Ordered unlock/back/forward/lock policy exists offline. | Supply the native action/chamber/readiness adapter and wire it to the weapon's independently selected feed. Magnified optics remain a separate capability. |
| Magazine-fed handguns/self-loading rifles | Magazine infrastructure is reusable; MP443 has two configuration/visibility rows in the package set. That does not grant handgun reload. | Review the correct native fire/reload dispatch family and bind existing/extracted components. Requested pistol slide/rack/release and chamber behavior need their own shared recognizer and conserved-ammunition/native-ready contract. |
| Revolver/other sidearms | No integrated cylinder/round mechanism established by this audit. | Bind their actual feed/action model; do not treat every sidearm as a detachable-magazine pistol. |
| Belt-fed LMGs | Authored parts/resource work and portable `FeedMechanism` ordering/contact queries exist. | Add the production cover/latch/feed/container interaction coordinator, resource accounting, native hold/commit/cancel and readiness acknowledgements. A query result or `rtMagazine` transfer category does not prove a detachable box-magazine mechanism. |
| Scoped-XM8 underbarrel | Sight manipulation/mode changes are integrated; ammunition remains the native attachment's own pool. | Preserve this baseline. Manual underbarrel reload remains deferred; stock reload is the current scope. |
| AEK/GP30 sight | Measured horizontal hinge, generic subtree/finger presentation helpers and exact family data are present; native acceptance flags are false. | Complete verified family dispatch/current-owner publication, paired subtree/hand presentation and release/equip/reload transition checks. No new per-gun sight state machine is needed. |
| Standalone launchers | Shared supply/insertion/request primitives and authored research are reusable; no admitted general manual launcher consumer is established. | Bind the actual one-round/breech mechanism, independent pool and native completion. This is distinct from rifle magazine or SPAS shell loading. |

`WeaponMechanism`, `WeaponCycle` and `FeedMechanism` being compiled/tested types
does not mean BC2 gameplay instantiates the full mechanism workflow. The current
native loaded count does not establish a separate chamber count. A tactical
reload retaining a chambered round and an empty reload must eventually select
different action requirements without inventing rounds or capacity-plus-one.

## What the acceptance evidence actually covers

- [202](SIMULATED-PLAYER-202.md) and [204](archive/COMBINED-INVENTORY-RELOAD-204.md) establish
  bounded actual-game XM8 original return/replacement and the combined XM8/SPAS
  shoulder/chest sequence. The latter passed its strict audit with conserved
  ammunition and no consumer cancellation or magazine palette fallback.
- [203](archive/RELOAD-TRANSITIONS-203.md) and [205](archive/RELOAD-RECOVERY-205.md) add persistent
  transition and interruption/recovery coverage with mocked native/renderer
  boundaries. A synthetic second profile is not a newly accepted weapon.
- [200](archive/SIMULATED-PLAYER-200.md) records two actual SPAS shell insertions with
  conserved ammo. That run's overall audit still failed the separate ordinary
  palette-fallback coverage requirement. Shell insertion is not manual pumping.
- [206](archive/RELOAD-RECOVERY-206.md) records the delayed-neutral-geometry reproduction
  and live recovery work. The instrumented pre-fix run completed the interaction
  chain but had two display-transfer timeouts; it is not a strict combined pass.
  The subsequent serial 206d x86/x64 builds each passed all 186 tests. This
  completes offline build validation; fresh corrected-build native recovery and
  human headset acceptance remain separate from that result.
- The latest recorded human headset feedback remains failed rifle reloads and
  unrequested draw in 193. Later native/CPU fixes do not replace a new headset
  acceptance check, nor establish every gun, pickup, vehicle or empty-ammo case.

The historical 183 status in [WEAPON-SYSTEM-PIPELINE.md](WEAPON-SYSTEM-PIPELINE.md)
and the old active-agent queue in [WEAPON-MECHANISM-NEXT.md](WEAPON-MECHANISM-NEXT.md)
are not current operating status. The inspected private `ACTIVE-WORK.json` was
a 205 snapshot; its old paths, next steps and process/agent fields must not be
used to infer what is running now. This audit reports no active process or worker.

## Reproduce the configured-binary report

The root ran the report successfully using the 206d build and the exact index
below. `Build-Checkpoint.ps1` now also emits the raw compiled-capability report
automatically. These manual probe/report commands are provided for reproduction
and were not executed by this audit. Use a fully completed matching build; the
probe opens no game process.

```powershell
$sourceRoot = 'G:\Unity\VRMMO\FrostbiteVR'
$coverageBuild = Join-Path $sourceRoot 'build/x86-checkpoint206'
$packageIndex = 'G:\Unity\VRMMO\bc2vr-recovery\pipeline-audit-20261005\assets\integrated-package-index.json'
& (Join-Path $coverageBuild 'BC2WeaponCapabilityProbe.exe') |
    Set-Content -LiteralPath (Join-Path $coverageBuild 'compiled-capabilities-current.json') -Encoding utf8
py -3.12 -B (Join-Path $sourceRoot 'tools/bc2_weapon_capability_report.py') `
    --package-index $packageIndex `
    --registry-receipt (Join-Path $coverageBuild 'compiled-capabilities-current.json') `
    --cmake-cache (Join-Path $coverageBuild 'CMakeCache.txt') `
    --out (Join-Path $coverageBuild 'weapon-capabilities-current.json')
```

The standalone probe reports its actual linked magazine registry and synthetic
visibility/holster policy inputs. Its cache metadata records selected header
paths/hashes; it does not prove all NativeProbe-only grip/support headers were
consumed by that standalone executable, or that a deployed DLL matches it.
Refresh the package index and its separate component closure when adding newly
joined components. Native transaction and headset acceptance remain separate
evidence fields throughout.
