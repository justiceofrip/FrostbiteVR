# Cycle recovery 420: overlapping native callback delivery

**Candidate; release blocked. No checkpoint 420 actual-game or headset
acceptance.** The seven focused suites passed for source `adfda81e`, including
the three cycle/recovery/journal suites. Both full normal builds passed288 suites;
the local runtime is frozen in receipt `fe55bcc3`, with no desktop promotion.
This extends [checkpoint 417's paid-stroke recovery](CYCLE-RECOVERY-417.md).

## Reproduced failure

The new regression starts with an acknowledged physical stroke and unchanged
ammunition. It then schedules these native observations:

1. An ordinary idle Update starts on a sibling firing copy.
2. A client Restore starts, carrying the established ready-to-tail snapshot.
3. The sibling's unchanged idle Update finishes and reaches the service.
4. The Restore finishes and reaches the service.

The previous code compared the Restore's entry time against the latest finished
Update across all branches. Step 3 moved that global time forward, so step 4
incorrectly cancelled the paid cycle. The retained cancellation blocks firing;
gameplay also uses that busy state to inhibit holsters and vehicle Use.

Checkpoint 417 already covered the opposite delivery order, where the Restore
arrives before the sibling finishes. The new test failed against the unchanged
service before this correction. **The scheduling is emulated.** It does not
reconstruct checkpoint 416's lost invocation 8748 or establish that this caused
the reported headset failure.

## Correction and retained guards

The service keeps at most one small copied idle-completion receipt per native
branch. It records the original invocation, operation and entry/exit times only
after proving unchanged idle state, ammunition, ownership, configuration,
capacity and flags, with the original deadline and no hold or guard mutation.

A later-delivered Restore may cross a sibling's finished time only when that
exact receipt proves the overlap. Every newer sibling must qualify independently.
Same-branch staleness still fails. New evaluations, restores, missing callbacks
and operation resets invalidate the relevant receipt; changed or missing proof
cannot borrow an earlier successful observation.

The receipt supplies no idle vote, physical authority, ammunition or additional
acknowledgement. The same paid cycle temporarily returns to convergence and still
requires two new guarded idle rounds from all three native branches. SPAS pump
and M95 bolt recovery share this correction. Pump audio playback, calibration,
custody and unsupported weapon behavior are unchanged.

## Validation and limits

Private evidence root: `G:\Unity\VRMMO\bc2vr-recovery\overnight420`.

| Evidence | Result |
| --- | --- |
| `red-validation.json`, `red-test.log` | Unchanged service fails the new `Converging && blocksFire` expectation; CTest exits 8 |
| `focused/validation.json`, `focused/tests.log` | Seven combined suites pass; no game input or headset run |
| `application.json` | Exact parent/candidate source and changed-file hashes |

The recovery tests cover both client Restore branches, client/server siblings,
SPAS/M95, changed state/counts/flags, expired or forged bounds, missing/reused
receipts, stale work and mixed valid/invalid siblings. The physical-pump
integration uses the production audio ledger and verifies one release/ACK and
two accepted cues through the newly reproduced ordering.

Unknown native contradictions remain failures. These results do not accept
headset handling, death/restart, the whole weapon inventory or campaign
playability. A new bounded actual-game check and headset acceptance remain
separate requirements.
