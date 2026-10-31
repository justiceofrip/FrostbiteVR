# Feedback59: shared interaction continuity

Private 0.1.22 headset feedback confirms AN94 magazine reload and smooth firing.
The remaining report is recorded in
[`0.1.22-buglist.md`](../../../docs/archive/2026-10-31-bc2vr-0.1.22-buglist.md).

This batch prepares these shared corrections:

- Pose grip/support/aim/sight from the selected mesh observation captured with
  that input, preserving current ownership and original lifetime checks.
  Subtree sight contacts also retain and recheck current configuration proof.
- Keep a held shell through a missing pump observation. Unknown observations
  still block actions; proven cycle debt and explicit cancellation still cancel.
- Retain last admitted HUD digits through count gaps using an independent
  current-owner lease. This does not change firing or native ammo authority.
- Persist bounded interaction outcomes in the normal 1 Hz session log, so
  support/sight/shell/feed rejection reasons survive a lost final native report.

Independent reviews identified lost sight-configuration proof and rejected
count metadata renewing HUD ownership; both were corrected before validation.
The existing HUD WARP regression now checks owner expiry during an upload gap.

Evidence is in worktree `reports/coordinator-20261031-feedback59`; canonical
build/package/install evidence uses the same run name. `receipt.json` records
the delivered outcome once validation and installation finish.

Limits: no new game or headset test. A distinct QJU88 reload failure has not
been diagnosed, and underbarrel completion remains unaccepted. Preserve the
confirmed AN94 reload and smooth firing. Empty-hand holstering remains deferred.
No public release or Steam compatibility claim; that executable is unavailable.
