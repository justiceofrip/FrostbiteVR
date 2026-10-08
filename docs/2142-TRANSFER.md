## October 8: reload authority and visual/hand lifetime — 207

A pending native ammo transfer must not implicitly hide support contact or keep
an inserted consumable attached to the hand. Use separate contact capability,
physical item ownership and exact native completion. Preserve the reservation
until its real receipt while releasing only the inserted item's original token.
Also compose both eyes from immutable source geometry and join optional props
by identity; dense array positions are not resource identities. Ammo HUD values
come from verified native state and expire separately from reused world images.
See [207 evidence and limits](RELOAD-PRESENTATION-207.md). No Refractor offsets or
new native reload calls were transferred.

## October 8: count configured mechanisms, not names — 206

Batch extraction must retain the original configuration and component provenance.
Joining magazine geometry by display name counted three variants from one saved
profile. The corrected offline preparation joins configuration resource/hash/GUID,
original binding digest, selected mesh/LOD and rig, then calls the existing
geometry exporter. The saved seven-profile batch yields seven exact candidates,
not 21. Native registrations remain disabled; configuration data and a working
native mechanism are independent requirements. This avoids transferring stale
native or attachment assumptions to another Frostbite adapter.

## October 8: release edges versus delayed geometry — 206

A controller release is an edge, not every later open-hand packet. Advancing
the release barrier continuously made N-2/N-3 renderer contact fail at the next
grip even while it remained fresh and in the same neutral period. The shared
arbiter now retains that interval's initial boundary; separate invalidation,
history, original expiry and ownership checks still reject unsafe evidence.
The actual-policy reload loop reproduces the old failure and passes after the
fix. Native recovery tests retain real consumers through input loss, retirement,
shoulder changes and subsequent reload. See [evidence limits](RELOAD-RECOVERY-206.md).

## October 8: preserve lifetime evidence across diagnostic episodes — 205

A scripted player may start another gesture sequence; the gameplay consumers
must retain their lifetimes. Baseline diagnostic counters only when the consumer
is idle, retain absolute counters alongside episode deltas, and reject rollback
or reused completion evidence. Real controller/hand/reload policies now pass 30
offline input-loss/recovery cases with mock native responses. This improves
repeatable testing; it does not establish native or headset acceptance. See
[205](RELOAD-RECOVERY-205.md).

## October 8: combine gestures without resetting the consumers — 204

Independent holster and reload success does not prove their composition. Drive
the ordinary inventory/chest-ammo path using actual assignments and retain its
consumer and hand lifetimes. Startup auto-stow must settle first; empty hands
must remain empty while leaving the shoulder. Keep callback recording deadlines
separate from gameplay authority so a longer setup cannot erase reload evidence.
The new driver passes CPU checks and one strict live combined sequence in
[204](COMBINED-INVENTORY-RELOAD-204.md), separately from headset acceptance.

## October 8: separate old retirement from current equipment — 203

Cleanup of an interrupted operation belongs to the old owner and cycle, while
fresh attachment evidence belongs to the current weapon's validated profile.
Comparing that new attachment to an old rig can silently block future reloads.
Persistent cross-profile CPU sequences reproduced and fixed this shared BC2
consumer defect; native original-return/replacement regression also passed.
No Refractor identity assumptions, offsets, or ammunition authority were reused.
Actual native cross-profile transitions and headset acceptance remain separate.

## October 8: preserve diagnostic completions without changing authority

Concurrent native callbacks can finish correctly while a nonblocking diagnostic
logger drops their completion. Preserve the already captured data in a bounded
queue and consume it under the logger's existing ownership. Do not reread native
state later, manufacture a receipt or make game threads wait for diagnostics.
Overflow and Begin misses must remain explicit. BC2's 202 sequence recovered four
such completions and passed the unchanged strict native audit. This pattern is
portable; the native invocation and identity proofs remain adapter-specific.
[Verified sequence and coverage limits](SIMULATED-PLAYER-202.md).

## October 8: original input pulse versus native hold establishment

An engine adapter must distinguish the input that starts a native reload from
the later state in which VR may hold its animation. BC2 now receives immutable
proof of the original Start control and its pulse end. All three native contexts
must actually have been observed after that end before establishing the hold.
Later processing or renewed controller packets cannot refresh that proof.
Native safety checks remain unchanged. This is shared magazine policy, not an
XM8 geometry adjustment. Other engines require their own startup evidence.

The repeated native test completed, but logger contention left a gap in its
strict sequence evidence. Authoritative per-invocation results and diagnostic
records have separate purposes; missing diagnostics must remain explicit.
[201 evidence and remaining limits](SIMULATED-PLAYER-201.md).

## October 8: repeated interactions and typed startup gaps

Synthetic controllers must drive the real persistent consumer through repeated
interactions. Returning an original magazine and immediately starting another
reload exposed an ordering failure that isolated fresh-state tests missed.
Native ammunition totals alone cannot prove a physical reload: the ordinary
engine fallback can produce the same final totals after cancellation.

The tube consumer now distinguishes a fresh typed observation gap before its
first native hold from an invalid owner/source. It preserves only the already
held shell under a fixed first-gap deadline and the original startup/control
bounds. It grants no geometry, insertion or native acknowledgement. Other engine
adapters must define their own observation and native startup boundaries.
[Evidence and current magazine failure](SIMULATED-PLAYER-200.md).

## October 7–8: measured simulated-player reload197

Shared free-carry and grip-retention corrections now pass a real XM8 reload with
12.6cm fast hand movement and37° wrist rotation. Native ammo is conserved, with
one completion and no cancellation or magazine palette fallback. Both builds
pass186 suites. Committed-empty recovery also has baseline-failing software
coverage. This is one bounded simulated-player check, not headset acceptance.
See [measured evidence and remaining checks](SIMULATED-PLAYER-197.md).

## October 7: headset193 failed; interaction196 corrections

The user reports broken rifle reloads and unrequested weapon reappearance from
holsters; grenade-launcher interaction worked. The completed trace has two
magazine starts, two cancellations and no completed reload. Shared free-carry
limits and committed-empty recovery are corrected with baseline-failing
regressions. A separate pre-removal cancellation remains unresolved.
See [evidence, changes and validation limits](HEADSET-196-INTERACTION-FAILURES.md).
This is not headset-ready or an all-weapon readiness claim.

## October 7: launcher interaction evidence194–195

Keep physical firearm identity distinct from native rifle/underbarrel subitems.
A mode acknowledgement may change the native item while preserving the physical
sight grasp; it must cancel and retire a rifle magazine transaction independently.
The new shared-consumer regressions exercise that boundary at three reload stages.

Automated controller fixtures must target the same anatomical point as runtime
interaction. Wrist-only calibration missed the current thumb/index mechanism point
by111mm. Preview validation must use the captured grasp basis, not a native pose
that has since animated into the other mode. Exact configuration and acknowledged
handover evidence remain required; neither relaxed radii nor borrowed Refractor
bone offsets were introduced. [Native checks and limits](LAUNCHER-195-MONITOR-CHECKS.md).

## October 7: shared render lifetime193

The render-only magazine target now retains the original controller geometry and
exact gun/hand claim deadlines. A shorter sampled native ammo observation no longer
prematurely expires that visual target. Native observation still independently
gates interaction geometry, insertion, reservation, submission and acknowledgement.
Current tracking, meshes, owner, cycle, role and claims remain checked at each
palette copy. Cached geometry timestamps are never refreshed.

Both architecture baselines reproduce the observed expiry case; candidate193
passes 40 focused physical groups, including rendering at native expiry plus
8 ms and rejection of expired native interaction evidence. Both complete builds
pass all 186 suites after correcting an old visual/native deadline assertion;
native-expiry cancellation and duplicate no-renewal tests remain intact.

The fresh actual-game monitor run magazine-render193-01 passed the unchanged
strict audit: one acquired/submitted/completed reload, zero cancellations, zero
magazine palette fallbacks, and all removed/hidden/replacement paired roles.
Native ammunition changed from 22/191 to 30/183; both late readers accepted 31
samples without rejection. The receiver consumed 240 stereo pairs. Loaded module
code matched probe SHA48e6c3c42bd565dec20b0a4e6cd0da3619a11b1aed393a69e9b288741476216d.
Hooks stopped and BC2 was minimized. The current process retains the disabled
monitor DLL, so the next headset session needs a fresh game process.

Raw shot-clock diagnostics now use shot_deadline_qpc_ticks. This is shared
magazine policy with no individual weapon geometry changes. Synthetic controller
input was used: headset feel, eye textures, prediction rollback and additional
weapon classes remain unverified by this run.

Next headset checklist: [RELOAD-193-HEADSET-CHECK](RELOAD-193-HEADSET-CHECK.md).
The optional Blender calibration workflow is described there; no Blender component
has been installed. Root owns integration/build/game checks and the isolated worker
has completed its implementation and focused verification.

## October 7: native completion and exact visual expiry checkpoint

The shared magazine transaction completed in three actual native monitor runs
(191-01, 191-02, 192-02), with one insertion and completion each, no cancellations,
and conserved 22/191 to 30/183 ammunition. Full x86/x64 compositions pass all 186
CTest suites; diagnostic192 physical checks pass39 groups on each architecture.
These are monitor fixtures, not headset or all-weapon acceptance.

The192 journal identifies both fallback events as expired old/current palette
proof: ownership and shot tracking matched, no source read/change failure occurred,
and the target expired roughly8ms before packing. The original and current target
snapshots were identical. Existing palette selection remains unchanged. Strict
zero-fallback visual audit remains inconclusive. Trace the shared target deadline
before altering presentation; do not extend stale native evidence.

First192 attempt rejected the fixture before any reload started. It is preserved
separately, not treated as a transaction failure. The completed repeat's late server
reader rejected one incoherent sample and accepted30. Current shot_deadline_ns
telemetry holds the native shot-clock value; its units must be documented explicitly.

Exact build, run, audit and expiry pins:
`<local-recovery>/pipeline-runtime-20261005/presentation-boundary192-evidence/evidence.json`.
BC2 remains minimized on the left monitor after hook shutdown. No headset session,
deployment, release or new weapon-class readiness claim. One worker is performing
read-only deadline provenance analysis; root owns any subsequent integration.

## October 7: shared reload startup and observation boundaries

The typed native observation repair is integrated through startup-wait191.
Unavailable native evidence defers interaction; it does not create a negative
hold or authorize magazine progress. The short hold-gap watchdog begins after
the exact unseat acknowledgement, while startup retains the original 4-second
acknowledgement and 30-second transaction limits. Deferred and duplicate startup
packets still check those original limits. The original 100-ms reload input pulse
and post-gate 50-ms gap remain unchanged.

Focused physical regressions pass 38 groups on x86 and x64, and both unchanged
baselines reproduce the startup failure. All 186 composed CTest suites pass on each
architecture. Two actual native
monitor transactions completed once each with no cancellations and conserved
22/191 to 30/183 ammo counts. Strict visual acceptance remains inconclusive: the
runs recorded 2 and 25 guarded fallback frames without timestamped causes. A
diagnostic-only classification is staged next; headset acceptance remains pending.
The previous hold190 monitor run failed
before its first gate; ammunition stayed unchanged. No headset-ready claim follows
from the focused tests. Accepted holsters and weapon geometry remain unchanged.

One isolated worker handled the staged implementation and focused tests; root
owns integration, full builds and game checks. No duplicate tasks or desktop
actions were delegated. Evidence: `pipeline-runtime-20261005/hold-freshness190-evidence`
and `pipeline-runtime-20261005/startup-wait191-evidence/evidence.json` under the
recovery root.

## October 7: observation availability is separate from native authority

An actual BC2 magazine transaction reached guided insertion, then cancelled because
an expired cached hold and an unavailable new observation became a negative hold.
Physical hand ownership and ammo-source evidence were still valid. The next adapter
repair must expose Ready, Deferred and Rejected observation outcomes and retain
original bounded lifetimes. Waiting must not produce insertion, seats, acknowledgements
or refreshed stale native proof. This distinction is shared across BC2 magazine
consumers; it is not an XM8 geometry adjustment. The recovered diagnostic builds
pass 186 suites per architecture, but the native manual transaction failed and
the typed repair is not yet integrated or verified.

## October 5: shared reload observation and prediction

See [headset183 evidence](RELOAD-20261005-HEADSET183.md). BC2 client prediction can
restore and replay a transfer before its local server confirms it. Magazine
completion now uses exact native Restore evidence for that transition while
retaining original operation/configuration/deadline and exactly-once server credit.
This adapter behavior must not be inferred from Refractor or assumed for future
Frostbite versions. The portable gesture policy remains independent.186 CTest suites
pass per architecture, but new native runs stop earlier on hand ClaimLost; no
new weapon-class, native prediction or headset acceptance follows from CPU tests.

## October 5: shared weapon lifecycle and reviewed operation class

Active source: `<local-recovery>/headset-20261005-115245-fix-composition/source`.
Checkpoint: `../checkpoint-183-shared-start/runtime-checkpoint.json` from the source root.
Normal binary pins: `<local-recovery>/pipeline-runtime-20261005/normal-start-preread-build-receipt.json`.
This entry supersedes older readiness and launch instructions below.

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

## October 3: combined pipeline checkpoint

The shared empty-reload repair and the broader offline weapon pipeline now build
together: 166 CTest suites pass on each architecture, plus 695 Python tests.
This composition retains the latest pickup recovery and native guard, adds the
body display batch and portable local asset preparation, and carries the GP30
sight/vehicle-reticle candidates with their native activation still disabled.
No new gun profile is enabled. The separate 190440 headset candidate remains
frozen and ready for its focused regression; its SPAS/XM8 monitor evidence does
not establish native acceptance of this newly combined binary.

## October 3: shared empty-reload repair; SPAS and XM8 monitor checks pass

The 190440 headset run accepted the empty-hands pickup recovery but reported
SPAS/AEK automatic empty reload. The SPAS omission in the shared native gate is
repaired. Full builds pass 163 CTest suites on each architecture and 659 Python
tests. Actual monitor checks fired SPAS 8-to-0 and XM8 22-to-0, then verified
three continuous seconds empty without automatic refill. The final diagnostic
build passed the SPAS check again. AEK's intermittent cause is still unproven;
the next headset run checks AEK and the deliberate physical reload path.

Active candidate: `<local-recovery>/headset-190440-fix-composition/source`.
Launcher: `<local-recovery>/headset-190440-test-readiness/Start-Focused-Test.ps1`.
Start a fresh BC2 process: the stopped monitor process retains its native DLL.
This candidate has not been headset-tested. New weapon profiles remain disabled.
See [evidence](HEADSET-20261003-190440.md) and [test card](HEADSET-190440-REPAIR-TEST.md).
Canonical runtime is not deployed; launch the pinned candidate above.
Earlier entries below are historical, including old agent and test status.

The policy is shared by reload mechanism, with exact configuration data supplied
by the BC2 adapter. Tube shells now enter the same scoped native idle boundary as
reviewed detachable magazines. Caller/ABI, current equipment, cancellation and
timing evidence remain mandatory; active manual phases bypass suppression.
Unknown families and underbarrels keep their stock behavior. The input-only
monitor fixture accepts any reviewed selected owner instead of switching on gun
names. Registry tests exercise all 21 generated configurations plus two builtins
on all three native copies, with disabled rows rejected. These are configuration
tests, not 23 playable guns. Future Frostbite adapters reuse policy and diagnostics
but must prove their own native boundary; no Refractor offsets are transferred.

## October 3: empty-hands pickup repair built; headset confirmation pending

The 182719 run is stopped and its complete evidence is preserved. The shared
pickup recovery repair passes a regression that fails on the previous build;
the combined candidate passes all 157 CTest suites on x86 and x64. It uses the
current carried inventory, fresh paired rendering and a released trigger before
reacquiring the gun hand. No new native hide profile is enabled.

The one-off automatic empty-mag reload remains unresolved. Its journal now
protects local transitions from unrelated traffic and reserves transition slots
against expired-lease boundary traffic. Native reload guards are unchanged.

Candidate: `<local-recovery>/headset-182719-fix-composition/source`.
Ready launcher: `<local-recovery>/headset-182719-test-readiness/Start-Focused-Test.ps1`.
A fresh BC2 process is required because the stopped session's DLL remains loaded.
No new game or headset run has occurred. See [evidence](HEADSET-20261003-182719.md)
and [focused test card](HEADSET-182719-REPAIR-TEST.md).
Earlier entries below are historical; they do not describe current active agents.

Transfer lesson: ordinary pickup recovery must validate the current native
inventory, not require unrelated old attachment/container entries to remain
unchanged. Preserve exact actor/equipment ownership, invalidate render requests
when the carried snapshot changes, and reacquire through the shared hand arbiter.
Diagnostic attribution to an expired owner never grants native write authority.
No Refractor offsets or per-weapon pickup branches are introduced.

## October 3: reticle evidence reaches each indexed draw

The bounded ACOG observer now retains exact submitted optic bytes, original
configured-mesh evidence and current per-eye/owner/equipment data. Its aperture
evaluation reports missing or ambiguous links instead of treating a geometry or
matrix match as native instance ownership. GPU-instance association remains
unverified and no reticle draw is suppressed. Focused x86/x64 CPU checks pass;
native capture and headset acceptance remain pending. See
[reticle evidence follow-up](RETICLE-DRAW-FOLLOWUP-20261003.md).

## October 3: profile-driven descendant sight hinges

The shared hand and sight policy now accepts measured arbitrary hinge axes and bounded travel while preserving legacy defaults. BC2’s AEK/GP30 offline profile is joined through exact config GUIDs, mesh and animation references; geometry cannot promote a native mode family or acknowledgement. Its sideways sight uses existing complete procedural fingers because its clips contain no accepted authored contact. Actual selected-owner binding and native subtree presentation remain separate integration work. See [AEK-SIGHT-GEOMETRY-20261003.md](AEK-SIGHT-GEOMETRY-20261003.md).

## October 3: exact zoom-mesh and HUD routes across weapon classes

The authored optic catalog shares bounded config/GUID/mesh readers and keeps zoom-level fields, weapon FOVs, mesh routes, lens-filter routes and HUD IDs separate. Null lens filters do not mean no scope; scope-class or native-name inheritance is insufficient. Exact shared zoom mesh bytes across three campaign weapons are offline asset evidence, not native draw ownership or magnification. See [OPTIC-CONFIGURATION-ROUTES-20261003.md](OPTIC-CONFIGURATION-ROUTES-20261003.md).

## October 3: shared gestures, exact native empty gating and batch evidence

Early shoulder squeeze, release-to-own-slot and direct shoulder swap now share
bounded contact/ownership policy. A same-gather approach carries exact slot/item,
hand token, input and original deadline into the ordinary selector; reread native
inventory before consuming it. Never synthesize input or treat tracking loss as
release. Both shoulders use identical policy; actual comfort remains a headset test.

BC2's empty-rifle transition needs native idle-Step control, not just input masking.
The adapter scopes and exactly restores one verified stack-context byte around the
original call. Manual zero-round admission requires fresh native restored-idle
receipts on all three firing copies, and cancellation still calls the native abort
helper. Preserve callback cohort checks after added lock waits and clip receipts to
the original server dependency deadline. Underbarrel stock reload remains separate.

Paired hand/item extraction now batches exact authored metadata and includes reload
references whose ordinary grip animation is nonstatic. This does not admit those
ordinary grips. LMG extraction separates parent motion from part-local hinge/slider
motion and rigid from mixed/deforming skin. Mechanism role labels and native ammo
authority require additional proof. Exact-config descriptors must replace per-name
registration growth before new candidates become playable. No Refractor offsets or
per-weapon state machines are transferred. Full CPU suites pass 148/148 per arch;
new native/headset acceptance remains pending.

## October 3: preserve authored contact pairs and actual item identity

A rifle with an underbarrel alias can also be held as the ordinary native carried
item. Resolve the actual held identity and prove its corresponding route; optional
launcher mapping must not be a prerequisite for ordinary rifle reloading. Keep
equipment and original input leases at every consumer.

When importing a magazine grasp, transfer the wrist-to-item transform and complete
finger hierarchy from the same authored frame. Copying fingers onto an unrelated
VR wrist/item alignment can increase skin intersections. Stable contact extraction
is shared tooling; measured geometry and headset feel remain distinct evidence.

Body equipment rendering can use private XR-host color/depth surfaces while native
scene query/depth integration remains unverified. Carry exact pair/space/equipment
identity and original source expiry; commit both eyes together. This provides solid
prop geometry with self-depth, not native scene/hand occlusion or complete inventory
presentation. Installed vertex bytes never enter public source/packages.

## October 3: preserve completed retirement across policy revisits

Persistent mechanism policies outlive which weapon family is selected. A
dispatcher epoch change cannot erase the exact old cycle's completed stop
proof while leaving that cancelled cycle inside its policy object. Retain the
stop-only fact by underlying policy/identity/cycle; invalidate it on accepted
new Start. Still require current callback exclusion and fresh native source
for every new operation. Test A→B→A→B without a reload on every visit, not only
one-way switching. See HEADSET-20261003-062749.md for the BC2 repair and limits.

## October 3: removal and replacement need distinct physical intent

Reinsertion guidance must not recapture an original magazine during its outward
removal. Replacement capture, retained-original return and visual continuity
have distinct evidence requirements while sharing one mechanism. A temporary
read gap cannot become fresh ammo/seat authority. Generated grasp frames also
need semantic palm/finger axes; contact-point placement alone does not validate
orientation. The revised batch preserves measured rails and marks carry feel
as an estimate pending HMD acceptance. See HEADSET-20261003-053630.md.

## October 3 shared-ammo portability correction

Free carried props need their original sampled coordinate frame as well as
original owner/input deadlines. Applying an old weapon-local target to a newer
weapon pose is a shared integration defect, not a reason for gun-specific reload
code. XM8 and generated AEK now exercise the same moving-weapon regression.
Body supply source/contact publication is common, while native render queries,
scene depth and installed part geometry remain engine-adapter obligations.
An old callback-drain receipt can release an unrelated equipment action block;
it cannot settle ammunition. No Refractor native assumptions are transferred.
See [shared-ammo checkpoint](SHARED-AMMO-20261003.md); GPU/native acceptance pending.

## October 3: separate shared mechanics from generated geometry and native authority

BC2 now consumes authored grip, support, model-axis and magazine profiles through
shared interaction code. A new gun supplies exact asset/configuration, rig/mesh
and reference geometry data. Native family descriptors retain BC2-specific ABI,
timing and client/server ammunition receipts; engine ports must reestablish those
instead of inheriting Refractor or earlier Frostbite offsets.

Keep extracted facts separate from VR design decisions. Magazine closed-part
frames come from matching authored mesh/skeleton/animation resources; carry pose
and magnetic entry rails are estimates until tested. Shared tests must exercise
actual generated profiles, off-center original returns and native failure paths,
plus no-header behavior and accepted-profile priority. A compiled asset count
never implies a playable-weapon count.

Optional reviewed model axes use pure orientation math while retaining native
feature verification gates. Support steering must rotate controller grip and aim
together. The ammo-hand claim outranks support so two consumers cannot move one
hand. Live owner/mesh/rig and original lease checks remain necessary at output
and both eye packs even when all static hashes match.

AEK authored candidate passes offline composition; live selection, ammunition,
firing and HMD acceptance remain pending. Do not reuse disputed AEK-labelled
capture or misidentified XM8 footage as evidence. Chambering stays deferred.

## Generated reference data must reach every live consumer

The BC2 authored grip route now reaches TrackedRig and private native palette
publication. Keep exact configured mesh and rig checks plus the original input
and metadata deadlines through each eye pack and direct contact/shot readers;
checking only retarget/publication leaves other consumers able to use stale data.
Do not compare native input epochs to physical hand/equipment generations as if
they were the same counter. Preserve both domains explicitly.

Regenerate profiles from portable source tools and verify the generated header
matches the tested artifact. Keep established profiles as the baseline and
make experimental reference substitution explicit. An authored reference can
place a weapon at a wrist without proving current submitted skin, mechanical
part semantics, muzzle orientation, reload operation or inventory behavior.
One working generated grip is a connection in the pipeline, not complete
multi-weapon support. Compare a known working weapon before the new one.

## Use authored animation references for batch weapon calibration

BC2 WeaponState.AnimTree1p points to SpecificAnimTreeData and its replacement
AnimationAssets. Resolve GUIDs and Name/Skeleton references, then decode the exact
resource bytes; archive co-occurrence and similar paths are insufficient.
Static authored hand controls can supply a first-pass grip independently of
unverified interior spline interpolation. Keep native child animation during
retargeting and preserve separate aim, muzzle, holster and reload capabilities.
The archive skeleton reproduces the mod's ordered rig fingerprint, allowing the
same data pipeline to check topology/bind conventions against the native adapter.

## Shared mechanisms need shared production consumers

Make selected weapon data reach every consumer: input, grip, presentation,
magazine identity, native timing and completion. A reusable coordinator behind
XM8-specific dispatch is not broad support. The complete consumer now accepts
different profiles; authored data changes must not require another copied
state machine. Family-wide ABI/behavior proof should be reused with exact current
configuration, while pose/mesh correspondence remains a separate data requirement.

The shared path exposed a fixed completion duration left behind after other
timing became configurable. Regression-test differing timing, geometry, capacity
and equipment transitions together. Passing these synthetic tests establishes
code behavior, not a named weapon's native/headset result.

## Capture provenance correction: selected item is not animated mesh identity

The user identified the alleged AEK reload footage as XM8. Asset-labelled pose
samples cannot alone establish a weapon-specific magazine grasp or trajectory,
especially when an equipment exchange reuses objects and both models share a
skeleton. Keep selected equipment, animation source and submitted mesh evidence
distinct. Correlate them before generating a weapon calibration. Actual archive
metadata does not validate the association of a separately captured pose with it.
The proposed AEK pose-derived profile is quarantined; no runtime admission used it.

## October 3, 06:05 UTC: family reuse must reach production consumers

Shared policy beneath weapon-specific dispatch does not deliver a shared weapon
pipeline. Inventory identity, geometry, detach/seat/presentation and native
configuration dispatch must all accept the same selected profile. Verify this
with a second ordinary weapon before expanding polish on the prototype.

Separate executable-wide enum/layout and reload-family evidence from per-asset
data. Read authored actions, bolt parameters, reload timing and exact mesh paths
in batches, then compare actual current native configuration. Do not require a
new implementation just because the asset name changes; do not infer different
mechanics merely from class names. rtMagazine also appears on underbarrel and
belt-fed items, so the native reload enum alone does not prove a box magazine.

BC2 authored persistence is not physical pairing: 20 launcher definitions share
generic 40mmgl persistence while live carried rifle/launcher identities may differ.
Use the actual selected inventory and switching relationships. AEK's side sight
also shows why shared rig fingerprints alone cannot supply per-part geometry.

## October 3, 01:25 Eastern: scale by authored data and shared mechanism

Authored attachment acquisition must settle for every weapon, including those
without an optional aim-alignment profile. Saved AEK data reproduced a 0.376136-m
transient relation error. Equipment replacement needs its own generation even
when native pointers survive; retirement requires fresh ordinary render/input
receipts, and a fresh trigger press, not a fabricated holster Draw acknowledgement.

Separate reload family, geometry roles, native ownership, aim/support/muzzle,
body inventory and underbarrel eligibility. Reusing a magazine policy does not
give Garand/M14 a launcher or make every LMG belt-fed. Batch installed-asset
extraction supplies exact names/hashes/parts and explicit unresolved records;
generated candidates never silently inherit runtime capability from a sibling.

Canonical native matrices use reflected Z: apply the same basis conversion to
raw asset vertices before inverse-bind/pose composition. Otherwise separately
weighted parts can look exploded even when the matrix math is internally valid.

A pump/bolt gesture releases an exact native held cycle; it does not create ammo
or readiness. Endpoint dwell needs fresh distinct samples, and an unchanged
sequence cannot extend a lease. Existing support may transfer through an actual
arbiter mechanism claim; a held grip bit is insufficient. The adapter owns native
closed-part, state, animation-tail and ready receipts. Shared code/CPU tests alone
do not enable a native pump, bolt or LMG mechanism.

When a first-eye private prop expires before the second eye, restore from the
ordinary/current hidden-prop source, never from the already-posed first-eye buffer.
A simultaneous gun magazine and belt prop require separately owned render geometry.

## October 2 headset follow-up: equipment lifetime and visual item ownership

A native weapon address can survive a ground exchange into a different weapon.
Track equipment identity/generation independently from actor anatomy and tracking
space. Invalidate old grip calibration, hand claims and pending publication on
an equipment replacement; do not force a body/recenter reset to repair a gun.

Physical seating transfers a magazine's visual ownership from hand to gun before
native reload completion. Fresh gun/native receipts may maintain that attachment;
elapsed time or appearance must never manufacture ammunition or completion.

Belt supply visuals must use the same body anchor and native reserve as pickup.
The BC2 SPAS has one verified idle shell leaf; its reusable presentation is not
an independent object instance. A magazine simultaneously in the gun and on the
belt needs a separately owned render instance. Do not confuse portable geometry
support with native rendering capability or clone game simulation for a prop.

# 2142 learning and implementation ledger

## October 3: preserve intent through the actual input boundary

An input policy's neutral rearm interval can reject a native input override
without representing a physical Draw. Test the complete ControllerActions →
InputOverride → inventory/holster route: a coordinator-only pause test missed the
outer cancellation. Preserve committed stow only for a registered exact-owner
gap; never preserve stale render leases or hand claims. Native022310 now proves
fresh paired Empty after pause. Scene/death/HMD recovery are separate boundaries.

Normal firing must not clear persistent item/holster identity just because it
suppresses shoulder gestures. Native015859 proves post-draw shots with native
ammo ownership intact. Report positive native shot evidence separately from
incomplete effect-callback coverage. Scoped XM8 now uses the same holster policy
as SPAS through narrow verified profile admission; no extra native slot is added.


## October 3: shared holster policy, narrow per-asset admission

The ordinary scoped-XM8 candidate reuses the SPAS BodyInventory/hand/suppression
policy through acceptance bit2. It adds no weapon-specific state machine, native
offsets or slot capacity. Exact XM8_sp_s plus current XM8/ACOG evidence remains
required. Native015859 proves a matched post-draw shot; complete effect pairing
and combined headset behavior remain separate. Admission awaits the final audit
and root review; see XM8-HOLSTER-DIAGNOSTIC-20261002.md.

## October 2 evening: temporary firing and scene evidence

A temporarily held world gun needs native fire-target/aim/ammo ownership even
before permanent stow. Clear native Fire before making it active, require a new
trigger release, and keep permanent holster assignments distinct from temporary
native equip projection. Post-fire settlement needs actual native consumption;
the earlier immutable pre-fire pickup snapshot cannot be reused after shots.

Physical persistent item IDs and native selected weapon IDs are distinct. The
corrected scoped-XM8 holster fixture pins both correctly and now has paired-eye
proof. Open policy gates still do not prove an actual post-draw shot.

Initial default stow has actual native/visual evidence. Pause/resume exposed an
equipment-epoch transition that loses committed stow despite unchanged native
carried identity. Preserve intent only with exact identity and new suppression/
paired render evidence; do not recycle expired leases. One native scene rebuild
with visible stereo is not a two-checkpoint or headset/death recovery pass, and
a scripted click is not proof that a restart confirmation was reached.


## October 3: shared detachable-magazine geometry profiles

The XM8 rail/removal settings, authored grasp/finger transforms and rig roles now
live in one BC2 geometry profile. Portable interaction/supply/receipt policies
remain shared. Only the existing scoped XM8 is registered; native family, reload
timing and ammunition authority stay separately verified. See
[MAGAZINE-PROFILES-20261003.md](MAGAZINE-PROFILES-20261003.md) for the exact AR/SMG
extension boundary. Offline synthetic profile reuse is not another gun acceptance.


## October 3: native full-mag return and pickup bundle boundaries

The corrected actual consumer001346 returned the same full magazine without any
native reload/ammo operation. Missing count reads must preserve only bounded
stop authority, never create pose/seat proof. Source processing time follows the
read; original input/count expiry stays immutable. Native interaction success and
diagnostic coverage remain separate: two missing after-records keep the strict
audit false despite current paired restoration and exact coherent count receipts.

Physical pickup needs separate world-item lifetime, temporary hand ownership,
chosen carried slot and native exchange/drop receipts. BC2 data contains linked
weapon slots (for attached launchers), so preserve weapon bundles and ammunition
ledgers rather than enforcing a fixed raw-entry count. No new native pickup
capability follows from discovering type/field names or sending Interact.

## October 2: preserve intent without preserving stale authority

Committed body stow is durable user intent. A headset tracking gap must expire old
poses/claims but should not imply a draw. Rebind only an unchanged native carried
item (container, slot/order, weapon/data/persistence/category and actor/equip),
then require new native suppression and paired render proof. This distinction
applies across same-space gaps as well as recenter/space changes. It does not
authorize carrying pose receipts or expiry times into the new tracking epoch.

The headset230526 trace proves four partial retained-magazine returns with exact
count preservation. Full idle-magazine manipulation is separate: timestamp a
processing decision after reading its data while retaining the original source
timestamps. A missing concurrent read is unknown, not changed ammunition; any
bounded continuity must use a genuine unexpired prior lease and require a fresh
post-commit read before new poses/claims/insertion. The235214 native fixture still
cancelled on such a pre-read gap; repair and full-return proof remain pending.
Keep missing diagnostic records visible rather than converting absence to success.

## October 2: full magazines need an idle interaction contract

The full-magazine consumer retains the same portable instance, measured pull and
insertion rail as partial return. BC2 must separately prove ordinary idle native
state and commit actual input-cache suppression before publishing a detached
private pose. It must not fake a reload request solely to manipulate a full mag.
An exact attached pair followed by fresh native attachment/count evidence ends
the interaction; no refill or count write is involved.

Treat interrupted partial reload retirement as higher priority than eligibility
for a newly full magazine. Reset input recognition on verified owner replacement,
while rejecting a same-owner rewind. Focus-loss suppression may stop native actions
using the original timestamps, but cannot renew pose authority. These boundaries
are adapter requirements for future Frostbite games, not transferable offsets.
Mode6 implementation passes both full builds and independent review; native and
headset validation remain pending. Ordinary magazine mode3 is unchanged.

## October 2: retained magazines and cross-weapon recovery

Keep a removed magazine's immutable instance and authoritative round snapshot
separate from replacement reserve. Same-magazine reinsertion is a physical rail
operation with unchanged counts, not a refill. BC2's partial-magazine adapter can
retire its verified native hold and prove fresh idle/attached state before
releasing firing; a full magazine needs a separate idle/suppression contract,
not a fabricated reload acknowledgement. Reuse these portable concepts in later
Frostbite adapters, while independently proving their native state boundaries.

The display hand must follow the authored item grasp while raw contact/pull
input remains independently measured. A contact within grab radius is not an
authored wrist pose. Keep held-item identity through the return rail and reject
owner, round-count or native-pool mismatches.

An accepted hide profile and a permitted Show recovery are different capabilities.
SPAS-only hide admission must not strand an ordinary selected XM8 in recovery.
The live cross-draw215055 recovered with a fresh paired visible receipt and new
GunHold; the original inventory request was interrupted by an owner freshness
gap, so liveness does not prove a clean selection acknowledgement.

Body-relative belt and chest supply contacts share one budget/reservation system.
Adding a contact must not create a second ammunition pool. Static weapon enums
also need semantic checks: BC2's WeaponClassEnum has no sidearm category, so hip
holsters cannot be assigned by inventing a pistol enum value.

## October 2: distinguish bounded recovery from session acceptance

Retired scene state must not be traversed just because an owned view still has
references. The recorded060039/060816 checkpoint fault occurred in the old-scene
view-cache refresh. The corrected controller-free fixture063123 then completed
two actual world/request replacements: fresh stereo and reviewed gameplay in
both eyes for all three scenes, 240 pairs, clean retirement and no observed
exception. Eleven loading timeouts remain reported, rather than erased by the
successful recovery verdict.

This does not transfer acceptance to the complete feature stack. Hands, physical
items, pending ammunition receipts, menu input, recenter and XR connections each
own separate lifetimes. Verify their cancellation/retirement across the same
transition before declaring a full session fixed; death and same-world owner
replacement also require their own evidence. Cross-title adapters need this
proof at their actual native teardown boundary, not a copied BC2 callback.

The later feature-enabled neutral fixture071330 extends that evidence to two
checkpoint retire/rebuild chains with idle hand/rig ownership and exact final
cleanup: all 54 audited checks passed, with both-eye review in all three scenes.
Final restoration must compare the current generation's surviving view order,
not historical child order. Record its 288 receiver timeouts/277 boat waits and
one missing idle after-boundary rather than converting them to pacing or ammo
success. Active item operations, death and headset transitions remain untested.

Export a useful public test card with the runtime, while keeping implementation
history and private diagnostics out. Source CI/build success, native fixtures,
both-eye image review and headset acceptance remain distinct results.

## October 2: runtime receipts, selected camera basis and scene teardown

XM8 physical trace051338 confirms the shared detachable-magazine interaction can
drive a proved native operation: removal, replacement supply, rail insertion and
native completion are separate claims. Keep original operation deadlines through
short native replication gaps and accept only coherent native ammo receipts.

Boat camera anchors must come from the actually selected native camera, not a
similarly named component transform. Fire modes also have distinct native state
graphs: the observed boat automatic mode goes directly through state9 rather than
the rifle5/6 expectation. Input commit counts alone do not establish visible fire;
correlate native state response and both-eye GPU evidence.

Reference-counted owner identity does not guarantee that a retired scene's entity
manager is still usable. Checkpoint dumps060816 prove a view-cache refresh traversed
obsolete scene state during cleanup. Validate each teardown call's dependencies,
preserve the owned-graph detach checks, and prove new-scene stereo after actual
confirmed reloads. Saved CPU diagnostic images may be released after hook quiescence
and successful serialization; retained modules need not retain those pixel buffers.

Public source must be rebuilt from a clean allowlist snapshot, include transitive
launcher dependencies and their notices, and distinguish headset-accepted features
from monitor-only experiments. A successful clean build is not runtime acceptance.

## October 2: native multi-copy completion and launcher families

Freshness failure during asynchronous native replication is not automatically
an operation failure. XM8 trace044723 completed a physical seat before all three
ammo copies transferred; the first transfer caused the all-copy reader to return
no snapshot and the physical consumer cancelled before the other two finished.
Service an already-owned pending operation through fresh structural identity and
its original source deadlines, without treating mismatched/stale counts as an
ammo receipt. Reconcile only a fresh coherent native outcome.

Launcher reloads share operation/hand/supply/rail infrastructure but require
their own native empty-ammo behavior and measured mechanism profiles. The scoped
XM8 launcher normally reloads from zero, outside the positive-loaded rifle abort
proof. Shared firing assets do not establish shared hinges or ammunition pools.
See LAUNCHER-RELOAD-PLAN.md for evidence and the first observation protocol.


## October 2: per-family operations and shared ownership

Scoped-XM8 native fixtures040421/040613 verify held/cancelled native reload and actual all-branch conserved magazine refill. Do not reuse tube-shell single-round completion for a detachable magazine. Keep native original-call lifetime, explicit family selection, per-family request counters and retirement independent. A visual removed magazine is non-spendable when the engine keeps loaded rounds and debits only missing rounds from pooled reserve.

The front boat weapon belongs to the driver through the native nearest-entry ancestor lookup, despite a separate dummy gunner seat and matching route tables. Porting requires proving this association and the selected camera's basis, not inferring authority from seat labels. Head aim should pass through native constrained rotation inputs, and the VR camera must compose head rotation only once.

## October 1: magazine families reuse policy, not shell completion semantics

Actual BC2 XM8 normal reload032717 transfers the missing magazine capacity in one call per native branch, with a timed pre-transfer gate and post-transfer tail. Manual removal must gate that actual action; insertion permission and completion must reconcile the real server/branch transfer. A removed visual magazine is not another spendable inventory source when the engine retains loaded rounds and debits only missing rounds from pooled reserve. Original-before-retarget capture033131 identifies the actual magazine/grasp for presentation.

Perceptible haptic acceptance remains separate from emission and IPC success. Persist dispatch events as they occur and support graceful host shutdown; terminating the host loses buffered final counters. User-approved SPAS/sight success is retained even while capacity/ack ordering and stronger haptics remain follow-ups. Vehicle camera anchoring must not depend on a temporary input admission gap, and turret-following cameras must not apply HMD yaw twice.

## October 1 headset acceptance: physical feedback completes the evidence

BC2 revision5 bottom-entry SPAS RailContact and anatomical launcher-sight contact passed the user's headset test in trace030952. The user saw magnetic loading and found manual reload feel decent, validating the portable separation of raw recognition, guided presentation and native ammunition completion in this adapter. This does not prove other weapons or later Frostbite bindings. Shell vibration was absent or too subtle to the user despite fake-runtime coverage; audit actual delivery before tuning amplitude/duration. Preserve this accepted build and distinguish visual/interaction acceptance from haptic and checkpoint-recovery acceptance. See reports/headset-reload-sight-success-030952.json.

## October 2: rendering ownership must follow level lifetime

Treat campaign reload as a native owner transition, not a tracking reconnect. A game may reuse its main-camera address while an extra VR view keeps an old request/world alive. Comparing addresses and returning forever leaves VR black while normal rendering continues. Retire only the adapter's verified orphan subtree at the native completed-work boundary; preserve owner lifetime through destruction callbacks, verify removals, then acquire fresh scene state. Never restore a retired camera snapshot onto a rebound address. Portable transition policy does not prove another Frostbite title's refcounts, ABI, callbacks or teardown phase.

Reload feedback also separates recognizer capture from engine completion. A short vibration marks magnetic attachment; a different pulse is emitted only after the engine confirms the ammunition receipt. Carry original source deadlines across IPC and discard queued feedback on focus/reference changes. A successful native receipt cannot override a failed user experience.

## October 2: ending ownership does not end the engine's action

BC2 demonstrated that releasing a manual reload hold restores the native reload loop and can let it insert more rounds. Revoking VR input/presentation ownership is therefore separate from asking the engine to stop its already-running action. Model bounded stop-only cleanup independently from ammunition claims, pending receipts and callback retirement.

Keep the original owner's deadline and current identity/revision/cycle through cleanup; never renew an old capability merely to finish an animation. Prove the native helper, ABI and exact side effects, call it on the real Update stack, preserve caller context, and observe the subsequent native transition. A branch may already be stopped through an ordinary engine state copy; record that separately from an actual helper invocation.

BC2 trace015932 proves the narrow post-receipt, neutral cancellation subset at7/14. The reusable lesson is lifecycle separation; the helper, state numbers and native topology remain BC2-specific and must be rediscovered for later Frostbite games. Native idle and callback retirement do not prove visual animation completion. Full-magazine tails and earlier cancellation phases remain distinct work.

## October 2: preserve wrist calibration and separate shell presentation from ammunition

A physical prop must use the same anatomical wrist mapping before and after acquisition. Reusing a weapon's authored support grip for free ammunition silently changes orientation even when the controller has not moved. Keep raw recognition contact upstream of IK/guidance, with one consistent anatomical mapping.

Ammo authority and native prop visibility have different lifetimes. An accepted native transfer may remove the held item while the ordinary animation still draws its prop. Active-cycle presentation control can suppress that exact skin leaf without inventing an ammo claim or allThreeHeld insertion permission. Recheck the original source and current cycle at each native consumer. Cancellation must revoke unpublished output as well as persistent state.

Three headset-triggered native receipts do not overrule the user's rejected presentation. Record reasons before cancellation destroys state; preserve bounded input and original lease evidence. A limited initial recorder cannot diagnose transfers that happen later in a long session. Animation tail completion remains separate from native callback retirement.

## October 2: separate ammunition orientation from insertion travel

A shell's long axis does not necessarily describe the hand's loading trajectory. BC2 SPAS geometry exposed this: the nose points forward while the reload approaches from underneath. Portable insertion profiles now express travel separately from item orientation, while BC2 owns its measured entry transform and direction. Keep grasp, item rotation, path progress and native completion distinct. Default +Z profiles retain their existing behavior.

A scripted reload can pass native ammunition accounting while using the wrong physical approach. Validate the path against the rendered weapon, including the complete prop's initial clearance, rather than merely reconstructing its endpoint. Distinguish a measured pre-transfer pose from a verified loading socket. A closed sampled model face remains a visual limitation, not evidence for an open port.

Recorded reduced telemetry can test known angle/distance restrictions but cannot reconstruct missing full rotations exactly. Preserve the tested profile revision and entry transform when replaying it. Revision 4's bottom-entry path passes offline builds; headset and native acceptance remain pending.

## October 1: physical capture versus rendered guidance

A recognizer can return valid raw targets while still Free. That is not rail
capture and must not pin a carried prop to older gesture geometry. Keep original
input evidence for recognition while drawing free-carried objects from the
current renderer wrist. Guided presentation requires an actual Guided/Seated
phase. This distinction is portable across engine adapters.

Make approach policy explicit in each insertion profile. BC2 SPAS revision2
accepts front-hemisphere entry from underneath into the same capture sphere;
other profiles retain axial approach. Native ammunition acknowledgement is
still required. Preserve per-item nearest-contact evidence alongside bounded
recent history so unsuccessful headset gestures can be diagnosed after a pause.

Separate game rendering, host frame delivery and remote streaming evidence.
Current Steam Link resets/timeouts coincide with a failed headset run, while a
different earlier run retained an Armed native view. Neither establishes the
other's cause. Exact read-order diagnostics are preferable to relaxing unknown
native ownership checks.

## October 1: repeated physical insertion and native readiness

Keep physical acquisition separate from native reload readiness. A player can
hold ammunition while a pump animation runs; retain the same item/claim, then
start only from a fresh coherent native readiness observation. Cached reserve
credit cannot dispatch Reload. Release, focus, actor/equip or input expiry still
cancel normally; do not extend source timestamps to wait.

BC2 trace231748-438 completed two distinct reserve-backed items and native
transfers in one reload cycle,6/24 ->7/23 ->8/22. This verifies the actual BC2
single-shell consumer under scripted motion, not other weapons or titles. The
same supply/claim/rail/receipt logic is reusable; engine readiness, ammunition
authority and mesh/grasp bindings remain adapter work. Both-architecture full
builds pass96/95; one recovered stereo timeout and unexercised fallback remain
explicit in the saved audit.

## October 1: source cadence, complete retries and actual visibility

Schedule a producer and its throttled resolver using the same original clock.
Coarse wall-clock scheduling against a fine source timestamp can skip a refresh
and create a real lease gap. Fix scheduling; do not extend an old observation.

A strict concurrent state-read failure can permit one entirely fresh coherent
read with the same owner/config/deadline. Never reuse a successful prefix from
the failed attempt. Preserve first-failure provenance and the retry outcome.

An issued trigger/reload command is not native acknowledgement. Pump cooldown
can consume the whole input pulse. Physical acquisition and native reload
readiness are separate; a carried shell should survive a wait for native readiness.

Palette-copy receipts and actual eye pixels establish different facts. BC2 now
has sampled SPAS both-eye hide/restore proof with arms preserved. This does not
by itself establish free hands, body inventory commit or full firing suppression.

## October 1: delayed renderer contact and body equipment

A physical contact may arrive after the policy already processed that input
generation. Retain its immutable item/claim/seat/native-cycle evidence until a
fresh packet can consume it; validate both original expiry and current ownership.
Do not drop a one-shot contact or restamp it as current input. Cancellation must
continue polling native retirement and fresh reserve before allowing acquisition.
BC2's first actual connected physical SPAS insertion now passes in native gameplay;
repeat loading and headset acceptance remain separate.

Body inventory uses actual carried instances and category preferences rather than
permanent slot/type assumptions. Shared chest/shoulder geometry and hand ownership
can port forward; reflected categories, native selector routes, selected rig and
visibility receipts stay in the game adapter. Shoulder selection is now integrated;
true holstering still needs reversible weapon hiding and native fire suppression.

## October 1: ownership, contention and shared body equipment

Ordinary concurrent client/server callbacks are not ownership loss. Keep exclusive
start/retirement admission separate from shared callback entry, and distinguish
brief in-memory lock contention from failed native identity evidence. Never hold
a policy lock over an original engine call. Bounded waiting must retain source
timestamps/deadlines and cancel on genuine exhaustion or owner loss.

Structural ownership reads should not depend on unrelated mutable reload timers.
Keep full state checks at the actual operation boundary. A demonstrated phase-only
read race can trigger one fresh complete read; it cannot authorize reusing a partial
snapshot or widening an identity mask without native evidence.

Chest ammunition and shoulder weapons share a configurable body reference and hand
arbiter. Dynamic pickups update physical item generations and native slot bindings.
Keep ammo reserve, visual representation, native selected weapon and empty-hand
presentation separate. Refractor's proven IK/crossplay design is a reference for
future Frostbite multiplayer; native player, rig and authority bindings stay local.

## October 1: physical consumer and native clock ownership

Separate policy processing time from the original input/native observation time.
A native sample can be collected before another API call advances the policy clock;
evaluate it using a current clock sampled inside the policy lock, while retaining
all original observation timestamps and deadlines. Never restamp old evidence.

Native reload animation can hide a prop independently of the held firing timer.
An exact owned prop may need a scoped visibility replacement in the private palette.
Retain the raw native source, every unrelated hidden leaf, and an immediate ordinary
palette fallback when ownership expires. This is not a generic animation override.

Component completion is distinct from Gameplay integration, native acceptance and
headset acceptance. Record each explicitly. A short one-round native pass does not
prove a persistent hand-driven loop or other weapon families.

## October 1: supply and presentation integration boundaries

Ammo acquisition must not require a reload cycle that itself starts only after
acquisition. An adapter supplies a genuine fresh reserve observation independent
of its cycle; shared AmmoSupply creates only a virtual representation of existing
reserve. Insertion still requires the exact native held cycle and real completion.
Carried presentation has no insertion authority; Guided/Pending preserve native
cycle identity. Keep physical and native equip generations separate in the bridge.

Keep original input/geometry sequence and deadlines across renderer-to-gameplay
handoff. The arbiter's current safety may cancel an old contact; it must not
retimestamp it. Exact reservation identity prevents a delayed acknowledgement
from consuming a replacement item. Unknown outcomes require proven native
cancel/drain followed by fresh counts before local rebaselining, never ammo undo.

An owned bone-palette operation and a per-draw shader operation have different
evidence requirements. For the exact SPAS rig, original-source checks plus the
authored single-entry shell skin and77 stereo frames of actual packed bone
equality support private bone placement. They do not authorize reticle/draw
suppression. Keep configured mesh, evaluated palette and submitted GPU identity
separate and report the actual proof used.

## October 1: reload families and actual palette ownership

Reuse physical intent, hand claims, guided insertion, request/acknowledgement,
and tracking-loss handling across weapons and engines. Supply weapon-specific
mesh/rig/grasp/socket evidence through profiles. Native firing ABI, prediction,
server ownership and completion remain engine/game bindings. BC2's currently
enabled diagnostic gate deliberately accepts only the exact campaign SPAS.
Magazine, belt-fed, launcher and pump/bolt interactions are separate family
extensions; they are not proven by the SPAS single-shell native pass.

The persistent request policy must not inherit diagnostic lifetime/capacity:
fresh real input can sustain a slow multi-shell reload, while each requested
insertion still has a bounded completion deadline. Original input timestamps,
exact pending item/claim identity and native ammo conservation remain mandatory.

BC2's palette worker can batch derived shadow/auxiliary views with its primary.
Exact constructor, table, request and parent relationships admit those views;
arbitrary type widening does not. Current/previous packed copies are not stereo
eyes. Matching draw geometry or configured mesh names does not establish skin
ownership. Trace194835 records exact owned shell bytes in both SPAS sections and
zero matches in the optic control; left-eye consumer proof remains pending.

## October 1: verified boat consumer and seated reference

The PBLB consumer supplies continuous axes to the actual native gather buffer,

separately from infantry snap turning and weapon cycling. Its action 4 means

steering. Available route names alone do not prove a working native camera

consumer; exact seat/reflection/router evidence selects the BC2 driver profile.

A portable per-seat anchor removes accumulated walking offset while preserving

native vehicle motion and runtime eye poses. Sixteen paired native post-setter

samples verify 60 cm / 20 degree entry cancellation, 10 cm lean/return and

64 mm IPD. Cache readbacks prove both control pulses and neutral release. These

measurements do not establish headset driving acceptance. Later Frostbite ports

can reuse the policy but must supply their own seat, input and camera bindings.

Reload hand requests and native acknowledgements retain separate equip identities

and exact shell/claim reservations. Releasing or replacing a hand claim cannot

consume a newly grabbed item when an older insertion completes. Ownership loss

does not roll back native ammunition. The bridge is tested and default off.

## October1 native hold and boat integration lessons

BC2 prediction temporarily toggles soldier flag0x10 inside one client update;

accept that proven phase difference between owner publication and invocation, but

keep exact before/after identity checks. A native350ms hold now covers both client

copies and the campaign server:70 restored invocations per branch, ordinary

conserved transfers after release. No ammo/state writes or simulation replay are

needed for this bounded experiment. Physical completion still requires its own gate.

Read-only GPU fingerprints now identify the actual ACOG dot section in both eyes;

geometry identity does not imply current item/skin ownership or authorize a draw

change. Filter unsupported layouts before diagnostic allocation so unrelated world

geometry cannot exhaust the per-frame capture budget.

The user's boat test exposed an integration gap: portable driver/gunner planners

alone cannot supply vehicle playability. Each Frostbite title must bind its actual

seat/router actions and own a seated tracking anchor across entry/exit/space changes.

The BC2 boat implementation is in progress, not accepted. No Refractor offsets or

vehicle camera conventions were copied.

The source is `<local-bf2142-workspace>\public-source\BF2142VR`, including its local changes

through the v35b development handoff. This is newer than the published v30

alpha. The manifest records the source HEAD and individual source hashes;

HEAD alone does not identify the modified working tree. Files in the original

2142 source, its runtimes, launchers and cloud setup were not changed.

| 2142 system / lesson | This workspace | Frostbite/BC2 work still required |

| --- | --- | --- |

| Asymmetric per-eye projection, head/eye separation | Shared math, explicit RH/padded conversion, BC2 owned-camera builder; eight native matrix cases pass | Visibility-stage pose pulse visibly controls/restores BC2 geometry; production ownership, native eye pair and physical unit calibration remain |

| Recoil-free view and coherent movement heading | Imported ComfortCamera, MovementFrame, tests | Verify camera/input split without removing gameplay recoil |

| Exact inverse of near-rigid animated bases (v35b) | Imported TrackingMath; regression for small native scale | Bind native animation matrices and verify palm axes |

| UI aim ray and panel mapping | Portable pointer/quad math, separate menu IPC/GPU transport, owned native cursor/buttons; BC2 menu roundtrips and fake-XR GPU tests pass | Headset acceptance, non16:9 native canvas mapping and persistent desktop visibility |

| Snap turns and standing/recenter references | Imported policies | Runtime actions and native yaw/body application |

| Crouch/prone | Shared semantic posture policy; explicit support flags | BC2 stance capabilities/mapping; no inherited prone toggle |

| ADS hysteresis and cancelled reload/sprint recovery | Imported AutoAdsPolicy | BC2 optic classification and native aim/fire behavior |

| Grip / empty hands | Imported WeaponGrip policy | BC2 held-item identity, native equip/hide and hand binding |

| Arm IK anatomy / fingers | Imported arm-pole math; topology-driven rig validation | Frostbite rig discovery, full solver binding, finger chains/curls |

| Native LOD animation reusing previous IK writes | Generic exact-write AnimationWriteCache and regression | Invoke at correct animation boundaries; stable actor generation |

| Render twice, simulate once | FrameCoordinator, separate visibility capability, shared conservative stereo culling; native plane containment passes | Staged cross-thread tracking handoff implemented/tested; native eye rendering and target/history restoration remain unfinished |

| Both eyes restore/submit together | State scope, whole-pair failure tests; D3D11 keyed-mutex pair bridge with real x86-to-x64 pixel verification | Connect verified native eye targets and cache restoration |

| Loading/device loss | Explicit inactive/unknown/loading states and epochs | Native lifecycle events and OpenXR frame pacing |

| Legacy shader low-address crash | Documented lesson only | Do not transplant Refractor d3dx9_29 allocator workaround |

| Two-hand support and physical body inventory | Design requirements retained | Generic item/slot policies plus BC2 anatomy/actions/attachments |

| Grenade/knife/support interactions | Design requirements retained | Authoritative BC2 projectile/melee/equipment interfaces; do not copy gravity/launch constants |

| Vehicle comfort, turret limits, seats, parachute | Capability reserved and independently gated | BC2 seat/articulation/traversal profiles |

| Missing weapon surfaces and scopes | Asset-repair approach retained | Inspect BC2 first-person meshes/materials; no blanket two-sided rendering |

| 3D lobby/menus | Presentation separation retained | BC2 assets/content pipeline and native menu integration |

| x64 OpenXR + x86 game split | x86-to-x64 GPU transport verified; x64 OpenXR host and strict pose/image matching implemented | Native producer/controller actions and post-fix headset acceptance; fixed-width tracking/ticket IPC now passes the cross-process GPU probe |

| Remote weapon roots vs body equipment | Preserve distinct semantic rig groups | BC2 remote animation/weapon identity; no 2142 mesh index lists |

| Remote IK + finger curls | Protocol/schema lessons recorded | New BC2 ownership/replication integration and real sender/observer acceptance |

| Shoulder radio and proximity voice | Interaction/device/focus lessons recorded | BC2 radio bindings, device selection, authenticated session mapping; no microphone activation here |

| Packaging and rollback | Separate mod workspace, no game changes | Reversible install/launch after native implementation; game/headset validation of exact payload |

No borrowed behavior is advertised as working in BC2 solely because its math

passes tests. No native Refractor offsets, DLL names, skeleton indices, item

lists, archive loaders, server addresses or credentials belong in this adapter.

## Specific regressions retained

- Cancel partial eye pairs, always restore native state, prevent replay of a

  failed native frame, and do not flatten gameplay into a menu panel.

- Focus loss and stale controls release held actions. New owners/respawns and

  regained focus require a neutral sample before buttons can trigger again.

- Duplicate tracking packets maintain locomotion without repeating press edges

  or snap turns; movement diagonals have bounded magnitude.

- Zero-height recenter is valid. Brief tracking dropout is not automatic

  evidence that the headset was removed.

- Native animation restoration uses exact previous writes and actor generation;

  it leaves subsequent native modifications alone.

- Signature bytes alone do not validate a native ABI or object relationship.

The latest native probe also captured BC2 world color after its original draw.

This proves a mono output path only; it does not establish stereo rendering.

The staged producer now preserves one immutable tracking sample from visibility

through graphics publication, with owner/device/native-frame and connection

identity checks, no retry of a consumed/cancelled native frame, and graphics-

thread-only GPU feedback. Both architectures pass ownership/cancellation tests;

the x86 staged GPU fixture delivered four verified pairs to x64. This exercises

the reusable framework and does not establish BC2 eye rendering. BC2-specific

culling now rejects the native vertical frustum clamp instead of silently

shrinking a requested cone. The inactive native factory experiment remains

quarantined after the 11:35:09 exit; see CRASH-20260925.md.

2026-09-25 native rendering update: the first distinct BC2 same-frame image pair

passes (native-trace-20260925-183733-545, frame93997). Native child-view setup,

borrowed and cached view lists, and a32-entry renderer command pool all required

BC2-specific ownership work. Shared policies were insufficient without these

bindings. One original world update produced both views; original pool/list

ownership restored and15-second stability check passed. Head tracking, sustained

native production, and headset acceptance remain pending; capability bits stay0.

## Historical September 25 headset result

The native BC2 producer now delivers tracked same-frame eye pairs through the

shared protocol-v2 D3D11 bridge to x64 OpenXR. A sustained offscreen 120-pair

run passed; the real headset run submitted 426 pairs but FAILED visual acceptance:

user reported "double image + insane flickering". Camera/list/callback/pool

restoration passed, with no crash during the bounded post-test observation.

Transfer lesson: correct native frame identity, GPU ownership and restoration

are necessary but do not establish binocular fusion or comfortable presentation.

The XR host currently submits zero layers on missing fresh pairs; investigate

continuity in the shared presenter. Diagnose eye/FOV/pose/scale/native-history

mismatch separately in the appropriate module. Do not weaken tracking identity

checks to conceal missed frames. BC2 native offsets remain adapter-local.

Production capabilities stay zero. Paused until September 30; see HANDOFF.md and

reports/native-xr-20260925-203409-570/visual-acceptance.json for exact evidence.

## September 27 transfer update

The user resumed work. RetainedPresentation, asynchronous frame-channel polling

and the OpenXR host changes are reusable framework components. The host preserves

original pose/FOV metadata across misses, validates candidate poses before release,

and protects retained images through private scratch copies. Tests cover focus,

tracking, recenter, stale age, partial-copy rejection and malformed provider poses.

Both architectures and real D3D11/OpenXR-host fixture tests pass; no HMD was used.

The BC2 adapter still fails controlled projection acceptance: both geometry passes

receive right-eye VP X/Y/W rows. This is separate from the working camera-math and

transport checks. Native gather resets command/constant storage between views;

proper per-view job lifetime/scheduling needs more work. A late right-gather

experiment stalled in native job wait, was saved for analysis, and was reverted.

Neither large CPU/GPU capacity nor different image hashes establish correct stereo.

The same lesson applies when binding BF3/4 or Battlefront II: reuse shared policy,

but independently prove each engine version's job, resource and view ownership.

Full rig/IK, native hand/input and weapon bindings remain incomplete.

See RENDERING-REPAIR-20260927.md and the current HANDOFF.md checkpoint.

September 27 small tooling follow-up: Test-Offline.ps1 collects both architecture

builds/regressions and optional headset-free GPU checks into one dated report.

It preserves failures and explicitly distinguishes skipped GPU tests, native

rendering (untested here), and headset acceptance (untested here). This gives

future Frostbite adapters a repeatable check of the shared framework before their

native bindings are exercised. Actual run: 21 + 20 suites, all three GPU checks

passed; controlled failure propagation also verified. Native projection status

and production capabilities remain unchanged.

## September 27 native camera-binding repair

The known BC2 wrong-eye projection now passes bounded native image/GPU checks.

The left camera constants were not overwritten: native batches consumed the last

gather's bindings. Matching main/depth-slice roles and caller identities lets the

probe scope the intended values through command preparation AND drawing without

replaying simulation or moving native job waits. Every write restores exactly.

Portable addition: runtime/ExactWriteBatch, with atomic batch validation, overlap

rejection and preservation of later owner writes. BC2 addresses, role discovery

and native scheduling remain in the adapter; this is a strategy for later Frostbite

ports, not evidence that BF3/BF4/Battlefront share the same structures.

Image inequality alone never proves stereo. Keep controlled FOV displacement and

sampled GPU camera ownership checks. Decode shader VP/WVP layouts explicitly;

unknown layouts are inconclusive. 288 native pairs across static, moving-pose and

FOV-control runs completed; sampled geometry and restoration pass. Headset fusion,

flicker, physical scale, HUD/weapons and IK still require their own acceptance.

## September 27 headset retest result

The user reports the corrected native headset image is successful except for a

small direction-dependent black-dot artifact. The 30-second SteamVR/OpenXR test

received 909 fresh pairs, 0 rejected pairs, correct sampled GPU eye matrices and

clean native restoration. Preserve this working source/binary checkpoint before

artifact work: reports/headset-success-20260927-205855/checkpoint.json.

Treat residual pass-specific artifacts independently: a correct geometry VP does

not prove decals, particles, culling/LOD, temporal history or HUD use that same

eye. The reported dots are not yet attributed to a specific subsystem.

## September 28 derived projection repair

Correct main camera bindings can coexist with wrong projection-only effect

contexts. BC2 worker jobs created separate depth-biased projections for mesh

decals/terrain; three sampled passes combined left view with right projection.

Scope those verified derived bindings as well, preserving their exact clip-Z

column. math/ProjectionOverride.h is portable; discovery and native binding

ownership remain adapter-specific. The repaired captures place boat marks on

the correct surface and remove the three measured mixed-camera samples.

The sampled GPU checker now includes mixed view/projection negative controls;

unknown shader layouts stay unknown. 32 moving-pose and 16 FOV-control pairs

pass with exact restoration. Headset confirmation of the user's dots remains

pending; see ARTIFACT-REPAIR-20260928.md. No Refractor native types or offsets

were added, and BF2142 files were not changed.

## September 28: separate render lifetime from CPU wait budget

A fast GPU does not remove the frame-boundary latency of split game/XR processes.

At60 nativeFPS, the fenced render/export handoff exceeded the old50ms request

limit. Shared IPC now uses a150ms async lifetime (cap200), retaining a50ms maximum

for synchronous waits, immutable source poses and250ms presentation age. The

corrected live check consumed96/96 pairs at60FPS with zero timeouts/discards;

40completed after50ms. This framework change is independent of BC2 offsets.

Both architectures and real GPU transport pass; headset acceptance pending.

## September 28: headset cadence must not inherit the desktop VSync wait

A qualified headset success (dots appeared gone, image solid) exposed quick-turn

lag. The BC2 desktop Present still requested SyncInterval=1. An opt-in bounded

adapter hook now feeds the verified desktop chain through a shared DXGI pacing

helper, skipping only its active capture wait while preserving flags/errors and

native setting values.160/160 live pairs pass; native cadence rose60 to~197FPS

and average handoff dropped52.8 to18.7ms. Headset acceptance of pacing is pending.

The native owner checks remain in BC2; the call policy can serve future D3D11

Frostbite adapters. This does not change simulation/job ordering or relax IPC.

The pacing candidate then received strong qualified headset feedback: excellent

for most of the run with late drops. Preserve headset-best-20260928-132241.

Steam Link packet-drop/reset and compositor timeout logs overlap this test;

streaming health must be measured independently of the native rendering path.

The candidate remains opt-in; no global SteamVR settings were altered.

Continuous sessions now follow held game/host process lifetimes rather than

restarting short probes. Native cleanup on host exit, user game close, immutable

poses and one-second telemetry remain separate concerns. The latest user test

is deliberately left running; pause development afterward per their request.

The user's interruption was in the middle, not progressive degradation.

September 28 continuous headset rendering subsequently received explicit positive

user confirmation ("Solid. I think you did it."). The accepted exact source and

binaries are preserved at reports/headset-success-20260928-133532. Development is

paused as requested; this does not promote unfinished interaction/IK/HUD features.

## September 30 controller and first-person transfer checkpoint

Portable action sampling, neutral arming, focus/tracking/owner release, snap turn,

HMD-relative movement and recenter now reach a BC2-specific native input binding.

The IPC input lane is independent of rendering and expires by original QPC time.

BC2 command buffers must persist beyond player-update return; restoring them early

cancelled movement. Commit until the next native gather, which rebuilds 49 actions.

This is not the render/animation exact-restore policy; those scopes still restore.

Native controls and independent angular weapon aim have actual campaign evidence.

The native view has an additional first-person transform; leaving it as identity

in the new view hid both guns. The first-person object branch also applies authored

desktop FOV. Verified per-eye anchor, projection and command-context correction

restores native gun/arms while simulating once. No Refractor offsets or rig indices

were imported. This is reusable architecture with a BC2-specific binding, not proof

that later Frostbite games share these offsets or object layouts.

Transferred math alone still does not establish IK, weapon translation/roll,

physical inventory/reload, collision-following roomscale or network replication.

Actual HMD/controller acceptance is pending. See CONTROLS-20260930.md and the

September 30 handoff for exact builds/tests and limitations.

### September 30, body-follow pilot

Portable RoomscaleFollow requests native locomotion and accounts for measured

collision displacement; BC2 alone binds position discovery, analog thresholds,

input dispatch and camera compensation. A 40 cm step / return now passes 240

native stereo pairs with a 12 cm lean radius. No Refractor collider offsets or

teleport implementation were carried across. Headset comfort, head-wall behavior

and vertical collider support remain separate gates. First-person pose observation

shows native recoil continues, and animation jobs execute on multiple threads;

hand translation/roll and IK writes remain disabled pending their own binding.

## September 30 headset acceptance and next scope

The user reports the controller/angular-aim/body-follow test was successful and

asks to continue. Exact source/binaries and feedback are preserved under

reports/headset-controls-success-20260930-174139. The LOD-like rendering issue

is explicitly deferred; its cause is unknown. Continue weapon position/roll and

native rig/IK work. This feedback does not establish full hand poses, wall-safe

head movement, vertical collider tracking or unsupported vehicle controls.

## September 30 - shared arm solver, BC2-specific evaluated rig

ArmIk is now implemented in the portable interaction core. It solves bounded

shoulder/elbow/wrist chains with controller wrist roll and carries native finger

and twist children in their segment frames. The BC2Rig adapter resolves names,

parents, handedness and palette layout independently; no 2142 index table or

native animation layout was imported. Its first campaign rig contains 147 bones.

BC2 exposes an ordinary world/skin pose and a separate evaluated IK skin palette.

Preserve the evaluated pose as the input to further retargeting, rather than

silently replacing native IK. Twelve copied-pose cases passed on that live rig;

native bone publication is still disabled. Getter/producer signatures, native

ownership and skin/bind consistency are checked separately from shared math.

Later Frostbite adapters must supply their own rig/palette producer and consumer

bindings; these BC2 offsets do not establish BF3/BF4/Battlefront compatibility.

See the newest HANDOFF section for exact reports and remaining publication,

grip-calibration and fire-origin work. LOD/render investigation remains deferred.

## September 30 - live tracked arms and weapon publication

The portable rig/IK increment now reaches visible BC2 meshes. Shared TrackedRig

calibrates grip position and orientation separately, uses current body/reference

space, and resets on actor generation, equip, skeleton or recenter changes.

Shared RetargetRigSubtree carries current native attachment-child animation.

BC2 supplies verified named arm roles, its weapon root and handedness conversion.

No Refractor offsets, native layouts or bone-index lists were transferred.

The engine-specific lifetime solution is a synchronous skin-palette packer:

private solved matrices are copied into the native request arena while original

animation sources remain untouched. BC2 also replaces a verified caller-owned

weapon-world result for native root/effect consumers. This avoids persistent bone

writes; it does not license blindly patching animation memory in BF3/BF4.

Final combined campaign evidence: native-trace-20260930-190048-929, 240/240 pairs,

1231 tracked poses and 2462 modified copies, zero unexpected tracking/owner/source/

packing/GPU/restoration failures. Tracking dropout and ordinary native fallback

observed; 40 cm roomscale input consumed 28.17 cm of native collision movement.

Both architectures pass (31/30 tests), actual executable discovery 51 mutations.

See HANDS-20260930.md. New hand/weapon poses await headset acceptance; translated

projectile origin, recoil/reload behavior, physical two-hand grips and finger input

remain unfinished. The user deferred the LOD-like rendering issue.

## September 30 evening - separate tracked anatomy from native weapon aim

Independent wrist targets alone do not ensure independent hands. BC2 rotates both

shoulders with native weapon aim, causing the opposite wrist to clamp to arm reach.

Shared TrackedRig now calibrates shoulder origins/poles in the body frame; ArmIk

accepts these origins while retaining current native limb lengths and descendant

animation. BC2 provides its own body and evaluated palette binding. A native yaw

sweep reduced stationary-left wrist drift from 24.57 cm to 0.197 mm, with no reach

error. Combined hand/roomscale checks and 31/30 suites pass; headset retest pending.

Later Frostbite adapters must verify their own body/aim split and rig publication.

Death crash is unresolved: grenade probes did not kill this campaign player and

therefore did not test respawn. See HAND-COUPLING-20260930.md.

## September 30 independent tracking and physical torso

Shared TrackedRig, TrackedBodyFrame and PhysicalTorsoFrame separate wrist tracking,

physical shoulder anchors and native collision-body movement. Tracking loss, action

re-arming and weapon-flick edges are independent per hand. Equip changes preserve

anatomical calibration; owner/skeleton/space changes reset it. Native reload-root

sway must not drag stationary physical wrists, and a lagging roomscale collider

must not pin shoulders behind the physical torso. BC2 alone binds character position,

input enums, weapon identity and animation publication. Native sources stay untouched.

See HAND-TRANSITIONS-20260930.md for three passing native runs and headset limits.

## October 1 native firing events and shared muzzle history

PlaceFireAtMuzzle preserves the final native aiming/spread basis and replaces only

origin, AFTER native authored offsets. FiringPoseHistory shares the first published

muzzle between consumers of the same adapter-verified event, keeping original

tracking deadlines and opaque owner/equipment/space identities. Its storage and

behavior are independent from diagnostic record capacity and contain no BC2 APIs.

BC2 binds both client effects and local-server firing, proves player pairing through

constructor ID/indexed-array links, and matches the context seed already used by

both native spread paths. Its muzzle comes from a named, topology-validated

attachment in the private published rig pose. Independently sampled recoil poses

proved insufficient on SPAS-12. Immutable publications and bounded guard-read retry

avoid borrowing animation memory or blocking on its mutex. Only caller-owned XYZ

output bytes change; sources, native angles, padding and animation remain intact.

The opt-in campaign -MuzzleFire pilot passes the shotgun timing check and a full

BC2XrHost test through a separate explicit test-only XR runtime: 507 native pairs,

34 muzzle writes, 18 beyond the log limit, zero fallback/source/restoration errors.

The real system runtime is not selected. Native executable bindings remain BC2-local;

BF3/BF4 require their own owner/event/attachment/callback proof. Production metadata

capabilities remain gated, and headset/impact acceptance is not established. See

FIRING-ORIGIN-20260930.md and reports/muzzle-fire-20261001.json.

## October 1: explicit support grip carried into the Frostbite boundary

The two-hand lesson is implemented in shared SupportGrip: intentional squeeze and

release, fresh owner/contact identity, neutral rearm after loss, and no coupling

while the off-hand is free. BC2 supplies its own validated wrist/weapon anatomy;

no Refractor indices or transforms are copied. Native XM8/SPAS grip checks and

XM8 supported firing through the actual XR host pass. Headset feel, physical

inventory/reload and additional firearm classes remain unverified. Detailed

evidence: TWO-HAND-SUPPORT-20261001.md; checkpoint:

reports/two-hand-support-20261001/checkpoint.json.

## October 1: headset removal, alignment and native hidden geometry

The user rejected the latest body's position and reconnect behavior. Shared

RecenterPolicy now drives actual OpenXR recovery; eyes, hands and torso share the

same reference generation. Rig actor identity survives input pauses. Absolute

wrist translation and BindAnatomyFrame keep controller startup placement and

native gun animation from defining physical anatomy. BC2 supplies its own

verified camera, actor and bind landmarks; no Refractor offsets or bones transfer.

The SPAS reload hides one native weapon leaf with a collapsed transform. BC2

validates that observed named-leaf pattern and preserves its bytes; portable IK

and subtree helpers keep unrelated geometry out of arm solving. Other Frostbite

adapters must establish their own hidden-geometry conventions. Native recovery,

roomscale and SPAS checks pass; the corrected build still needs headset feedback.

Physical body weapon grabbing is explicitly deferred, as are death-crash and LOD

work. See HEADSET-CALIBRATION-20261001.md and the headset-recovery checkpoint.

## October 1: delayed equipment and headset presence

The latest headset feedback exposed a Frostbite-specific ordering: equipped item

changes before its animation does. Shared attachment settling prevents capturing

the old gun's grip; recenter preserves that attachment. Shared absolute XR aim

removes arbitrary startup wrist-angle calibration; BC2 alone supplies verified

gun axes. Intentional support grip now attaches the published off-hand while raw

controller contact remains independent. This preserves the 2142 free-hand lesson

without copying its bones or item offsets.

Optional OpenXR user-presence events address removal that leaves tracking/focus

valid. The host logs events durably; all adapters share the new reference epoch.

Native switches, presence recovery and supported firing pass the checks recorded

in WEAPON-ALIGNMENT-20261001.md. Actual headset wear/reconnect and hand feel still

need acceptance. Empty-hand fingers, physical inventory and renderer polish remain

deferred rather than represented as implemented.

## October 1: profile pipeline instead of per-weapon runtime forks

The user requested parallel agents and reusable batch work. Shared WeaponProfile

now separates aim, support and translated-muzzle evidence. BC2's static registry

owns exact item names and native axes; authored grip offsets remain dynamic,

never copied from2142 or indiscriminately shared by a rig family.

Per-item source capture plus an offline batch audit converts multi-weapon sessions

into stable, named candidate measurements and identifies missing/ambiguous data.

A real XM8/SPAS/XM8 capture yielded73 samples and two profiles with clean native

publication/cleanup. The shared147-bone family enables reuse but does not prove

all weapons' grips, barrel axes or interactions. The current40mmgl sight/handle

still needs a native binding; physical sight manipulation can reuse a later

shared mode policy with game-specific state changes behind it.

The accepted headset checkpoint remains preserved; the pipeline refactor passed

native tests but has no new headset acceptance claim. See

WEAPON-PROFILE-PIPELINE.md, GRENADE-LAUNCHER-RESEARCH-20261001.md and

reports/weapon-pipeline-20261001/checkpoint.json.

## Physical weapon-mode interaction and launcher pilot

Portable SightFlip consumes a verified local hinge/contact, semantic physical-item

identity and grip intent. It owns angle/detent hysteresis, cancellation, timeouts

and request-correlated native acknowledgement. Duplicate tracking cannot commit;

an already acknowledged expected mode transition survives the temporary contact

gap until a fresh sample. Shared policy has no engine offsets or native slot IDs.

BC2 supplies exact scoped-XM8 family discovery, native selector/edge/boolean-reader

proof and read-only ordered switch-map resolution. Entry action33 activates its

launcher; the validated current map uses action36 to return to its rifle. These

are requests through original input consumption, never equipped-pointer writes.

The physical family identity survives that expected native equipment change.

Named source jntWpn_9 contact is measured against an approximate11cm ladder segment,

after source rig fingerprint/name/parent validation. Raw free-hand targets remain

independent of visible support attachment; nearest contact prevents grip coupling.

Native sight animation performs the completed flip without palette overrides.

Final native gesture/grip/aim checks pass; headset feel remains pending. The shared

rifle flash remains unchanged in launcher mode, so grenade muzzle translation stays

disabled. See LAUNCHER-PILOT-20261001.md and launcher-sight checkpoint/summary.

## Headset-driven sight input timing repair

The first headset sight toggle failed while manual recenter and launcher grip

were accepted. Shared SightFlip must preserve arming across idle duplicate

packets. SightFlipPackets pairs raw intent with the actual published contact

generation; BC2 retains native ownership/deadline validation and mode dispatch.

Current release/tracking loss overrides buffered intent. Real controller timing

must be tested alongside ideal geometry fixtures. See

LAUNCHER-INPUT-TIMING-20261001.md for regression evidence and remaining checks.

## October1 sight geometry lifetime correction

Native equip readiness is adapter-owned: unsettled palettes cannot establish

interactive hinge geometry. Shared SightFlip accepts validated geometry updates

only while idle, preserving arming and unique request IDs; an active manipulation

keeps its hinge fixed. The adapter refreshes from the same publication paired with

input, rather than retaining the first session-wide native transform. Bounded

gesture summaries retain native/configured hinge and angle/dwell extrema.

See LAUNCHER-GEOMETRY-20261001.md for evidence and validation status.

## Fixed sight grasp after headset toggle acceptance

SightGrasp is portable immutable hinge/contact math. BC2 supplies measured

finger-base/palm geometry and named rear/front sight snapshots; visible wrist

attachment and continuous pre-detent motion are applied only to private palettes.

The native policy/animation remains authoritative at dispatch. Preview identity

cancellation is atomic before the contended pose publication and selects the

ordinary tracked palette on release. Contact always uses the raw hand, preventing

a snapped pose from maintaining its own grab. See SIGHT-GRASP-20261001.md for

native evidence, accepted toggle feedback and the pending new headset test.

## October 1 â€” independent hand poses and reload foundation

Transferred the modular anatomy/pose principle rather than Refractor rig indices.

Portable HandPose separates Free/WeaponSupport/MechanismGrip; BC2 resolves verified

bind metadata once per skeleton and applies only private render-palette writes.

Neutral off-hand is open; trigger/squeeze drive authored curls. Mechanism grasp no

longer inherits the gun's support animation. Real support/right gun stay unchanged.

44x86/43x64 suites and native105057-836 pass all three roles, both launcher toggles

and240pairs/0timeouts. Headset pose comfort, exact pad contact and capacitive input

remain unverified. Earlier frame interruptions are retained in the candidate notes.

ManualReload now provides tested engine-free physical-step/ack coordination,

including repeatable shell insertion and ownership/cancellation. Existing BC2

reload29 remains automatic; staged magazine/shell/chamber bindings are unknown,

so no runtime feature is enabled. See HAND-ROLES-20261001.md and

MANUAL-RELOAD-20261001.md.

## October 1 â€” hand input, reload attachment and scalable port plan

The user's free-left-hand animation acceptance is recorded at 105939-315.

Shared GripAttachment now holds authored item-local wrist contact for one unique

grab despite native reload animation. BC2's explicit Reload cancellation was

removed; invalid ownership/tracking and actual release still cancel. In native

112224-172, the same grasp spans four SPAS shell transfers with 0.194 mm rendered

attachment drift. This is ordinary native reload, not physical shell insertion.

Portable FingerCurlOverlay plus verified BC2 right anatomy drives the right index

without changing the gun wrist or other branches. Optional capacitive pose input

uses versioned fixed-width transport with sensor availability distinct from touch.

Both build suites 48/47 pass. The launcher regression 113015-478 passes 240 pairs / 

0 timeouts and both gestures; reload's 1 timeout and a preparation91ms outlier remain

recorded. New right-index/capacitive/support feel still needs headset acceptance.

BodyInventory provides category-based slots and dynamic pickup identity with

explicit native acknowledgements. Gun-hide and empty-hands runtime bindings remain

unverified. ManualReload's CycleAction can serve pump or bolt gestures; native

SPAS per-shell transfers and cycle code are observed, but physical stage gating

is not enabled. Do not reuse Refractor offsets, rig indices or ammo arithmetic.

FROSTBITE-PORTING-ROADMAP.md now plans reuse across later Frostbite titles while

making per-title native discovery, rendering ownership and asset evidence explicit.

BC2 validation grants no feature automatically to BF3, BF4, Hardline or later games.

See HAND-INPUT-RELOAD-20261001.md and the linked mechanism/inventory research.

Later user feedback removes manual ADS from the left trigger while preserving

finger posing. Eye-box-controlled scopes will need independent native ADS and

visual-effect bindings; do not transfer a flat-game zoom button as a VR requirement.

Native115047-514 proves recorded full left trigger with no AlternateFire requests

and both launcher gestures; all240 pairs arrive with2 recovered timeouts, so its

strict zero-timeout launcher audit fails and remains saved. Earlier zero-timeout

evidence is explicitly tied to the previous payload. The blur cause and new

headset appearance remain unverified.

October1 XR launch follow-up: a readiness file can exist before its writer closes

a complete document. The launcher now waits for parseable required fields within

the existing deadline, retaining all session/dimension checks. Initial115939-922

failed before native attachment; retry120128-090 is live with unchanged tested

payload. See reports/xr-launch-ready-20261001 and the active HANDOFF entry.

October1 headset acceptance follow-up: user says the hand/input checks generally

worked but reported two early shotgun support drops and inability to reload.

Host ended on explicit authorization; hooks disabled, game/SteamVR preserved.

Read-only post-state SPAS/XM8 are0/0, consistent with ammo exhaustion but not

attempt-time reload proof. The rolling support log lost the early release causes.

Preserve discrete interaction events independently of routine recent samples.

BC2's diagnostic-only follow-up now retains earliest512grip transitions, prior

tokens, native ownership, tracking/contact/actions and explicit forced resets,

with overflow counts. No release guard or native gameplay behavior changed.

Both builds48/47 and native121153-717 pass button/tracking release recording;

timing outliers remain reported. No claim the user's intermittent drop is fixed.

See GRIP-EVENTS-20261001.md and the feedback/candidate records.

October 1 parallel implementation: FeedMechanism and HandInteraction separate

physical prerequisites/contact, exclusive hand ownership and native command

acknowledgements. This avoids weapon-specific controller routines and prevents

rendered belt/cover progress from becoming invented ammo completion. Failed hand

transfers keep valid support ownership; stale release/lease/intent reuse is tested.

These policies are portable; no Refractor offsets or native operations migrated.

The offline optic inventory parses actual saved capture schemas and preserves

unknown asset/mode/ADS phase, missing viewport/SRV/material evidence, sampling and

overflow limits. A disabled empty capture is not proof of no optic. Source tuples

are capture-local identities, not portable material bindings.

Both builds pass 50 x86 / 49 x64 suites and the new optic tool passes 16 tests.

All four runtime payload hashes match the prior native-checked diagnostic

candidate; this batch has no new native or headset acceptance claim.

User release direction is single-player first, multiplayer potentially afterward.

See SINGLEPLAYER-RELEASE-PLAN.md and AGENT-STATUS.md.

## October 1: hand arbitration wired to Frostbite/BC2

The transferable lesson is explicit interaction ownership separate from visual

hand pose. HandInteraction, historical-contact validation and bounded sight/

support continuation are portable. BC2 supplies native physical-item identity,

the scoped-XM8/launcher family relation, QPC conversion and exact native ack.

No Refractor offsets, rig indices or resources were imported for this work.

Both builds pass52/51suites. Final campaign ownership/hand-role/preview checks

pass; combined delivery retains one recovered timeout and support delivery one

139.285ms outlier. No new headset acceptance or manual-reload implementation is

claimed. [Native evidence and remaining gates](HAND-OWNERSHIP-BC2-20261001.md).

## October 1 menu and reload evidence

The native menu adapter now discovers its own caller, controller, input node and

APT queue instead of copying Refractor frontend assumptions. BC2 migrates its UI

pump across worker threads, so a fixed thread ID was incorrect. Native Menu and

Back are distinct actions; exact owned releases preserve native behavior.

Reload observations prove five SPAS shell transfers on the selected+3C branch;

+40's final ammo change occurs outside the observed update/transfer path. Shared

ManualReload/FeedMechanism foundations cannot imply authority over these native

branches. Magazine/shell insertion must consume adapter-proven hand/item/socket

geometry, with raw tracking distinct from guided visual poses. No ammo writes or

manual native gate have been enabled. Optic discovery similarly separates proven

filter/zoom metadata from unproven magnified scene and reticle ownership.

October 1 real headset menu rejection: native raster/click checks and a fake XR

runtime did not establish SteamVR format support or usable menu-button routing.

The user observed black headset output on native menu entry. Preserve that failed

acceptance independently of passing transport tests, and test runtime format

negotiation explicitly. Physical reload remains observer/policy work until native

ammo completion and real item/hand/socket geometry are connected.

The menu repair candidate negotiates a same-family typed UNORM/SRGB swapchain,

with real-GPU texel-byte tests, while retaining RGBA/BGRA separation. App-side

left Primary+Secondary dwell offers menu access when the driver exposes no menu

source. The chord suppresses conflicting gameplay holds until both release.

Both architecture builds pass65/64suites; live headset acceptance remains pending.

Reload snapshot restore/export is now code-proven and observed without changing

native calls. Correlation is not yet proof of authoritative ammunition control.

## October1 video-driven sight repair and reload pipeline

Keep raw controller evidence separate from displayed hands. BC2's native mode

acknowledgement precedes its sight animation; freezing gesture visuals at that

acknowledgement caused a visible halfway pause and later native catchup jerk.

The portable visual policy now follows the held raw target after acknowledgement

and rate-limits only native fallback. Both directions pass a campaign fixture;

this does not replace a headset feel check or prove the separate shoulder bug fixed.

Local campaign ammo has three firing copies. A BC2-specific server-owner chain

and exact function proofs now account for all six calls in a two-shell reload,

with no gaps. Do not import Refractor ammo ownership or freeze one predicted

client branch. Next hold/resume must cover client and server callbacks on separate

threads. Actual BC2 mesh/finger/bind evidence now generates a disabled SPAS grasp

and rail draft, keeping measured landmarks separate from selected UX tolerances.

Full integration passes66x86/65x64 suites and58 focused Python regressions.

## October1 headset-driven menu repair and reticle evidence

Do not infer XR texture storage from a negotiated typed format: actual GPU tests

reproduce missing pointer pixels when a default RTV is requested on typeless

storage. Explicit typed RTVs and stable resource ownership repair that path.

Do not conflate zero-wait IPC contention with menu/focus loss: keep bounded fresh

menu state and preserve unseen command edges without replaying stale actions.

67/66suites and a native menu roundtrip pass; headset smoothness remains unverified.

The reticle side-view video establishes off-axis leakage, not its depth/material

or transform cause. Preserve accepted through-optic aiming while identifying its

specific native draw. These fixes add no Refractor offsets or new native menu calls.

### October1 recovered integration

Physical torso heading now composes tracked physical yaw only into shoulder/torso

anatomy; controller, weapon and shot frames retain their shared tracking basis.

The correction has deterministic coverage; the recorded sleeve seam still needs

specific native/HMD validation. SPAS shell interaction and presentation planning

reuse portable hand claims and magnetic insertion policy. The diagnostic reload

hold targets BC2's two client firing copies plus campaign server with exact current

invocation delta restoration; first native trial175727 did not arm and is a failed

hold check, although ordinary ammo transfer completed. No Refractor offsets,

rig topology or server authority assumptions were transferred to Frostbite.

