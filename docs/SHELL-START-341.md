# Deferred shell-start recovery341

The rendering capture also exposed a gameplay failure in
`bc2vr-recovery/depth340-native01`. The combined test completed the first SCAR
magazine operation and the SPAS shot/manual pump, then failed to acquire a shell
for insertion. BC2 remained alive, hooks detached and ordinary state drained.
This is a failed combined test, not a pass based on the successful pump alone.

The shell consumer recorded one acquired item, then `start_rejected` at input1795.
The source was a fresh SPAS owner with7 loaded/24 reserve/8 capacity, a valid
current gun claim, valid raw wrist, hand inside the chest pouch and no cancel
flags. `reloadInputReady` and all-three-idle were true. The native-start API
returned only a Boolean, so callback contention, a changing preread and an
actual invalid operation all produced the same immediate shell cancellation.
The exact failed native gate was not recorded in340; do not claim it was proven
to be one specific lock.

341 separates native start results into accepted, deferred and rejected. No
existing identity, callback exclusion, prehold or ownership check is bypassed.
Before an operation is admitted, a transiently unavailable native cohort or
callback boundary can defer. The physical consumer retains the same shell and
claim under normal freshness checks, sends no reload pulse and retries only on
a new controller sample. The wait is capped at250ms from the first deferral and
does not move with subsequent input. Each attempt uses a fresh cycle identifier
because a registered but unpublished cycle may have been cancelled when a
callback arrived. A rejection, release, ownership loss, stale source or timeout
still removes the pending shell safely. Only acceptance enables native reload.

This corrects the shared native entry/physical custody contract. It does not
enroll another weapon or assert that all weapon classes are accepted. The
currently proven shell consumer remains the exact SPAS configuration. Existing
Boolean callers keep their previous accepted-only behavior.

New deterministic cases cover same-claim retention, no native pulse or ammo
change while deferred, duplicate input suppression, fresh-sample success,
non-sliding timeout, explicit rejection and release. The focused actual-consumer
suite passes56 cases. Full normal builds and actual-game verification are recorded
separately after completion; no headset acceptance is implied.

The native report now includes bounded per-gate accepted/deferred/rejected
counters in `reload_flow.shell_start_attempts`. Physical reporting includes
`start_deferrals`, `start_retry_expired` and event13 for each deferred attempt.
