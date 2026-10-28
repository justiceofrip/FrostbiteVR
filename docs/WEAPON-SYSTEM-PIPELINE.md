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

# Shared weapon system

## Current evidence — October 9, local checkpoints252–255

The shared resource consumer completed the same actual-game controller sequence
with F2000/SPAS and SCAR/SPAS, without a new rifle-specific code/geometry change
between those runs. This includes original magazine return, chest replacement,
holster switching, interrupted pump recovery, shell insertion and support return.
See [252's receipts and limits](archive/RECOVERY-252.md). The17 compiled exact magazine
configurations are prerequisites, not17 accepted guns or an all-roster claim.

New supported magazine-fed weapons reuse the consumer after exact configuration,
component geometry, visibility and resource bindings are joined. Pump, bolt,
slide and belt-feed need their distinct shared mechanism adapters. Ordinary bolt
coordination is still under actual-game investigation in [255](archive/RECOVERY-255.md);
pistol and belt-fed completion, last-round edge cases, scene recovery and headset
acceptance remain open. Published245 is a separate forkable snapshot.

## Historical composition — October 8, checkpoint206

The active checkout is this repository. Read [current weapon coverage](WEAPON-COVERAGE-CURRENT.md)
and [the current state](CURRENT.md) before using the historical paths below. The normal
checkpoint integrates two exact detachable-magazine profiles, scoped XM8 and
AEK971_sp. Prepared descriptors, grip data and visibility qualifications are
separate from compiled reload coverage. The wider registry is not selected.

The pipeline must join immutable configuration and component evidence, then
reuse the corresponding mechanism consumer. An asset-name match must not
silently turn one measured magazine into every attachment variant. The existing
authored exporter already accepts exact configuration metadata; the coverage
and preparation tool now uses that contract. The saved batch regenerates seven
exact candidates; its former 21-path count relied on unsupported name matching.
Native registrations remain disabled for these candidates.

## Implementation decision and historical checkpoint — October 5

The unit of work is a mechanism and its lifecycle, not a weapon name. An ordinary
new rifle must supply configuration and geometry data to the same consumers.
Categories organize coverage; they do not establish mechanics or native authority.

## Verified shared lifecycle and operation-class checkpoint

Active source: `<local-recovery>/headset-20261005-115245-fix-composition/source`.
Checkpoint: `../checkpoint-183-shared-start/runtime-checkpoint.json` from the source root.
Normal binary pins: `<local-recovery>/pipeline-runtime-20261005/normal-start-preread-build-receipt.json`.
This was the October 5 composition; it is not the current launch instruction.

Current normal composition passes **183 CTest suites per architecture** and
**788 Python tests**. The immutable checkpoint records exact logs, build settings,
source/class receipts and evidence hashes. No new headset test, canonical deployment
or release is claimed.

Shared input/presentation suspension now preserves only independently fresh native
inventory lifetimes; owner/data/container replacement or expired native proof retires
keys. Recenter retains exact shoulder assignments while invalidating old gestures.
Selection rejection suspends interaction separately. Full-loaded rifles can show
fresh chest reserve props without granting reload/start eligibility.

Configured SPAS05 and XM8-02 bounded native hide/show, challenged suppression,
draw/ordinary-fire tests passed independent audits. A root-reviewed common
operation class is compiled with the exact source receipt. Actual CPU visibility
and holster consumers qualify **35 exact configurations**; **11 geometry rows lack
configuration backlinks** and remain excluded. This is not all-weapon manual reload,
GPU/headset quality, chamber/pump/bolt/slide or multiplayer readiness.

Public repeated cancellation05 passed before the subsequent expired-observation
I/O optimization. Arming04 retained paired evidence for all three branches and
ordinary post-hook count restoration; independent live-zero proof remained
inconclusive. The real-Start race exposed by Arming05 was corrected in shared tube/magazine
CPU paths. Current bounded native Start status: **bounded_tube_start_observed**. Run06 supports one
representative tube Start/cancel with three coherent independent zero-ammo attempts
and ordinary post-hook count restoration; two structural coherence gaps remain
explicit. Native/global stability, magazine Arming, chamber and input flags4/5
remain unverified. Successful builds do not promote those scopes.

Next work checks actual magazine startup and repeated transition sequences,
then composes shared feed/action/chamber consumers and exact package data,
with representative mechanism/transition monitor checks. Geometry-only or unknown
native action evidence stays unavailable; no per-gun state-machine rollout.

## What the audit found

The current magazine consumers already reuse removal, original-magazine return,
insertion and native transfer logic. The principal integration problems are:

1. Independent eligibility: reload, calibrated handling, hide/show, body display
   and local ammo assets each have their own registry. AEK can have reload geometry
   and a reviewed native magazine binding while production stow is unavailable.
2. Inconsistent joins: native magazine registration distinguishes configuration
   paths, but geometry previously joined by display name. Same-name attachment
   variants could collide or share a geometry record accidentally.
3. Transition fanout: actor lifetime, physical item identity, native selection,
   attachment function, render cohort and tracking origin have different lifetimes.
   Their cancellation/rebinding is distributed across Gameplay and consumers.
4. Native reload entry precedes the hold. Losing interaction freshness while
   Arming can retire the VR operation without the held capability needed by the
   existing native abort path. The animation can then continue without insertion.
5. Prepared data/policies were counted too optimistically. FeedMechanism and
   WeaponCycle have no production gameplay consumers. PumpPart is presentation
   groundwork. None proves functional manual pumping, bolt cycling or chambering.

These are shared-system defects. Measuring a new magazine hand offset cannot fix
them. The latest trace supports native continuation after early cancellation;
it does not establish that every unwanted reload shares that exact cause.

## Package, adapter, instance

```mermaid
flowchart LR
    A[Installed configuration and authored assets] --> B[Exact configuration package]
    B --> C[Geometry and component validation]
    C --> D[Explicit capability report]
    E[Reviewed game adapter bindings] --> D
    D --> F[Shared feed and action policies]
    G[Live instance and ownership receipts] --> F
    F --> H[Native commit and exact restoration]
    H --> G
```

**Package:** immutable source configuration resource/hash/GUID, complete configured
mesh references and digests, rig identity, and independently referenced components.
Components contain grip/hand data, magazine or round grasp, feed contact and travel,
moving-part axis/stops, attachment contacts, selected visibility and body geometry.
An asset name is a label. A scope or launcher variant is an exact configuration;
it can share an unchanged component through an explicit verified reference.

**Adapter:** BC2-specific observation, operation entry, hold, transfer, cancellation,
fire gating and restoration. Bind an exact configuration to a proved family of
native behavior. Compatible ARs/SMGs reuse the magazine consumer and reviewed
native family; a category name cannot substitute for a config/timing match.

**Instance:** actor incarnation plus actual carried equipment identity/incarnation.
Keep selection lease, attachment function, hand ownership, body slot, render epoch
and tracking-space epoch separate. Recenter changes spatial evidence, not ammunition
or the identity of the gun. Reusing an address after pickup is a new item when the
verified data/persistence identity changes.

## Feed and action compose independently

| Feed | Action data | Intended shared behavior |
|---|---|---|
| Detachable magazine | Optional charging handle or slide | Eject/retain original rounds, grab/insert; cycle after empty feed when the chamber rule is known |
| Tube/internal rounds | Pump or automatic action | Insert rounds separately; an admitted pump action gates the next shot |
| Belt/container | Cover, belt seat, latch and charging action | Authored ordered feed workflow, then independently acknowledged action readiness |
| Internal or detachable | Bolt | Feed separately; unlock/back/forward/lock uses the existing bolt policy |
| Clip or single round | Exact breech/action | Use an authored feed sequence; do not assume magazine behavior |
| Underbarrel attachment | Its own feed/action and sight hinge | Share parent physical identity where proved; automatic reload remains the interim scope |

`WeaponMechanism.h` selects a declarative plan from separate feed, after-empty-feed,
after-shot and chamber knowledge fields. It reuses ManualReload validation and the
existing Pump/Bolt policy types. It is not connected to BC2 gameplay yet. Charging
handle and slide gesture recognizers remain absent; an empty mapping says so.

An occupied chamber retained during a tactical reload must not require the same
action as an empty chamber. Unknown chamber is neither empty nor occupied. BC2's
loaded count currently does not establish a separate chamber count. An eventual
VR semantic chamber model must conserve total rounds and have an admitted fire
gate; animation completion or a controller gesture cannot create ammunition.

## Shared operation lifetime: integrated boundary

Coherent native inventory observation is independent of current interaction and
presentation eligibility. Suspension clears pending gestures and fresh display
permission; it cannot mint a hand claim, hide/show receipt, empty-hands state or
native reload authority. Real owner/data/container change and native observation
expiry retire physical keys. Spatial-only recenter preserves exact assignments but
invalidates previous spatial interaction evidence.

Entered native operations retain their own bounded cleanup identity. Public
repeated cancellation and independently verified configured hide/draw/fire are
recorded above; empty-Arming tube Start/cancellation has only the bounded Run06 result
above. Magazine startup and repeated sequences remain separate missing coverage. Feed and action/chamber capabilities must not borrow a
visibility/input class receipt.

## Implemented in the October 5 composition

- Shared shell/magazine typed keepalive and observer deferral. Temporary contention
  does not impersonate cancellation or renew the original deadline. Limited native
  cohort gaps preserve only already-held/submitted continuity, never new authority.
- Surviving shoulder assignments are retained before placing new equipment.
- Support grip keeps a genuine neutral baseline during temporary hand-busy/contact
  waits; real owner/focus/tracking loss still resets it. Authored support-only data
  and failed-grab diagnostics help distinguish bad contact from operation ownership.
- Generated complete configured-visibility descriptors; all new rows remain
  unadmitted pending native verification. Explicit unsupported-stow diagnostics.
- Magazine geometry joins by configuration path. Legacy pathless metadata is
  restricted to the original builtin descriptor objects; it cannot enroll a new
  same-name variant. Exact configuration fixtures exercise the actual consumer.
- An offline exact package index and compiled-registry coverage report, plus the
  portable mechanism selection contract. Neither enables new native behavior.

The package index currently covers 35 exact authored configurations, not 35 distinct
guns or the full roster. Eleven visibility rows without configuration backlinks
are explicitly excluded. Exact component closure currently joins 20 two-hand grip
records and 10 detachable-magazine contact records using complete configured
resource/LOD digests and authored configuration backlinks. A magazine contact is
not a visible ammunition cache. Body/ammo/attachment caches without immutable
configuration backlinks remain explicitly unbound.

Native visibility admission now separates authored weighted-section data from
the shared adapter operation proof. The default operation-class table is empty.
An exact executable, discovered native binding, operation source revision, render
copy/restoration receipt and challenged input-suppression receipt are required
before a reviewed shared class can admit a compatible descriptor. The ordinary
data path adds no weapon-name branches or per-gun native success claims.

## Build and verification gates

1. Generate packages from installed assets, preserving all source identities.
2. Validate component closure, exact variant joins, duplicate identities, rig/mesh
   mismatch and unavailable data. Generate explicit missing reasons.
3. Compile the capability probe with the same source/library build. Report what
   that binary actually contains, including absent generated headers. A compiled
   registry receipt is not proof that a DLL in a running game matches it.
4. Exercise shared consumers over descriptors and transitions: fresh draw, full
   magazine removal/return, partial and empty replacement, support regrip, both
   shoulders, pickup replacement, vehicle exit, tracking loss and cancellation at
   each native phase. Include same-name variants and both tube/magazine feeds.
5. Verify native behavior by mechanism and native code/config family. Reuse that
   proof only when an exact new descriptor satisfies its conditions. Add exceptions
   as data or a newly identified mechanism, not a new weapon-name state machine.
6. Headset-test representative mechanisms and ambiguous contact/animation outliers.
   The user should not be the first test of every ordinary rifle. Data coverage and
   CPU tests cannot promise that every geometry or native family is already correct.

## Order of remaining work

First close pre-hold native cancellation and extract the shared transition/operation
lifetime seam. Then unify selected hide/show and holster admission, bind the existing
body/ammo/hand components into the package index, and report capability closure for
the actual build. Only then connect pump/bolt/handle/slide adapters, each with native
readiness and conservation evidence. LMG feed and manual underbarrels follow their
own mechanism proofs; automatic underbarrel reload remains acceptable in the interim.

For BF3/BF4/Hardline and other Frostbite ports, retain the package schema, canonical
poses, interaction policies and scenario matrix. Replace asset decoding, native
configuration/instance resolution, render and transaction adapters. BC2 addresses,
rig indices and native state numbers must not cross that boundary.

## Reproduce the offline coverage report

After the normal x86 build, run from the source directory with the locally
generated visibility evidence. The evidence file stays outside a public export.

```powershell
python tools/bc2_weapon_package_index.py --visibility PRIVATE/descriptor-evidence.json --output reports/weapon-packages.json
python tools/bc2_weapon_component_closure.py --index reports/weapon-packages.json --visibility PRIVATE/descriptor-evidence.json --metadata PRIVATE/grip-bindings.json --metadata PRIVATE/magazine-geometry.json --metadata PRIVATE/support-bindings.json --output reports/weapon-components.json
& ./build/x86/BC2WeaponCapabilityProbe.exe | Set-Content reports/compiled-capabilities.json -Encoding utf8
python tools/bc2_weapon_capability_report.py --package-index reports/weapon-components.json --registry-receipt reports/compiled-capabilities.json --cmake-cache build/x86/CMakeCache.txt --out reports/weapon-capabilities.json
```

The probe performs no game access. Exact configuration path and rig joins determine
the reported static edges. Missing native registration, geometry or stow admission
remain separate reasons. Current transaction receipts, headset comfort and a match
to an independently deployed DLL remain unknown unless separately established.

Latest completed combined validation: 177 CTest suites passed on each architecture
and the full Python run passed 769 tests. Diagnostic startup and draw/release regressions,
retained operation journals and bounded native contention deferral are integrated.
The selected-configuration reader repeat-verifies
the established SoldierWeaponData native configuration-path field alongside the
owner, pointer, complete mesh set and configuration bytes. Thirty-five generated
rows carry exact authored configuration paths; eleven missing backlinks remain
explicitly unavailable. This does not establish native or headset acceptance.

The isolated early-cancellation monitor first exposed a cross-copy failure:
client-only aborts were overwritten by native server state restoration. The
server-first correction passed SPAS04 with 7 loaded / 24 reserve unchanged. Native
Restore returned both client copies to idle without client helper calls. The
subsequent ordinary reload completed 8/23, proving the stopped diagnostic did not
leave reload disabled.

XM8-01 then exposed a skipped observer phase: one OriginalUpdate advances from
idle directly to current11/next12 through nested commits. Exact recognition of
that proved transition passed XM8-02 with 22/191 unchanged and the same server
helper/native-client-propagation sequence. A later ordinary reload completed
30/183. Both checks drained hooks, retained the game, and sampled all three copies
after the original cleanup window. These prove bounded native cancellation, not
complete headset behavior or every weapon configuration.

The production candidate removes forced callback cancellation and records the
first actual cancellation origin, including implicit timeout/rejection. Holding
cancellations retain the existing held-abort path. A distinct fresh server-idle
receipt covers a server already idle while clients entered; it never fabricates
a helper call. Original operation deadlines, exact owner/configuration/counts,
once-only branch dispatch and native callback ownership remain mandatory. New
operation/rebind cannot erase unfinished cleanup inside its original window.

The first normal public repeated-cancellation fixture stopped during startup before
any operation. Its correction waits for coherent startup evidence before committing
an owner. Repeat02 then acquired an actual operation, but a roughly152-microsecond
policy-lock miss enqueued global cancellation before native entry. Server-first
cleanup preserved 6/24 across all three native copies; the requested repeated
scenario did not complete. The shared contention correction defers observational
lock misses and only verified neutral/loaded/all-idle evaluation; decision-critical
held, explicit reload, empty and expiry paths retain their existing behavior.

Configured-fire02 drew and fired one SPAS round (8/24 to7/24), then the assigned-slot
observation vanished while the original hand/owner remained. Strict completion
remains false. A diagnostic-only fix now models real release-to-stow behavior while
leaving a shoulder; the remaining assignment issue is a shared integration task.
