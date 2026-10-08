# Native ammunition operations — checkpoints 213–219

## Latest: native inventory identity — 219

`Bc2BodyInventory::ResourceBinding` supplies the existing coherent carried-item
generation to the shared ledger. The binding survives ordinary selection and
reference-space changes, is independent of display/hand claims, and expires
without native observations. Native weapon data/persistence replacement retires
the old key. The private dispatch rechecks those native fields before using the
binding; no equip generation is substituted for a physical weapon lifetime.

`root-monitor/rifle-inventory219-02` starts in a verified boat seat. A single
controller Use pulse exits it, followed by two ordinary rifle/shotgun selection
round trips. The resource ledger controls native 22/191 -> 0/191 -> 30/161 with
the original 22 rounds discarded, both completions deliberately deferred and
recovered, and one rebind onto a new equip context retaining weapon generation
8. Both client copies converge in approximately 17–18 ms, without stock reload
animation. Strict resource/handoff/inventory auditing and mapped-code checking
pass. Receiver: exactly 240 pairs, zero transport timeouts; postflight: SPAS
8/24 and all 31 client/server drain samples accepted.

The earlier `rifle-inventory219-01` passed ammunition/rebind assertions but its
extended receiver consumed 254 pairs and reported failure. Tooling revision C
stops consumption at 240 while continuing the input schedule and adds the
explicit vehicle-exit option, admitted only after occupant preflight. Failed
on-foot readiness checks and both runs remain preserved. These are synthetic
controller tests in BC2, not headset acceptance or physical holster gestures.

Both normal builds pass 195 suites. Additional deterministic checks exercise
32 native-reader-backed selection changes, recenter, same-pointer metadata
replacement and native observation loss. Multi-weapon ledger interruption
tests use mocked native receipts. The ordinary player interaction still needs
its magazine/hand/renderer contracts migrated to this resource backend.

This is private diagnostic work toward replacing reload-animation timing as
the ammunition backend. It is **not enabled in ordinary headset builds** and
does not establish additional weapon coverage or headset acceptance.

## Verified before checkpoint 216

- A live SPAS diagnostic removed eight loaded rounds on the owning server
  Update. Both client firing copies reached zero through normal replication.
  Returning those same eight rounds restored all three counts without changing
  reserve ammunition. Every observed operation stayed in idle state 2, timer 0.
- A separate live diagnostic discarded the original eight rounds, then called
  the native Transfer helper once: 0/24 became 1/23 on all three copies. This
  inserted a shell without entering the stock reload animation. Discarded
  original rounds are explicitly accounted for; they were not credited to reserve.
- The helper is selected by the native reload type: type 0 transfers one round,
  type 1 fills available capacity. Capacity checks must precede the shell call:
  the unguarded native helper will overfill an already full shotgun.
- Every mutation verifies the owning server callback, full native identity,
  selected configuration, exact function bytes/owned trampoline, surrounding
  firing-object bytes and unchanged Update context. Client agreement requires
  each client's own completed original Update, not copied server observations.

Private evidence under `bc2vr-recovery/pipeline-runtime-20261005/root-monitor`:
`ammo-move214-02/native-audit-client-schema.json` and
`ammo-refill215-01/native-audit.json`. Original inconclusive audit files remain.

## Rifle diagnostics and the 216 correction

Preserved rifle setup failures are useful negative evidence. Forward cycling
selected the underbarrel launcher; the older XM8-only diagnostic omitted the
ordinary empty-ammo inhibition and allowed stock reload. The corrected early
previous-weapon fixture with `--magazine-reload` selected XM8, removed 22 rounds,
and observed all three copies at zero. Refill was rejected before the call:
XM8's reserve multiplier is 2, while the initial diagnostic required 1.

The inspected Transfer instructions use this multiplier only to determine
finite reserve behavior. Checkpoint 216 admits exact bounded nonnegative
products of the configured magazine count and positive finite multiplier,
excluding the infinite sentinel and rounding/overflow ambiguity. Refill
eligibility is also checked before the initial removal.

The actual installed instructions pass 400 added reserve-multiplier cases,
400 previous refill cases, 68 signed remove/return cases and 14 explicit
prediction-snapshot cases in isolated x86 emulation. Emulation does not prove
thread scheduling, real client convergence or hand interactions.

The corrected live rifle run passed: XM8 22/191 became 0/191, then 30/161.
Its original 22 rounds were deliberately discarded by this diagnostic. Both
client copies matched refill within 14.3–15.3 ms; each operation's server Update
completed idle with timer zero. The strict reload-type-1 audit passes, the
mapped executable DLL sections match the frozen private build, and a separate
post-detach inventory read still shows XM8 30/161, SPAS 8/24 and launcher 1/7.
Evidence: `root-monitor/rifle-refill216-02`, including `native-audit.json`,
`mapped-module.json` and the immutable trace. No hand interaction was involved.

## Resource accounting and native evidence bridge

`AmmunitionLedger.h` now separates stable weapon-instance identity from equip
and reference-space context. Removing a magazine requires a confirmed resource
operation before minting its original-round token. Returning it restores exactly
those rounds; discarding it grants no reserve credit. Reserve refill and shell
insertion use the same conservation rules. Empty magazines require a logical
native-state acknowledgement even when no ammunition write is necessary.

Commands dispatch at most once. A timed-out or interrupted dispatched command
remains unresolved until exact evidence is supplied; it does not trigger an
automatic inverse operation. An old duplicate acknowledgement cannot complete
a different operation in flight. Fresh holster/recenter context may rebind the
same stable resource without replacing the original magazine token. The adapter
must provide a real stable weapon generation before this can be used in gameplay.

`Bc2AmmoResourceEvidence.h` checks the exact owning server invocation and each
client's independent completed Update before generating a ledger receipt.
Wrong ownership, repeated invocation identities, animation holds, missing
copies, prediction rollback and stale completion evidence are rejected.
The actual 214 remove/return trace and 216 rifle remove/refill trace replay
successfully through this C++ bridge and ledger using
`tools/replay_native_ammo_ledger.py`. Replay is offline evidence; it does not
enable a native call or prove the player's hand-input dispatch.

Both full normal builds pass **195 C++ suites** with private ammo probes off.
The focused ledger and native-evidence tests also pass independently. The
earlier sandboxed build-runner stall was retried with process permissions;
its failed build log and all rejected campaign preflights are preserved.

## Checkpoint 217: ledger controls the live private rifle diagnostic

The private runtime now queues the ledger command before calling the native
helper. It passes that helper's exact postcondition into the BC2 completion
observer, then feeds the observer each branch's own completed Update. The ledger
must acknowledge removal before a replacement operation can dispatch. The
diagnostic supplies intent; no pose or elapsed animation time supplies completion.

`root-monitor/rifle-ledger217-01/native-resource-audit.json` passes both the
reload-type-1 audit and the additional live-ledger requirements. XM8 again moved
22/191 -> 0/191 -> 30/161. The ledger records two completed operations, zero
failures, no pending command, an attached replacement and a discarded original
resource containing 22 rounds. Client refill convergence was 19.4–20.5 ms.
Mapped executable sections match `ammo-move217/BC2NativeProbe.dll`. Post-detach
inventory confirms SPAS 8/24, XM8 30/161 and launcher 1/7.

This is still a **single immutable selected-owner diagnostic**. Its provisional
resource generation uses that owner's equip generation, so it does not prove
persistence across real holsters, pickups, respawns or recentering. Ordinary hand
dispatch remains unchanged and both private flags stay off in normal builds.
The callback consumer uses the existing nonblocking diagnostic gate; production
integration must retain the exact server completion across contention instead
of losing its one required acknowledgement. The current check does not stress
that boundary. The first 217 campaign process exited after Continue and before
the reload module attached; a second launch completed the test. Cause unknown.

## Shared integration still required

The existing full-magazine interaction only changes presentation and suppresses
input. It assumes native loaded counts remain unchanged, and its recovery can
reattach the magazine. Simply calling the new helper underneath that contract
would invalidate its own evidence and reproduce another recovery bug.

The new ledger implements removed magazine rounds, an empty weapon, return of
that same magazine, discarded ammunition and reserve refill. Wiring the native
command queue and the physical hand/renderer consumers to it is still required.
A native acknowledgement must identify the exact operation and owner;
animation state, elapsed time and geometry cannot substitute for it. Tracking
or weapon changes must retire intent without returning ammunition to a new
owner. Support-grip release and rendering must consume the resource result
without waiting for the stock animation's duration.

Shared resource rules belong outside weapon-specific geometry. BC2 supplies
the verified native operation and replication evidence. Rifle/SMG magazine
poses, shell entry rails and future mechanisms remain profile data. Stock
underbarrel behavior stays separate until its manual mechanism is admitted.

Normal builds force both `BC2_AMMO_MOVE_PROBE` and `BC2_AMMO_REFILL_PROBE` off.
The frozen checkpoint 212 headset stage remains unchanged. No publication or
multiplayer compatibility is implied by these single-player diagnostics.
# Callback completion retention — checkpoint 218

The private native resource path now retains the exact server Update that
contains each ammunition call in a bounded, allocation-free handoff. Callback
contention cannot erase that receipt. The observer accepts delayed provenance
without replacing newer count observations or renewing their timestamps.

`root-monitor/rifle-handoff218-01` deliberately defers both helper-owning
completions to another callback. The strict audit requires and verifies two
retained, two drained and two deferred receipts, two completed ledger commands,
zero failures, and final XM8 counts 30/161 after discarding the original 22
rounds. Both clients converge about 16–18 ms after each call. Actual native
Update records remain idle, with no animation hold; mapped probe code matches
the frozen 218 DLL. The mod detaches and leaves BC2 responsive.

The first client drain includes one rejected ownership-change read, retained
as evidence. A separate settled capture accepts all 31 reads; server drain
accepts 31. Deterministic tests cover 10,000 threaded handoffs, stale/duplicate
publication, delayed provenance after newer server state, and independent
weapon ledgers across interrupted operations and changed equip/space contexts.
Those multi-weapon cases use mocked authority receipts. They do not prove the
BC2 inventory binding or headset behavior.

Player dispatch remains disabled for this new backend. The next integration
must reuse the body inventory's native item lifetimes, then migrate hand and
renderer custody away from the older animation-hold contract.

