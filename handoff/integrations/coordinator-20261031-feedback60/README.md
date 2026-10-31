# Feedback60: transfer the working weapon mechanisms

The 0.1.23 session confirms T194 shell insertion and rail improvement, but
support grips still detach, shotgun behavior lacks SPAS reliability/audio,
and QJU88 feed reload never starts. The user requires shared behavior rather
than another per-weapon acceptance loop.

This batch corrects shared consumer paths:

- Accept live configuration and controller observations with independent
  original deadlines. The grip regression uses the production support catalog.
- Retain an already held shell through missing render contact while the exact
  current owner/native state and original accepted control lease remain valid.
  A gap supplies no motion, insertion or native transfer authority.
- Arm feed-cover startup from released neutral away from the cover, then
  consume the next grip edge at valid contact. Rejected presses require a
  new release, including when renderer packets arrive late.
- Route exact authored shotgun pump/insertion/finish identities through the
  shared audio ledger. Existing owner-extracted SPAS samples are generic
  physical cues; this does not claim each weapon's original sound timbre.

Regressions exercise real support, shell, pump and feed consumers over the
shipped profile sets. Enrollment counts alone are not completion evidence.
New feed idle masks identify contact/neutral/hand/ammo startup rejections.

Evidence: worktree `reports/coordinator-20261031-feedback60/session01` and
scoped agent reports. The original stream retains all 15,168 transition events
without drops. The final native report is empty; do not repeat that capture.
The exact user-failing feed idle predicate was not previously recorded.

`receipt.json` records completed validation and installation.
No new game/headset check or public release is implied by offline evidence.
Preserve AN94 manual reload, smooth firing and the improved shell rail.

Completed: private 0.1.24 installed; settings/logging preserved; 0.1.23 rollback.
Source pushed; both normal builds 341/341, focused 9/9 and generator 12/12 pass.
Focused01 stale-neutral fixture failure is retained; corrected feed-only check passes.
Native source was unchanged by that fixture correction. All agents retired.
No game/headset session ran. See receipt.json for exact pins and limits.
