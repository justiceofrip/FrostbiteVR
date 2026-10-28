# Checkpoint 220: inventory commands and resource-backed magazine hands

This checkpoint connects the existing portable hand and insertion-rail policy
to an explicit ammunition-resource contract. It does **not** enable that path in
the BC2 player adapter or establish a new headset result.

Both final ordinary builds pass **196 C++ suites** with `checkpoint220b` source
binding and private ammunition probes disabled. The ten focused private suites,
five Python audit checks and final native diagnostic also pass. The frozen
normal artifacts are `normal220-resource-hands-20261008` in the private recovery
directory; `recovery220-evidence.json` records their hashes and test evidence.

## What changed

`AmmunitionInventory` owns a fixed set of independent `AmmunitionLedger` instances
keyed by actor and weapon lifetime. Physical events enter as one-shot intents;
the native callback still has to admit and dispatch the exact command against
fresh counts. Weapon selection suspends old admission without deleting its
magazine. A late completion returns to the original item, including when another
gun is selected. Discard never credits reserve. A recycled address with a new
item generation cannot receive the old magazine's ammunition.

The inventory requires a globally ordered adapter selection publication. It
does not evict old entries to conceal ambiguous native effects. Its bounded
capacity is explicit; actor retirement and eventual safe reclamation need an
adapter lifecycle contract before long-running player use.

`DetachableMagazine` now has two explicit backends. Existing BC2 consumers keep
the default animation-hold backend. The resource backend reuses the same raw
controller contact, hand claims, extraction motion, magnetic rail, original
magazine grasp and `AmmoSupply` reservation. It requires a completed ammunition
receipt instead of `allThreeHeld`. An animation acknowledgement cannot substitute
for removal of actual loaded rounds. Reinserting the original requires an exact
return of its retained rounds, separately from legacy animation retirement.

Physical seating attaches the magazine and permits the adapter to release the
loading hand immediately. It does not invent ammunition confirmation. An exact
later receipt can complete the refill while a new support claim belongs to that
hand; resolving the old ammunition reservation cannot release the new claim.

The private native diagnostic now submits through `AmmunitionInventory` rather
than a singleton ledger. The existing native helper signatures, server callback
ownership, whole-object checks, delayed completion handoff and three independent
firing-copy observers remain unchanged. Diagnostic scheduling is still finite
and does not represent player hand input.

The first full x86 run exposed stack overflow `0xC00000FD` in the legacy physical
consumer regression. The new receipt payload had expanded each entry in its
128-record diagnostic journal. Revision 220b moves resource evidence onto the
physical input sample and leaves the repeated animation observation compact,
with a compile-time size bound. No stack-limit increase or skipped test is used.

## Deterministic coverage

- Fifty synthetic weapon instances retain independent partially loaded
  magazines across selection, return or discard/refill. These are accounting
  identities, **not fifty supported BC2 assets**.
- A rifle removal can finish after selection changes to a shotgun with its own
  pending shell operation. Receipts cannot complete the other item's command.
- Stale gestures, duplicate dispatch, expired admission, changed generations,
  capacity changes and unexpected native refills are rejected.
- Actual hand/rail consumers run original-return motions for empty, partial and
  full magazines at capacities 8, 15, 30, 32 and 100, both with and without reserve.
- A detached original is discarded, a chest replacement travels the existing
  magnetic rail, the hand regrips support before completion, and the exact refill
  conserves ammunition. Native receipts in these tests are mocked.
- Missing, wrong-owner, wrong-count, mistimed or animation-only completion
  evidence cannot advance the resource-backed hand transaction.

## Native check

The first `rifle-inventory220-01` run passes the strict native resource,
completion-handoff and inventory-rebind audit: 22/191 becomes 0/191, the original
22 rounds remain discarded, and the replacement finishes at 30/161. The queue
consumes exactly two intents, stores one native weapon resource and retains
that identity across the automated selection roundtrip. Both client copies
converge within 19 ms; the receiver delivers 240 stereo pairs without timeouts.
One read-only postflight sample reports an ownership race; a settled repeat
accepts all 31 samples. The diagnostic detaches cleanly.

This is separate from the mocked physical-hand tests. The final compact-record
revision uses `ammo-move220b` and `rifle-inventory220-02`; its results are retained
with the normal build receipt. It passes the same strict audit, actual-trace
replay and mapped-code check, with both clients converging within 18 ms, all
postflight reads accepted and 240 stereo pairs without timeouts.
The original failing x86 report remains in
`reports/220-x86-tests-failed.log`.

## Remaining integration

`Bc2MagazinePhysicalReload` and `Bc2MagazineDetached` still use the old animation
contract in normal gameplay. Their rendering/visibility selectors must migrate
with the new command service: a detached well no longer contains the rounds,
and a seated prop does not depend on a stock reload timer. Do not feed fake
`allThreeHeld` into the existing adapter.

Required next steps are a bounded gather-to-native command/result channel,
physical-to-stable inventory identity mapping, cancellation/resumption of hand
presentation for an already detached resource, and exact render ownership. Then
exercise persistent physical interactions across holstering, pickup, tracking
loss and actor replacement with the actual BC2 adapters. Empty-magazine no-op
completion is tested in portable policy, but still needs its native observer
path. Native source admission and headset acceptance remain separate.

The BC2 physical actor key currently combines weak-reference and soldier
identity, while native resource receipts use the soldier identity. Physical
weapon keys may also identify the shared rifle/attachment family. The new
portable resource tests use one consistent semantic namespace. The BC2 bridge
must provide an explicitly verified mapping for these different namespaces
before enabling the backend; do not rewrite a receipt's owner merely to pass
the portable identity check.

The immutable headset212 stage remains unchanged. Underbarrel reload behavior,
weapon profile enablement and the published GitHub snapshot are unchanged.
