# Belt-fed and LMG manual reload plan

October 1, 2026. Proposed contracts and discovery work only. No LMG reload,
feed-cover control, belt manipulation or ammunition binding is implemented or
enabled by this document. No live game operation was performed for this task.

Build one reusable mechanism layer, then bind representative native families.
Do not implement a bespoke controller routine for every LMG. The first useful
work is an exact-asset configuration/animation capture followed by offline
cover/container/feed fixtures. Native per-stage control follows only after its
ammunition, timing and fire-readiness boundaries are proved.

The immediate deliverables are:

1. A candidate inventory of actual BC2 LMG assets, reflected reload configuration,
   named native mechanism geometry and normal partial-reload traces.
2. A portable mechanism/resource contract with explicit cover, container and feed
   semantics, plus independent gesture and native completion records.
3. One offline representative family with interruption and ownership tests.
4. One bounded native binding experiment after discovery identifies a valid
   dispatch/observer boundary; further assets become profile exceptions and
   capture reviews where that same binding is demonstrated to hold.

## What BC2 currently proves

[ManualReload](../include/fvr/interaction/ManualReload.h) provides a tested pure
transaction coordinator. It recognizes six operations, accepts a fixed ordered
plan, requires fresh gesture events and exact native acknowledgements, and cancels
intent across actor/item/equip/space changes. It performs no native reload,
ammunition arithmetic, contact recognition or resource-object ownership.

[BC2 pump/reload research](BC2-PUMP-RELOAD-20261001.md) establishes ordinary reload
input, native phase/timing/transfer code and reflected configuration for
**SPAS12_sp, XM8_sp_s and the scoped-XM8 40mmgl only**. Both saved
`reports/reload-config-20261001.json` and
`reports/reload-config-all-weapons-20261001.json` contain those three firearms;
there is no captured LMG in them. The SPAS dynamic audit proves native shell
transfers while support remains held. It does not establish an LMG sequence.

The examined native transfer path uses `rtSingleBullet` versus `rtMagazine` to
choose one round or a capacity-sized transfer. The latter is an engine transfer
category: it does **not** establish that a rendered weapon has a detachable box
magazine rather than a belt/container. A weapon-class label, real-world name,
shared skeleton or long reload animation likewise does not prove its mechanics.
No specific BC2 LMG is assigned a real-world sequence here.

The current [weapon registry](../src/games/bc2/Bc2WeaponProfiles.cpp) contains
SPAS, XM8 and its launcher. [WeaponProfile](../include/fvr/interaction/WeaponProfile.h)
only gates aim alignment, support grip and translated muzzle. Reload/mechanism
capabilities need a separate versioned profile and evidence; existing grip
acceptance does not grant them.

The [hand-input candidate](HAND-INPUT-RELOAD-20261001.md) preserves ordinary
support during native reload. A new manual interaction must not reintroduce the
old blanket rule that pressing Reload forcibly releases support. Its hand transfer
must be an explicit successful acquisition of the new interaction.

## Family classification before assigning a sequence

These are candidate game-mechanism families, not claims about BC2 weapon names:

| Captured family | Candidate representation | Evidence needed |
| --- | --- | --- |
| Detachable-magazine-fed LMG | Existing unseat/seat magazine semantics; cycling only when the native state requires it | Actual detachable object, native magazine/loaded state, chamber/readiness behavior and contact geometry |
| Belt feed with replaceable ammo container | Independent container attachment, feed-start seating and cover/latch constraints | Which visible pieces move, whether the box is an ammo object or only a mesh, native transfer time, and actual ordering dependencies |
| Belt feed without a separately manipulated container | Feed placement/removal plus the verified access mechanism; no invented box step | A captured native representation of feed and access, including partial/empty behavior |
| Alternative feed access or coupled mechanism | Its measured translation/rotation/coupled primitive and verified predicates | Exact asset/mode evidence rather than assuming a hinged top cover |
| Unsupported or visually inseparable asset | Retain ordinary automatic reload and native presentation | Record the missing geometry or native boundary; do not synthesize a successful manual reload |

Opening access, releasing a latch, placing a feed start, closing/latching access
and charging are separate possible constraints. An asset may couple or omit some
of them. Empty and partial reloads may need different plans, and charge readiness
must be read from native state rather than appended to every LMG reload.

## Proposed reusable mechanism and resource semantics

The existing six operations are `UnseatMagazine`, `SeatMagazine`, `InsertRound`,
`CycleAction`, `OpenBreech` and `CloseBreech`. They are insufficient to represent
belt feeding accurately. In particular, do not rename a feed cover as a breech,
a belt as a single inserted round, or every ammo container as a magazine.

Proposed additions below are **not new enum values in the current source**:

| Proposed semantic | Meaning and possible constraints |
| --- | --- |
| ReleaseFeedLatch / EngageFeedLatch | A distinct latch state, only where independently represented; a coupled latch may instead be a profiled part of the cover gesture |
| OpenFeedCover / CloseFeedCover | Access mechanism reaching its valid open/closed state; carries a specific mechanism identity, not a whole-weapon animation command |
| DetachAmmoContainer / AttachAmmoContainer | Detach/attach the exact compatible container instance, retaining its ammunition/resource identity |
| WithdrawFeed | Remove the currently seated feed start where required, retaining any remaining supply rather than silently discarding it |
| SeatFeed | Place an exact feed/belt start into the verified feed location/orientation; does not add ammunition merely because contact passed |
| CycleAction | Existing semantic for a completed profiled charging cycle when needed; reuse ordered stroke/rotation phases, including pump/bolt variants, but supply this mechanism's native completion rule |

A `FeedMechanismProfile` should declare:

- Exact game asset/family/mode and profile revision, native configuration evidence,
  topology/bind fingerprint and supported feature gates.
- Named roles such as access cover, latch, feed start/seat, container mount and
  charging handle. Names become roles only with correlated native/visual evidence.
- Canonical metre-based closed/open frames, local hinge/slider axes, contact frames,
  travel bounds and hysteresis. Units and frame ownership are explicit.
- Dependency predicates, allowed concurrent actions and hand requirements. For
  example, a measured access condition can gate feed seating; a different asset
  may couple those motions. Do not hard-code one universal LMG order.
- Native readiness/transfer/dispatch observers, supported interruptions, and which
  stages are visual gesture constraints versus actual native operations.

Represent cover/latch/container/feed/cycle state separately, each with Unknown
as a valid state. Illustrative states include cover Closed/Open/Moving, latch
Engaged/Released, container Detached/Attached, feed Unseated/Seated/Depleted and
cycle NativeReady/Required/Pending. These are proposed adapter observations;
they are not recovered BC2 enum values. Profile predicates choose a valid next
operation from a reconciled snapshot instead of deriving the whole state from an
animation timer or a single `reloading` boolean.

A feed start needs an attachment pose and resource association. It does not require
simulating every belt link in the first prototype. Cosmetic belt segments may
follow a bounded profiled curve or authored animation after mesh ownership is
proved; their shape is not ammunition authority. Missing deforming/rigid geometry
must be reported rather than fitting a rigid hinge to every changing bone.

## Evolve the coordinator without false acknowledgements

Keep the portable request/ack coordinator's current behavior intact until a
versioned extension has tests. Its present `MaxSteps=8` can be exceeded by a
sequence with separate latch, feed withdrawal, container replacement, seating,
closure and charging. Its immutable linear plan also cannot express arbitrary
branches/concurrent hands, and its request does not include an ammo-resource or
mechanism key.

The smallest useful extension is a profiled outer state machine that chooses a
bounded native-operation plan from a fresh authoritative snapshot. Add explicit
resource/mechanism IDs and revisions when requests need them, and increase a
step bound only after the captured representative sequence justifies it. Do not
silently split a transaction into several fresh coordinators that lose pending
native requests, accepted progress or resource reservations.

Keep three records distinct:

1. **Gesture completion:** a fresh coherent controller/contact sample reached a
   profiled detent or placement constraint. This authorizes intent only.
2. **Presentation completion:** private cover/belt/container/hand visuals reached
   a resolved target. It neither consumes ammunition nor proves native readiness.
3. **Native completion:** the verified adapter correlates an accepted native state
   transition/resource change with the exact issued operation and owner token.
   Only this can be an `Applied` acknowledgement to the current ManualReload API.

Where BC2 has no native property for a cosmetic cover/latch, keep that constraint
in the mechanism-presentation state machine. Do not invent a native acknowledgement
for it. A future adapter may expose a proved composite native operation, but must
label that operation and its observable completion accurately. Issuing ordinary
EiaReload after a gesture sequence is at most a **gesture-gated automatic reload**,
not proof of separately controlled belt, cover and charge stages.

If full manual behavior requires deferring an existing sequence, the adapter must
prove the native update/remaining-time contract and every transfer/interrupt path
before enabling the gate. The current pump/reload research explicitly shows why
blindly skipping a native state handler can break that loop. Preserve one native
simulation advance and native fire restrictions; do not replace them with render
animation timing or direct ammo writes.

## Ammunition/container identity and interruption

An `AmmoResourceKey` needs an opaque instance ID, generation, ammunition-kind and
ownership revision separate from both the weapon and cosmetic mesh. A box and its
belt can represent **one supply** with different visual/contact parts; they must
not each grant a second count. A detached empty box remains an object only if the
verified game/adapter model supports it; it never implies a fresh belt.

The adapter must declare its resource model:

- If native gameplay has individually owned containers/belts, bind those exact
  native instances and observe their transfer/destruction/remaining state.
- If it exposes only aggregate reserve/loaded counters, a visual ammo box is a
  proxy unless an explicit reservation/accounting model is implemented. Current
  BodyInventory does not provide that model. A body-pouch gesture cannot create
  a resource or subtract/re-add reserve speculatively.

Reserve or acquire a compatible source once through a verified authority; carry
that exact key through grab, attachment, feed seating and native acceptance.
Native pickup/resupply, another operation and rollback must not consume the same
supply twice. Native loaded/reserve/chamber semantics remain distinct; the current
BC2 research does not establish a separate chamber counter.

For depleted versus retained belt, snapshot the verified remaining supply and
native feed/readiness state. If those are unknown, do not assume a partial reload
discards everything or permits a ready shot. A charge gesture alone cannot mark
fire-ready; the adapter must observe the applicable native cycle completion and
preserve native restrictions while unresolved.

On interruption, cancellation means **stop new local intent**, not undo history:

| Interruption point | Required reconciliation |
| --- | --- |
| Contact lost before dispatch | Release only the local hand claim; no ammunition or native operation changed |
| Native request pending | Retain its request/resource identity, disallow a replacement request and observe completion/rejection or confirmed expiry; timeout alone cannot prove nothing happened |
| Container detached or feed removed | Preserve the accepted physical/native state and remaining-resource identity; do not automatically recreate the original arrangement |
| Ammunition already transferred | Keep the accepted native transfer, reconcile readiness and visuals, and never refund/reload the same source merely because tracking was lost |
| Equip/death/actor replacement | Invalidate hand contacts and old requests, reconcile native cancellation/completion under the old generation, and create no attachment on the new item/actor |
| Tracking loss/recenter/disconnect | Clear gesture continuity and release private hand ownership; block new operations until fresh native/resource observations and neutral input are available |

The outer adapter rebuilds a continuation plan from reconciled state. It must not
mutate an immutable active plan or replay already accepted operations after a
recenter. Profile replacement and pointer reuse advance their relevant generations.
If a deployed multiplayer authority is later involved, an acknowledgement needs
that authority's generation/event identity; a client cosmetic pose packet is not
an ammo transaction. No server integration is claimed here.

## Two-hand ownership and hand presentation

Add a shared interaction arbiter before manual reload runtime integration. The
existing [HandPose](../include/fvr/interaction/HandPose.h) role is a rendering choice,
not exclusive ownership of a hand. Proposed claims include gun hold, weapon
support, sight manipulation, reload mechanism, ammo object and body inventory.
Each claim carries actor/item/equip/space identity, hand, contact token and original
tracking deadline. One hand cannot own two incompatible interactions.

When a valid new reload contact is intentionally acquired, release that hand's
support claim before seating its mechanism/object pose; continue normal weapon
ownership in the gun hand. A squeeze held over overlapping volumes must not grab
both a sight and cover, or both an ammo box and feed start. Profile contact priority
plus neutral/new-event rules resolve the contest. If acquisition fails, do not
arbitrarily tear down the existing supported pose.

If a specific operation genuinely requires both hands, the weapon needs a verified
supported/holstered/resting presentation and native firing restriction; it cannot
float in place because both controllers were reassigned. Those dependencies are
not implemented today. An initial supported family should keep the gun hand on
its proven weapon attachment and manipulate with the other hand when its actual
mechanism allows it.

MechanismGrip can supply compact finger poses; an ammo-object grip may need its
own profiled reference. Raw controller targets remain the source for gesture
recognition, while resolved wrists follow the visual contact. Never feed snapped
wrist output back into the next grab test. Release/invalid-owner paths restore
normal private-palette presentation immediately, without editing native source
animation or the accepted gun/shot transform.

Existing grip/trigger controls can establish discrete intent. Capacitive touch is
optional finger-pose information, not proof of grasp; optical finger tracking is
not implemented. Body-pouch ammunition acquisition depends on future physical
resource inventory. The existing [BodyInventory](BODY-INVENTORY-20261001.md) is a
weapon draw/holster foundation, not a magazine/box reservation or ammunition API.
Offline fixtures may explicitly supply test resource identities without implying
that body grabbing is available in BC2.

## Batch discovery and exact bounded captures

Use the existing [weapon pipeline](WEAPON-PROFILE-PIPELINE.md) for stable grip/rig
candidates, and [mechanism analysis](../tools/weapon_mechanism_pipeline.py) for
native root-relative motion. Its rigid comparisons and hinge candidates do not
name a generic joint or establish a deforming belt binding.

The raw collector currently samples approximately every 100 ms and retains at
most 96 samples per group/32 groups, with at most 64 named weapon bones per sample.
A long reload may outlive retained baseline data or contain transitions between
samples. Preserve its invalid/drop/incomplete counters; do not call missing stages
successful. A reload-specific event/pose capture must complement this inventory.

For the first candidate, root should coordinate one **nonfiring ordinary reload**
from an already partially loaded weapon when such a native state exists. Do not
assume old ammo values or fabricate a shot to prepare it. Proposed capture budget:
30 seconds total, at least 2 seconds of settled pre-state, one explicit native
reload input edge after settlement, then completion plus 2 seconds of post-state.
If readiness never settles or the operation exceeds the budget, end with an
incomplete result and investigate rather than auto-retrying or extending forever.
The game operator owns any selection/input; this research task performs neither.

Capture at verified native update/animation boundaries with bounded records:

- Exact executable/signature/ABI evidence, asset path/family/mode, actor/item/equip
  generation, rig/bind fingerprint, units, native frame/sequence and clock domain.
- Reload input edge and request token, both relevant firing-object branches,
  current/previous/next native phase and timing, loaded/reserve plus verified
  capacity/multiplier, native reload begin/end and transfer/cycle events.
- Original evaluated weapon/hand/mechanism bones before VR edits, parents,
  hidden/visibility state, authored transforms, mesh/attachment ownership and
  correlated native event frame. Keep degenerate hidden leaves intact.
- Hand interaction claims, raw targets, resolved targets, contact/resource keys
  and private edits, with source-preservation/packing/fallback/cleanup counters.

Start with bounds such as 4,096 native event records and 2,048 pose snapshots per
30-second run, sized after actual rig memory measurement. These are proposed
instrumentation limits, not current telemetry features. Report overflow instead
of silently discarding transfer events. Native events can traverse several states
within one update; the existing external read-only sampler alone cannot prove
complete event coverage or atomic pose/state correspondence.

Capture representative families before many assets: one actually observed belt
family, one magazine-fed LMG if present, then an exception only when its native
configuration/geometry differs. Repeated exact-asset runs cover partial/empty,
zero-reserve, interrupted before/after transfer, equip and tracking loss. Split
these into separate bounded captures after the ordinary sequence is understood.
Do not classify every item in a category from one representative.

Batch output should join capture hashes with a family candidate and per-asset
exceptions: contact frames, cover geometry, container/feed roles, native transfer
strategy, charging predicate and required hand claims. Preserve independent
statuses for geometry, dispatch, completion/resource authority, interruption and
headset acceptance. A shared topology may reuse calibrated anatomy, while a
different mechanism or native transfer rule keeps a separate feature gate.

## Tests and implementation gates

Portable offline tests should cover ordered/coupled cover/latch constraints,
incorrect feed direction/seat orientation, wrong ammo kind, exact resource
identity, depleted/partial retained supply, resource replacement, and charging
required versus already-ready. Use representative synthetic profiles, not gun
names or mirrored implementation assertions.

Lifecycle tests must cover both hands competing, duplicate/stale gestures,
acknowledgements after timeout/cancellation, accepted transfer followed by tracking
loss, equip/pointer reuse, death/recenter/disconnect and repeated partial-resume
attempts. Assert no duplicate resource consumption, no false native completion,
no readiness granted by visual animation, and unchanged local gun/shot attachment.
Malformed geometry, reflected/sheared/degenerate matrices, inconsistent units and
incomplete capture should leave the feature disabled.

A BC2 binding needs independently verified native dispatch and completion for its
claimed operation, preserved remaining-time/interrupt behavior, exact source
restoration and native ammo conservation. Then test physical reach, grip transfer,
cover/feed contact, release and recovery in headset. Passing offline policy tests
or seeing a cover move is not native manual-reload acceptance.

The [Frostbite roadmap](FROSTBITE-PORTING-ROADMAP.md) remains the architecture rule:
share gesture primitives, resource/transaction policy, hand arbitration, anatomy
and tests. Each title supplies its own native commands/observers, item/resource
identities and authored mechanism profiles. No Refractor offsets or real-world
weapon-name assumptions become Frostbite bindings. Existing ordinary reload stays
available while native manual stages remain disabled.
