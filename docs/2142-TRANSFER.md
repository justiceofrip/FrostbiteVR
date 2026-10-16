# 412: cosmetic lifetime and shared body placement

A completed native ammo transfer does not imply that its stock loading prop is
already gone. Retain presentation ownership separately, bound to the exact gun,
claim and mesh; let physical props and equipment recovery take priority. This
policy transfers across adapters; the SPAS leaf binding remains BC2-specific.
Never extend ammunition transactions merely to hide an unwanted animation.

Displayed chest ammo and its grab region share one body anchor. Moving this
anchor lowers every supported ammo type together. A read-only counter uses
native ammo samples and current XR poses with independent freshness checks;
neither render placement nor retained pixels authorize gameplay operations.
See [412 evidence and limits](CHECKPOINT-412.md).

# 410: verify view-specific postprocess state separately from cameras

Paired observations identified 24 numeric values whose primary-view state was
stable through drawing while the extra view retained defaults. Native copy and
downstream float consumers distinguish these from adjacent opaque fields. The
BC2 adapter now uses three independent scoped spans with aggregate rollback;
one failed group cannot skip restoration of another. These offset/type proofs
are BC2-specific. The ownership and all-groups cleanup policy can transfer to
future adapters. Both full 283-suite builds passed. Actual mission08 delivered
240 pairs and exact restoration; all 24 saved eye pairs now show consistent
damage tint, blur and darkening. This bounded visual result does not establish
actual death/restart or headset acceptance. See
[410 diagnostics](CAMPAIGN-DIAGNOSTICS-410.md).

# 409: independent native consumers and scoped render effects

The knife's session-close transition initially checks all three native copies,
then validates each pending native consumer independently. A copy already
deselected by the engine must not block the remaining consumer merely because
its old knife is no longer idle. Final convergence and ammunition checks remain
mandatory. This distinction applies to future Frostbite adapters; the concrete
BC2 offsets and state values do not transfer automatically.

Native stereo also needs per-view presentation state beyond camera matrices.
Measured BC2 damage effects at +1650 differ between its primary and extra view.
The 409 adapter copies only four verified inline scalars around the extra draw
and restores exact bytes afterward, using the same scoped machinery as RGB
modulation. Render-phase admission is separate from identity-based restoration.
Both normal and exceptional exits are covered by deterministic regressions.
Actual 409 testing confirms 118 exact restorations and blood borders in both
eyes, but stronger blur/tint remains in eye0: per-view scalars alone are not the
whole postprocess state. The remaining fields need measured adapter work. See
[the 409 evidence record](CAMPAIGN-DIAGNOSTICS-409.md).

# Development storage — 2026-10-15

Build and diagnostic storage is now a shared workflow constraint. Standard
launchers check generated disk usage, output/TEMP free space and planned raw
capture bytes; private wrappers must perform the same admission. Keep source
patches, concise results and current recovery artifacts instead of copying a
complete build for every experiment. These changes do not alter engine bindings
or extend weapon/campaign acceptance. See [storage limits](STORAGE-BUDGET.md).

Historical raw test sequences were pruned with explicit user approval. Source,
logs, compact visual evidence and current390/407/408 capture sets are preserved;
local cleanup manifests record missing historical raw paths. No2142 files changed.

# Current408: pump appearance passes; normal knife close and stereo damage remain open

Reviewed408 source now integrates the fixed SPAS ring and axial bolt movement,
shared unscoped XM8/launcher eligibility, moving knife custody, held contextual
tool Fire admission, and normal knife/tool configuration with held-session-close
handback. Native config is1220bytes; input3/frame7 are unchanged. Normal tools
compile behind separate options and remain runtime opt-in. Both full builds pass283 suites against source6950eac3…abf16d. Actual
pump mechanics and both-eye stock appearance pass120 pairs. Normal knife draw
works, but host close returns only mask2 and truthfully retains its hooks; the
owned game was then closed normally. That transition needs correction.

Actual407 mounted firing now passes: a900ms held trigger reached two exact
selected-gun Shoot callbacks and240paired frames. The SP009 jump traversed the
doorway and reached real damage100→7.13, then replacement actors appeared at
spawn. Both-eye review found damage/vignette/blur in eye0 only: visual FAIL.
Health zero was not sampled; missing reads are not evidence of zero health.

The next changed native checks are pump appearance, normal held-knife session
close, and a passive per-view damage-field capture on the established jump route.
The damage observer remains a private overlay. No guessed native field copies,
extra simulation replay, or unchanged recipe repetitions are authorized by these
results. Actual407 pump mechanics pass but old companion appearance fails.
Latest human headset result remains failed390; no full campaign acceptance.
See [407 retained results](CAMPAIGN-DIAGNOSTICS-407.md) and
[408 work](CAMPAIGN-DIAGNOSTICS-408.md).

# Current405: combined corrections, verification in progress

The source now includes the held pump-to-support handoff correction, the completed
holster sample for contextual tools, and actual knife-recognizer readiness for the
finite test. A private fixed-part palette capture investigates the stock distortion
reproduced in both native eyes. Full builds and new native results are recorded in
[405 evidence](CAMPAIGN-DIAGNOSTICS-405.md). They remain pending at this snapshot.
The exact SP_04 fixture produced three coherent mounted-weapon snapshots, then
closed normally and restored all profile files. Firing during VR remains unproved.
Human390 remains the last failed headset result; no full campaign acceptance.

# Current404: preserve held-contact tolerance across interaction transitions

An accepted held rail stroke must not immediately become a new grip acquisition
with a smaller radius. Keep continued custody distinct from fresh grabs, using
the same verified item/lifetime and current tracking. The existing straight-line
pump fixture misses an off-axis completion which the rail itself accepts.

Likewise distinguish elapsed driver time from recognizer readiness. A delivered
physical knife stroke and native GunHold do not alone prove the stroke recognizer
was armed. Record the actual prerequisite/reset leaf before repeating a failure.
Mission identity must come from loaded-level observations, not save shorthand.
See [404 evidence](CAMPAIGN-DIAGNOSTICS-404.md).

# Previous403: distinguish prediction copies, physical intent and mission assets

A native state copy is not an additional input consumer. Verify the engine's
copy/export/restore path, send input only to actual consumers, and still observe
every state used for completion.403 corrects this distinction for knife motion.

An emulated player's already-admitted bounded physical grip must not turn into
a release merely because the native effect briefly lacks a held witness.
Keep readiness admission separate from physical intent, retaining original
gesture timing and lifetime/menu stops. Record the first failed predicate with
its actual witness; aggregate rejection counts alone can hide the root cause.

Asset names also do not establish mission semantics. SP_02 intentionally uses
SP_LaserDesignator_Dummy with binocular effects and a scripted designation
chain. Follow the authored graph and verified native consumer rather than
substituting a differently named asset. See [403 evidence](CAMPAIGN-DIAGNOSTICS-403.md).

# Previous402: missing observation is not a release or a native failure

Keep an accepted initial draw across a transient native selection overlap,
without granting stale grip or attack authority. Require fresh selected native
and rendered contact before first hold. A transport Busy result is nondelivery,
not a release; retain the original deadlines and do not replay old input.

402 proves actual focus-loss delivery/recovery and native knife draw/hold/return.
Strict external read gaps remain explicit even where coherent observations and
native receipts agree. Native grenade aim and expenditure do not prove damage
or death. Independent client/server callback tokens are not assumed identical.
See [402 evidence and limits](CAMPAIGN-DIAGNOSTICS-402.md).

# Previous401: test the delivered input and native outcome separately

An intended disconnect is not exercised if its unfocused controller packet
fails the actual input codec. Keep original deadlines and clear inactive hand
payloads. A missed health sample grants no input authority, but need not abort
an admission before its existing window expires. Preserve publication failures
and distinguish them from native failures. JSON serialization changes tuples to
arrays; comparison fixtures must preserve values without rejecting that format.

Vehicle input readback and paired stereo establish those boundaries, not native
discharge. Similarly, knife draw/return counters do not prove a held physical
knife. Observe the consumer's custody and native selection transition before
enabling a feature.401 evidence and remaining limits are in
[the campaign diagnostic record](CAMPAIGN-DIAGNOSTICS-401.md).

# Previous400: held-shell read gap isolated; helicopter seat observed

Both normal builds pass261 suites. The changed actual reload case exposed a
brief native observation gap cancelling shell2 before focus loss. Its scoped401
correction reproduces fail-old/pass-new without granting stale native authority.
Death capture reached42 paired images but no native grenade action or damage;
file publication and native admission are separate issues. A later mission
reached its helicopter gunner seat and passed three native profile observations;
active VR firing remains untested. Human390 remains the latest failed headset
run; no full-campaign acceptance is claimed.
See [400 evidence and remaining work](CAMPAIGN-DIAGNOSTICS-400.md).

# Previous399: builds pass; changed diagnostic startup corrections pending

Both normal builds pass261 suites. The focus receiver rejected its option before
injection; the death fixture stopped before hooks/gestures because its file
duration limit lagged its direct configuration. These narrow400 corrections are
being checked at their actual startup boundaries. The completed398 mechanical
recovery evidence remains valid and was not repeated. Direct newSP launch now
has three native loaded-map confirmations for SP_04; no vehicle seat was observed.
Human390 remains failed; no full campaign or new headset acceptance is claimed.
See [399 evidence and limits](CAMPAIGN-DIAGNOSTICS-399.md).

# Previous398: native recovery passes; visual readiness remains open

Both normal builds pass259 suites. The actual five-second input gap recovers,
three pumps and two manual shells complete, and31 coherent post-detach samples
retain7/22 without an unwanted extra shell. Active PBL camera selection also
passes three coherent native snapshots. Saved shoulder-transition pixels expose
near-eye clipping or deformation still under investigation. No headset candidate
is staged; human390 remains the latest failed run and campaign acceptance is open.
See [398 evidence and limits](RECOVERY-AND-CAMERA-398.md).

# Previous397: separate cleanup authority from presentation authority

An exact pending native stop can outlive a controller or presentation gap without
granting any new interaction. Keep its original owner, counts and deadline. A
same-owner callback prepared just before the stop was armed must wait for a fresh
callback rather than discard the stop. Actual owner replacement still retires it.
The397 game run reaches three recovered pumps and two shell transfers, then
exposes a failed stop and unwanted shell. The398 correction is not yet validated.
See [397 evidence and limits](RECEIVER-GAP-397.md).

# Previous396: retain event time across recovery and distinguish test failures

A native recovery can begin before a diagnostic's delayed observer handoff.
Compare the recovery grant with the first genuinely fresh neutral packet in the
new tracking space, preserving that packet's original time and deadline. Do not
replace it with a later observer timestamp or extend expired input authority.

Menu ownership is a fresh native observation, not an assumption made when a
channel is absent. A publisher's Busy result must retain unknown delivery unless
the exact implementation proves no write occurred; gesture edges and later
neutral publication failures are different evidence. Keep strict failed reports
when a narrower functional interpretation is justified by source and logs.

Campaign vehicle labels are insufficient: an attached soldier can still own a
personal weapon, while a mounted gun or UAV has a different native control path.
Keep each verified ownership chain distinct. See [396](CAMPAIGN-INTEGRATION-396.md).

# Previous394: preserve admitted wrist contact through the shared arm solver

An avatar's native limb length must not move an already admitted controller
attachment away from its target. Keep normal reachable poses unchanged, extend
only segment translations for overreach, and measure the final wrist after hand
and finger posing in both successful native copies.394 records357 paired poses
with zero wrist error; this does not establish natural-looking arm extension.
Mechanical pose, hand placement, input freshness and native ownership remain
separate checks. The policy is shared; Frostbite supplies verified rig data.

A fresh input rejected for an aggregate admission reason needs the individual
predicate recorded before a retry or policy change. The grenade394 run emitted
no native edge; do not call its untouched ammunition evidence of a successful
throw. Native FireOnDeploy belongs to the exact firing-data object: the held
rifle and unselected grenade intentionally have different values.
See [394](TRACKED-WRIST-394.md). No headset or campaign acceptance.

# Current393: separate mechanical parts, tracked hands and per-view effects

Do not discard a valid measured weapon-part pose because the avatar arm cannot
reach its target.393 retains the mechanical plan and independently tests that
path in both native pack callbacks. This does not itself keep a clamped hand on
the controller; shared tracked-wrist placement is a separate constraint problem.

Stereo camera correctness does not imply matching native postprocess state.
The original view receives a colour fade that an added eye did not. Verify the
actual consumer, copy only the exact same-frame field during its draw/capture,
and restore before pair publication. Do not infer other nearby fields are safe
to copy. A rejected external observation is not a recorded ammo mismatch, and
must stay visible when reporting retained stable samples.
See [393](NATIVE-PRESENTATION-393.md). No headset or campaign acceptance.

# Previous392: preserve exact identity and test the actual timing boundary

The actual expired-control check now crosses the original deadline with only
processing time changed. It rejects that packet while preserving the pending
mechanism, then accepts fresh input. Persistent item identity includes its
generation: successive supply items can intentionally share a namespace ID.
Rejecting that reuse produced a false integration failure despite two conserved
native shell transactions. Fix validators against the shared identity contract;
retain the old result and re-audit captured evidence instead of repeating play.
See [392](NATIVE-RECOVERY-392.md). No new headset or campaign acceptance.

# Previous391: separate input gaps, equipment ownership and gesture intent

An expired packet is not a request to cancel an owed mechanism. Reject its
movement/release authority while existing bounded native recovery owns the gap.
Likewise, independently verified mutable state flags are not pointer identity:
allow only proven publication differences, keeping current reads and object
links exact. Drawing and immediately releasing at the same shoulder is not a
second deliberate stow; require a fresh observed departure before release-stow.
A supported carry can establish that departure without acquiring stow authority.
See [391 evidence and limits](HEADSET-RECOVERY-391.md). Human390 failed.

# Current390: retirement must follow the persistent weapon operation

Do not keep the only retirement fact in a current dispatch epoch. Returning to a
previously used weapon family must see that family's exact completed retirement;
a new operation must invalidate it.390 connects that existing policy fact to the
shared native pump/bolt handoff. The actual-game second-draw sequence failed389
and passes390. Native owner/configuration, current counts, physical custody and
callback exclusion remain separate gates. See [390](NATIVE-HANDOFF-390.md).

# Current388: actual empty-tube and cross-family checks pass; headset pending

Both normal388 builds pass246 suites. The actual-game SCAR/SPAS run completes
eight shots/pumps/support returns, inserts one shell into the empty tube, returns
to the rifle through the shoulder holsters, handles its magazine and fires.
It exposes and fixes a separate387 bug: cancellation released the native hold
without arming the stop helper, which automatically filled seven more shells.
388 completes all three native stop branches; the stowed SPAS remains1/23.

A second actual F2000/SPAS run on the same gameplay source completes three
magazine cycles, shoulder swaps, one pump, shell insertion, support return and
subsequent rifle fire. Twelve sampled side-view frames show the stock/ring
attached; this is not all-frame or human visual acceptance. Each run consumes
240 stereo pairs without discard, drains hooks and leaves BC2 alive.

Latest human result remains the October12 failed375 test. The original native
PublishedLinks failure and intermittent385 claim loss have not been reproduced
in388. Whole-campaign stability, full callback coverage and headset acceptance
are not claimed. Further testing targets drawing/pumping SPAS again after using
the rifle, without restarting persistent consumers. See [388](SHELL-STOP-388.md).

# Current386: diagnosing a separately reproduced hand-claim loss

Candidate384 fixes the connected aiming gap on physical-to-support return.
Its actual-game test completes four pumps with support returned, then exposes
a fifth-cycle input-pause timeout. Candidate385 converts a freshly validated
suspended hold to existing guarded recovery instead of waiting for that fatal
timeout. Both385 full builds pass all246 suites. Its actual-game run completes
two pumps, then loses support and the gun claim before the third acquisition;
this is a different path with no native suspension. Candidate386 adds bounded
inner-arbiter drop evidence to identify the cause; its full builds are running. The October12 headset failure is still the latest human result.

A normal384 actual-game menu run successfully restarts the checkpoint, observes
the replacement actor, and reopens/closes its menu. All240 stereo pairs arrive,
all hooks drain, and the game survives. This is one restart check. No whole-
campaign, visual or expanded weapon acceptance is claimed. See
[385 evidence and limits](INPUT-PAUSE-RECOVERY-385.md) and
[384 aiming correction](PUMP-RETURN-AIM-384.md). Older entries below are history.

# Headset375 failed; candidate381 is not accepted

The October12 headset run exposed pump-to-support release, persistent cycle3
failure and unreliable holsters. Candidate381 decouples returning the hand from
native completion, with a reproduced regression, and preserves additional
rejection evidence. Animation distortion and the native lockup remain unresolved.
No additional weapon or playable-campaign acceptance is claimed. See
[the review and correction](HEADSET-FAILURE-AND-PUMP-HANDOFF-381.md).

381 passes all 246 checks on each architecture. Physical hand continuation now
uses completed gesture/contact evidence independently of native ammunition
completion; it does not erase native debt. The recorded third-cycle ownership
failure remains unresolved. Shoulder reach measurements also expose geometric
misses before that failure, rather than one shared explanation for all symptoms.
The previous readiness statements below are historical, superseded by the
October 12 headset failure.

# Offline380: preserve valid parts when one authored track fails

The shared action analyzer now isolates failed parts and their dependencies.
The three PP2000 reload variants retain five valid parts each while rejecting
the unresolved jntWpn_12/13 branch. This does not qualify a manual PP2000 reload.
All231 relevant Python tests and both246-suite builds pass; the earlier five
reload output files remain byte-identical. The379 survey retains427 action
records across39 models, separately from the focused380 delta.

Normal player375 remains the pinned headset candidate. Its two combined game
checks exercise rifle reloads, both shoulders, pumping, shell insertion and
tracking-loss recovery; human visual/feel and sustained campaign acceptance are
still pending. No new native weapon is enabled by these offline results.
See [380](PARTIAL-ACTION-DATA-380.md) and [375](SHARED-PUMP-GEOMETRY-375.md).

# Offline379: shared action pipeline across 39 weapon models

The new reusable batch follows exact authored animation roles and reuses measured
geometry/contact code. It produces427 action records from93 content jobs across
169 configuration entries and39 models. Three PP2000 reload variants remain
explicit decoder failures; absent roles are separately recorded. Existing reload
outputs are byte-identical. All228 relevant Python tests and both246-suite builds
pass. Normal player375 remains unchanged and is the next headset candidate.
See [379](AUTHORED-ACTION-PIPELINE-379.md). No new native admission is claimed.

# Offline378: shared animation source lookup

The extractor now follows verified shared archives for clips absent from a weapon
bundle. All18 prior mechanism records are preserved and eight SPAS configuration
records are added. All219 relevant Python checks and both246-suite builds pass;
native player375 is unchanged. See [378](SHARED-ANIMATION-SOURCES-378.md).
No new native weapon admission is claimed.

# Offline377: verify unsupported file layout before adding weapon exceptions

A wrapper word labelled as flags was actually a record count. Following the
installed loader recovered10 clips across four families using one parser fix.
Compare all prior parsed contents, retain opaque metadata, and keep native
action semantics separate. Reuse exact mesh/clip/config joins and existing
contact extractors; a failed contact window stays unresolved. See [377](ANIMATION-WRAPPER-377.md).

# Offline376: measure the actual mechanism instead of copying a stroke

Use exact part, mesh and clip identities. Separate motion extrema from the
runtime closed stop, and component geometry from its native action semantics.
The 870 fore-end and bolt both translate; movement alone cannot choose the pump.
The NeoStead moves in the opposite direction. Bound the decoded spline over
every knot interval, and retain rejected wrappers rather than guessing a prefix.
No reference or shared rig fingerprint grants ammo/chamber authority.
See [376](LINEAR-MECHANISM-DATA-376.md).

# Local375: mechanism geometry belongs in family data

Keep the pump part name, axis, travel, immutable closed pose and fixed assembly
in explicit selected-family data. A skeleton shared across weapons cannot
identify the stock pieces of a particular gun. Reuse the physical consumer,
ownership/recovery and private-palette adapter; admit native operations separately.
Validate another geometry/axis offline, then verify existing native behavior.
Scope emulated scenarios explicitly: sideways inspection alone did not exercise
holding support through a shot. A separate tracking-loss run did. See [375](SHARED-PUMP-GEOMETRY-375.md).

# Local374: keep emulated support independent of reload enrollment

An emulated player should derive its controller mapping from original tracking
and rig evidence. Borrowing a magazine/shell contact silently prevents support
tests on other weapon classes, even when those weapons have valid grip data.
Keep the diagnostic observation separate from ammunition authority and check a
continuous ordinary hand claim, genuine release and unchanged native counts.
MG3 and TOZ pass the shared corrected path; see [374](SUPPORT-PIPELINE-374.md).

# Local372: distinguish static, bounded and native-proven references

Authored control data can contain tiny numerical motion. Keep strict constant
classification intact and provide a separately labelled full-curve bound when
choosing a reference pose. Propagate position/orientation uncertainty through
the hierarchy and cancel common ancestry algebraically. Equal endpoints or a
few samples cannot prove a full animation bound. Keep measured contacts separate
from ammunition, mechanism and native interpolation admission. A named flash
bone is not sufficient evidence of the muzzle location across a weapon catalog.
See [the fleet audit and authoring tool](AUTHORED-REFERENCE-372.md).

# Local369: obey native graphics-thread ownership during setup too

Render-thread-only drawing is insufficient if injection setup still queries and
releases a single-threaded graphics interface on its worker. Collect interface
metadata at a verified native graphics boundary, release temporary references
there, and publish plain values once. Keep initialization and timeout cancellation
separate from normal rendering and retain callback code through shutdown.
An observed integration defect is not automatically the cause of a saved crash.
See [369](GRAPHICS-BOOTSTRAP-369.md).

# Local367: publish body display as one coherent observation

An immutable pose/input snapshot must not require a separately published firing
guard to carry an identical packet number. That comparison can reject valid
equipment while the publisher is between its stores. Keep pose and tracking
admission together, then independently validate current native ownership and
explicit hide/custody withdrawal. A display snapshot cannot create ammunition,
extend its original expiry or substitute for an interaction claim.

Separate game failures from the test controller:366's Windows status-file
replacement fix uses deletion-sharing readers and ReplaceFile for existing
destinations, with an actual open-reader replacement regression. A completed menu
interaction plus incomplete disk evidence is recorded as partial. Single-thread
graphics-device setup must also be audited beyond the three measured render
callbacks. See [display367](BODY-DISPLAY-367.md) and
[comparison366](GRAPHICS-AND-MENU-366.md).

# Local361: make visual acceptance an explicit diagnostic outcome

A loaded texture and successful pair composition can still display no target
prop.360's neutral capture immediately stowed the gun; its first deliberate
grab then selected a different shoulder's weapon. Preserve both outcomes.
Bind a visual check to the actual inventory/contact and exact prop geometry,
inspect the saved pixels, and distinguish requested input from native commits.

The reusable361 receiver accepts a measured contact and records its one-grab
schedule without granting native equipment/ammunition authority. This lets a
new asset reuse the diagnostic without another weapon-specific implementation.
Exact shader families similarly organize conversion work, but do not establish
pass selection, parameters or reload support. [361 evidence](BODY-INSPECTION-361.md).

# Local359: material conversion is separate from geometry and gameplay

The shared prop renderer consumes explicit converted albedo, UVs, mips and
color-space declarations. A private sidecar matches exact geometry identities
and section fingerprints; malformed data cannot partially replace a catalog.
Asset adapters must establish texture role and shader mapping before supplying
that data. Reflection of geometry also requires matching triangle-UV order.

Software previews should call the actual rendering library, and compare both
changed target meshes and untouched assets. A successful texture upload does
not prove visible coverage: the first SCAR Ammo preview changed almost no side
pixels because it textured cartridges, not the casing. A separately reviewed
base-color conversion addresses the casing; native shading remains incomplete.
Keep preview caches separate from player packaging. [359 evidence](MATERIAL-PIPELINE-359.md).

# Local357: mechanism contact and hand shape are separate references

A correctly placed wrist and completed stroke can still render an unsuitable
finger pose. Supply the authored mechanism grasp through a shared wrist-relative
shape path; retain verified finger-chain topology, world scale and descendants.
The adapter owns asset selection and transforms. Do not change physical contact
or native cycle authority to compensate for a finger-presentation mismatch.

The SPAS implementation and actual-game held-support test verify this distinction.
The normal runtime consumes measured data while isolated input scripts establish
visible pose and gameplay evidence. Support-to-pump transfer needs a continuously
held-support case; acquiring only after the shot misses that behavior. Sampled
images supplement completed native receipts; neither alone proves headset feel.
See [357](AUTHORED-PUMP-HAND-357.md).

Texture coordinates also require shader-interface linkage: follow semantic names
and component swizzles through vertex/pixel arithmetic, validate the declaration
against independently measured mesh layout, and preserve repeat coordinates.
Unknown runtime dependencies stay unknown; cube and array sampling are distinct
from 2D. A plausible UV set or texture name is insufficient material evidence.

# Local354: make visual evidence visible; resolve material tables

A mechanics pass with the weapon outside the captured view cannot establish
rigid stock/fore-end appearance. Use an isolated input pose within actual reach
to inspect assembly while preserving the real consumer and native completion
requirements. The new sideways fixture does this; normal player input is unchanged.

Material names in mesh files lead through level-specific compiled shader tables
to parameter groups, textures and vertex declarations. Resolve exact mesh/material
references and retain all candidate passes. A texture named for a weapon may be
a packed mask rather than diffuse color; do not apply it as albedo by name. Match
the actual shader channels before connecting local asset caches to rendering.
BC2's table/layout details stay in its asset adapter. [Evidence](VISUAL-INSPECTION-354.md).

# Local353: transport depth with the delivered color pair

Depth provenance needs the complete delivered pair ticket, both eyes, adapter,
resource generation, format and sample description. Keep shared producer copies
immutable until consumer copies finish; compose only from host-owned local copies
after acknowledgment. A discarded pair retires its resources. A native owner or
device change also requires a fresh color allocation generation even when dimensions
match. Verify world and first-person visibility separately and require successful
composition of both eyes before publishing.353 connects and checks these rules
in actual BC2; the transport/compositor is reusable, BC2 clear-stage detection is not.

Interaction admission and physical custody also have separate lifetimes. An exact
terminal rejection with no native dispatch can permit a new request from fresh
input while the same valid held item/claim survives. It does not renew the old
request, excuse uncertain execution, or retry after release/tracking/count changes.
353 reproduces expired magazine admission across18 registered configurations;
352 covers shoulder contact across a short visibility-receipt gap. Preserve current
physical intent without retaining stale native authority. [Evidence](DEPTH-AND-ADMISSION-353.md).

# Historical local350: preserve native depth stages and consumer ownership

Frostbite may reuse the same depth resource for world and first-person rendering.
BC2's tested8× MSAA path clears the full-resolution D24S8 resource twice per eye.
Copy world depth before the second clear and first-person depth afterward; the
currently bound end-eye DSV is insufficient. Never identify a pass from stencil1
alone or promote one scene's draw counts into an engine signature. Shared MSAA
contents are verified separately from handle creation, with producer/consumer
GPU completion. The new host-only compositor intersects per-sample visibility
against both layers and restores its graphics state. Frame/eye provenance and
IPC ownership remain adapter responsibilities. Normal VR is not connected to
this depth path yet. See [350 evidence](DEPTH-PASSES-350.md).

# Local342: admission failure is not item ownership loss

A busy native callback/cohort should report Deferred separately from invalid
operation Rejected. Before admission, live hand/item custody can continue without
minting ammo, renewing a native lease or sending reload input. Repeated packets
cannot retry. A fresh attempt uses a new operation ID because even an unpublished
native registration may consume an ID. Actual release, tracking or identity loss
still ends custody. Do not discard a persistent physical item merely because an
arbitrary pre-admission timer expires. Accepted-operation deadlines remain separate.
342 verifies this in the actual shell consumer and passes the native combined run.

Graphics research also needs exact pass coverage: an end-of-eye world-depth copy
does not prove first-person occlusion. Draw type, texture format, dimensions,
sampling and viewport must be measured before binding a depth source to VR props.
Fork asset-loader findings likewise require map/save-specific verification and
ABI validation. The proposed manifest addition crashes our SP03 Continue path;
the original bytes were restored. These ownership/evidence rules transfer to
future Frostbite adapters; BC2 offsets, assets and rig indices do not.

# Local339: root alignment and rigid assembly are distinct references

A correct hand-to-gun contact does not establish controller-to-model orientation.
339 enrolls SCAR's reviewed model axes without changing its support contact or
granting native firing/ammunition capability. Starting-controller orientation is
now part of the deterministic regression. This uses the existing shared aim path.

Fixed stock pieces also need the same receiver frame while a manual pump owns
presentation.339 joins three exact SPAS authored parts to the resting/active pump
palette, preserving its original lifetime and exact-write restoration. It does
not freeze unrelated animation. Geometry-size checks distinguish a normal-size
chest magazine from unresolved overlay/occlusion behavior. These distinctions
transfer; BC2 part names and transforms do not. See [339](VISUAL-ALIGNMENT-339.md).
Both full244-suite builds and the combined actual-game emulation pass. The
presentation counters establish execution, not visual acceptance; native scene
occlusion and human confirmation remain separate work.

# Local338: manual mechanism presentation includes its resting state

An active manual stroke target alone does not own the visual interval after a
shot and before grip acquisition. Retaining the native animated fore-end in that
gap plays the stock stroke before the manual one.338 overrides only the measured
resting leaf under current selected-pump ownership; actual movement still requires
the original hand claim and cycle lease. Resting visuals grant no firing or ammo
authority. The pattern transfers, but BC2 part indices/calibration do not.
Both full builds and the combined native emulation pass; headset visual acceptance
and the reported magazine/right-hand alignment issues remain open. See
[338 evidence](PUMP-PRESENTATION-338.md).

# Private336 transfer: native melee scheduling includes prediction

Read the exact firing data and native scheduler before replacing attack timing.
The knife's500ms spawn delay is explicit; moving it into recovery preserves the
attack interval. Eliminating it entirely passed isolated x86 emulation but lost
the client effects callback in BC2. A50ms diagnostic windup retained both client
and server attacks at17–49ms with exact client-copy evidence. This is not yet a
production melee transaction or chest-knife feature. Integrate with the existing
native callback dispatcher; do not install competing Step/Update hooks. See
[336 evidence and limits](KNIFE-TIMING-336.md).

One fresh crash occurred before the VR probe attached; a Continue helper had
run, so do not call it a vanilla reproduction or assign an AMD offset without
that process's module map. See [334 startup evidence](STARTUP-CRASH-334.md).

# Local333: menu input owner is distinct from queue storage

The BC2 adapter now preserves releases across transport-buffer growth and retires
old input only after a complete stable replacement owner is verified. The new
target never receives the old release; the menu epoch and neutral input baseline
reset. Missing/blocked/unstable reads retain ownership. This lifetime distinction
can transfer; BC2's APT/controller layout and callback bindings cannot.
Both244-suite builds and a normal-build emulated rendered-menu restart pass.
The replacement-owner branch itself is deterministic-tested only. Intermittent
graphics crash326 remains open. See [333 evidence](MENU-LIFETIME-333.md).

# Private330: melee pose output follows a separately owned intent

Both BC2 melee effect paths reach the verified composition callback, including
the null-context server call.330 validates changing only the caller-owned stack
result, preserving input matrices, under a bounded diagnostic intent. Its two
directions pass numerical checks on both sides. A missing native token remains
missing: the private intent ordinal is not a general replacement transaction key.
The remaining player adapter needs its own attack custody and downstream hit
receipts. The native464–507ms delay is still present. Player329 is unchanged.
See [330 evidence](KNIFE-AIM-330.md); no Refractor offsets or assumptions transfer.

# Local329: bounded menu-press retention during contention

The menu consumer retains an already-owned press through Busy only while its
original focused pointer-down sample is unexpired in the current menu epoch.
It emits no event and renews no deadline. Expiry/loss still releases and rearms.
Focused regression and both244-suite builds pass. Normal329 native menu stress
check passes35/125ms contention, balanced releases,240 stereo pairs and clean
detach. Prior327/328 restart passes and the unresolved326 crash retain their scope.
See [329 menu behavior](MENU-CONTENTION-329.md).

# Local327: menu fix and two different emulated restart sequences

Private327 passes a restart with a removed rifle magazine;328 passes a restart
with unfinished pump debt, then rifle magazine return and a fresh pump/shell cycle.
Each consumes240 stereo pairs, disables its hooks and leaves BC2 alive. The328
retirement selects the new shotgun first: rifle-first native retirement is still
unverified.326 intermittent graphics crash is not fixed by these passes.
See [327/328 evidence and headset checklist](RESTART-EMULATION-327.md).

# Local326: tracked menu release; campaign restart crash remains open

Normal pointer release positions the fresh VR cursor before button-up; lost-input
cleanup does not reuse stale coordinates. Both full builds pass244 suites. A real
game emulated pointer restart reached the transition and crashed at the same AMD
command-worker fault site as265, before scene recovery. Root cause remains unknown.
See [326 evidence](MENU-RESTART-326.md).326 and318 are preserved; no new headset,
sustained campaign or whole-roster acceptance. Latest combined equipment pass is311.

# Local318: portable melee motion, separate native delivery

`MeleeStroke` consumes current shared hand custody and measured blade-tip motion;
it does not own an inventory, claim another hand or manufacture native completion.
Drawing and reacquiring custody require quiet settling. Pose discontinuities and
recenter epochs retire motion. A pending native attack independently blocks new
intents until its outcome settles. Use a fixed tracking frame, not camera motion.

Two simulated strokes pass private BC2 native attack/hold/return checks across
all three firing copies. The native adapter needs separate scoped FireOnDeploy
and automatic-return gates; do not carry these BC2 addresses into another engine.
The knife's melee effect types differ from firearm types. Hand-directed damage,
chest grab and tracked render integration remain unproven. Both full builds pass
244 suites; no new player-facing knife feature is enabled. KNIFE-MOTION-318.md
records evidence and the remaining integration.

# Local312: shared mesh-layout correction

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

# Local308: shared weapon-class grips and combined native checks

Measured contacts cover58 variants/32 meshes through shared grip logic, plus
preserved SPAS baseline. Five handgun variants use primary-hand aiming and
pistol-only hip metadata. Contact coverage does not grant native reload/action
capability; compiled magazine prerequisites remain18. Dynamic/missing references
remain explicit gaps. See SHARED-GRIP-BATCH-308.md.

Both normal builds pass243 suites. Input-only native emulation passes rifle
support, three magazine cycles across shoulder exchanges, SPAS pump/shell,
immediate support regrip and subsequent rifle firing. A separate pistol test
passes support grab/release, chest-magazine replacement and firing. Independent
server/client receipt audits pass; both runs consume240 stereo pairs and drain
cleanly. Normal308 is staged with diagnostics disabled;305 remains intact.

No new headset or sustained campaign acceptance. Pistol support was only held
for two sampled frames; prolonged feel remains unverified. Knife motion attacks,
third native weapon capacity, full LMG loading and pistol chambering remain open.
No publication. Details and exact evidence limits are in the308 document.

# Carry location is separate from firing classification —305

Use explicit carry metadata when an engine class is ambiguous: BC2's MP443_sp
is wcSmg, but it belongs at the hip while actual SMGs belong on shoulders.
Coherently capture that metadata with inventory identity, include it in pickup
replacement detection, and share anchor lookup between contact and rendering.
A new body location does not grant native inventory capacity, reload support,
or slide/chamber capabilities. Native305 proves pistol hip stow/draw and firing;
three simultaneous native weapons and headset placement remain unverified.
See HIP-CARRY-305.md. Physical grenades are deferred; chest knife draw/hold and
motion hit integration are the next separate capability.

# Local304: queued generations use the registered-view bound

303 successfully retired the old scene after a VR-pointer checkpoint restart,
then initialized a replacement eye view. Its final shutdown still rejected
the replacement graph. Two identical read-only snapshots proved26 registered
views and18 queued children: the current six children had holder+queue refs2,
and twelve previous-generation children had queue-only refs1. Every private
child reference was accounted for. The snapshot's16-entry queue limit was
incorrectly tied to the current holder-count bound.

304 gives the pending queue the same64-entry bound already enforced on the
native registered-view snapshot. It keeps the exact holder/queue ownership
union introduced in303, without changing native reference counts or calling
child Release directly. The root destructor handles all owned generations.
Duplicate, foreign, shared, missing and unaccounted references still reject.
Tests cover up to54 queued children/62 live registered views, along with
overflow rejection and preserved original-view order/reference baseline.

Evidence: scene303-native01/queued-generations-read.json and its failed
scene-queue-audit.json distinguish successful scene replacement from failed
final shutdown. The earlier301 destructor proofs apply to the same executable.
No engine ammunition, health, gameplay timers, native menu deadlines or
controller admission rules are changed by this work.

Focused progress/graph suites pass on both architectures (23 graph groups).
Both full normal builds pass243 suites: x64 39.43s and x86 127.88s.

The native304 combined audit passed. The VR-pointer menu restarted the
checkpoint; a replacement actor/world loaded; the old seven-view subtree and
its six pending references retired exactly once. A replacement eye initialized.
Final shutdown accounted for18 pending references across three generations,
released the private19-view subtree, restored the current original seven-view
order exactly, and returned request references27->8. Cache refresh completed,
reload flow drained, every hook disabled, and BC2 remained alive with no captured
native exception or camera/GPU restoration error. The old initialization view
order differed from the current survivor order; the current order was preserved.

Evidence: scene304-native01/scene-queue-audit.json. Source SHA-256:
5eeca63b4f04cb6e9a74026c23fa51ace8266e2e2d8b49457464e2d0f5527ec1.
Trace SHA-256:46115499c019fcb933730c0f7f803fa51d6ba70a20e83b62777b025206f4a42e.
The no-frame receiver intentionally requested zero world frames/controller
packets. The general stereo probe therefore exits12 (wrapper2) by design;
the independent audit verifies scene retirement and shutdown, not image
delivery. Prior rendered-frame and reload evidence retains its original
checkpoint identity. The isolated304 headset package requires a fresh game.


# Exact pending references —301

For an unrendered extra eye, account for each exclusive child holder plus each
unique entry in that same eye's pending queue. Verify the owning destructor
loops and let the native root destructor perform cleanup. Shared/unexplained
references remain rejected. Do not infer retired-scene or headset acceptance
from a same-world no-frame shutdown. See SHUTDOWN-301.md.

# Pending render ownership —300

A never-rendered native view may retain child references in its own pending
queue as well as child holders. Rejecting every reference count above1 can
prevent cleanup forever. Account for exact native owners/destructors rather
than altering counts or releasing unexplained references. See SHUTDOWN-300.md.

# Build dependency boundary —299

Changing a reviewed profile must invalidate its actual consumers, without forcing
unrelated targets to rebuild. Keep verification on every build, and pin generated
header contents as well as compiler include wiring. A source archive and normal
build receipt do not replace native evidence from a different checkpoint.
See [299 evidence](BUILD-PROVENANCE-299.md).

# Asset catalog capacity —298

Available asset count and simultaneous rendered object count are different
bounds. The cache and both consumers must share the catalog bound, with exact
geometry/identity validation retained. Enrollment validation must queue the
whole generated catalog and render an entry beyond the old limit, not merely
regenerate identical cache bytes. A successful nonblocking connection poll also
must not be discarded by an immediate second poll that returns Busy.
See [298 evidence](BODY-CATALOG-298.md).

# Original native callbacks overlap —296

Independent audits must verify native ordering rather than assume serialized
callbacks. The server Update containing the verified helper is itself its
server observation. A client Update can overlap it and still provide exact
post-authority evidence. In296 both client reads matched the result, entry was
after operation start, and exit followed the entire server Update. Retain real
native invocation IDs, count evidence, owner identity and original deadlines.
Likewise, temporary body metadata suspension during fire must not create a
second synthetic trigger edge. See [296 evidence](SINGLEFIRE-EMPTY-296.md).

# Single-fire magazine enrollment —295

Carry exact capacity and authored insertion geometry through generic tests as
well as production consumers. A 17-round curved pistol magazine must not inherit
30-round or straight-path fixture assumptions. Reuse reviewed native operation
families and existing hand/resource ownership; enroll exact asset bindings.
Static weapon grip, sampled reload grip, slide capability and live acceptance
are independent evidence. A finite single-mechanism input script may skip
unrelated weapon tests without bypassing production admission or receipts.
See [295 native evidence](SINGLEFIRE-MAGAZINE-295.md).

# Record physical custody before native admission —274

A hand may own a reload item before the engine is ready to begin its operation.
Record loss of that custody independently from native-cycle cancellation; a
zero native-start counter does not mean the player never grabbed an item.
Retain the first rejection of each cause rather than letting expected startup
input gating or later terminal churn hide the first useful failure.
See [the diagnostic coverage](CUSTODY-DIAGNOSTICS-274.md).

# Shared sampled magazine geometry —273

Reuse the same ownership/ammunition consumer with an explicit measured path.
Removal, original return and replacement must agree on the exact seated item
transform. Arc progress requires fresh real motion and local orientation;
duplicate packets/time alone never complete the gesture. Preserve the straight
rail arithmetic and existing operation admission. Controller emulation must
consume the same geometry as the player. A measured path and a passing mock
receipt test do not establish native fire/reload admission or a chamber action.
See [273 evidence and remaining work](SAMPLED-MAGAZINES-273.md).

## Checkpoint272: evidence capture must not stall simulated controller input

The retained support-event log identifies stale_input; the receiver had a125ms
gap against a100ms lease after saving a pixel-checked pair. Inspect both the
rolling samples and retained event journal before adding diagnostics. Move large
file writes off the controller loop, with immutable buffers and bounded backlog.
Do not extend source lifetimes to conceal a diagnostic stall. A single scripted
pre-shot neutral/regrip may recover only from fresh evidence of unchanged native
state, and its outcome must remain distinguishable from uninterrupted support.
See EMULATED-PLAY-272.md. This does not change normal gameplay custody policy.

## Checkpoint271: distinguish controller-script stalls from native failures

Run complete input sequences against real native state before headset requests.
SCAR/SPAS and F2000/SPAS completed on identical binaries; one F2000 attempt lost
support before firing and the scripted player kept squeezing until timeout.
Retain that failed evidence despite a passing repeat. A script needs explicit
neutral/regrip recovery after input loss, with bounded retries and evidence of
the new claim; it cannot assume continued squeeze reacquires a cancelled hand.
Capture the actual invalid-input predicate before attributing a shared reset
event to packet timing. No pump debt or native cancellation existed in this
failure. See EMULATED-PLAY-271.md; the transient remains unresolved.

## Checkpoint271: callback drain survives completed physical actions

A native pump/bolt completion can overlap a different branch's idle callback.
If that callback loses its final read after completion, close only its exact
invocation; leaving it outstanding poisons the next valid prediction Restore.
Retain the completed physical action and original acknowledgement receipt.
Do not relax same-owner ammunition checks or erase active mechanism debt.
The fail-before regression covers both operating classes and wrong-ID/count/
owner negatives. See SCENE-RECOVERY-271.md for actual-game acceptance status.

## Checkpoint270: test real body gestures and completed-cycle lifetime

After actor replacement, the native selected item can be SPAS while both hands
are empty. Diagnostic setup must use ordinary shoulder acquisition rather than
legacy stick selection. Actual270 verified that transition and original rifle
magazine custody before exposing post-pump idle Restore cancellation. A completed
physical/native outcome and a later prediction snapshot require separate lifetime
reasoning; the current rejecting invariant still needs reproduction. See
SCENE-RECOVERY-270.md. Do not infer all-weapon acceptance from these specific runs.

## Checkpoint269: diagnostic lifetime versus equipment publication

An input-only recovery driver must not confuse same-actor equip publication churn
with a replacement actor. Preserve its immutable old-operation witness while
waiting neutrally; only verified actor replacement restarts the test trajectory.
No equipment/hand/ammunition authority is renewed by diagnostic waiting.
See SCENE-RECOVERY-269.md for actual failure evidence and validation limits.

## October9: family-neutral actor retirement —267

An explicit actor-lifetime retirement must not require selecting the same weapon
class that held the old debt. A replacement player may draw a rifle first.
Validate the replacement's native configuration independently, prove the old
callbacks drained, archive the original outcome, and retire only that lifetime.
Keep operating-mode selection separate and preserve sequence high-water marks.
Same-actor equipment changes still cannot erase persistent action debt. The267
regression covers this boundary; native and headset acceptance remain pending.

## October9: menu-driven scene checks —265

Exercise existing native menu input through the same IPC used by VR, verifying
the current native raster before each dependent click. A click receipt alone is
not scene recovery: require changed world/request owners, completed callbacks,
actor retirement and new nonblack stereo frames. Keep idle, pending-interaction,
death and headset outcomes distinct. The neutral264 restart now passes; it does
not clear the prior active pump/reload crash. External command polling must use
fresh monotonic samples and bounded retry of transient receipt-sharing failures.

## October9: prediction replay ordering —262

Native client prediction can withdraw an already observed provisional transfer
before the server performs it. Admit that only with exact Restore-hook and
immutable snapshot evidence. Withdraw the affected client's completion vote;
retain the same request, deadlines and server authority. Test both client-first
and server-first update order. Invalidating a prior hold must not invalidate an
unrelated, still-fresh context timestamp or renew it. Counts alone are no proof.

## October9: post-action hand return and processing clocks —260

A completed ammunition transaction may retain bounded intent to reacquire
support while fresh input and exact gun custody continue. Missing source
publication does not undo completion, and retained intent grants no source or
geometry authority. At the actual acquisition, use current processing time for
the same original packet; another serialized consumer can advance the shared
arbiter between reload and support stages. Do not renew sequence, observation or
deadline. Both ordering and observation-gap regressions now cover this case.

## October9: support, mechanisms and empty intent —259

A hand-ownership transfer must not reset a two-hand aiming correction and
thereby manufacture weapon-relative mechanism travel. Keep raw off-hand motion
separate from claim-bound gun-hand aim continuity. Linear sliding mechanisms
use ordered endpoint crossings; rotary bolt detents retain their own policy.
Temporary input or mechanism unavailability cannot become an implicit draw of
a committed holstered weapon. Retained intent grants no stale rendering or
claims: revalidate the current carried item and obtain new presentation proof.
Authored left-hand references can enroll support without changing a verified
right-hand/ammunition path. See RECOVERY-259.md for validation limits.

## October9: selection identity is not a per-frame native lease —258

A nonblocking native view can be unavailable while a callback owns its gate.
Retain the last accepted selection identity across that gap, but never native
hold/readiness/count authority. Replace it on a concrete native selection
observation; require exact weapon, epoch and tracked-space match. A failed read
must not skip the physical coordinator and silently restore a different gun
attachment. Pump and bolt share this policy and missing-view regressions.
Actual recurrence and headset confirmation remain separate.

## October 9: submitted physical actions survive native tail replay —256

Keep physical completion separate from provisional readiness in each native
prediction branch. An unchanged original snapshot may return an idle client to
the server's remaining animation tail. Withdraw that client's readiness and
require its own replay and all-branch convergence under the same transaction
and original deadline. Do not mint another gesture, restore native holds or
write ammunition. Exact snapshot/owner/count checks and post-acknowledgement
rejection remain necessary. The shared selected-boundary policy has pump/bolt
regression coverage; actual runtime and headset acceptance are separate.

## October 9: original render contact and physical custody —255

A missing renderer publication must not masquerade as a foreign native owner.
Keep a bounded last successfully mapped contact for the same item and tracked
space. Reuse only its remaining original lifetime, validate current controls
and body mapping independently, and never replace an explicit mismatch with
cached evidence. This preserves hand/weapon placement through a short read gap
without minting contact, a grip edge or native completion. Regressions must run
through the actual player coordinator after a completed cycle, including absent
publications, resumption, expiration and foreign-owner rejection. Offline255
passes these checks; actual-game acceptance is separate.

## October 9: native prediction can replay one physical shot —254

An original client Restore can undo a tentative shot before its replay and
server confirmation. Withdraw only that client's observation after verifying
the exact unchanged snapshot and pre-interception state. Preserve the physical
action's original identity/deadline and require the client's own replay plus
independent evidence from every native branch. This is not an ammo write or
permission to roll back a held/completed physical transaction. Exact capture
and negative regressions cover supported pump/bolt classes; broader native and
headset acceptance remain separate.

## October 8: carried supply is not a continuously renewed ammo request —250

A fresh native reserve publication is required to acquire or seat replacement
ammunition. Keeping an already acquired object in the same tracked hand is a
separate operation. Expose an explicit custody-only renewal accepting no counts,
geometry or receipts; retain the exact item and accept real release. An expired
count snapshot must neither destroy custody nor authorize new ammunition. Test
replacement carry, guidance and pending completion across missing publications,
not only original-magazine removal. Delayed mock callbacks need fresh modeled
own-Update convergence as well as original helper provenance.

## October 8: native admission and drawing have different lifetimes —249

Actual BC2 rendering can occur after a native ammunition publication expires
even while the original pose, source and hand/weapon identity remain valid.
Record when the immutable pose was admitted from fresh native evidence. Drawing
checks that original admission plus current pose/mesh/family/claim deadlines.
Keep contact acquisition, supply and ammunition operations on fresh native
observations. Do not restamp either lease or let a new packet extend an old pose.

## October 8: unavailable publications are not lost custody —248

Return a typed read outcome from the same guarded channel read. A temporary
publication gap must not erase a magazine already owned by a hand. Retain that
custody under current independent identity/input checks and original timeouts,
without restamping native evidence or issuing new work. Actual release remains
release. Completed original returns must retire their presentation seat records,
or a later cycle can be blocked by the prior one. Test at least two complete
cycles through the actual controller coordinator; isolated first-cycle tests
missed the retained record.

## October 8: neutral input and interruption coverage —246–247

Separate the first established native idle time from the latest refresh. An
original neutral controller packet may be processed after newer callbacks;
rejecting it solely for preceding the latest refresh can starve recovery forever.
Keep its source timestamps and require subsequent fresh native proof before
issuing authority. Shared shotgun and bolt-service regressions reproduce this
ordering without a headset.

Emulated tests must also fire with support already held, interrupt a partial
gesture, and repeat the same controller packet through the outer coordinator.
A direct consumer test misses wrappers that restore tracking flags or release
grip on duplicates. Recovery completion still needs ordinary physical ownership,
the full measured stroke and the actual native acknowledgement.

## October 8: processing time, submitted work and visible custody —239–241

A queued hand-ownership operation needs its actual processing time even when
its controller packet is unchanged. The combined shell/support test reproduced
a stale renewal after consumer adoption; updating only processing time fixes
it without renewing the original controller or contact deadline.

An accepted native insertion can outlive the short observation lease that
authorized submission. BC2's ammo copies need not change simultaneously. Retain
the exact pending operation under its original deadline while validating fresh
input and the same gun; issue no new claim, pose, command or completion during
a typed count gap. Later coherent counts and a real native receipt are required.
The reproduced complete-sequence regression passes; actual241 SCAR/SPAS also
completed magazines, pump, shell, support return and holster switching. Its
recording has one missing held callback, so full recording completeness remains
unverified despite functional completion.

Visible gun custody also outlives a completed native bolt transaction. Keep its
current claim-derived placement after readiness; retiring the native hold must
not make the renderer fall back to a different attachment. The synthetic test
must model that fallback to reproduce the actual jump. Actual M95 241 now
returns custody/support and fires again; its second-cycle native prediction
failure remains separate.

Shared rigid magazine assemblies now support complete measured descendant
subtrees. Root-only caches miss visible pieces; indiscriminate subtree movement
can move an unrelated animated part. Require measured rigid transforms and exact
topology, then apply/hide every member through the same presentation operation.
Existing leaf profiles remain byte-identical. Authored finger movement is a
separate question from wrist-to-magazine attachment stability.

## October 8: concurrent publication and shared family geometry — 237

The combined native test exposed a race hidden by the shorter isolated pump
fixture: a native callback could publish after Gather saved its processing time.
The newest lease then looked like a future observation, erasing a still-valid
gesture. Keep only an already-issued, unexpired lease for that exact cycle;
never renew its deadline or use it after suspension, release or cancellation.
The actual adapter regression reproduces the old failure without headset input.

Emulated controller poses must also obey actual IK reach. The combined script
placed the closed SPAS pump beyond the arm reach; it now smoothly approaches the
previously measured reachable controller pose. Geometry and reach limits stay
unchanged. Native combined acceptance remains a separate check.

SCAR campaign configurations reuse the existing resource consumer and native
operation class. Shared extraction now handles thin magazines through measured
axial extent and independently observed withdrawal direction. Two exact SP rows
join registry, hand contact, visibility and body geometry together. A descriptor
or generated profile alone never proves native or headset acceptance.

## October 8: empty completion and detachment have different contracts —234

An eight-shot simulated-player test caught a final-round path missed by two-shot
testing: the scoped Step empty-control reader rejected the finite pump because
ordinary combined families were disabled. Admit the exact active cycle explicitly,
retaining native identity, configuration and restoration checks. Verify empty idle
while the mod is active; restoring stock behavior on detach can reload afterward.
Also keep recorder row ids separate from native invocation ids across dropped rows.

## October 8: validate the phase actually used by the native player — 233

Disassembly identified a plausible server seat callback, but it received zero
calls in active single-player gameplay. A constructor-disabled observer recorded
the actual outer dispatch at0x6b0830 from0x697b44. Exact code, owner, thread and
idle-state checks then passed repeatedly; one native M95 equip and restoration
of the original rifle both converged across client/server copies. This is test
setup evidence, not a manual-bolt result or ammunition rollback guarantee.

The live two-cycle SPAS pump also passed with actual shots, restored holds,
controller strokes, native tails and support-hand return. Last-round cycling is
separate: its native6→1→2 tail bypasses positive-ammunition7→8 states. Treat that
as an explicit mechanism boundary rather than broadening positive-state checks.

## October 8: mirror actual hand arbitration in gesture tests — 232

Actual231 native pumping survived overlapping client/server callbacks but failed
when ordinary support acquired the hand before delayed pump contact arrived.
The earlier test only renewed support claims and missed this competition. A
fixture performing real Acquire/Transfer/Renew reproduced it. Transfer now accepts
the exact live original press and recorded support history; held-bit retries,
expired claims and restamped evidence remain rejected. Offline checks pass;
actual232 subsequently passed two complete pump strokes and support returns.

## October 8: callback and renderer timing are separate ownership domains — 230

Real BC2 client and server firing Updates overlap normally. A global callback
count is therefore not evidence that a selected weapon changed ownership.
Keep invocation-local mutation/restoration, exact structural owner checks and
per-firing collision detection separate from unrelated branch activity. The
230 actual pump trial exposed this false cancellation; the branch-scoped fix
survived904 holds in actual231; that run did not complete the physical gesture.

Likewise, an existing support claim can retain the original renderer N-1 input
while Gather has controller N. Transfer through the arbiter using that original
recorded evidence and current safety; do not require or fabricate a current-frame
renewal before the support pass runs. The realistic regression now passes on
both architectures while stale/restamped history still fails.

## October 8: terminal results outlive physical custody — 223–224

An interrupted hand or changed selection must not lose a submitted ammunition
operation. Retain its exact native outcome and settle its original supply token
without creating a fresh source. Native uncertainty remains distinct from
pre-dispatch rejection. Shared pump/bolt gestures can use the same hand arbiter,
but engine state boundaries differ even inside BC2; the sniper cycle does not
use the SPAS positive-delay boundary. Executing the original instructions and
recording state-labelled rig samples reduces guesswork before headset feedback.

The first live resource-hand run also exposed a distinction absent from the
initial mocks: a terminal native receipt can arrive before the next coherent
count publication. Tests must schedule those events separately.

## October 8: distinguish physical seating from native completion — 222

Rendering and support-hand admission must cover the exact submitted seat before
the server publishes its acknowledgement, as well as the interval after counts
change but before completion is proven. Retain the original request as bounded
visual evidence. Never turn it into an ammo receipt or refresh its deadline.
Tests must inspect the submission frame itself and delay owning Updates; only
testing the eventual stable frame missed both failures in the BC2 adapter.

## Earlier: keep engine and physical identities separate — 221

BC2's physical actor combines a weak reference and soldier identity; its held
weapon can be a persistent attachment-family alias. Native ammunition instead
belongs to a specific inventory weapon and independent equip generation. The
adapter now verifies that mapping explicitly while retaining the original native
receipt. A superficial ID equality would reject valid hands or misroute results.

Connect physical policy through a bounded request/result API, and give rendering
its own resource-custody evidence. Neither an animation hold nor a copied client
count substitutes for the owning native Update and convergence receipt. A settled
empty well can restore a seat-only gesture plan without repeating native removal.
This does not settle interrupted in-flight writes or establish all-weapon support.

## Earlier: separate selected context from retained magazines — 220

The shared command inventory owns one ledger per stable weapon lifetime.
Selecting another gun cancels admission, not the removed magazine's identity or
rounds. Late native completion targets the original weapon, never the selected
one. Fifty synthetic item lifetimes test this shared accounting independently
of BC2 model coverage.

Reuse physical geometry and hand ownership while replacing the authority
contract explicitly. The same detachable-magazine rails now run with verified
resource receipts in deterministic tests, without synthetic animation holds.
Native BC2 command tests pass through the inventory-backed queue; player hand
and renderer adapters still need migration. These results do not imply that
Refractor animation or server ownership rules apply to Frostbite.

## October 8: resource completion and inventory lifetime — 218–219

Retain the exact native callback completion before attempting a contended
observer gate. A later count sample cannot replace proof that the helper ran;
conversely, a delayed helper receipt must not overwrite newer count timestamps.
Checkpoint 218 verifies both delayed completions in the actual game and passes
195 suites on both architectures.

Use the existing carried-item lifetime domain for ammunition ownership. The
219 binding comes from the body inventory's coherent native read, including
weapon data/persistence identity. It remains separate from selected-equipment
and XR reference-space generations, rendering, hand claims and ammunition
authority. Interactions may suspend while native identity continues to be read.
An unobserved lifetime gap or replacement retires that key; never transfer an
old magazine to a reused pointer based only on the weapon name.

## October 8: separate physical ammunition from reload animation — 216

Follow-up 217 connects the ledger to real native dispatch in a private rifle
diagnostic. The two operations require actual callback/cohort receipts and pass
the live resource audit. This single-owner check uses a provisional equip-bound
resource identity; it does not establish durable inventory identity or player
hand dispatch. Preserve exact server completion through observer contention when
turning the diagnostic path into the general backend.

Use shared resource accounting with engine-specific authority evidence. BC2's
signed loaded-ammo helper and native Transfer can change finite counts on the
owning server Update while normal replication updates both client copies. Live
diagnostics verify original-round return, shell refill and magazine refill in
idle state. A finite reserve multiplier of two on XM8 required inspecting and
emulating the native multiplier semantics; do not assume the first gun's value.

Stable physical magazine identity must survive equip/reference-space changes;
individual native commands still bind the exact current context. Dropping a
magazine does not credit reserve, and ambiguous dispatched work cannot be retried
or rolled back by timeout. The new portable ledger and BC2 completion bridge
pass recorded-native-data replay, but player integration remains pending. Do
not relabel an empty-state receipt as an animation hold. See [216](NATIVE-AMMUNITION-216.md).

## Earlier October 8: execute real native instructions in isolated reload tests — 212

Capture typed weapon configuration privately and run the installed engine's
reload instructions in an x86 emulator. Explicitly mark mocked effect/listener
boundaries and scheduling that is not represented. This reproduced an automatic
reload bypass in the inner Step's 50 ms guard after the outer Update policy had
already been corrected. The shared 100 ms bound fixes that isolated failure
while preserving explicit reload and stock underbarrel behavior. Negative
controls must still reproduce the old bug. Native signed ammo adjustment also
needs server/prediction ordering: replaying an older native snapshot restores
the removed rounds. Portable arithmetic and local function success cannot grant
live engine authority. See [212 evidence and limits](RELOAD-EMULATION-212.md).

## October 8: separate simulation delta from evidence age — 211

A 59.6 ms native frame cancelled a valid manual magazine hold because the adapter
reused a 50 ms delta limit. Ordinary magazine and shell cycles now share bounded
hitch admission while keeping context-read age, input expiry and native identity
checks separate. Fresh branch completions must rebuild positive hold evidence.
Do not count conserved final ammo as a successful gesture: this failed run filled
through native fallback after cancellation. The explicit seat, transfer and
completion sequence remains the acceptance criterion. See [211](RELOAD-FRAME-HITCH-211.md).

## October 8: observe delivered presentation without a headset — 209

Synthetic controller tests can verify the real IPC body-prop and HUD data before
requesting headset time. Record both eyes' original times, owner generations and
counts at delivery, then use the production freshness policy. Successful stereo
images alone do not establish present body props. A background SPAS run produced
valid ammo HUD samples and reproduced missing chest props while keeping desktop
focus unchanged. Native loaded-code and settled-state evidence remain separate
from headset visibility or comfort. See [209](MONITOR-PRESENTATION-209.md).

## October 8: render observations cannot enter gameplay coordination — 208

A display query that reads counts can still affect native coordination. The
207 per-eye reserve read entered reload invocation exclusion; presentation now
consumes a saved immutable observation and validates its original identity and
deadline after the read. Owner invalidation defeats older reads finishing late.
Do not regenerate freshness in the renderer. Likewise, body props must derive
from the final shared interaction publication, not disappear solely because
full magazines choose a different controller. Consume a squeeze used for an
ammo object before allowing a later support grab. Release an inserted item's
hand independently from its still-pending native ammo receipt. These changes
do not supply the missing persistent discard/accounting operation for a full
magazine. See [208 evidence and limits](RELOAD-FEEDBACK-208.md).

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


##243 cross-engine lessons

Native prediction can progress before a cycle hold is first applied; validate
immutable owner/configuration, exact snapshot and real shot debt while allowing
measured original transitions. Do not treat every changing state number as a
new owner. Test ordinary input-only release/regrip separately from a fixture
that directly requests custody. Preserve first failures before shutdown polling
can overwrite them. Functional completion and complete callback recordings
remain separate evidence. Multi-part magazine assemblies and sampled authored
withdrawal paths belong in shared geometry policy; exact native enrollment stays
inside each engine adapter.
# Headset275 sliding stops and tracking origins

A held virtual mechanism has no physical controller stop. Measured part travel
and the controller's retention envelope are separate constraints: clamp the
displayed part to its rail, permit bounded overshoot, and still require ordered
real motion. The prior ideal straight-stroke emulation missed actual human
lateral motion/overshoot; the expanded test fails before the correction.

Changing a tracking origin does not replace a weapon or settle its mechanical
debt. Transfer only the coordinate epoch at a verified callback drain, preserving
item/cycle/shot/ammunition and invalidating old pose authority. BC2-specific native
identity and guard evidence stays in the adapter. See PUMP-HEADSET-275.md for
validation limits; this is not headset or all-weapon acceptance.

# Recovery251 shared handoff lesson

Cleanup is not an input observation. A cancelled recognizer can outlive its
replacement/recovery and the next hand owner's acquisition within the same
controller packet. Release only exact still-owned mechanism custody; do not
replay old processing time through the global hand arbiter. Preserve immutable
input evidence, pending native receipts and mechanical debt. Regression251
reproduces actual250's shell-after-recovered-pump claim loss. This rule applies
to shared mechanisms, not a weapon-specific offset or animation workaround.

# Recovery264 time domains and persistent custody

A consumed physical input and the native observation used to construct it have
different bounded lifetimes. Preserve immutable construction provenance and
the original input deadline; native dispatch still requires fresh exact current
authority. Never renew the physical event to compensate for delayed admission.

Geometry continuity compares observation times. Processing delay belongs in
current sample expiry, not the interval between two source observations. Apply
this in shared insertion code so shells and magazines use the same rule.

The startup deadline ends when a native hold is established. A later bounded
publication gap may preserve existing custody without granting new hold or
seat authority. Do not reapply startup timeout to an established interaction.
# Actor lifetime retirement —266 candidate

Persistent physical debt must survive ordinary controller, tracking and
equipment interruptions, but must not permanently block a replacement player
after checkpoint reload. The BC2 cycle service now has a separate retirement
operation requiring changed client/server actors, increasing actor generation,
fresh coherent replacement state and native callback exclusion/drain. It keeps
the old outcome for diagnosis and retains sequence high-water marks. The new
physical adapter is constructed only after native selection accepts retirement.
This boundary transfers across engines; BC2's actor identities and callback
proofs do not. Deterministic evidence is not native/headset or crash acceptance.

275 native follow-up: the expanded F2000/SPAS controller sequence completed,
including interrupted pump recovery and subsequent shell/holster/rifle actions.
This validates the larger motion through the actual adapter, not headset feel
or actual-runtime tracking-origin rebasing. See PUMP-HEADSET-275.md.

## 276: tracking coordinates are not equipment identity

Keep owed cycles and ammunition through a tracking-origin change. Route a
same-item/newer-space input to the existing recovery adapter without granting
new selection/fire authority. Rebase the private service before requesting its
new observation watch, then commit under unchanged owner/config/callback-drain
checks. An exact previously issued zero-delta held callback must be allowed to
drain after recovery starts; mismatched or expired callbacks must still reject.

Validate independently fresh snapshots against a boundary timestamp sampled
after the read, not the earlier callback-entry clock. Preserve original lease
deadlines. A torn native read may receive one complete bounded reread; never
assemble state from partial reads. Actual BC2 origin recovery and subsequent
full physical pump completed with unchanged ammunition. The passing run did
not need the retry; deterministic tests cover it. See TRACKING-RECOVERY-276.md.

277: a callback-entry clock cannot validate a publication that another thread
created afterward. Sample freshness under the publication gate, preserving the
lease's original deadline and revision. The forced producer/consumer regression
fails before and passes after; actual M95 cycles passed separately. Do not claim
the latter reproduced the race (counter0). Native pause/input epochs still need
separation from equipment identity; see OWNER-TIMING-277.md.

278: input observation generations and physical equipment generations must be
separate. A coherent returning native read may retain the exact item across a
scheduler pause while all old action/render leases remain expired. Likewise,
observing the current branch of a submitted reload does not require another
copy's previous Update context to stay fresh; holds and final transfer receipts
still require their own evidence. Native combined pause test and fail-before
regressions pass. Keep visual continuity and mechanical test results distinct:
a presentation fallback must remain visible without manufacturing a mechanical
failure by immediately removing the emulated player's controls.

279: emulate complete player interactions through actual native menus and actor
replacement. Keep rendering and mechanical outcomes separate: a visual failure
must fail visual acceptance without forcing controller release that invents a
mechanical failure. Record the original visual event and continue the bounded
gesture; require real native outcomes and both results for overall acceptance.

280: physical mechanism debt and the engine's authored clock need not run in
sequence. A distinct continuous input-guard authority lets original native
Updates advance while the same ordered physical gesture remains owed. Never
label advancing native state as frozen or idle. Completion requires the exact
gesture plus independent, fresh all-copy idle receipts with conserved ammunition.
The connected M95 adapter reduced the native post-stroke wait from~2.3s to41–52ms
in the actual game. This is a common contract, not blanket enrollment of snipers.

283: a native callback frame existing does not mean it issued a hold. Unheld
normal Updates must still run ordinary empty-ammunition suppression, including
additional Steps after the final pump's idle commit inside the same Update.
The full-to-empty diagnostic exposed an error invisible to positive-ammo tests.

284: missing current native authority is distinct from losing the existing
physical grasp. Retain only the same debt's already-live claim under its original
deadline; publish no new guided target or release until fresh authority returns.
Do not silently renew the claim, ignore changed ownership, or discard pump debt.

286: one native branch can publish completion while another original Update
is still running. Completion retires new observation admission, not an already
issued same-cycle read before its original deadline. Nested callback checks
must keep the parent's revision domain. Actual owner/cycle transitions still
revoke the old read, and structural ownership must be rechecked before writes.

287: physical custody may renew from fresh original controller/contact evidence
while current native permission is missing, but only through the hand arbiter
for the same live grasp and same observed debt. This is separate from moving a
mechanism or renewing native authority. Repeated/forged evidence and an expired
claim cannot acquire that permission.

288: predicted clients need not execute identical local Commit tails. An exact
unchanged snapshot may copy an independently completed server tail into a client.
Retain the actual Restore as its evidence, require that client's own neutral
idle Update, and keep source completion and ammunition checks. Do not manufacture
omitted Commit records or reject a proven native transition simply because the
three branches took different routes to the same completed state.

289: once acquired, a mechanism's grip custody follows fresh recorded controller
input; delayed render contact is a separate motion proof. Keep the original
pose time/deadline and reject fabricated or expired geometry. Missing evidence
before acquisition waits without inventing progress; an actual release remains
regrippable and must not be misclassified as forged evidence.

290: model the observed prediction graph rather than requiring every client to
follow the server directly. BC2's server -> clientB -> clientA completion path
needs a fresh completed source and each receiver's own idle Update. Forwarded
snapshots never replace the receiving Update or create fake Commit receipts.

291-293: prediction rollback can restore the final cartridge as well as a
positive magazine count. Preserve the one real owed cycle and original deadline;
withdraw only the rolling-back client's tentative shot evidence and require its
own replay. Completed bolt ownership must not absorb an unrelated native
magazine refill: separately verified passive updates observe conservation but
grant no hand lease, manual reload or Ready. Diagnostic cycle duration and its
recorder share one finite plan so test setup cannot silently reject its own mode.
# BC2 411: distinguish a native death from a missing observation

The 410 damage-window images could not establish death merely because health
reads disappeared and a new actor later appeared. Private 411 joined a verified
native lethal caller, exact local client/server ownership, zero server health
at entry and the original function's return. The changed actual run then joined
positive health on a new actor with bounded input, neutral release and stereo
frames. This verified post-death recovery without requiring a headset or a
repeat of unchanged reload/pump tests. Both-eye captured death/fade presentation
passed, with a 312 ms pre-capture gap explicitly untested.

Keep event evidence separate from input-publication success and physical
movement causality. A missed read is unavailable evidence, never zero health.
Keep storage scanning before a timing-sensitive game fixture; a reusable
admission must stay bound to the same invocation/output and recheck free space.
Lossless compression preserved the new raw evidence and recovered 359 MiB.
Complete generated storage measured 37.43 GiB after this checkpoint, below 40 GiB.

## Desktop launcher / updater 0.1.0

Keep the player's launch path separate from research/build orchestration. The
desktop app reuses pinned normal410 launch helpers; its Launch button never
compiles, provisions weapons or starts emulated fixtures. Update packages are
verified in versioned directories and selected atomically, with one rollback
version and persistent user settings outside the payload. Game-derived caches
and absolute-path private helpers are not portable release assets. A tested
local launch profile is distinct from a clean-machine native installer.

The actual WPF button/worker path, local dependency verification, GitHub feed
lookup and updater transaction/rejection checks were exercised. No new native
or headset proof: C: free space blocks the existing launch guard. The G: text
application adds under 100 KiB locally; no native build tree was duplicated.
See DESKTOP-LAUNCHER.md for the release contract and remaining portability work.

### Storage correction, launcher 0.1.1

The user explicitly reserves C: for the OS. Route actual process TEMP/TMP/TMPDIR
to G: before checking storage; do not merely ignore a full drive while continuing
to write scratch there. A real child-process temporary-file test verifies G:
inheritance and an unchanged Windows user TEMP setting. Both launcher and
native/build admission use this routing. The prior C: blocker is superseded;
the 15 GiB reserve on used volumes and 40 GiB generated ceiling remain intact.

### 412 desktop startup: bounded admission instead of repeated inventories

The player launch path had four complete inventories of the same 235k-file
retained evidence tree, including one after it reported VR startup. A compiled
four-reader traversal retains exact compressed-byte accounting and reduces the
measured fresh scan from 27.08 to 7.50 seconds. The quick launcher shares that
single 640 MiB session reservation through exactly three ordered startup stages,
with authenticated receipt, process ancestry/creation, a 180-second lifetime,
covered roots, unchanged limits and fresh free-space checks at every stage.
Standalone builds/captures still inventory independently. The parent waits for
trace admission before reporting startup. Offline real-child tests passed;
new cold-start and headset performance remain unmeasured. Do not generalize this
handoff into a persistent cache or grant larger/concurrent output budgets.
