# Manual reload preparation — October 1, 2026

## Launcher reload request and next agent assignment — October 2 Eastern

User explicitly requests launcher-specific manual reloads, including the XM8 underbarrel launcher, recognizing that other launchers have different mechanisms. The next available reload agent will survey the actual BC2 assets/config/native behavior and group them by opening, round insertion and closure. XM8 underbarrel is the first target. Other underbarrel and standalone grenade launchers follow; reloadable rocket tubes and single-use launchers remain separate families rather than assuming a common action from their names.

Reuse shared hand ownership, raw contact versus guided presentation, ammunition supply, haptic feedback, and verified native completion. Add mechanism profiles/adapters only where measured behavior requires them. Opening or closing a breech must not itself grant ammunition; ammo credit still requires the native operation receipt. Spent-round visibility, closure/fire interlock, cancellation and death/equip transitions need explicit evidence. Existing launcher-sight acceptance proves only sight/mode interaction, not a manual launcher reload.

Keep XM8 rifle magazine integration and boat head-aim work active. The survey should deliver a family matrix, current evidence/missing bindings, and the smallest monitor diagnostic needed for the first XM8 launcher reload. Do not infer a total weapon count or enable unverified launchers from generic category names. Canonical source must not include extracted game assets.

Current verified reload evidence: accepted SPAS headset run030952 (audited after session end), and scoped-XM8 native Cancel040421/Insert040613. Physical magazine integration is in progress, including persistent-family/native-rifle identity mapping and truthful start-failure recovery. Refer to HANDOFF.md for current build/test counts and historical limitations.

## SPAS headset acceptance — October 1 Eastern

The user accepted manual SPAS loading in trace030952: it worked, felt decent, and the magnetic load was visible. The launcher-sight interaction also worked. This is headset acceptance of the revision5 candidate described below, superseding the prior failed feel verdict. Shell haptics need investigation/polish because they were not clearly perceptible. Final native receipt and feedback-delivery counts await session-end logs; the session remains running. Other reload families and full campaign readiness are not implied. Evidence: reports/headset-reload-sight-success-030952.json.

## Latest SPAS candidate — October 2 UTC

Headset020931 failed user acceptance despite two verified shell insertions. Revision5 replaces the tiny entry-only catch with explicit round-shell RailContact assistance around the full bottom-entry path. A60mm capture capsule,120mm release distance and120ms alignment are UX choices; a20mm new net raw inward stroke plus dwell is still required. The first near-mouth owned sample may latch but cannot auto-load. Existing magazine/keyed profiles keep their prior behavior. Guided previews survive only an unsampled tick within the original deadline/claims; real cancellation or rejected geometry wins.

Optional haptics now distinguish capture from accepted native receipt. This has software/fake-runtime verification, not new headset acceptance. Closed loading-mouth geometry, held-trigger/other cancellation paths, full-capacity animation tails and other weapon families remain open. The launcher sight also has a tested anatomical-calibration repair awaiting headset verification. Read HANDOFF.md for the exact current build and runtime limits.

## Stop after a manual SPAS insertion — October 2 UTC

The native cancellation repair is integrated and monitor verified: trace015932 completes one physical 6/15 -> 7/14 insertion and returns all three firing copies to idle without filling the last slot. Two exact native abort-helper calls plus one ordinary native restore to idle account for all three copies. Final read-only postflight retains7/14. This closes the demonstrated neutral, post-receipt cancellation case from trace013308; it is not headset acceptance or coverage of every cancellation path.

Full builds pass98/97. Wrist calibration and active-cycle shell hiding are integrated; the same run verifies49 paired hide operations. Full-capacity animation tails, early Arming, pending/expired controls and zero-loaded cancellation remain outside the verified abort subset. Haptic capture feedback and an open underside loading aperture are still absent. See the current HANDOFF and reports/reload-native-abort-review-015932.json for scope, recording coverage and excluded preparation attempts.

## SPAS headset failure and presentation repairs — October 2 UTC

Actual headset trace010416 completed three physical native receipts but the user rejected inverted pickup, floaty guidance and native animated shells. Use the failed headset verdict, not completion counters, for release readiness. Raw reload contact now uses the same anatomical wrist calibration as the open hand. Active-cycle shell suppression covers pending advancement and gaps between real physical items without granting ammo authority. Bounded cancellation diagnostics distinguish input, ownership, expiry, capacity and other exits.

Full97/96 suites pass. New headset feel remains unverified. Full-capacity completion tails and extra native loading after cancellation remain separate issues; active-only shell suppression does not solve them. The bottom-entry profile remains revision4. Closed underside faces/open-port validation and haptic capture feedback remain limitations. See current HANDOFF for runtime results and exact scope.

Actual monitor check013308-369 (IPC013307) ran the updated DLL E15A2E4E9E6B2170BE81E8E26F57C92A239469AB5A368C1855359132EBDA5AF6 with scripted controller motion. One physical insertion completed4/19→5/18. Active-cycle shell suppression was verified by51 paired/102 individual native Pack destination comparisons, with0 source rejections or packing failures. All240 stereo pairs arrived with0 timeouts and0 camera restore/GPU failures; one completion exceeded50ms (maximum138.886ms). All974 temporary holds restored; hooks detached and15.234s stability passed. Ordinary ownership-expiry fallback was not exercised, so the existing audit's aggregate verdict remains false despite consumer/receipt/private-Pack/delivery/cleanup sections passing.

This run REPRODUCED THE TAKEOVER: fixture cancellation at2910087025200ns is labelled requested_input/Fixture2, after native receipt and without a pending item/request. Three later native server transfers occurred outside the physical transaction:5/18→6/17→7/16→8/15. They completed at ordinary~0.72s intervals after cancellation. The receiver schedules no extra Reload or Fire in this physical fixture. Cancellation currently restores the native loop without ending that reload. This is now a demonstrated adapter lifecycle issue, not a conjecture about user motion, missing guidance or streaming. Native abort operation research is ongoing; no fix for this extra loading is yet integrated.

The fixture's post-Tick completion now clears its unpublished output BEFORE cancellation, preventing stale preview/shell-control/hand ownership/start pulse from being republished. Its cancellation also records the Fixture flag. Three exact-helper cases pass each architecture, plus x86 Gameplay compilation and both full suites.

## SPAS bottom-entry path correction — October 2 UTC

User clarification: load from underneath, never through the side ejection port. Saved native geometry puts the target under the centerline, but the previous guide incorrectly followed the shell's almost horizontal nose direction. SPAS revision 4 keeps shell orientation/grasp and the observed endpoint, while guiding upward and forward from below the receiver. Item orientation and insertion travel are now separate reusable profile fields.

Both complete architecture suites pass (97/96). No new native or headset acceptance is claimed. The most recent headset run001451 acquired six shells and completed zero insertions. The sampled underside still has closed render faces; actual visual clearance during insertion remains to be checked. See the latest HANDOFF and loading-port source/independent evidence reports.

## Headset guidance feedback and repair — October 1, 23:58 UTC

The first usable headset run spawned8 shells but completed0 manual insertions.
Free carry was incorrectly labelled Guided, using older wrist geometry; that
presentation bug is fixed. SPAS profile revision2 also accepts approach from
underneath within the same front capture volume. New metrics retain genuine
capture/seat state and nearest approach per item. No haptic cue was added.

Full97/96 builds pass; scripted native regression235641-321 proves two captures,
two seats and6/9 ->7/8 ->8/7 native receipts after the changes. One recovered
frame timeout and unexercised fallback remain explicit. New headset feel is
unverified. See PHYSICAL-RELOAD-GUIDANCE-20261001.md and current HANDOFF.md.

## Repeated physical SPAS insertion verified — October 1, 23:20 UTC

Trace231748-438 completed two actual items/hand claims/rail seats, acknowledged
by separate native transfers5925/7813:6/24 ->7/23 ->8/22. The first shell stays
carried through the remaining184.9ms of pump cooldown before native reload is
ready. Full builds pass96/95. Scripted controller input drove this run; headset
feel and other weapon bindings still need testing.

reports/physical-repeat-audit-231748.json retains one recovered frame-request
timeout and unexercised ownership-expiry fallback. Its transaction, receipts,
raw-motion, private-palette and cleanup sections pass; aggregate flags remain
false. Do not confuse this partial acceptance with a fully clean delivery run.

## Actual physical SPAS insertion passed — October 1, 22:11 UTC

The real Gameplay consumer completed pouch acquisition, hand ownership, actual
renderer contact/rail, submission and native shell transfer in trace220114-852.
Loaded/reserve6/10 ->7/9; exactly one original reservation resolved. Synthetic
controller motion drove this check; repeated shells and headset feel are next.
See reports/physical-audit-220114.json. Its native consumer and private Pack
verdicts pass; overall false identifies an unexercised fallback path, not a failed
insertion. Other weapon families still require their native/profile bindings.

Explicit -BodyInventory now puts the existing SPAS supply on a chest comfort
anchor and enables shoulder selection. Ordinary sessions retain their current
reload behavior. Empty hands and manual pumping are not enabled by this change.

## Actual consumer integrated — October 1, 21:07 UTC

The physical SPAS consumer is now wired into Gameplay behind explicit
-PhysicalReload/bit0x2000000. Its real reserve, shared hand claims, pouch pickup,
raw renderer contact, rail, native request and item reconciliation pass9 composed
cases per architecture. Full builds pass 90/89 suites. Normal sessions remain
unchanged; this is not native/headset acceptance. The persistent request fixture
still cancels on a held server-boundary read failure before submission.

The shared pipeline is reusable across reload families. Each family still needs
its native operation and each weapon its prop/grasp/socket profile. The table below
is a rollout plan, not an all-gun compatibility list. Magazine, belt-fed, launcher,
pump and bolt bindings are not made playable by the SPAS component checks.

## Family coverage and integration status — October 1, 20:30 UTC

The target remains all campaign weapons, with reusable interaction families:

| Family | Shared work | Native/profile work still required |
| --- | --- | --- |
| Single-shell shotguns | Pouch, held ammo, rail, reservation, one-round request | SPAS physical consumer; compatible weapon-specific grasp/socket profiles |
| Detachable magazines | Hand arbitration, supply accounting, insertion and acknowledgement | Eject/seat state, magazine geometry and native completion |
| Belt-fed LMGs | Transaction steps, mechanism claims, hand poses | Cover/belt/box sequence and native family binding |
| Launchers | Mechanism claims, insertion, native command acknowledgement | Breech/projectile placement and launcher-specific state |
| Pumps and bolts | CycleAction coordinator and mechanism gestures | Verified cycling gate, geometry and completion observer |

These are rollout families, not completed compatibility claims. The native SPAS
350ms hold and one-shell transfer/rehold passed earlier. The first persistent
30s request fixture fails early with Owner/read rejection and is being repaired.
The common supply code passes13 portable and4 BC2 groups on each architecture;
full builds85/84 pass. Other weapons do not inherit an unverified manual capability.

## Current implementation and scaling — October 1, 19:49 UTC

The objective is coverage by reload family, not a separate implementation for
every weapon. Hand arbitration, item reservations, insertion rails, request IDs,
acknowledgements and tracking recovery are reusable. Each weapon still needs
verified grasp/socket transforms and compatible native state/prop evidence.

The campaign SPAS has passed an actual three-copy hold, exactly one conserved
shell transfer, re-hold and ordinary completion. Its default-off persistent
request policy passes repeated insertion and slow-reload checks. Full hand-driven
reload is not enabled yet. These checks do not establish native magazine,
belt-fed, launcher or manual pump/bolt support. Their family-specific operations
remain on the existing development plan, with automatic reload retained until
a replacement is actually verified.

The portable transaction coordinator is implemented and tested. Manual reloading

is **not enabled** in BC2. Existing controller reload still requests BC2's ordinary

automatic reload. The initial coordinator work inspected saved evidence and

source only; it did not read a live process or modify native state. Subsequent

October 1 research added a guarded read-only process observer and correlated a

root-owned automatic-reload fixture, as recorded below. No current weapon profile

gains a manual reload capability, and the manual native binding remains disabled.

## October 1 native evidence update

[BC2 pump/reload research](BC2-PUMP-RELOAD-20261001.md) and

`reports/reload-native-audit-20261001-112223.json` supersede the initial absence of

live reload observations. Exact reflected SPAS configuration identifies

`fltSingleFireWithBoltAction`, `rtSingleBullet` and `IsPumpAction=true`. Verified

native code locates firing-cycle states 7/8, reload states 10/11/12, the ammunition

transfer routine and pump/reload listener notifications. A nonfiring native fixture

then observed both firing branches changing **4/8 -> 5/7 -> 6/6 -> 7/5 -> 8/4**,

with approximately 0.72 seconds between shell transfers. The same support grasp

remained held through all four transfers. The audit records its source hashes,

47 rejected identity-changing external reads and one receiver async timeout.

These are ordinary native reload observations, not manual-stage control. External

reads are not atomic with pose evaluation; transient current state 12 was not

sampled, and no separate chamber state or authoritative branch was established.

The native pump delay/notification path is supported by configuration and decoded

code, but this nonfiring fixture did not dynamically validate a fired pump cycle.

Physical pump/bolt geometry and a native cycle deferral/completion gate remain

unverified. No native gate is enabled.

`CycleAction` remains portable: a future verified pump profile can recognize

rearward/forward phases; a turning-bolt sniper profile can recognize

unlock/lift, rearward, forward and lock/down phases. These are proposed geometric

phases, not claims that those specific native stages or a sniper binding are

already verified. A sniper's actual native cycle, mechanism roles, gesture limits,

fire restrictions and completion acknowledgement need their own evidence. The

shared coordinator can accept the resulting semantic action without weapon

category assumptions or copied BC2 offsets.

## Implemented boundary

`include/fvr/interaction/ManualReload.h` is a header-only C++20 policy with no

Windows, OpenXR, native addresses, ammunition arithmetic, meshes or game IDs.

It coordinates a bounded, immutable plan supplied by a future verified adapter.

One policy can therefore serve different reload mechanisms without embedding a

separate controller state machine for every gun.

The six semantic operations are `UnseatMagazine`, `SeatMagazine`, `InsertRound`,

`CycleAction`, `OpenBreech` and `CloseBreech`. Example plans are illustrative,

**not bindings for the current SPAS, XM8 or launcher**:

| Mechanism | Example plan | Adapter-dependent decisions |

| --- | --- | --- |

| Detachable magazine | UnseatMagazine → SeatMagazine → CycleAction | Whether cycling is needed, retained chambered round, magazine ownership and remaining ammunition |

| Individual rounds | InsertRound repeated N times | Verified available rounds/capacity, insertion recognition, interruption, whether a subsequent action cycle is needed |

| Single-round breech | OpenBreech → InsertRound → CloseBreech | Ejection, native open/close stages, ammunition compatibility and chamber readiness |

`ManualReloadConfig` holds at most eight steps and 64 total operations, with at

most 32 repeats per step. Every repeated shell/round needs a distinct physical

event and authoritative acknowledgement. There is no automatic refill or

inferred count. A caller chooses an appropriate plan from verified native state;

this foundation deliberately does not choose ammunition quantities or manufacture

an inventory item. Invalid/default plans cannot emit requests.

## API and state transitions

| Type | Responsibility |

| --- | --- |

| ManualReloadOwner | Opaque actor, actor generation, item, equip generation and reference-space identity; all required |

| ManualReloadSample | Coherent packet sequence, monotonic time, focus/tracking, verified-binding gate, explicit cancellation, recognizer neutral/event and native acknowledgement |

| ManualReloadGesture | Monotonic event ID plus semantic operation; physical hit testing and item ownership already resolved by the caller |

| ManualReloadRequest | One-shot monotonic request ID, complete owner identity, operation, step and repetition |

| ManualReloadAck | Exact request ID, complete owner identity, operation and Applied/Rejected status |

| ManualReloadResult | Phase, next expected operation, optional request, acknowledged/completed edges, accepted-operation count and cancellation details |

An initial fresh neutral packet arms the recognizer. A subsequent distinct event

matching the first operation starts a transaction and enters

`AwaitingAcknowledgement`. Only an acknowledgement matching the request ID,

owner and operation advances to `Active` or, after the final operation,

`Complete`. An observed magazine pose or ammo count alone is not an acknowledgement.

The future adapter must correlate the accepted native transition to this request.

Each next operation requires fresh neutral and a new physical event. Neutral

means the **gesture recognizer** is ready for another operation; it does not mean

releasing the gun or necessarily dropping a magazine already held in the hand.

A recognizer can become neutral after leaving a contact volume while retaining

that object. The policy supplies no grip-button mapping.

Duplicate input packets and event IDs never emit another request. Events arriving

while a command is pending are consumed rather than queued. An exact native ack

can arrive during a duplicate XR packet, but the same packet cannot also arm or

start another operation. Fresh neutral after completion releases the completion

latch. Request IDs are never reused by `Reset` or an owner change.

Focus/tracking loss, loss of verified bindings, actor/item/equip/space changes,

explicit cancellation, malformed input, time/sequence/event rollback, stale

tracking, unexpected operations and timeouts cancel local intent. Repeated input

still advances caller time and therefore cannot keep a pending reload alive.

Tracking recovery needs fresh neutral; a new packet arriving after a tracking gap

cannot continue an old gesture.

Cancellation reports the pending request ID and the number of already

acknowledged operations. **It sends no inverse command.** A magazine or shell

already accepted by native gameplay must remain subject to native state and the

adapter's reconciliation. A future adapter must clear `bindingsVerified` while a

cancelled or timed-out native operation may still complete; it must reconcile

native state and resource ownership before admitting a new plan. The core does

not implement that reconciliation. Keeping the flag true is a caller assertion

that the whole plan is currently safe to execute, not merely that signatures

were found once.

The policy instance must outlive in-flight requests. Replacing a policy/profile

requires an equip/owner generation change and native reconciliation so an old

ack cannot collide with a new instance's request IDs. Samples/acks are internal

C++ values, not an exported DLL or IPC ABI. A future transport extension requires

its own versioned fixed-width schema.

## BC2 evidence available now

These offsets identify inspected evidence in the installed executable; they are

not new signatures, public API or permission to write these fields. They remain

inside future BC2 binding work and must be rediscovered/validated for another

executable or Frostbite game. Runtime pointers and inventory slots are not

persistent weapon profile IDs.

| Evidence | What it establishes | What it does not establish |

| --- | --- | --- |

| `Bc2Profile.cpp` verifies reflected `EiaReload=29`; `Bc2InputBinding.h/.cpp` maps Reload into native gather-cache +0x98 bit 29 | Existing ordinary automatic reload input path | Separate eject/insert/chamber operations |

| `ControllerInput.cpp` maps right Secondary to Reload | Current button behavior | A manual-reload gesture mapping |

| `docs/CONTROLS-20260930.md`, native controls section | Historical fire 8→7 and reload 7/24→8/23 observed during native control validation | Exact authoritative chamber/magazine layout or per-shell completion events |

| `NativeIpcProbe.cpp --shot-reload` | Reproducible native reload input fixture: right Secondary at 700–850 ms | A physical reload test or stage control |

| `reports/native-trace-20261001-030650-167`, `docs/HAND-TRANSITIONS-20260930.md` | Native reload-root motion and stable tracked hand translation through it | Ability to pause native animation at a physical reload stage |

| `reports/launcher-selection-audit-20261001.json` | Guarded historical actor/item identity, original executable signatures/accessors, both firing-state branches and read-only ammo counters | A native reload command, per-stage callback or safe ammo write |

| `reports/launcher-state-fields-20261001.json`, `docs/LAUNCHER-BINDINGS-20261001.md` | Reflected WeaponStateData animation configuration, including SkipReloadAnimation at +0xc5 and IsPumpAction at +0xc0 | Mutable reload progress or an independently verified pump/bolt interface |

The selection audit is a dated read-only observation of PID 146632 at

2026-10-01T09:35:13.935699Z, not a claim about the current process. It verifies

`ClientSoldierEntity`, local-player ownership and stable inventory rereads.

Weapon wrapper accessors at original VAs `0x7efce0` and `0x7efcf0` return fields

`+0x3c` and `+0x40`, respectively. The eligibility path chooses a branch from

soldier flags rather than blindly reading one firing object. It observed SPAS

8/16, XM8 30/90 and selected launcher 1/5 at the two counters `+0x7c/+0x80`.

Those observations must not be reinterpreted as a proven chamber/magazine model.

The eligibility helper's state `+0x48` comparison does not establish a reload

phase enum. Old zero-count snapshots were previously superseded; this project

must not assume a current reload is blocked by historical ammunition values.

The WeaponStateData configuration field `SkipReloadAnimation` is not a shortcut

to manual reload. Suppressing an authored animation could leave native ammunition,

fire restrictions and hand/weapon geometry at unrelated stages. Likewise, the

existing translated firing-origin hook does not authorize reload-state mutation.

## Reusable capture and profile work

The existing batch collector already records exact weapon asset name, actor/item,

owner/equip episode, reference space, rig fingerprint, units, timing, settled

attachment state and optional named `native_weapon_bones`. The latter include

parents, native matrices, hidden flags and completeness/drop counters before VR

IK publication. This is a useful starting point for reload discovery across a

loadout. The short launcher name `40mmgl` is reused, so the exact scoped-XM8 asset

path and persistent family must accompany it. Use stable asset/profile identity,

never a numeric capture pointer, as the persistent key.

`weapon_mechanism_pipeline.py` can compare native root-relative bones across

saved poses and flag repeatable changes. It cannot infer that `jntWpn_N` is a

magazine, bolt or shell. Continuous reload motion also need not have the same

stable endpoints as the existing sight roundtrip, so preserve raw timed samples

and add explicit stage/command evidence before treating any fit as a binding.

At the initial coordinator checkpoint, the ordinary rig/profile capture lacked

reload-phase and ammo-transition observations. The later read-only observer and

112223 audit now supply SPAS state/ammo timelines, while an authoritative observer

on the same native update as the pose, interrupted cases and other mechanisms

remain future work. These observations were not implemented by the coordinator.

A bounded native automatic-reload capture should eventually correlate these

records on the **same native update**, using the original pose before edits:

1. Request/input edge and exact selected actor/item/equip identity.

2. Read-only verified firing/ammunition state before, during and after reload,

   including native interruptions and both relevant ownership branches.

3. Named weapon bones, parent topology, native hand poses and hidden leaves.

4. Native animation phase/event evidence sufficient to distinguish visual motion

   from an actual resource transfer or fire-ready transition.

Capture representative detachable-magazine, individual-round and launcher reloads

in batches. Add interrupted, empty/partial and zero-reserve cases after the normal

path is understood. Capture rate/retention must cover brief reload events; the

ordinary bounded/downsampled profile sampler alone may miss a transfer edge.

Compare repeated examples, identify mechanism roles using correlated geometry,

and retain uncertainty when evidence is incomplete. Discovery does not require

a headset, but physical reach, grip and comfort ultimately do.

Preserve the validated SPAS hidden leaf behavior: `Bc2Rig.cpp` observes a collapsed

`jntWpn_7` during reload and keeps exact native bytes. A collapsed leaf is not a

rigid grip frame. Do not substitute identity transforms, unhide it globally or

reuse rendered IK output as an authored mechanism baseline.

A future per-asset reload profile needs its own revision, exact native family and

rig identity, mechanism/contact roles, physical recognition parameters, operation

plan, native bindings/completion observers and per-operation evidence. Existing

aim/support/muzzle acceptance does not grant any of these. Unknown roles and

operations remain unverified. Do not enable a whole weapon from one successful

magazine animation or copy rifle reload offsets onto the launcher.

## Native integration gaps and next work

Native code and the later SPAS capture now identify ordinary ammunition transfer

and return-to-ready behavior. The next binding work is to validate ownership and

transition timing at the native update boundary, dynamically observe a fired

cycle, and establish whether stage commands or an isolated adapter can defer the

existing sequence without breaking its remaining-time loop. Do not implement a

gate by merely skipping a handler that must consume update time. No operation in

the new policy is currently bound to BC2.

Before runtime enabling, the adapter must establish:

- Exactly one authority for resource ownership and ammo transfers, including

  predicted/corrected state, ammunition conservation and no duplicate effects.

- Per-operation native dispatch and completion/rejection evidence, with native

  firing restrictions and interrupted/death/equip behavior preserved.

- Physical magazine/round identity, compatibility, reservation and cancellation

  reconciliation; the coordinator does not create those objects.

- Coherent current gesture geometry and button state. Never pair a current press

  with older publication geometry or replay a historical press after release.

- Off-hand ownership arbitration among support grip, sight manipulation, reload,

  and use. Gun aim/trigger hand ownership remains explicit; a reload request does

  not silently fire, select another weapon or steal desktop input.

- A hand-pose/weapon-mechanism presentation layer that follows the transaction

  while preserving native authored animation and releasing immediately on cancel.

  The current hand-pose preparation can provide mechanism grip visuals once its

  own bindings are verified; visual attachment alone never acknowledges gameplay.

The ordinary button reload remains useful as a compatibility fallback while those

bindings are developed. It must not be described as manual magazine/shell control.

BF3, BF4 and other Frostbite adapters can reuse the coordinator and capture method;

they need their own native signatures, asset profiles and completion observers.

No Refractor offsets or native layouts were imported.

## Validation and provenance

`tests/ManualReloadTests.cpp` contains 22 deterministic cases covering magazine

order, repeated shell insertion, breech sequencing, startup held input, event and

packet replay, pending-event discard, exact ack identity, native rejection,

partial progress, all owner identity fields, tracking/focus/binding loss, explicit

cancellation, time/sequence/event rollback, staleness, both timeouts, completion

latching, reset without ID reuse, invalid plans/samples and disabled bindings.

Standalone MSVC C++20 builds with `/W4 /WX /EHsc` passed all 22 cases on both x86

and x64 on October 1. Root owns CMake registration and whole-project builds.

This is deterministic policy validation only. Native manual reload and headset

acceptance are both unverified; no gameplay feature was enabled in this work.

Selected immutable evidence hashes (SHA-256 at this review):

| Artifact | SHA-256 |

| --- | --- |

| Installed executable identity recorded by selection audit | 3911fcc8914b0158c434cee36f19295316e715d4c9a0e70c857e8c39e3d54258 |

| reports/launcher-selection-audit-20261001.json | 2a80a8e0323e5ac2eba1bfa358927ec7d65c6e368e83ff672e2c13f00a6cfa2f |

| reports/launcher-state-fields-20261001.json | 8edd95772ce9f7284c7f795fcf7638032e7904e7cf997c6135e82204f5f8b1a8 |

| reports/controller-buttons-20260930.json | 46e2889baeeabd5d835d99c30941304d3c2df2839bcda271023d187c1b3d36a9 |

The controller-buttons JSON accompanies the historical controls report; its pose

and button samples alone are not an independent ammo-transfer trace. The numerical

reload observation above is explicitly attributed to the native-controls document.

