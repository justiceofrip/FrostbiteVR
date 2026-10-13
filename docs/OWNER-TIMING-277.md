# Shared owner publication timing — candidate277

The native callback captures its entry time, then acquires the owner-publication
gate. Another thread can publish a valid newer snapshot between those steps.
Owner previously required that snapshot to be current at both the earlier entry
time and the acquisition time. The first check rejected it as being in the
future, even though it existed and was fresh by the time it was read. That can
unnecessarily discard native reload evidence or begin pump/bolt recovery.

The callback now copies the publication under the same gate and validates it
against the acquisition clock. A backwards clock, revoked revision, expired
lease, actual future observation or wrong firing object still rejects. The copy
retains its original timestamp, deadline, owner and ammunition. Downstream
native boundary/owner checks remain required. This is shared reload admission,
not a new per-weapon adjustment or an extension of controller freshness.

A threaded regression forces publication after callback entry and before gate
acquisition. It fails under the previous two-timestamp predicate and passes
after the change, with future/expired/revoked/backwards-clock cases rejected.
The existing reload callback and native-cycle recovery suites pass. Full normal
builds and an actual M95 check are in progress. The trace includes a bounded
counter for accepted publications newer than callback entry; a native run that
does not increment it does not reproduce the timing race.

This is not claimed as the cause of every earlier intermittent reload failure.
Candidate276 is preserved separately. No new native weapons are enrolled.

## Completed campaign check on276

`scene276-recovery-01` passed the native-menu checkpoint restart during an owed
pump. The replacement player exited the boat, drew XM8 through shoulder input,
removed and returned its original magazine (22/191 unchanged), drew and fired
SPAS, completed pump cycle2, inserted one shell (7/24 to8/23), and regained
support grip132ms after shell completion. All240 receiver stereo pairs arrived;
157 followed the rebuild. There were no captured exceptions or graphics errors.
Hooks detached and the game remained alive.

This is one checkpoint restart and a bounded interaction sequence. It does not
prove death recovery, a sustained campaign playthrough, the rifle-first native
retirement branch, headset laser visibility or hand/animation quality.
The independent audit is `bc2vr-recovery/scene276-recovery-01/recovery-audit.json`.

## Completed277 checks

Both normal architectures pass all243 suites. Normal binaries/symbols are frozen
in normal277-player-checkpoint. The ordinary M95 input run277-01 completed two
physical cycles and firing-hand custody returns. Independent native reads verify
5 to3 loaded,45 reserve unchanged and retained item/actor identity. All four bolt
motion phases have paired render evidence for each cycle; no presentation
fallback or evidence rejection occurred. No native owner fallback read failed.
The counter for publication after callback entry is0: this run verifies the
ordinary bolt route, not occurrence of the new timestamp-race regression.

Native Ready followed physical release by2.273 and2.283seconds. That delay is
retained as an unresolved user-facing timing issue. Last-round behavior, scope
optics, recenter recovery for M95 and headset hand placement are not accepted by
this two-cycle check. See the private m95-ordinary277-01 receipt-review.json and
mechanism-status.json. The normal build excludes the scripted controller driver.
