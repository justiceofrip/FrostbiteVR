# BC2 pump, bolt and reload binding research â€” October 1, 2026

Read-only native analysis establishes a real bolt-action firing cycle and
single-round reload configuration for the exact campaign SPAS12_sp. The generic
reload coordinator is now backed by concrete BC2 state-machine and ammunition
transfer evidence, but **no manual-cycle/reload native binding is enabled**.

Research inspected the installed executable and verified live PID 146632 using
QUERY_INFORMATION | VM_READ. It issued no game input, memory writes, native calls,
hooks, suspension or focus changes. Root owns the separate native input fixture.
The hand-role checkpoint and runtime sources were not edited by this research.

## Exact weapon configuration, not category assumptions

`reports/reload-config-20261001.json` records reflected types, field offsets,
enum labels and values. `reports/reload-config-all-weapons-20261001.json` also
records both firing branches for every inspected firearm in the current inventory.
The executable SHA-256 is
`3911fcc8914b0158c434cee36f19295316e715d4c9a0e70c857e8c39e3d54258`.

| Exact asset | FireLogicType | ReloadType | IsPumpAction | Bolt delay/time | Reload time / threshold / post sequence |
| --- | --- | --- | --- | --- | --- |
| SPAS12_sp | fltSingleFireWithBoltAction (1) | rtSingleBullet (0) | true | 0.5 / 0 seconds | 0.72 seconds / 1.0 / 1.0 second |
| XM8_sp_s | fltAutomaticFire (2) | rtMagazine (1) | false | 0 / 0 seconds | 2.8 seconds / 0.75 / 0 seconds |
| 40mmgl, scoped-XM8 family | fltSingleFire (0) | rtMagazine (1) | false | 0.5 / 1.0 seconds | 2.45 seconds / 0.55 / 0 seconds |

All three use `FireInputAction=EiaFire(8)` and `ReloadInputAction=EiaReload(29)`.
Both SPAS `HoldBoltActionUntilFireRelease` and
`HoldBoltActionUntilZoomRelease` are false. The launcher's hold-fire flag is true,
but its actual FireLogicType is single-fire, not bolt-action: unused configuration
members must not be mistaken for an active mechanic. SPAS's separate
WeaponStateData `IsPumpAction` confirms its visual handling, while the actual
FireLogicType establishes the native firing-cycle behavior.

The chain is freshly resolved from current ownership:

- SoldierWeaponData +0x98 â†’ WeaponFiringData.
- Reflected `PrimaryFire` at WeaponFiringData +0x40 â†’ FiringFunctionData.
- Reflected `FireLogic` is an **embedded** FireLogicData at FiringFunctionData +0x0c.
- Reflected `Ammo` is embedded AmmoConfigData at FiringFunctionData +0x170.
- WeaponStateData is a reflected array at SoldierWeaponData +0x88, not a firing
  state object. Its `IsPumpAction` is +0xc0; `SkipReloadAnimation` is +0xc5.

These offsets describe this inspected build and are retained in the BC2 evidence.
They are not portable Frostbite offsets. The capture tool validates live reflected
class names and field metadata, unique executable signatures and ownership links.
Numeric process addresses, slots and short launcher names are not stable profiles.

## Native firing object state and time

`reports/reload-native-code-20261001.json` preserves 12 decoded native functions,
entry bytes, hashes and live-byte agreement, the 16-entry dispatch table and
current listener relationships. All inspected code matched the installed image.
Names below describe behavior recovered by inspection; they are not debug symbols.
Preferred-image VAs use base 0x400000.

| Native function | Evidence |
| --- | --- |
| 0x6e9000, firing update | Advances native timers, carries update context, dispatches transition/step/timed-step paths in a remaining-time loop |
| 0x6e62d0, state step | Dispatches on firing object +0x3c through the table at 0x6e6994; sets next state and phase time; invokes native shot/reload/pump notifications |
| 0x6e69e0, timed step | Consumes elapsed/remaining update time and handles firing/reload interruptions |
| 0x6dd6a0, transition commit | Writes old current +0x3c to previous +0x40, then next +0x44 to current +0x3c; emits applicable exit/entry callbacks |
| 0x6d6f40, ammunition transfer | Adds one round for rtSingleBullet or the missing capacity for rtMagazine, bounded by reserve, to +0x7c; subtracts transferred rounds from +0x80 under ordinary finite-ammo rules |
| 0x6cd540, effective capacity | Uses explicit override +0x98 or AmmoConfigData.MagazineCapacity multiplied by runtime +0x74 |
| 0x6ddb30 / 0x6ddb60 | Reload begin/end notification through listener slots +0x38 / +0x3c |
| 0x6ddb90 | Tests current state in 10..12 and derives native reload progress |

Wrapper fields +0x3c and +0x40 point to two different firing objects. Inside each
of those objects, offsets +0x3c/+0x40/+0x44 mean current/previous/next state. Do not
confuse those levels or rename both objects as a single authoritative object.
The existing eligibility branch uses soldier flags bit0x10 to choose the wrapper
field; the sampler retains both branches and records that choice explicitly.
The state update's ordinary phase timer is object +0x50. Other timers remain
available as raw bounded words rather than being assigned speculative names.

The transfer code establishes +0x7c as loaded ammunition and +0x80 as reserve in
this path. It does **not** establish a separately modeled chambered cartridge.
The SPAS has base magazine capacity4 and observed multiplier2, so its effective
capacity is8. A fresh guarded read at 11:18:29.312583Z found both SPAS branches
idle with4 loaded/8 reserve: room for four rounds without a preparatory shot.

## Observed state-machine meaning

These meanings come from configuration accesses, call relationships and state
writes; dynamic transition coverage is listed separately below.

| Current state | Native behavior relevant to this work |
| --- | --- |
| 2 | Ready/input processing; an eligible reload request or eligible empty-ammo automatic reload moves next state to10 |
| 5 / 6 | Initial shot preparation and shot step; successful bolt-action firing with further usable ammo selects state7 |
| 7 | Bolt-action hold gate. It tests the authored hold-until-fire/zoom-release flags; when released it sets the phase timer to BoltActionDelay and next state to8 |
| 8 | Notifies listener slot+0x30 (cycle/pump start), sets phase time to BoltActionTime and next state to1; leaving state8 notifies slot+0x34 |
| 10 | Reload-begin notification, total reload timing and initial ReloadDelay, then next11 |
| 11 | Waits ReloadTime Ã— runtime speed multiplier Ã— ReloadThreshold, then next12 |
| 12 | Calls the native ammunition-transfer function once, waits the remaining fraction, and repeats11 for single-round reload while capacity/reserve permit; otherwise next1 |
| 1 | Return/release wait, including reload-end notification and PostReloadSequenceTime when coming from12, then ready2 |

The timed-state path also has interruption branches that can transfer a round
when ending an individual-round reload. Instrumenting only the normal state12
call would miss those paths. All five static callers of 0x6d6f40 must be accounted
for before a manual insertion can become authoritative. Native dispatch can
complete several zero-duration states within one update; polling is not proof
that a transient state or callback did not occur.

## Pump callback and actual animation consumers

The current SPAS firing objects have listener arrays at +0x18/+0x1c. Read-only
listener inspection ties one +0x3c-branch listener to the wrapper's native firing
effects object +0xd0 and another listener to the soldier +0x1c4. Their callback
addresses and relationships are saved in the code-evidence report.

The effects listener's cycle callback at0x8024a0 forwards listener slot+0x30 after
setting its event bit. Reload begin/end forwarders at0x80f640/0x80f6b0 similarly
forward slots+0x38/+0x3c and emit native events. This provides concrete observation
boundaries for correlating pump/reload animation with native cycle state. Calling
one of these callbacks manually would only reproduce some notifications; it
would not prove authoritative ammunition transfer or completion.

## What physical cycling can control next

The native SPAS is a suitable candidate for a future **CycleAction** adapter.
Its state7â†’8 boundary separates firing from the native cycle notification. However,
its two hold-until-release flags are false, so holding/suppressing the ordinary
fire input cannot defer that automatic native cycle. There is no separately
verified EiaPump command in the current bindings.

A real physical cycle needs an owner- and shot-scoped hold at an established
native boundary, then exactly one continuation after the physical gesture. It
must coordinate the applicable predicted/corrected and local-server state,
preserve native fire restrictions and ammo, handle last-round/empty reload paths,
and leave unrelated actors/weapons untouched. The current external observer does
not establish all those authorities or a safe write/call ABI.

Do not implement the hold by blindly returning from0x6e62d0: its caller loops on
remaining frame time, and the step handler itself initializes phase time from
that delta. Skipping its time-consumption contract can stall the update loop.
Do not edit shared FireLogicData booleans or timers globally, write ammunition,
freeze the entire animation or replay simulation. An observer at the native
update/transition boundary, followed by an isolated ABI/ownership fixture, is the
next evidence needed before selecting a native deferral implementation.

Portable geometry should consume a **profiled phase sequence**, not a weapon
category or hard-coded gun ID:

- Pump: rearward travel, then forward return/closure.
- Turning bolt: unlock/lift, rearward extraction, forward return, then lock/down.
- Another mechanism: its own verified ordered translation/rotation phases.

Each profile supplies native mechanism roles, local axis/pivot, phase direction,
travel/angle thresholds, hysteresis and completion semantics. The core consumes
coherent controller poses relative to the held weapon and emits one semantic
CycleAction request. Native acknowledgement alone marks the action completed.
Grip release, equip/death, reference-space changes or tracking loss invalidate
partial gesture progress and require reconciliation with the native pending
cycle. The system must not create a new round or mark fire-ready from gesture
completion alone. Existing ManualReload supplies request/ack sequencing; a generic
stroke recognizer can be implemented against this contract after the native gate
and actual pump/bolt geometry are measured. No per-gun stroke constants or guessed
native writes were added here.

A bolt-action sniper rifle can reuse this contract after its own FireLogicType,
cycle configuration, actual bolt geometry and native ownership are verified. A
shared skeleton or the fact that a weapon is a sniper rifle is insufficient.

## Support-grip release finding

The pre-repair gameplay code explicitly set `cancel=true` for Reload held or
pressed before calling SupportGrip.Update. Ordinary reload therefore released
support even if the user kept squeezing. Separately, support contact and visible
attachment were derived from the currently animated native wrist, so pump/reload
motion could move an acquired attachment. Root owns the repair: remove the reload
cancellation and freeze the acquired authored support relation with its existing
owner/equip/reference-space guards. This research made no runtime changes.

## Read-only observer and checks

`tools/capture_reload_state.py` takes a PID, bounded duration (0..60seconds), sample
interval (2..1000ms) and output path. It validates the executable identity/path,
unique/live context, manager, soldier accessor and firing destructor signatures,
manager/firing vtables, reflected data types, current local actor ownership,
selected inventory, and firing-objectâ†’configuration links. It rechecks ownership
after each sample. No cached object pointer from an earlier process is used.

Example, run alongside root's explicitly controlled native input fixture:

```powershell
& <python> -B tools/capture_reload_state.py --pid 146632 --seconds 20 --interval-ms 5 --all-weapons --output reports/reload-state-<timestamp>.json
```

The selected item remains in `samples[].states`; optional `all_weapon_states`
records inactive items separately with an explicit selected flag. GetTickCount64
and monotonic timestamps permit correlation with native telemetry. Both firing
branches retain a bounded0xb0-byte state sample and unambiguous offset labels.
These external reads establish stable **identity**, not an atomic native-update
snapshot. Capacity/multiplier configuration and all reflected enum labels are
recorded once with provenance. A changed config/object is rejected.

Both one-shot read-only smoke captures passed with zero rejected samples. Nine
offline mutation checks reject wrong data/config/ammo/vtable links, owner-pointer
races, ambiguous signatures and out-of-image reads, while accepting the saved
valid state. Evidence: `reports/reload-observer-offline-checks-20261001.json`.

## Native reload observed while support remained held

`reports/reload-native-audit-20261001-112223.json` preserves source hashes,
identity, both firing-branch timelines, support correlation and explicit limits.
Sources are `reports/reload-state-20261001-112223.json`,
`reports/native-trace-20261001-112224-172/native-trace.json` and the receiver
`reports/native-ipc-20261001-112223/result.json`.

Root's fixture selected SPAS, acquired support, then sent ordinary EiaReload.
It did not fire. The read-only observer accepted 3,603 samples (2,507 SPAS) and
rejected 47 reads when its ownership recheck changed. That comparison includes
flags and selection; its generic reason does not prove actor replacement. The
accepted SPAS actor, player, inventory, selected item and firing-object identities
remained stable. The observer started before the optional all-weapons mode was
added, so this particular report contains selected-item states only.

Both native firing objects changed **4 loaded / 8 reserve -> 5/7 -> 6/6 -> 7/5 ->
8/4**, conserving 12 rounds throughout accepted SPAS samples. Main-branch shell
transfers occurred at GetTickCount64 ticks 457278906, 457279625, 457280343 and
457281062: intervals 719, 718 and 719 ms match the configured 0.72-second shell
reload. State 10 starts reload, state 11 times each shell, and previous-state 12
accompanies the transfers. Current state 12 is too brief to have been sampled in
this capture. The full magazine enters state 1 for the configured one-second
post sequence and returns to ready state 2. The other branch reaches the same
ammo result with independently observed timing; external polling is not an atomic
native snapshot and does not establish branch authority.

Support used **one grasp token** from tick 457275593 to 457281593, covering all
four shell transfers. Closest support records to those transfers retain that same
token and `holding=true`. There were 1,136 held samples, one grab, one release when
the fixture relaxed its squeeze, zero preservation failures and zero fire samples.
The grip was released after the fourth transfer but before the native post-reload
sequence finished; this capture does not establish support persistence through
that entire later sequence. Root's separate grip audit checks authored/visible
attachment relations.

Rig source changes, packing failures, fallback failures, rejection counters and
tracking failures were all zero; native animation source writes remained false,
and native hooks were disabled when the session ended. The receiver consumed
240 stereo pairs with **one async timeout**, mean latency 16.916 ms, maximum
46.818 ms, no completion after 50 ms and no GPU-busy response. This is functional
reload/support evidence, not a zero-timeout transport result or a headset test.

The dynamic capture confirms ordinary native per-shell transfer and repaired
support retention during those transfers. Manual native-cycle deferral, physical
pump/bolt geometry, server coordination and headset handling remain unverified.
No manual native binding was enabled and no ammunition was written.


## Proven reload flow observer added later on October 1

`src/games/bc2/Bc2ReloadFlow.h/.cpp` now turns the static call graph into a
validated, read-only binding alongside `Bc2ReloadState`. It adds six complete
function fingerprints and explicit call links to the six original state proofs.
The new functions are Update 0x6e9000, forwarding thunk 0x6ec6c0, special
FireLogicType=4 update 0x6e1bb0, preparation 0x6e6d20, and soldier routes
0x8ad460 / 0x89b800. Discovery requires unique executable prefixes, exact full
fingerprints, the original firing vtable and all 16 state-dispatch targets. A
copied-memory callback can revalidate the same proofs before observation.
Unsupported or relocated images fail closed. There are no calls, hooks, native
writes, ammunition writes or enabled manual-reload capabilities in this helper.

Both wrapper branches have concrete update routes:

| Wrapper field | Getter | Soldier route calls forwarding thunk | Forwarding target |
| --- | --- | --- | --- |
| +0x3c | 0x7efce0: `mov eax,[ecx+3c]; ret` | 0x8ad8c3 / 0x8ad9c3 | 0x6ec6c0 -> 0x6e9000 |
| +0x40 | 0x7efcf0: `mov eax,[ecx+40]; ret` | 0x89baa5 / 0x89bb8e | 0x6ec6c0 -> 0x6e9000 |

This proves separate object/caller relationships. It does **not** yet prove which
branch owns a campaign ammunition commit, whether either path is prediction or
correction, or how a corresponding local-server firing object is reconciled.
Identical final counts in the earlier capture are not that missing authority proof.

The transfer classifier records the exact caller return address and matched
owner-validated wrapper branch, with these five distinct paths:

| Transfer call -> return VA | Proven context |
| --- | --- |
| 0x6e682c -> 0x6e6831 | Ordinary state12 |
| 0x6e6b94 -> 0x6e6b99 | Timed-step interruption |
| 0x6e1cb4 -> 0x6e1cb9 | Special FireLogicType=4 update |
| 0x6e1dab -> 0x6e1db0 | Special FireLogicType=4 update |
| 0x6e6d67 -> 0x6e6d6c | External preparation when loaded is zero, reserve is positive and total configured reload duration is zero |

The last path is **not a second timed-interruption path**. Its containing function
also performs preparation/reset/notification work. Calling it as a reload
shortcut would skip assumptions made by its native callers. The classifier labels
function-entry provenance only: it cannot acknowledge insertion, confirm transfer,
mark fire-ready or establish authority. The caller must supply a fresh snapshot
from the same validated owner/invocation; address equality does not prove timing.

`DecodeReloadUpdateContext` consumes an already-copied 0x30-byte native context.
It exposes delta seconds at +0x18, an opaque word at +0x1c, the reload-time
multiplier at +0x20, raw boolean flags +0x24..+0x28, and the low three bits at
+0x2c. Caller action getters establish these bits as Fire, **Order**, and Reload;
the second bit is not a zoom request. Bounds on copied delta (0..1 second) and
multiplier (0..1024) are conservative observer acceptance bounds, not claimed
native simulation limits. Unknown bits, malformed bools and nonfinite values
are rejected. The +0x24 flag gates both listener notification and reload
initiation, so it cannot safely be treated as an effects-only switch.

### What still prevents physical insertion from owning the reload

State2's 0x6e6535..0x6e6579 branch accepts an eligible empty-ammo condition even
when the Reload input bit is absent. Suppressing EiaReload therefore does not
prevent automatic empty reload. Once SPAS enters state12, 0x6e685a..0x6e6892
selects state11 again while the single-round configuration, capacity and reserve
allow it; another Reload edge is unnecessary. One gesture-generated button press
per shell would still allow the native loop to fill the weapon on its own.

A native whole-reload request after a physical sequence can be an explicitly
assisted mode, but cannot be presented as per-stage native authority. It also
needs a clear treatment of the automatic-empty path. Calling ammunition-transfer
0x6d6f40 directly is not an insertion API: it bypasses the scheduling, branch
coordination and native callbacks surrounding the ordinary transfer.

The remaining-time loop at 0x6e9135..0x6e9183 dispatches transition commit when
next differs from current, ordinary step otherwise, or timed-step while the phase
timer is positive. A prospective deferred state must preserve this loop's
remaining-time contract. Simply returning from step/commit, or zeroing a timer,
is not an established safe hold. No such hold is enabled.

The next bounded native observation should record update entry/exit, context,
exact firing pointer and wrapper branch, current/previous/next states, timer,
transfer call-site and pre/post ammunition, native tick/sequence plus nesting,
and the corresponding local-server object relationship. Cover one partial SPAS
reload, an interruption, one detachable-magazine reload and the empty-ammo path
as ammunition and the campaign permit. Do not infer missed callbacks from polling.
First establish which changes persist through both native branches and their
server counterpart. Then choose an owner- and request-scoped state deferral that
consumes time and resumes exactly once through the native implementation.

That boundary can back ManualReload's native acknowledgement after real transfer
or confirmed native cycle completion. Detachable-magazine gestures, individual
shell insertion, belt-feed presentation and pump/bolt phase sequences remain
portable policy; their ammunition authority and per-stage dispatch stay in the
BC2 adapter. A belt container, chamber or bolt-ready state must remain unknown
until its own native representation is observed. The current helper reports
`manualGateEnabled=false`, `nativeCallEnabled=false`, `authorityProven=false`,
and transfer classifications are never acknowledgements.

Validation: `tests/Bc2ReloadFlowTests.cpp` passed four deterministic test groups
under x86 and x64 with `/W4 /WX`. Optional installed-file-only tests passed all
12 function proofs, all five transfer sites and all 16 dispatch entries, rejected
15 changed byte regions plus duplicate signatures, wrong ownership, malformed
context/proof metadata and relocated bases. This was static/offline validation;
no new native or headset test was performed by this work.

## Bounded native flow runtime observer

`src/games/bc2/Bc2ReloadFlowRuntime.h/.cpp` adds observation-only instrumentation
for root's next bounded native fixture. It is not a manual-reload gate. The
separate `ReadReloadFlowBoundary` callback reader and `ReloadFlowRecords` fixed
storage are deterministic and usable without a game process.

The x86 ABI was checked from complete native functions and call sites:

| Boundary | Native convention and stack cleanup | Observer forwarding |
| --- | --- | --- |
| Update 0x6e9000 | ECX=this, context pointer then one opaque DWORD; `ret 8` | Original called once with unchanged context pointer and DWORD |
| Commit 0x6dd6a0 | ECX=this, context pointer; `ret 4` | Original called once with unchanged context pointer |
| Transfer 0x6d6f40 | ECX=this, one stack DWORD; `ret 4` | Original called once with the complete raw DWORD |

The transfer body does not consume that stack argument in this inspected build;
it remains opaque rather than being narrowed to a guessed boolean. All five
known callers ignore its return value. The inspected update callers at 0x6b0014,
0x6ec6ea and 0x7d4dfa pass the same two arguments and ignore the return; the sole
commit caller at 0x6e9151 does likewise. These are void observation forwards.
Synthetic MinHook tests exercise the same signatures and exact DWORD preservation.
They establish wrapper mechanics, not a successful native session.

Integration is explicit:

1. Call `reloadFlowRuntime::Install` after MinHook initialization. It requires
   discovery and live code-proof agreement and creates three disabled hooks.
   Root's existing global enable remains responsible for enabling them.
2. `Start` starts one recording window of at most 20 seconds. It does not enable
   MinHook or make a native gameplay call.
3. Publish successful, fresh `ReadReloadState` snapshots at no more than 100ms
   intervals with `PublishOwner(snapshot, deadlineNs)`. The deadline uses the
   absolute QPC-derived nanosecond epoch and may extend at most 250ms beyond the
   snapshot time. The report's existing 128-row snapshot cap must not prevent
   fresh leases during the bounded observation window. Call `ClearOwner` when
   the actor/item becomes unavailable. Expired or cleared publications cannot
   authorize another sample.
4. `Stop` disables only these hooks, stops recording and drains active callbacks
   for at most two seconds. A failed drain requires retaining the module and its
   trampolines, then retrying; no original trampoline is freed by this observer.
5. Call `Report` after successful drain for the complete JSON object. While a
   drain is incomplete, it emits counters and an explicit unavailable-records
   status rather than racing writes to the record array.

Callbacks first match the published exact firing pointer, then recheck local
player/weak/soldier/controlled-actor links, selected inventory and item, both
wrapper pointers, the weapon/config chain and current firing vtable. Two copied
0xb0-byte states plus repeated ownership reads reject changing observations.
Reflection field discovery is done in the published snapshot path, not every
native callback. No callback allocates storage, waits on a mutex, scans an
unbounded collection, writes native data or modifies the caller's context.
Short atomic try-locks protect copies into private storage; contention becomes
an explicit dropped-observation count. Native originals always run, including
when recording is disabled, stale, full, malformed or unmatched.

Each retained invocation records entry/exit times and GetTickCount64 ticks,
thread, bounded nesting depth, parent and enclosing-update IDs, actual return
address, exact owner/branch and before/after current/previous/next state, timer,
loaded/reserve counts and raw flags. Update/commit retain all 48 copied context
bytes before and after the original; decoding failure does not hide raw evidence.
Transfer calls retain the exact known-site classification or `null` for an
unknown caller. The zero-duration preparation transfer may legitimately have
no enclosing Update record. New/changed-owner, lease expiry or unvalidated exit
state produces an explicit incomplete/identity-lost observation, never a native
acknowledgement. A function entry with unchanged ammo is not called a transfer
success.

Storage is fixed at 8,192 invocation records and nesting is capped at depth8.
Overflow, lock contention, missing owner, state-read failure, context failure,
excess nesting and calls after the capture window each remain visible in report
counters. First records are retained rather than silently overwritten. This
captures the local selected weapon's two validated client branches only; other
objects are counted as unmatched. No local-server authority is inferred from
those counts or from a matching final ammunition total.

Validation: six deterministic groups passed x86/x64 under `/W4 /WX`, covering
both branch layouts, read-only preservation, malformed/racing state and ownership,
lease expiry, nested update/commit/transfer, independent preparation transfer,
wrong-thread/owner/completed-parent rejection, incomplete exits and fixed-capacity
overflow. Synthetic x86 MinHook tests passed 3,072 calls with exact arguments,
unchanged context storage and correct stack cleanup. Root still owns native
installation, fixture execution and evidence review. No game process was opened
or altered by these tests.


## First native flow capture — 14:53 UTC

`reports/reload-flow-audit-20261001-145328.json` records hashes of the finalized
native trace, receiver, manifest and completion report. The matching trace is
`reports/native-trace-20261001-145328-952/native-trace.json`. Its authored rig
samples identify the exact selected `SPAS12_sp`, with94 samples matching the
observed weapon pointer. Process144972 used the same inspected executable hash.

The observer retained3,817 finished, identity-preserved invocations on native
thread146756:3,783 updates,25 commits and9 transfers. Ownership reads, copied
contexts, nesting, try-locks and storage reported zero failures or drops. All
nested records fall inside the corresponding same-owner/thread update. Every
owned Update returned to0x6ec6ef. Hooks drained and were disabled; the game stayed
responsive with no new crash report. Native state/animation writes remained off.

The +0x3c branch at capture-local address0x067e2470 made five ordinary-state12
transfers, each exactly one conserved round:
`3/24 -> 4/23 -> 5/22 -> 6/21 -> 7/20 -> 8/19`. Intervals were718,719,719,719ms.
All entered and exited transfer with current state12 and returned to0x6e6831.
Their enclosing Update contexts had the Reload bit clear, dynamically confirming
that the native single-round reload continues without another Reload request.
The first four update invocations contain11->12, transfer, then12->11 commits;
the fifth exits toward state1 and later returns to ready2.

The +0x40 branch at0x067e23c0 has only **four** matching transfer invocations.
It reaches7/20 through those calls, then changes to8/19 outside a recorded Update.
Snapshot614 still reads7/20 at469946343743200ns; Update3311 begins8/19 at
469946348993800ns, about5.25ms later. The current owner lease covers this interval
and no recorder failure occurred. This identifies a state-propagation/write path
outside the observed Update/transfer boundaries; it does not identify that writer
or prove which branch is authoritative. The two branches also differ in raw
context flag+0x25 (0 for+3c,1 for+40), retained without assigning an unproven name.
Global counters saw14 transfer calls, of which9 matched the selected owner.
The remaining5 are unowned evidence, not proof of a local-server counterpart.

This fixture does **not** establish support retention across reload. Support
was acquired at tick469939765 and released at469940234, before Reload begins at
469942265. The release reports cancel_flags4 and reason3 while squeeze remained
held; its cause belongs to the separate interaction diagnosis. The capture fired
no shots. The receiver consumed240 pairs with2 async timeouts and3 completions
above50ms. Observer overhead was not isolated, and this was not a headset test.

Interruption, automatic-empty reload, detachable-magazine reload and bolt-cycle
paths remain uncovered by this run. The next authority work must identify the
+40 state propagation and native-server ownership before choosing a deferral and
one-shot continuation boundary. No native manual gate or acknowledgement was
enabled as a result of these observations.

## Physical insertion presentation requirement

The user now explicitly wants magazines and shells positioned correctly in the
hand, followed by a smooth magnetic slide into the insertion path. The proposed
portable `ReloadInsertion` increment is independent of the unresolved native gate.
It needs an authored item-to-hand transform and measured item landmark, rail entry,
axis, travel, seated pose and orientation tolerance in weapon-local metres.
Those dimensions must come from actual mesh/bone evidence; this ammunition-flow
capture supplies no magazine or shell insertion geometry.

The recognizer should accept exact actor/space/equip/resource generations, the
current hand ownership claim, and coherent raw hand/item/weapon poses. It detects
capture from the item landmark, tracks raw axial motion relative to the moving
weapon, and returns smoothly guided rendered item/wrist targets separately from
raw input. Hysteresis and finite capture/release bounds should allow natural
withdrawal without jitter, reject back-side entry and teleports, and release on
tracking/ownership loss. A snapped render wrist must never become its own contact
proof. Completing the rail emits a one-shot SeatMagazine or InsertRound candidate;
it cannot decrement reserve, create a physical resource, assert fire-ready or
acknowledge a native operation. Native ammo-resource ownership and actual per-stage
completion remain adapter obligations beyond current BodyInventory support.


## Missing snapshot writer located; next native capture prepared

Static inspection after the failed menu headset test located native firing-state
restore at 0x6d71a0 and the matching export at 0x6cd940. The 64-byte snapshot
carries loaded ammo at +0x18, reserve at +0x1c, current/next state at +0/+4,
and phase time at +8. Restore writes these into firing +0x7c/+0x80,
+0x3c/+0x44 and +0x50. It also updates previous state and sends a listener
notification for a changed snapshot flag; replacing it with plain copies would
skip native behavior.

At 0x8bb42c the caller obtains wrapper +0x40 through the proved 0x7efcf0
getter, then calls restore (return 0x8bb438). A separate path exports wrapper
+0x40 and restores wrapper +0x3c (return 0x89216b). These are concrete state
propagation routes, not yet dynamic proof of the fifth SPAS ammo update or
identification of the authoritative local-server branch.

The existing bounded reload observer now also forwards/records original restore
calls, with the exact selected-item lease, source address and 64-byte source,
caller, before/after state, and source stability. Event kind3 is Restore.
restore_fields_matched means the captured source projection matches the native
output with identity retained; it is not an ammo-operation acknowledgement.
No manual gate or ammunition write was enabled. Original restore runs exactly
once regardless of observer rejection. The user need not wear the headset for
the next native auto-reload capture that will test this missing-writer hypothesis.

Targeted x86/x64 checks pass: 14 full function proofs, 17 byte-mutation rejections,
ambiguous signatures rejected, five decoder/provenance and seven runtime groups.
The x86 four-hook ABI fixture forwards 4096 synthetic calls unchanged. Root owns
full integration builds and live checks. Evidence and source hashes:
reports/reload-snapshot-restore-20261001.json. No live process was used here.
