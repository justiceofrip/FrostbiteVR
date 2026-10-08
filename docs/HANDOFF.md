# Receiving agent: checkpoint245 GitHub handoff

Read [CHECKPOINT-245-HANDOFF.md](CHECKPOINT-245-HANDOFF.md) first. It includes public build commands, failed headset results, next work and links to separately saved unmerged agent payloads. Local evidence paths below are historical references, not downloadable repository files.

# Checkpoint245 FAILED headset acceptance

User reports SPAS shot locked holstering/reloading, then they tried reloading
the mission checkpoint and the game crashed. Game53440 and host53436 have
exited. Do not relaunch245 or describe it as ready. Evidence is preserved at
local checkpoint245-local/headset-failure/failure.json. Native final report and
crash XML are empty; surviving frame/input telemetry has no pump state, so the
first cancellation and exception location are unknown. Native probe thread
returned0x80000003; this is not a stack/location. Scene-change/rebuild events
preceded exit. Pump Cancelled remains action-blocking in the current service;
that confirmed recovery gap fits the lock but is not a proven onset.

A postrun launcher-only correction now keeps completion.json when optional
crash-XML copying fails. Locked/readable fixture checks pass. Frozen player
binaries and source archive remain preserved. No new native run was started.
Prior successful scripted mechanism sequences do not override failed human
acceptance. Next: interrupted pump/holster/shell replay and durable first-failure
evidence; checkpoint-reload crash is a separate investigation.

# Active245 headset checkpoint

Headset245 is running for the user with F2000_sp/SPAS. Leave it running until
the user finishes. The player-only build passes230 suites on each architecture;
source and binaries are saved in the local checkpoint245. Ordinary F2000
magazine cycles/holsters/SPAS pump/shell/immediate support completed. The final
driver assertion expected one rifle shot but native server and both clients
fired two during its trigger pulse; preserve the failed overall status. M95
ordinary Unlock remains unresolved and is excluded. No new feature merges or
heavy builds during this headset session. Read ACTIVE-WORK.json for exact live
PIDs, session/stop-file path and stage. Collect feedback/logs before closing
the checkpoint. Sampled rails/recovery/pistol work remains isolated.

# Active244 player checkpoint

Read [CHECKPOINT-244-PLAYER.md](CHECKPOINT-244-PLAYER.md) first. User requests
a headset check and checkpoint next; hold further feature integration. Both244
ordinary diagnostic variants passed230 C++ suites per architecture; tooling874
passed/1skip. M95 ordinary244 cleared startup then failed native Owner, before
completion. F2000 ordinary244 has not yet run. Player-only build is running.
No headset-ready claim yet. Root owns native actions; ACTIVE-WORK.json contains
current process identity and frozen artifact pointers.

# Active243 integration — continue emulated and actual-game testing

Read [CHECKPOINT-243-INTEGRATION.md](CHECKPOINT-243-INTEGRATION.md) first.
Current source adds ordinary input-only M95 testing and first resource-failure
retention. Full243 builds are running. Normal242 passed229 C++ suites per arch;
the last frozen normal receipt remains240. M95 physical242 completed both native
cycles and grip returns; SCAR+SPAS241 completed the combined sequence. F2000242
removed its magazine then failed View validation during original return.
No headset-ready candidate. Root owns native actions; agents stage isolated
convergence, ordinary-controller/audit and measured resource-path changes.
Use ACTIVE-WORK.json for the current process identity, never these historical PIDs.

# Historical239 integration

Read [CHECKPOINT-238-INTEGRATION.md](CHECKPOINT-238-INTEGRATION.md) for the latest
native results and limits. The full normal238 build passed225 C++ suites on
both architectures. Current239 includes shared support ownership processing-time
and completed bolt placement fixes. Combined20 focused suites passed both
architectures; full normal239 and actual combined acceptance remain pending.
The older sections below are historical checkpoints.

Root owns game actions. Current campaign:25524,
start2026-10-08T21:28:29.5185683Z, left monitor, combined239 diagnostic.
No active headset session. Pump agent owns ordinary pump boundary review, bolt
agent ordinary custody/dispatch/wiring, resource agent rigid magazine assembly
and authored finger pose selection. Three isolated tasks, no main-source edits.
SingleFire proof is disabled; ordinary pump activation remains default off.
# Active236 native failure — continue debugging, not headset ready

236source`2c43c5617512d80d6809ff1090b11dcec14ccc1c99787889afc0d495893a1de6`.
Merged5-file pump tracking recovery and corrected root combined startup order
(after EnableBodyHolsters). Private combined20suites/both architectures passed;
full normal latest235 remains221suites/both. Frozen resource-pump236-private.

Actual root-monitor/resource-pump236-01: first rifle two magazine cycles passed,
shoulder SPAS draw and realshot succeeded; pump nestedphase6 timedout (failure8),
combinedfailure19. Native held allthree; no release/Ready. Physicaltargets1,
rigattempt1/reachreject1/pairs0; driveroriginalraw609/poses434. Pumpagent owns
combined conflict fix and audit extension. Rootdoesnotclaim combinedsuccess.
Mod detached cleanly; process43912/start2026-10-08T20:11:19.8531357Z responsive,
SPAS selected, nativeprobe DLL retained; freshprocessneedednextattach.

M95stock235 actual capture PASS:109rigsamples incl28 stable nativebrackets;
full148mm bolttravel and measured .925s authored hand contact match. Bolt agent
froze physical-bolt2355newfiles then develops actualM95service/fixture. Rootstaged
root-bolt237-wiring (notmerged) and StageBoltWire237.py: config1192, newexplicit
physical-bolt CLI/neutralreceiver, CMakeprivateoption. Agentagreed validator
Bc2M95PhysicalBoltDiagnostic.h and EnablePhysicalBoltProbe(cycles1..2).
Do notmerge wiringuntil agent declarations/header and servicepayload exist.

Resourceagent now generated SCAR_sp/_s pairedgeometry via shared extraction rules;
SP registry/body/identity payload pending.12MP profiles alreadymerged are notSP.
Keep source236 immutable for frozenreceipt unless snapshot/binarysourcepins are
preserved. No needretest unrelated234completedtrials. Continueuserlarge-test goal.

# Current235 composition — full normal builds passed

Both complete architectures passed221 C++ suites. Source digest:
`2217a381a9fdb1c4c4565608af07f2ab10a7a4dbe5266423bc0396f5c884b9f1`.
Frozen normal binaries: `<local-recovery>/normal235-combined-family-20261008`.
Ordinary resource magazine/shell/body integration and12 additional exact MP
configuration data rows are compiled. These12 are not campaign SP coverage;
SP SCAR/F2000 data extraction and authored contact work are separate and ongoing.
Normal native pump/bolt admission remains off pending actual combined evidence.

Merged ordinary pump/shell/support coexistence, finite combined driver (pump,
shell/support, shoulder return, rifle fire), neutral held-start receiver, and
M95 bounded shot-window rig capture. The first235 link failure was a pure helper
in the hook runtime; its unchanged definition is now inline in the shared header.
One holster test fixture lacked exact configuration-path evidence; corrected the
fixture, retained strict runtime matching. Earlier failed build/test logs remain.

Actual M95 stock234 test passed one original shot,5/45 to4/45, all-three native
cycle transitions and clean restoration. Its late ring lacked moving rig samples;
235 capture is being built to retain32 pre-shot plus320 post-shot frames.
No physical bolt or headset acceptance is claimed from the stock firing test.

Current root-owned BC2:46060/start2026-10-08T19:55:52.4949256Z.
Background campaign loaded; EngineSetup observe/setup passed and left boat with
one internal Use edge. Harness4 observe/equip passed one original constructor
call and all-three M95_sp convergence. No desktop input. Game is idle with M95;
source235 stock-shot capture attach has not started. Exact pointers are in
`root-monitor/sniper235-residency-01.json`, expire on process restart.

Next: freeze/run235 M95 capture; compile/run235 resource+pump combined trial;
use Harness4 plus exact SP selectors for further rifle family checks; integrate
separate pump interruption recovery and physical bolt adapter when measured.
Root alone owns game actions; three agents stage nonoverlapping isolated payloads.
Continue through this checkpoint: user explicitly requests a large combined test,
not a stop at the magazine classes. No headset requested until substantive bundle.

# Latest234 actual result —eight manual pump strokes completed

Actual physical-pump234-01 completed eight shots, eight physical strokes, eight
native acknowledgements and eight ordinary held-support returns. Read-only
postflight while the mod was active observed0/24. Receiver240pairs/0timeouts,
all helpers0, responsive BC2, hooks disabled. All explicit shots and native tails
are present. Trace completeness remains unverified:7 begin-lock drops,5 deferred
ends recovered and one held event without a row (native2996 vs recorded2995).
All2995 recorded holds restored exactly; no in-window read misses. Preserve
initial audit and corrected owner-state audit (soldier flags3→11 are gameplay
state; resolved pointers and inventory layout bit did not change).

Final234 source is9810b6f9fbe03eae4537cfc714bd311ae1ab3fc9074088ba383a028948fa1cbb.
Pump and isolated M95-stock variants each passed24suites on both architectures.
Frozen receipts: <local-recovery>/pump234-private and m95-stock234-private.
The stock fixture's wire validator was split from typed engine configuration;
NativeProbeConfig is1188 and ABI regression tests were updated. No headset claim.

Current45740/start2026-10-08T19:28:09.6124455Z is a fresh campaign, seated in boat,
with no NativeProbe attach. Root owns native runs and stockshot launcher; bolt
agent stages separate engine-input exit/setup so it does not consume an attach.
Twelve-family resource/body payload is frozen outside main; ordinary pump/shell
coexistence is still staged. Integrate only after234 native source checks finish.

# Historical234 preparation

Continue autonomous development and actual BC2 simulated-controller testing;
the user wants a substantial combined headset build with pump and bolt actions.
No desktop input, other games, SteamVR or GitHub actions. Normal227 remains the
last full build (212 suites each architecture). Earlier headset212 is not a new
candidate. Source234 is pinned to
0cd56fac1d413374394e1b9b3d190ec356d7891fac29d53d59c08db5d2669fee.

Actual physical-pump233-01 completed seven pump cycles and eight shots. The
eighth native release reached its reviewed empty6→1→2 tail, then client0 entered
stock automatic reload2→10 at native invocation9786 (record id9785). The finite
pump uses combinedFamilies=false, which prevented Step from reading its empty
control context. The scoped nativeCycleTarget admission fix retains all exact
owner/config/context/write-restoration checks; original-machine replay reproduces
the old failure and corrected empty idle. Actual234 repeat is pending.

Do not equate recorder id with native invocation: one recording-lock drop shifted
them in233. The corrected audit joins explicit lineage and per-thread order.
Eight-shot postflight now reads stable0/24 while the mod is still active. Once
detached, original automatic reload legitimately resumes; record that separately.

M95 provisioning3 has actual observe/equip/restore evidence from the reviewed
outer0x6b0830 boundary. It replaced the selected rifle once, converged all three
copies, restored original configuration and removed hooks. Constructor restoration
is not ammunition rollback. M95 stock-shot234 fixture is integrated: exactly one
finite trigger pulse, no native hold or manual-bolt admission, no retry after an
evidence gap. Thirteen ControllerActions groups pass separately; the main build
composition is in progress. ABI1188 requires trace and DLL from one build.

Agent ownership: pump ordinary shell/body/resource coexistence; bolt measured
M95 action plus finite original-shot setup; resource twelve exact family profiles,
whole-weapon visibility and body geometry. All stage outside main. No main source
changes during frozen native runs. Current game39940/start2026-10-08T19:03:06.1921232Z
has detached233 and needs fresh launch. Root prepares23424-suite private builds.

# Historical232/233 preparation

Actual physical-pump232-02 PASSED two shots8/24→6/24, two complete controller
strokes, native firing tails, acknowledgements and held support return.749
restored holds across branches249/249/251; independent audit also passes all15
checks. Trace SHAae081a61f7f2cdbda48e2ac2f1d82498c0239cd7910a8c9b01f9aae65f2b8cd2.
This is a bounded normal-loaded pump result, not headset or last-round acceptance.
First232 attempt stopped before injection while campaign was still loading.

Resource-inventory232-01 completed ordinary persistent rifle→SPAS→rifle consumers:
6 native magazine calls/6 completions, global7/7 including discard; SPAS shot and
one shell7/24→8/23; rifle ends30/161 after full-magazine return. Independent v2
audit verifies6 authorities+18 branch Updates and the shell transfer. Its overall
status remains inconclusive for6 untimestamped recording-lock drops and34 read
gaps at the two equipment transitions; no operation-phase read misses. V1 audit
incorrectly required server links in client rows and held shell state at capacity;
preserve both reports. Native sequence completed; do not claim blanket recording
completeness. Source232 private builds both passed20 suites per architecture.

Current process42428,start2026-10-08T18:43:55.4138724Z is on foot withXM8, detached
resource232. Sniper observe01 made0 calls/0 seat callbacks; observer02 found3465
native Updates,297 owned outer/inner/server calls, still0 proposed695ed0 seat
callbacks. Hooks restored. The game is visibly active; no focus issue established.
Agent investigates actual outer caller0x697b44/inner0x6b0b21 before provisioning.
Fresh typed M95sp asset121178464,data911171200; scoped124548736/436401328.

Root owns unpinned233 eight-shot fixture/CLI/receiver/audit expansion;8 simulated
driver groups pass both architectures,9 audit groups pass. Pump agent stages
last-round6/5/1→1→2, native-only submitted completion retention and60s recorder;
combined shell/body admission follows separately. Resource agent builds exact
family enrollment and whole-weapon visibility, bolt agent actual provisioning
and recovered M95 format17 measured motion. No source pin/build233 yet.

# Previous232 preparation

Continue autonomous implementation and actual BC2 emulation through a substantial
combined test, including pumping and bolt actions. Compaction is not a stop.
Normal227 remains the last full build,212 suites per architecture.

Private231 pump and resource builds each passed20 focused suites per architecture.
Actual pump231 recorded904 branch-local holds after a real8/24→7/24 shot without
the old concurrent-callback cancellation, but failed at Grip: ordinary support
acquired the hand before the delayed pump renderer input. Root reproduced this
by mirroring actual support acquisition in the driver fixture, then fixed transfer
on the exact original press and retained live history.24 shared cycle groups and
8 faithful driver groups pass both architectures. Native completion is pending.

Actual resource-inventory231 never armed: its setup selected40mmgl. The corrected
wrapper uses Previous(-1), as proven225 did. Cached reload state is not current
selection authority. Bounded input logging now preserves every phase/action/owner
change without periodic terminal-neutral overflow. Failed reports remain intact
under root-monitor/physical-pump231-01 and resource-inventory231-01.

Source232 is pinned to
c435e0daf98faf394e6f65634e3573a7946cf6743fc5ca56101268251dbd0d05.
Private pump232 passed20 focused suites per architecture and is frozen under
<local-recovery>/pump232-private. Resource232 builds and native runs follow.
Do not edit source during receipt checks. Agents stage last-round pump/recovery,
independent combined evidence audit, and concrete x86 sniper provisioning outside
main. Current BC2 PID42928,start2026-10-08T18:28:15.4211678Z has detached231 probe;
test-temp/RestartPump232.ps1 guards that identity for a fresh process.

# Historical work after checkpoint230 — native overlap and support transfer

Continue autonomous implementation and actual BC2 emulation. The user requests a
large combined test including pumping and bolt actions, not another isolated AR
headset loop. Root controls BC2 only; no desktop input, SteamVR, other games or
GitHub updates. Compaction is not a stopping point.

Normal227 remains the last full x86/x64 build:212 suites each. Private230 source
`b3f72d33a27766641a1ba329830a56d9e2722e4fcb9a2e3311dec528f3cb4ad7`
passed16 focused suites per architecture; receipt in
`<local-recovery>/pump230-private`. Ordinary resource activation and retained
terminal outcome settlement across shotgun/vehicle selection are integrated;
the private pump test explicitly disables the resource backend.

Actual229 and230 pump trials failed after one real shot (8/24 to7/24), before
any physical pump completion. Source230 preserved first cancellation evidence:
Owner failure after Restore1042. Updates1043(client) and1044(server) overlap,
which the global quiet/revision guard incorrectly turns into Missing. The
recorded1042 is the prior callback, not the violating callback. Pump agent is
building per-firing callback exclusion plus actual-invocation diagnostics.
No in-trial owner-read or controller rejection explains that cancellation.
Both failed reports remain preserved under root-monitor/physical-pump229-01
and physical-pump230-01. Helpers exited; BC2 remained responsive.

After230, root integrated the finite driver's held-support-return proof and
fixed shared PhysicalWeaponCycle support transfer from an exact renderer N-1
claim. The new realistic regression failed before the fix;23 groups now pass
both architectures, including stale/restamped evidence rejection. The arbiter
still verifies original history, ownership and live bounds. These source edits
are not yet pinned or fully composed.

Resource agent is staging a combined persistent rifle/shoulder/shotgun/rifle
fixture. Its first isolated run passes6 resource commands,1 shot,1 shell with
conserved rifle30/161 and SPAS8/23; native behavior is mocked there. Bolt agent
is building verified native provisioning: root's read-only
root-monitor/sniper230-residency-01.json found stable stock M95_sp/M95_sp_s
Asset+Data/config resources in the actual map. This proves residency only,
not inventory grant or bolt operation. No native provisioning call yet.

Last observed BC2 PID43780,start2026-10-08T18:04:23.4883762Z; detached230 probe
still mapped. Fresh process required for next native injection. Use exact
identity checks and existing hidden background campaign start on left monitor.
Current agents: pump_mechanism222(callback concurrency),resource_driver222
(combined persistent equipment fixture),bolt_mechanism222(native sniper setup).

# Historical work after checkpoint 228 — actual pump trial exposed input ordering

The user requests sustained autonomous implementation and actual-game emulation
until a substantial combined test includes pump/bolt work. Continue after context
compaction; no headset is needed for these checks. Do not touch other games,
SteamVR, desktop mouse/keyboard or GitHub. Root alone controls BC2 processes.

Normal227 remains frozen with212 suites passing each architecture. Private228
source`cbb6077b4de04e988ee8743f7e9c9f5004ebec91f690ddd1b932f73c9b113357`
passed12 focused suites each architecture, including180 repeated pump service
cycles across branch-order permutations. It includes timestamped native read
misses and the bounded existing policy lock. These are separate from normal227.

Actual`root-monitor/physical-pump228-01` FAILED: the driver requested Fire for
100ms but Gather had already copied/mapped neutral action input. All18 own native
Updates during that interval had Fire clear; SPAS stayed8/24. No physical pump
stroke/hold/release completed. The receiver also overshot240 to257 pairs.
All26 journalled read misses preceded the baseline. Independent evidence is
preserved at`<local-recovery>/pump228-audit-candidate`.

Root now defers action mapping until pump Prepare and tests through the actual
ControllerActions mapper, including the failed pre-mapping order. Receiver fix
is integrated. Ordinary resource activation plus terminal-outcome settlement
across SPAS/vehicle selection is being merged by the resource agent. Pump agent
stages support-hand continuation after a verified pump, with fresh renderer
evidence after release; bolt agent investigates native sniper provisioning.
These later source edits are not yet a verified combined build.

Last observed BC2 PID44404, start2026-10-08T17:36:29.9004043Z, responsive with
detached228 probe. Fresh process required before another injection. Reverify
identity and use the existing hidden/background campaign startup on left monitor.
No active diagnostic helpers or headset session at this checkpoint.

# Historical checkpoint 227 — combined emulation work continues

The user explicitly requests continued autonomous implementation and testing
until a substantial combined headset test is available. Do not stop at a small
build checkpoint or ask for a headset to check deterministic state logic.

Current source227 is pinned to
`ff94b1e375b94b365ad406862b79c3e4588ea8d7b3f67a3b196ccf84c33b8517`.
Full x86/x64 builds passed212 suites each and are frozen under
`<local-recovery>/normal227-mechanisms-family-20261008`. Source228 work now
adds timestamped read misses and bounded native-cycle policy lock recovery;
these later edits have not yet been built. Agents stage ordinary resource
activation and the finite native pump controller fixture outside main.

Actual private resource225 run `root-monitor/resource-hands225-01` passed both
original magazine return and discard/chest replacement in BC2:22/191→0/191→
22/191→0/191→30/161. Four actual native commands, independent callbacks and
hand/render publication receipts;240 stereo pairs, zero timeouts, responsive
game. Its independent v3 audit passed. This is scoped XM8 native evidence,
not headset appearance or acceptance of other configurations.

Pump226 run `root-monitor/pump-capture226-03` has valid external baseline and
postflight:8/24→6/24 after two shots. A350ms diagnostic hold applied/restored
65 original Updates across all three branches[22,22,21]; game stayed responsive.
The separate independent audit preserves aggregate observer read-miss limits.
This is a calibration hold, not a controller-operated pump completion.

Source227 integrates actual Gameplay/Rig physical pump contact/palette paths,
fixes service handling of post-shot prediction Restore and rollback, and tests
the exact release interleaving. Private pump activation remains OFF. The pump
agent is building a bounded controller-driven native fixture in isolation.
The configurable shared hand roles and bolt custody coordinator now exercise
measured M24/SV98 pose sequences. Native sniper control/closed-stop binding is
still missing; the bolt agent is investigating it and no-focus provisioning.

Five permanent batch suites cover12 exact configurations across M416, XM8
Compact, MG36 and XM8 LMG. They include144 magazine cycles/support regrips,
36 pickup/context transitions and144 malformed configuration rejections.
Native responses in these family suites are mocked. Reviewed private profile
headers and measured geometry live in `profiles/resource-batch227`; no ordinary
build enables them automatically. Mock registry data lives only in tests.

Last observed BC2 PID44824, start2026-10-08T16:56:45.3056842Z, contains detached
226 diagnostic. A fresh process is required before another native injection.
Re-read identity first. Only BC2 may be restarted; use the left monitor and
verified background campaign helper without desktop mouse/keyboard input.
The headset212 stage is unchanged. No GitHub publication is authorized here.

# Historical checkpoint 225 — pre-verification notes

Current work is autonomous simulated-controller testing, pump mechanics and bolt
mechanics. Do not ask for another small headset test. The user requested a broad
combined test after substantial implementation and bug discovery.

Checkpoint225 is pinned to source
`51d793daebb6f4aabba148ce1eae969a6c73895e71c4413fcab17ea61233e7eb`.
Its private x86 build passes23 focused suites; full x64 is building and normal
x86 has not yet run. Preserve source while builds/native traces are active.

Actual run224 removed XM8 ammunition22/191 to0/191 through one completed native
operation, but the physical consumer canceled31ms after grasp. Offline replay
reproduced a completion-publication race: completed ledger state was exposed
with pre-operation ammo counts. The225 service retains its bounded dispatched
view until an actual matching fresh selected snapshot arrives. Runtime Select
failure republishes that retained view rather than clearing it. Native retry
is pending. See RESOURCE-PLAYER-223.md; no live reload success is claimed yet.

Native pump service and sniper cycle evidence are integrated with private pump
activation OFF. The pump receiver can exit a verified boat before the existing
two-shot calibration diagnostic. Pump/bolt cycle machinery passes isolated
tests, but full hand/rig/native integration and headset acceptance remain open.
Three isolated agents own pump integration, bolt animation/native evidence, and
resource driver/profile expansion. They must not edit main during root builds.

Root restarted only BC2 for the next check: PID38340, start
2026-10-08T16:28:55.0317038Z; startup Continue helper resident, no new mod attached.
Always reread process identity before acting. Last full normal pair is223
(201 suites each), frozen in local recovery. Private224 and normal x64-only224
are separate evidence; do not describe them as full normal224 verification.

# Earlier checkpoint 222 — magazine seat publication boundary

Read [222 reproduction and fix](RESOURCE-SEAT-HANDOFF-222.md). The actual BC2
resource consumer lost its attached magazine target before server admission
and again during delayed completion. Exact submitted-seat presentation bridges
those boundaries without completing ammunition early. Tests cover both enrolled
profiles and immediate support admission, plus malformed/rejected/expired
requests. Native Updates remain mocked; the new live player path is unverified.

Full normal x86/x64 builds pass all 198 suites each. The private x86 candidate
passes 12 focused suites. Both are frozen under local recovery
`normal222-resource-adapters-20261008` and `resource-adapters222-private`.

The existing physical simulated-player driver assumes animation-held counts and
completion. It must use explicit resource state before it can validate this
private candidate. Do not run its old assertions against the new path and claim
success. In-flight cancellation and full gameplay arbitration remain separate.
The headset stage is unchanged, no gun was added, and BC2 PID39304 was observed
responsive with no active mod; no process controls or injection ran here.

# Earlier checkpoint 221 — BC2 adapter connection

Read [221 implementation and limits](RESOURCE-ADAPTERS-221.md). The private x86
resource-hand candidate builds and passes 12 focused suites, including the same
BC2 physical consumer with both XM8 and AEK profiles. Native Updates are mocked
in those consumer tests. The exact private binary/source receipt is
`<local-recovery>/resource-adapters221-private/build-receipt.json`.

Both full normal builds pass **198 suites**. Their immutable binaries, test
reports and source receipt are in
`<local-recovery>/normal221-resource-adapters-20261008`; the combined evidence
is `<local-recovery>/recovery221-evidence.json`. All private ammunition switches
are OFF in these normal builds. This compilation and regression result does not
promote the private native candidate or change the headset stage.

The new API connects the gather consumer, bounded request/result channel, native
service and resource-custody render publications. Native dispatch only occurs in
the owning server Update; the old finite two-operation diagnostic is disabled
in this candidate. The normal checkpoint script selects profile221 and keeps
`BC2_AMMO_RESOURCE_HANDS=OFF`. The headset212 stage remains unchanged.

Do not call the new path native/headset verified or all-guns ready. Next work is
retained outcomes across in-flight context changes, render handoff continuity,
full gameplay arbitration and actual simulated-controller checks in BC2. No new
weapon registrations were enabled. No game injection, agent delegation or
GitHub publication occurred in this checkpoint. BC2 PID39304 remained open with
the previously detached private DLL; a fresh process is needed before attach.

# Earlier checkpoint 220 — resource inventory and hand/rail contract tested

Read [220 implementation and limits](RESOURCE-HANDOFF-220.md). Both full builds
pass **196 suites** with profile `checkpoint220b`, geometry 202 and private probes
OFF. The normal frozen build is `normal220-resource-hands-20261008`; evidence is
in private `recovery220-evidence.json`. The immutable headset212 stage and the
published fork snapshot remain unchanged. No new agents or headset test ran.

The new `AmmunitionInventory` stores one ledger per stable weapon lifetime,
consumes one-shot intents and routes late completion to the correct item.
Fifty synthetic items test returns, discards, replacements and selection.
`DetachableMagazine` now runs the existing hand claims and magnetic rail with
an explicit resource backend. Its tests cover original returns with zero,
partial and full magazines, chest replacement, immediate support-hand reuse
before completion, and rejection of forged or animation-only receipts. These
physical tests mock native completion; the BC2 player adapter remains unchanged.

The initial x86 full run exposed stack overflow from copying the larger receipt
into all 128 legacy diagnostic records. Revision 220b separates the resource
payload from the compact animation observation. The failing report is retained;
the focused physical-consumer test and both corrected full builds pass.

Final native `root-monitor/rifle-inventory220-02` uses frozen `ammo-move220b`.
The strict audit, mapped-code check and compiled trace replay pass. Two intents
remove 22/191 to 0/191, discard the original 22 rounds, and refill to 30/161.
The inventory identity survives the selection roundtrip. Both clients converge
within 18 ms; two deferred completions drain; 240 pairs arrive without timeouts.
All final postflight reads pass. Earlier `-01` passed the native audit but had
one read-only ownership sampling race, followed by 31 accepted settled reads.

BC2 PID **39304**, start UTC `2026-10-08T14:16:21.9757944Z`, is responsive on
the left monitor with SPAS 8/24. The mod is detached, but its DLL remains mapped;
restart BC2 before another attachment. Launch folder:
`monitor220-inventory-20261008-02`. No mouse/keyboard input was used.

Next: implement the bounded BC2 hand-command/result channel and explicit
physical-to-native identity mapping, migrate magazine visibility/render custody,
and resume detached-resource presentation through interruptions. BC2 physical
actor keys are compound identities while native receipts use soldier identity;
do not rewrite receipt owners to force a match. Validate empty-magazine no-op
completion in actual owning callbacks. Then exercise complete BC2 consumers
with simulated controller inputs before requesting another headset test.

# Earlier October 8 checkpoint 219 — native inventory lifetime and completion verified

Read [native ammunition evidence](NATIVE-AMMUNITION-216.md). Both ordinary
builds select profile 219, geometry 202, private ammunition probes OFF, and pass
195 suites. The normal snapshot is `normal219-resource-20261008`; the immutable
headset212 stage is unchanged. No player resource dispatch or headset fix is
claimed. No agents, GitHub updates or BF2142 changes occurred.

Private `ammo-move219-tooling-c` uses the same probe/trace as `ammo-move219`
with the corrected x64 controller receiver. `root-monitor/rifle-inventory219-02`
passes strict native resource, delayed-completion and inventory-rebind auditing.
It exits a verified boat seat with the existing controller Use action, removes
22 rounds, discards that resource, refills 30 from 191 reserve, switches away
and back, and retains the body's weapon lifetime. The server and both clients
converge at 30/161; two retained completions drain; the receiver delivers exactly
240 pairs with no timeouts. Both postflight drains accept 31 samples and the
mapped module check passes. The original `-01` resource audit also passes, but
its receiver overran to 254 pairs and exited 1; retain that failure.

BC2 PID **41960**, start UTC `2026-10-08T13:27:19.3765345Z`, is responsive on
the left monitor, on foot with SPAS 8/24 and the diagnostic detached. It still
has the DLL mapped, so another attachment requires a fresh process. Its launch
directory is `monitor219-inventory-20261008-02`. Saved Continue can enter the
boat; use `Run-Background-Inventory219c.ps1` and its vehicle-aware read-only
preflight. Inactive vehicle weapon counts of zero are not on-foot ammunition.

Next: general player command submission and per-weapon ledger storage; migrate
`Bc2MagazineDetached`, `Bc2MagazinePhysicalReload` and rendering from assumptions
that counts remain in a removed magazine well or that native animation must be
held. Do not fake `allThreeHeld` as empty-ammo evidence. Keep hand release and
visual custody separate from native acknowledgement. Validate original return,
discard, empty magazines, interruptions and switching with emulated gestures
before another headset request. Current identity integration verifies ordinary
selection; native pickup/scene replacement and detached-resource holstering
still need live evidence. Underbarrels retain existing stock reload behavior.

# Earlier October 8 checkpoint 217 — ledger-controlled native operation verified

Read [the live-ledger section](NATIVE-AMMUNITION-216.md). The private module now
queues resource commands before native dispatch and accepts completion from the
actual server callback plus both client Updates. `root-monitor/rifle-ledger217-01`
passes `native-resource-audit.json` with type 1 and live ledger required: two
operations completed, none pending, zero failures, native counts 30/161 and the
original 22-round resource marked discarded. Mapped module verification passes.

Normal builds select source profile 217, geometry 202 and private ammo probes
off. The frozen 216 normal artifacts and 212 headset stage remain separate.
Player hand/renderer integration is still pending. Also required before that
integration: a stable inventory generation independent of equip changes and
retention of the critical server completion if a nonblocking observer gate is
contended. No contention/headset/all-weapons acceptance is claimed.

Current root-launched game: PID 1292, start UTC `2026-10-08T12:44:07.1914951Z`,
left monitor, diagnostic detached, responsive. Another attachment needs a fresh
process. Latest inventory: SPAS 8/24, XM8 30/161, launcher 1/7. Prior PID 41316
exited after startup Continue before the reload diagnostic attached; cause was
not established. No other games, headset sessions or GitHub content were changed.

# Earlier October 8 checkpoint 216 — direct native ammunition and shared ledger

Read [216 evidence and limits](NATIVE-AMMUNITION-216.md). Private native
diagnostics now verify original-round remove/return, single-shell refill and
XM8 magazine refill without stock reload animations. The corrected rifle run
is `root-monitor/rifle-refill216-02`: 22/191 -> 0/191 -> 30/161, exactly two
native calls, both clients converged, idle state/timer unchanged, clean detach.
Its mapped probe code matches `ammo-move216/BC2NativeProbe.dll`. This diagnostic
deliberately discards the initial 22 rounds and does not grant player dispatch.

The reusable ammunition ledger and BC2 evidence bridge now have regressions
for original-round return/discard, no-op empty magazines, reserve conservation,
holster/context rebind, one-shot dispatch, stale/duplicate acknowledgements,
wrong owners and prediction rollback. The 214 and 216 live traces replay through
the C++ bridge successfully. Both normal builds pass 195 suites; source profile
216 and geometry 202 remain selected.

Next: connect a native command queue and physical interaction consumers to this
resource contract. Existing player magazine code still assumes counts stay in
the gun during removal; do not put a direct subtraction underneath that old
contract. Do not fake `allThreeHeld` to represent an empty gun. Empty-mag logical
acknowledgement, stable weapon-instance generation, native interruption drain,
support release, renderer custody, and persistent tokens need the actual adapter.
Underbarrels retain stock automatic reload behavior. No all-weapon/headset claim.

BC2 PID 31124, start UTC `2026-10-08T12:05:04.3733587Z`, remains responsive on the
left monitor with the diagnostic detached. Its latest inventory read is XM8
30/161, SPAS 8/24 and launcher 1/7. Verify identity before any action; another
attachment needs a fresh process. The frozen 212 headset stage is unchanged.
There were no agents, headset tests or GitHub pushes in this continuation.

# Earlier October 8 checkpoint 212 — native empty-ammo test and machine-code emulation

Read [212 evidence and limits](RELOAD-EMULATION-212.md). The actual x86 reload
instructions reproduced an empty-ammo guard bypass on a 59.6 ms frame. Shared
Step admission now uses the same bounded 100 ms delta policy as ordinary holds,
preserving the original owner/context deadlines and explicit reload behavior.
All 191 C++ suites per architecture and 45 focused Python checks pass. Two
independent private campaign captures pass 23 empty-state and 10 removal/return
emulation cases each; 68 native adjustment and 14 snapshot cases also pass.

The fresh background campaign loaded on foot with SPAS. Its input-only live
depletion run consumed all eight shells and verified three seconds at zero,
with matching applications/restorations on all three firing copies. The strict
empty audit passes after correcting the reporter's stale 128-entry assumption
for the existing 131-entry producer. Original failed audit/read files remain.
Mapped probe code matches the frozen build. Stock refill after detachment is
expected: the late reader settled at 8/16, conserving ammunition after eight
shots from 8/24. No >50 ms Step appears in retained native rows; hitch behavior
is machine-code emulation evidence, not an observed native stress test.

Build-Checkpoint selects operation profile 212 and geometry 202; x86 still builds
under `x86-checkpoint208`. Use the frozen 212 binaries, updated tooling receipt
and separate native/emulation evidence. BC2 PID 39640 (creation FILETIME
134359286258848810) remains open on the left monitor after the finite test.
Verify current identity; another attachment requires a fresh game. No headset,
desktop focus change, agent task or GitHub push was used.

Full-magazine discard is still presentation-only in gameplay. A signed native
round adjustment and its bounds are characterized, but no new ammo mutation is
enabled: server scheduling and stale prediction snapshots still need an exact
operation. AEK live acceptance and headset chest/HUD/support feel remain open.
The new private-fixture capture tool makes future configuration capture reusable;
do not publish its game bytes or claim additional weapon coverage.

# Earlier October 8 checkpoint 211 — combined native sequence verified

Read [the frame-hitch investigation and evidence](RELOAD-FRAME-HITCH-211.md).
The shared magazine and shell policies now tolerate bounded native frame hitches
without renewing controller, context or ownership evidence. Three focused suites,
all 190 C++ suites per architecture and software-D3D HUD readback pass. The native
combined XM8/SPAS shoulder and chest-reload sequence completed with zero
cancellations, 240 pairs and conserved ammunition. The initial postflight failed
a read race; its separate settled follow-up passes strict checks, with the raw
failure retained. No >50 ms delta occurred in that corrected native recording;
hitch acceptance is deterministic evidence, not a native stress-test claim.

Build-Checkpoint selects `profiles/checkpoint211` and calibration 202. The x86
build directory remains `x86-checkpoint208`; use the frozen 211 receipt and stage.
BC2 PID 40384 is left open after diagnostic cleanup; verify current identity and
use a fresh process before any new attachment. Automatic Continue does not
guarantee an on-foot spawn: the user exited the boat and selected XM8 for 211.
Full-mag discard/accounting, AEK native verification, chest flicker and headset
HUD/support acceptance remain open. No agent is active; no GitHub push occurred.

# Earlier monitor follow-up — 209 and 210

Read [209 monitor evidence](MONITOR-PRESENTATION-209.md) first. The strict native
XM8 original-return/replacement sequence passed with conserved ammo and 240
stereo pairs. A separate background SPAS holster run verified live paired HUD
counts and reproduced eight missing chest props among 175 valid-counter frames.
The 209 x86/x64 builds each pass 190 suites. Headset acceptance is still separate;
full-magazine discard and AEK native verification remain open. No GitHub update.

Candidate 210 added bounded fresh body-source rereads and per-source recovery
counters. Both builds passed; its combined run exposed the timing failure
corrected in 211. The protected
source receipt is in `profiles/checkpoint210`; native calibration remains 202.
The x86 incremental build directory is still named `x86-checkpoint208`; rely on
the new frozen receipt, not that directory name. The 208/209 headset stages and
native evidence retain their original binaries and results.

The private startup helper now has the verified primary UI vtable. It restores
its hook after queuing Continue. The loud EA/Dolby startup movies were renamed
with local backups, and the user confirmed the fix. Do not restore them during
routine launch. Do not change campaign movies, gameplay audio or BF2142 files.

# Earlier October 8 headset follow-up — candidate checkpoint 208

Read [208 investigation](RELOAD-FEEDBACK-208.md) first. Checkpoint 207 **ran in
the headset** and was not accepted: no ammo counter, chest flicker, unreliable
AEK reload starts, grip handoff and full-magazine discard recovery remain in
the user's report. The permitted mod stop finished cleanly with BC2 responsive.
The 208 candidate separates HUD rendering from native reload reads, repairs
the counter clock order and shared chest publication, consumes magazine grip
input before support regrip, and releases a seated magazine's hand immediately.
All six focused suites and all 190 suites on each architecture pass, as does
the HUD software-D3D readback. Matching private artifacts are frozen in
`headset208-20261008`. Full-magazine discard remains unresolved;
its later scoped native pass is recorded above, with no headset acceptance.
No GitHub update was requested.

Build-Checkpoint selects `profiles/checkpoint208` with the existing checkpoint
202 calibration headers. The two enabled exact magazine configurations have
not expanded. Historical entries below describe their original validation.

# Historical checkpoint 207 preparation

Read [207](RELOAD-PRESENTATION-207.md) first. Frozen 206d **did run in the headset**:
much more stable per user, but magazine/chest flicker and post-insertion support
delay remained. The permitted mod stop completed without closing BC2. Current
source added ammo HUD telemetry, separate support-contact admission,
shell hand release and coherent stereo body props. Both full builds pass 189
suites; the HUD software-D3D readback passes. Matching artifacts are frozen in
private `headset207-20261008`. The later 207 headset result and required fixes are
recorded in 208 above.

# Fork handoff

Local checkpoint [206](RELOAD-RECOVERY-206.md) fixes a reproduced shared hand
admission failure with N-2/N-3 renderer geometry. Targeted hand and actual-policy
reload tests pass. Its explicit native interruption driver retains consumers
through tracking loss, retirement, shoulder changes and chest reload. One
instrumented native run completed the interaction with conserved ammo but two
transport timeouts; combined acceptance remains inconclusive. After the PC crash,
the final 206d composition passed all 186 suites on x86 and x64, built sequentially
with one compiler job. Source and saved native evidence survived. The large
diagnostic record aggregate has been replaced by bounded setup-time allocation.
The frozen local receipt is `normal-recovery206d-build-receipt.json` under the
private pipeline runtime; the later 206d headset result is recorded in 207. The user's
fork supplied a UI-thread campaign-start method. Its bindings are verified against
the installed executable; a separate private auto-Continue prototype is being
built to load the saved test checkpoint without OS input. Its queued log must not
be mistaken for observed gameplay. No pre-crash PID is authority for a later session.

`Build-Checkpoint.ps1` now selects `profiles/checkpoint206` and the same 202
calibration headers. Local x86 output is `build/x86-checkpoint206`. No GitHub
push is authorized by this continuation. The user's later instruction to let
root continue superseded the earlier reservation for launch-switch experiments.

The current registry has two compiled magazine profiles, scoped XM8 and AEK971_sp.
The wider prepared data is not enabled. Read [current weapon coverage](WEAPON-COVERAGE-CURRENT.md)
before reporting gun counts. The offline batch tool now uses exact configuration
and original component bindings. Its 89 focused Python tests pass, and the saved
batch initially regenerated seven exact profiles with zero enabled production rows.
The subsequent variant extraction independently recovers the other 14 profiles;
all 21 pass the shared partial/empty consumer checks with mocked native responses
on both architectures. See the private `weapon-pipeline206-variant-extraction`
manifest and consumer audit. This tooling work does not alter the frozen 206d
native binaries or enable these profiles in the game.

Local checkpoint [205](RELOAD-RECOVERY-205.md) extends the reload motion driver
to repeated episodes without resetting persistent consumers. Offline tests now
cover 30 tracking/focus/input interruption cases followed by recovery, repeated
reloads and rejection of old evidence. Native responses and renderer receipts
are mocked in this matrix. All 186 C++ suites pass on both architectures.
The historical 205 build used its own operation receipt with 202 calibration
headers. It added no live interruption mode or headset acceptance.
The x86 205 output is `build/x86-checkpoint205`, because the old DLL at
`build/x86/BC2NativeProbe.dll` remained locked. Do not select the old path by
habit. The new offline build receipt pins the separate output directory.

Local development now adds [204](COMBINED-INVENTORY-RELOAD-204.md), a bounded
combined shoulder-switch/chest-reload controller sequence. All 186 suites pass
on x86 and x64. The corrected live run passed its strict combined, reload and
startup-pulse audits: 20/191 to 30/181 ammo, zero cancellations/fallbacks, 240 pairs
and clean restoration. At the end of that run BC2 was minimized with its disabled
DLL still mapped. That historical PID is not authority to close any later game
session; verify the current process before future tests. No GitHub push was
made; the public repository remains the user's fork snapshot.

The prior [checkpoint 203](RELOAD-TRANSITIONS-203.md) corrected a reproduced
cross-profile magazine-retirement deadlock in the shared consumer.
All 185 public C++ suites passed on each architecture, and the rebuilt module passed
the existing strict native original-return/replacement sequence. Cross-profile
active-reload interruption recovery itself is CPU-tested; 204 adds ordinary
holster/switch/reload coverage without interrupting an active reload.
The prior 203 receipt is retained for its exact historical source.

This public source snapshot starts at checkpoint 202. Read [STATUS.md](STATUS.md)
for current acceptance and [../BUILDING.md](../BUILDING.md) for ordinary and
checkpoint builds. The latest human feedback is the mixed 206d result recorded above.

The shared magazine startup pulse now has immutable original input timing and
requires genuinely fresh native contexts before Holding. A bounded diagnostic
completion journal retains exact End data when the record gate is contended.
The actual original-magazine return then replacement sequence passed its strict
native audit. See [SIMULATED-PLAYER-202.md](SIMULATED-PLAYER-202.md).

Do not revive old absolute launch paths or treat historical research notes as
current release instructions. Raw traces, media, game content and local config
are intentionally excluded. The small checkpoint calibration headers and exact
source-bound operation receipt are in `profiles/checkpoint202`.

Next work is the final 206 actual-game check, then pickup/vehicle/empty-ammo transition coverage,
followed by mechanism integration and exact-package headset acceptance. There are no agents
or local game sessions that a fork needs to coordinate with.
