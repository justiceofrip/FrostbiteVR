# SPAS cancellation on Fire: bounded deferral and retained evidence

The headset run `native-trace-20261002-020931-412` recorded nine shell pickups,
three captures and two native insertions (6/14 to 7/13 to 8/12). It did not pass
the user's magnetic guidance test. Later cancellation cycles 2–5 were labelled
Fire while holding an item, with all three native branches held and no pending
physical insertion. No abort helper call was recorded for those cycles. Five
reserve rounds disappeared after the two physical receipts, without a further
physical request. Detailed native recording ended before those cancellations,
so the exact transfers and cancellation-versus-next-arming timing are unknown.
The two successful abort calls belonged only to tracking-loss cycle 7.

The original policy permanently abandoned cleanup on any native Fire input.
The saved baseline reproduction demonstrates that a subsequent fresh neutral
callback could not recover, even with unchanged owner, ammunition and state.
This is a reproduced source admission failure, not proof of the unrecorded
native sequence in every failed headset cycle.

The candidate defers a genuine Fire-only callback: `inputFlags == 1`, Fire true,
Order and Reload false, and all existing timing/configuration/owner/ammunition/
state checks satisfied. It does not claim a branch or call the native helper.
Original Update still receives its genuine, unchanged Fire context exactly
once. A subsequent neutral original callback may claim the existing helper
path inside the original lease deadline. That deadline never renews. A changed
count/state, stale owner, unsupported input or expired deadline still abandons
cleanup. Mixed Fire/Order, Fire/Reload, malformed flags and unsupported context
booleans remain rejected.

This does not establish a combined helper-plus-Fire native path. Ordinary Fire
in the same trace legitimately changes 2/2 to 6/7 and consumes one loaded round;
the current neutral abort postcondition intentionally still rejects that
outcome. There is no new helper write allowance or ammunition permission.
Held-trigger cancellation, native transfers before the neutral callback, and
all extra-refill cases are not claimed fixed.

`native_abort_cleanup.cycle_history` retains the latest 32 armed cycles in
chronological order, independently of the main trace's 20-second window.
`cycles_total` and `cycles_overwritten` disclose truncation. Every retained
cycle includes original deadline, initial ammunition, first/last admission,
first Fire deferral, per-branch deferred counts, claimed/completed masks and
terminal reason/time. Admission samples use the existing before-Update value
observations; they are not new native reads or post-Update receipts. No admitted
callback yields zero samples; a terminal timestamp is zero while still active.
There is no timer thread: expiry is recorded when observed by the next policy
operation, and no late operation can authorize a call.

Admission values are 0 None, 1 DeferredFire, 2 AlreadyClaimed, 3 AlreadyIdle,
4 HelperClaimed, 5 Rejected. Failure values retain their existing meaning:
0 None, 1 Expired, 2 Owner, 3 Cycle, 4 Input, 5 State, 6 NativePostcondition,
7 UpdatePostcondition, 8 NewCycle, 9 Stopped. HelperClaimed records policy
admission; actual invocation and exact postconditions remain in `records`.

Offline checks pass on x86 and x64: 17 abort policy groups, the original-code
failure reproduction, candidate short-Fire recovery, and `/W4 /WX` compilation
of the runtime translation unit. The tests cover sustained Fire until expiry,
mixed/malformed input, native shot/refill/pump changes, owner/cycle invalidation,
history rollover, exact helper byte contract and rejection of the unverified
combined Fire result. Full canonical builds and native verification follow
integration; no runtime or headset action was performed by this candidate.

The next bounded native check should use the existing established-Holding
SPAS setup, with positive loaded/reserve counts and no pending insertion. A
short genuine Fire pulse must occur inside both the original lease and the
20-second detailed recording window. Observe deferral, then original neutral
admission and all three unchanged-owner exits, exact helper side effects and
unchanged ammunition across each abort call. Correlate any genuine fired round
separately from reload transfers. If Fire lasts beyond the deadline or native
ammunition/state changes first, the expected result is a recorded rejection,
not a retry or a renewed lease. A longer held-trigger path requires additional
native evidence; do not enable combined Fire execution from this test alone.
