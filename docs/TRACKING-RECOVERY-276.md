# Tracking recovery and campaign menu: checkpoint276

The successful focused actual-game run is
`bc2vr-recovery/pipeline-runtime-20261005/root-monitor/resource-origin276-05`.
It uses source hash
`b9d9200da0e69d306b86b147f064172e79d149f8f8e9b0f322cb4f5984256540`.
Read `origin276-review.json` and `origin-receipt-review.json` for independently
checked outcomes and their limits. Human headset acceptance remains pending.

## Production changes

- A tracking origin change routes same-actor/same-item SPAS input to its existing
  physical adapter. It does not falsely accept a new native selection, recreate
  the adapter, forgive an owed stroke or enable firing. The native adapter must
  commit its explicit, drained rebase before ordinary selection becomes ready.
- Rebase converts the private native-service candidate before asking for its
  observation watch. This also permits a still-held cycle that has not already
  entered tracking-loss recovery. Configuration, identity, callback exclusion,
  cancellation epoch and publication revision are checked before committing.
- A native held Update already inside its original call may finish its exact
  zero-delta Step after recovery starts on another thread. Its invocation,
  branch, cycle, context, actual override and original deadline must match.
  No new callback or controller authority is granted by this drain path.
- Boundary sampling takes its timestamp after acquiring a native owner snapshot.
  An independently fresh snapshot created after callback entry is no longer
  checked with an earlier timestamp. Neither XR input nor native deadlines are
  restamped to prolong them.
- Native-only recovery may re-read one full snapshot after ChangedDuringRead.
  The original deadline and expected owner remain fixed; a second failure,
  changed owner, malformed state or expired clock still rejects. Bounded logs
  distinguish native observation and boundary failures without extra reads.

The origin route currently applies to SPAS. This does not enroll origin rebasing
for a bolt held in the opposite hand or change weapon-class/native prerequisites.

## Actual-game evidence

The emulated player first completed two F2000 magazine cycles, including original
return and replacement: four native commands retained their authority/copy
receipts and conserved ammunition. It swapped to SPAS, acquired support before
firing and began a partial pump. Tracking was interrupted after30mm of travel;
the receiver then changed the input and camera origin generation once.

Production code rebased cycle1/shot1 without changing7 loaded/24 reserve. The
emulated player observed that receipt, released/regripped with a new gun claim,
and completed22cm rearward/8cm lateral controller motion against the measured
9.5cm pump rail. It required fresh native recovery authority and actual paired
palette output. There was one recovery release and one acknowledgement. Support
returned32.271ms after native readiness; firing was unblocked. No native read
miss, pump-control rejection or raw-contact rejection was recorded in this run.

All240 recorded guarded Updates passed the independent guard/restoration audit.
Each of the three native copies supplied at least two idle observations after
the release and before readiness.26 recording lock drops elsewhere mean the
trace is not complete callback coverage.240 stereo pairs were consumed and40
image pairs saved without write failure. Sampled eye images show the weapon and
hands, but their view does not establish precise fore-end contact or animation
smoothness. Hooks and temporary render state detached/restored; BC2 stayed alive.

The diagnostic stays in its explicit origin-observation phase after this focused
pass. It does not run the shell/return-rifle steps and does not claim that the
ordinary combined sequence completed. Its generic combined status therefore
remains false. The preceding275 combined run covers those other steps separately.
The one-retry path did not activate in this passing native run; forced torn reads,
changed ownership, expired bounds and repeated races are covered offline.

## Menu and retained failures

`menu276-normal275-03` used frozen normal275 gameplay/render code. Emulated menu
toggle opened the native pause menu, actual UV pointer down/up selected Resume,
and eight stereo pairs rendered afterward.65 menu images were delivered, with
one down/up, no pointer rejection and no recorded graphics failure. The captured
menu was inspected. This verifies the menu channel and native callbacks, not
OpenXR laser visibility or headset comfort. No production menu change was needed.

Failed origin runs01-03 remain retained. Run04 verified native rebase and31s of
retained debt, but did not attempt a full stroke; its shutdown observed a torn
full-state read. Startup attempts also exited before the VR module was injected.
Their game crash-report stacks have addresses mapping into AMD/DXGI modules in
an adjacent process; root cause is unproved. Delaying the Continue helper is not
a demonstrated crash fix. No GPU driver or game graphics settings were changed.

Remaining: sustained campaign/death/checkpoint recovery, earlier intermittent
magazine/input-epoch failures, pistol/LMG/chamber and broader weapon enrollment,
and headset validation of visual pump handling/hand fit. A passing focused
sequence does not establish a fully playable campaign or all-weapon support.

Both normal x86/x64 builds pass all243 suites. Frozen normal binaries and symbols
are in normal276-player-checkpoint. The private origin driver is excluded from
the normal native DLL. Source checkpoint276 is local; no publication or headset
acceptance is claimed. Campaign lifecycle regression work continues.
