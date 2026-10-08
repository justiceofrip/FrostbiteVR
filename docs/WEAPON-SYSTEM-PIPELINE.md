# Shared weapon system: implementation decision, October 5

The unit of work is a mechanism and its lifecycle, not a weapon name. An ordinary
new rifle must supply configuration and geometry data to the same consumers.
Categories organize coverage; they do not establish mechanics or native authority.

## Verified shared lifecycle and operation-class checkpoint

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
