# Campaign follow-up 276 (in progress)

The user requested continued development and emulated campaign testing while
asleep. Do not ask for headset feedback to resolve behavior the emulator can
exercise. Keep failures and distinguish native completion from visual acceptance.

## Completed this run

- Normal275 native menu acceptance: `bc2vr-recovery/menu276-normal275-03`.
  VR menu-channel toggle opened the menu, pointer down/up selected Resume,
  and eight stereo pairs rendered after closing. One native down/up, no pointer
  rejections or graphics failures, hooks detached, BC2 alive. Captured native
  menu image inspected. This does not test OpenXR laser visibility or headset feel.
- Corrected `NativeMenuProbe` to publish neutral tracking/controller input needed
  by the current body/vehicle camera, and require stereo after menu close.
  The old fixture sent no controller input. Its failed run01 is retained.
  Run02 started before native subsystem ownership was ready and rejected;
  run03 used verified on-foot gameplay. No product menu fix was required.
- Corrected runtime tracking-origin rebase order: convert the private drained
  service candidate before requesting its observation watch. An unsuspended Held
  cycle has no such watch before conversion. Configuration/identity checks and
  commit exclusion remain unchanged. Six focused suites pass, including a new
  Held-to-rebased-debt regression with unchanged ammunition and no reused grant.

## Active check

The diagnostic probe writes one read-only `pump-origin-ready.txt` observation
when an existing native cycle reaches Converging. An explicitly opted-in receiver
changes both input and camera origin generation once and saves its receipt.
It never submits a native cycle or acknowledges completion. This feature is
excluded from the normal native DLL at compile time.

The combined input driver now has an explicit focused origin-observation phase:
it retains the gun grip, releases the mechanism hand and waits for the production
adapter's exact rebase receipt. It cannot acknowledge a pump or complete the
combined sequence. Full-stroke recovery in the new origin remains required.

The first actual run, `resource-origin276-01`, failed: the native service cancelled
Owner with invocation0 during convergence, while the new origin stopped physical
pump ticks. The selection tracker required exact space equality, preventing the
existing pump adapter from receiving the new-origin input it needed to rebase.
The native Step guard also rejected a previously issued held Update if another
thread began recovery while that original callback was still running.

The next candidate routes only same-item/newer-space SPAS recovery to the existing
adapter without accepting a new selection or granting fire. It also permits an
exact pre-conversion, still-open, zero-delta held callback to drain; unrelated,
expired or unprotected callbacks remain rejected. Eight focused suites pass,
including wrong actor/item/space and wrong invocation/cycle/deadline cases.

Run02 fixed physical routing but still cancelled at the first expired-input
native observation. The reader used the callback-entry timestamp for a snapshot
created later inside Owner. The callback now samples boundary time after Owner,
preserving the actual snapshot and controller deadlines. Run03 no longer had
that boundary rejection but still lost the owner read. Those failures are kept.

Run04 (`resource-origin276-04`) observed a real native rebase with unchanged
cycle1/shot1 and7/24 ammunition. It remained in recovery for31 seconds, with5614
guarded updates, until the finite input session ended. The new read diagnostics
then observed a full snapshot changing during the read. The run has no full
stroke and is not a combined-sequence pass. A pass does not erase run03's miss.

The next candidate permits exactly one complete re-read after ChangedDuringRead,
with the same expected owner and original deadline. All other failures still
reject. The input-only pump script can follow a real rebase receipt, observe a
new neutral/new gun claim, and complete a fresh full stroke plus support return.
The original held lease stays historical; no diagnostic constructs native
authority. Nine focused suites pass, including complete physical/native-service
recenter recovery and torn-read/changed-owner/expired/repeated-race cases.

Private candidate: `ordinary-resource276-origin07-private/build-receipt.json`.
Source hash: `b9d9200da0e69d306b86b147f064172e79d149f8f8e9b0f322cb4f5984256540`.
Both normal276 builds now pass all243 suites. Binaries, symbols and build receipts
are frozen in normal276-player-checkpoint; normal275 remains preserved.

Two origin-test startups exited before VR injection. Game crash-report stack
addresses map into amdxx32.dll/dxgi in an adjacent vanilla process module list;
that observation does not prove the driver caused the exits. No driver or game
settings were changed. A vanilla startup allowed to settle before the Continue
helper succeeded. The next launcher waits12.5 seconds before that helper and
then checks actual actor readiness read-only. Its next startup also succeeded.
Start07 nevertheless exited during loading before VR injection, with a similar
game crash-report stack. Its report is preserved. Delaying Continue is not
declared a fix for these startup exits.

## Remaining priorities

1. Completed in run05: actual origin recovery, fresh full stroke, native completion
   and support return32ms later; no owner-read misses or control rejections.
   See TRACKING-RECOVERY-276.md. This is a focused origin branch, not the full
   combined sequence.
2. Pump presentation/stock-animation conflict using captured rig evidence.
3. Failure-directed campaign checkpoint and equipment lifecycle checks.
4. Required normal builds and a source/binary checkpoint with explicit gaps.

No new weapon enrollment or public release is claimed. Do not repeat the passed
menu or ordinary combined sequence unless a change or failure warrants it.
