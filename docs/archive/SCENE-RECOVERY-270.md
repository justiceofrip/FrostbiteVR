# Replacement-player physical shoulder setup270

The269 run proved new-actor SPAS retirement and vehicle Exit, then stalled before
rifle testing. The old diagnostic tried stick selection while ordinary body
inventory requires a shoulder draw. This checkpoint changes only scene test
input: the receiver supplies one Exit pulse and no selection sticks; the scene
driver uses existing shoulder gestures and fresh actual inventory evidence to
draw the rifle before starting its reload sequence. No native inventory call,
ammunition edit or consumer reset is authorized by diagnostic progression.

The original58-second driver bound remains in effect. Focused x86/x64 suites pass the actual SPAS-first bootstrap regression, partial
and full magazine return, subsequent pump/shell/support within35.5 seconds, and
ambiguous/stale/foreign target rejection. The receiver Exit-only schedule also
passes; actual-game acceptance remains pending.

Both full normal builds passed all240 suites on x86 and x64. The source digest
is `2da3210022e46302b1494c6630fc5393943d66801520aa99e589e59a9aad8a4e`. Normal binaries and build symbols
are frozen in `bc2vr-recovery/normal270-player-checkpoint`; scene input is off.
The separate diagnostic build and actual-run acceptance are pending.

## Actual runs and current blocker

Run01 stopped before any restart because the reference matcher rejected a
confirmation dialog during fade-in. Root visually verified the saved dialog;
the private runner now permits at most two extra captures, with no further input
and unchanged matching thresholds. Frozen270 binaries were not changed.

Run02 reached checkpoint restart, boat Exit, ordinary shoulder draw of XM8,
original magazine removal/return with22 loaded and191 reserve conserved, and
return to SPAS. The new SPAS cycle2/shot2 completed a physical pump and received
one actual native acknowledgement. There were160 post-rebuild stereo
pairs, zero captured exceptions and clean detach/camera restoration.

The full sequence FAILED. Client Restore native invocation10382 (diagnostic
row8040) followed Ready by14.720ms. Its measured
before/after state is identical: current/previous/next2, timer0, loaded7,
reserve24, flags224; the captured snapshot is unchanged and restore matching
passed. Nevertheless the cycle service marked Restore failure7 and blocked
firing again. Pump input then failed native_cancelled, preventing support return
and shell insertion. The exact rejecting internal predicate is not established.

Next: reproduce this post-acknowledgement idle Restore against the persistent
service/adapter lifecycle, identify the rejected invariant, and preserve valid
completion without admitting contradictory ammunition/owner evidence. Do not
merely disable Restore validation or suppress this failing test. The fixture
now reaches the actual blocker without headset input. See the private
`scene270-recovery-02/partial-recovery-audit.json` and exact failed-native record.
No combined/headset/whole-campaign/whole-roster acceptance is claimed.
