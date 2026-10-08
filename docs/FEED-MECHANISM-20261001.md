# Portable feed-mechanism preparation â€” October 1, 2026

[FeedMechanism.h](../include/fvr/interaction/FeedMechanism.h) now supplies a small
portable state/contact evaluator for a synthetic belt-feed family. It chooses the
next permitted physical operation toward an exact observed supply and checks its
current contact geometry. It does **not** dispatch a native operation, advance
mechanism state, acknowledge a reload, alter ammunition or mark a weapon ready.
No BC2 LMG, game runtime or existing native-acknowledgement API was changed.

The [belt-fed plan](BELT-FED-RELOAD-PLAN.md) remains the larger discovery roadmap.
This increment is deliberately narrower and unintegrated. Single-player binding
and headset validation come before multiplayer work; none is claimed here.

## API

| Type / function | Responsibility |
| --- | --- |
| FeedMechanismProfile | Opaque profile ID/revision, explicit Belt family, ammunition-kind identity, available cover/latch/container/charge parts, three access prerequisites, freshness bound and five bounded contact bindings |
| FeedMechanismSnapshot | Reconciled observed states and current contact frames, exact actor/item/equip/space/mechanism identities, profile revision, snapshot/raw-input sequences and time |
| FeedSupplyKey | Separate opaque `{id,generation}` keys for ammunition resource and container; a container and its belt do not become two supplies |
| EvaluateFeedMechanism | Validates profile/snapshot/desired resource and returns one physical candidate, NoPhysicalStep or a specific rejection/busy status |
| FeedPhysicalCandidate | Exact operation, part/role, affected resource and desired goal, owner/mechanism/profile identities and source snapshot sequence |
| EvaluateFeedContact | Revalidates that candidate against current state, checks same-packet raw contact geometry and returns ContactMatch/ContactMiss plus distance, angle and a presentation target |

The header depends on shared math types and the C++ standard library. It contains
no native addresses, gun names, OS/XR APIs, timers, allocations, networking or
engine writes. It does not include or alter ManualReload.

The physical candidates are ReleaseLatch, OpenCover, WithdrawFeed,
DetachContainer, AttachContainer, SeatFeed, CloseCover, EngageLatch and CycleAction.
These names describe intent candidates, not verified BC2 commands or new
ManualReload enum values.

## Supported family and observed-state rules

The first family is explicitly **feed replacement, closure/latching, then optional
charging**. It can omit cover, latch, separate container or charging handle. It
supports feed/container access requiring an open cover, and detaching a container
requiring an unseated feed. Profiles with a latch but no cover, access prerequisites
without their parts, invalid/duplicate contact roles, or invalid tolerances reject.
A Magazine/Unknown family is unsupported rather than being forced through belt
steps. Another ordering, including charging before loading, needs a separately
validated policy; this is not a universal LMG procedure.

All observed part states must be known. Absent is explicit for omitted parts;
Unknown and out-of-range enum values reject. An open cover cannot simultaneously
have its modeled latch engaged. An attached container and a seated feed must name
the **same exact resource/container pair** in this first family. A legitimate
native mechanism with a different relationship needs a different binding/policy,
not a forced rewrite of its snapshot.

A detached container with retained seated feed can be represented. The
`detachNeedsUnseated` prerequisite governs an actual detach operation, not a later
attach. If the retained feed already belongs to the desired supply, the evaluator
can suggest attaching that matching container without needless feed withdrawal.
This is synthetic-policy coverage, not a claim about a BC2 asset.

Desired supply must exactly match the current observed supply, including both
generations and ammunition kind, and be explicitly Usable. Unknown, Depleted or
Unavailable supplies reject. This flag is supplied by the future verified resource
adapter; the evaluator has no round count or ownership-reservation implementation.
Absent/nonseated/nonattached parts cannot retain dangling supply keys.

The evaluator withdraws an old feed before replacing its supply, respects access
prerequisites before manipulation, seats the desired feed, closes/latches present
parts, then proposes charging only when the observed cycle says Required. Pending
cycle blocks new candidates. `NoPhysicalStep` means no remaining step in this
specific physical plan. It is **not** an ammunition, fire-ready, native completion
or resource-ownership acknowledgement.

## Geometry and hand ownership

Profiles bind five independent semantic contact roles. The adapter supplies each
present part's current proper rigid frame in **weapon-local metres**, in the
shared canonical row-vector convention. Native offsets, palm mapping and units
conversion belong to the adapter. Reflected, nonfinite, degenerate, sheared or
scaled frames outside the strict rigid tolerance reject; no engine animation
matrix is silently repaired. Contact distance and relative rotation are checked
against the profiled radius and angle. No world-unit or centimetre assumption is
hidden in the evaluator.

The raw hand contact sample must match current owner, mechanism, snapshot sequence
and original input sequence, with a valid timestamp. A current squeeze cannot be
paired with an older publication's geometry. An unavailable hand or untracked
sample produces no target. ContactMatch returns the current part frame as a
possible presentation/contact target; it is not a resolved wrist, a full hinge or
charging-stroke recognizer, or proof that anything moved.

The caller must already arbitrate support, sight, gun hold and reload ownership.
`handAvailable` is an explicit caller assertion, not an implemented arbiter. This
module never releases an accepted support grip or moves the gun hand. Gesture
recognizers must keep raw targets separate from snapped presentation targets.

## Statelessness and interruption contract

Every query uses the current observed state. Repeating ContactMatch or a candidate
does not advance it, enqueue another action or acknowledge anything. The module
has no held-button/event-edge history. The caller must supply immutable snapshot
sequences, maintain observation/input ordering and reject replay, require genuine
neutral rearming, and translate a completed profiled gesture into the appropriate
separate operation system. It must not dispatch every returned candidate per frame.

After tracking loss, equip/death, recenter, cancellation or a pending native
operation's timeout, the adapter sets `reconciled=false` until native/resource
outcomes are known. The evaluator blocks during that period. Fresh reconciled
state resumes from the actual remaining physical step; it never rolls back an
accepted native transfer or refunds ammunition. Candidate/contact identity includes
actor generation, item/equip generation, reference space and mechanism generation;
a changed identity/profile/snapshot invalidates the old candidate.

The existing [ManualReload contract](MANUAL-RELOAD-20261001.md) still requires an
exact authoritative native acknowledgement. Neither ContactMatch nor
NoPhysicalStep can be used as a fabricated Applied acknowledgement. Body-pouch
resources, resource reservation, native fire restrictions, LMG geometry, per-stage
native dispatch and complete interruption reconciliation remain separate work.

## Validation

[FeedMechanismTests.cpp](../tests/FeedMechanismTests.cpp) passes **27 deterministic
cases** in isolated MSVC C++20 builds on x86 and x64 with `/W4 /WX /EHsc`.
The suites cover a complete synthetic nine-step sequence; partial continuation;
retained feed; optional parts; closure-before-charge; conflicting supply identity;
magazine exclusion; wrong resource/generation/kind; unavailable/unknown state;
contradictory profiles; pending cycle; reconciliation and freshness; translation/
rotation contact limits; actor/item/space/mechanism/profile invalidation; same-input
geometry; hand ownership; malformed transforms/roles; forged candidates; and
repeated contact producing no implied completion.

The executables were built under `%TEMP%/FvrFeedMechanismTests/x86` and `x64`.
Root registered the test in CMake and reported full integration passes of **50
x86 / 49 x64 suites**, including FeedMechanism and HandInteraction. No native, live
LMG, headset or manual-ammunition acceptance follows from these offline tests.
