# Checkpoint271: drain the callback after cycle completion

The shared native pump/bolt service left an idle Update marked open when its
final observation was unavailable after another branch completed the cycle.
The next exact unchanged idle Restore then cancelled a completed physical pump.
This is callback bookkeeping, not a SPAS geometry or ammunition change.

Actual270 run02 supplies the ordering: client Update10379 began at
102436365724200ns. Server Update10372 finished its ordered tail and published
Ready at102436366099200ns. Client10379 ended at102436366297200ns without a
retained final boundary. Missing() ignored Complete and kept its open marker.
Unchanged idle client Restore10382 then rejected that stale marker at
102436380819700ns, blocking firing before support return and shell insertion.
Native invocation10382 is diagnostic row8040.

Missing() now drains only its exact selected firing object and nonzero open
invocation in Watching/Complete. It does not clear ammunition, cycle history,
owner identity, or the original pending Ready receipt. Active manual debt still
cancels on missing evidence. Wrong callback IDs and contradictory ammunition or
owner snapshots remain rejected. Pump and bolt use the same correction.

The focused regression reproduces completion during an outstanding callback,
with acknowledgement both before and after the missing observation. It fails
on the old service and passes with the correction for SPAS and M95. Negative
cases cover wrong branch, wrong/zero invocation, changed count, changed owner,
and missing evidence during an active hold. Existing repeated-cycle tests pass.

Both full normal builds passed all240 suites on x86 and x64. The source digest
is `4a76d481b5ee23907c37572611a26cb79b3b7bf70f9ab70f77314396c4b830ae`. Normal binaries and build symbols
are frozen in `bc2vr-recovery/normal271-player-checkpoint`; scene input is off.
The matching diagnostic build passed the actual recovery sequence below.

Private evidence: test-temp/cycle271-before-test.log,
cycle271-after-test.log and cycle271-recorded-overlap.json. No headset or
whole-roster acceptance is inferred from deterministic callback fixtures.

## Actual-game acceptance

Private scene271-recovery-01 completed the combined ordinary-input sequence:
unfinished old pump, native-menu checkpoint restart, verified client/server
actor replacement, boat Exit to empty hands, physical shoulder draw of XM8,
original magazine removal and return, SPAS draw/fire/manual pump, support return,
then shell insertion and support return. XM8 remained22 loaded/191 reserve.
SPAS loaded7/reserve24 after firing, then8/23 after one shell. Pump cycle2/shot2
received exactly one native release and acknowledgement. No final cycle failure
or fire block remained. Support returned46.442ms after pump Ready and131.817ms
after shell completion; the former three-second hand delay was not observed in
this sequence.

There were240 consumed stereo pairs,161 after rebuilding;40 saved pairs had
pixel comparisons and were distinct. No captured exception, GPU failure or
camera-restoration failure occurred; hooks detached and BC2 remained alive.
One additional published pair was discarded (241=240+1). The private audit
initially required published==consumed; it now checks exact accounting, the
bounded receiver's240 ordered receipts, saved-pixel evidence and graphics errors.
StagedFrameProducer permits one pending pair to be discarded during shutdown.
This does not claim that every frame was pixel-compared or that no frames dropped.

The original fail-before regression covers the precise overlapping callback
ordering; the new game run verifies the full recovery path without requiring
that race to recur. Retirement happened with the replacement SPAS selected;
the separate rifle-first retirement branch was not exercised. This is one
native-menu restart, not death/restart stress testing or a general graphics-crash
fix. M95 shares the deterministic fix but was not fired in this new native run.

## Next headset check

Use the staged normal271 build with a fresh BC2 process. Verify rifle magazine
return/replacement, SPAS fire and physical pump with immediate support regrip,
then shell insertion and shoulder stow/draw. Check that grip alone in front of
the body does not draw a holstered weapon. Finally open/close the menu and reload
the checkpoint once, then repeat a reload. This remains pending visual/feel
acceptance; the automated receiver supplies poses but cannot judge comfort.
