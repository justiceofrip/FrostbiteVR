# Feedback61: recorded interaction failures

Session `20261031-123121-711-83a672cf` used private 0.1.24. The user reports
T194 breaking during a partial pump and QJU88 support/reload still failing.
The complete native report and 4,980 lossless streamed events are retained.

- Feed: 1,807 admitted idle samples fail orientation. Event1640 passes every
  pickup gate except its 2.29598-radian wrist orientation. Physical grabs now
  use explicit acquired orientation, retaining position and subsequent motion
  constraints. Default authored orientation preserves other consumers.
- Support: the shared aim gate rejects a newer same-configuration observation
  while the published input guard lags. Real caller ordering permits this;
  seven QJU88 contact losses and 4,283 authored rejections support the finding,
  but the exact rejected predicate was not logged for individual events.
- Pump: recovery drops a progressed grasp during missing renderer contact.
  Preserve exact live custody without motion or completion during that gap.
  The earlier input10718 native held-lease disappearance remains unattributed;
  bounded pump diagnostics now distinguish its native predicates.

Regressions model rotated acquired grasps through every feed stage, real
producer ordering, and partial-pump recovery on repeated controller packets.
Production generated header equality is checked from pinned original inputs.
Review/test failures remain in the run folder; they are not headset acceptance.

Root owns integration, builds and installation. `receipt.json` records
completed validation and installation. No new game/headset session is requested for this batch.

Private 0.1.25 is installed; source pushed, settings/logging preserved, 0.1.24 rollback.
Normal builds pass 341/341 each; focused 13/13, generator 22/22 and production
header equality pass. Fixture/import failures are preserved in focused01/02.
All agents retired. No new game/headset session ran.
