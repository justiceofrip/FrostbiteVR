Feedback79 - use admitted initial reload observation

Actual37 game02 fired the XM8 launcher again1/7->0/7. Both reload attempts
discarded an admitted ready snapshot before their first command; the redundant
next request returned disabled tracking or missing raw contact. The driver now
plans its first stage from that exact readiness snapshot, with fresh-time
admission, original deadlines and all publication checks intact.

The fixed-container suite passes18 tests, including both saved failure shapes
and continued refusal of invalid later observations. Root reviewed the diff.
The disabled-tracking frame records native gate-publication refusal; the exact
predicate is unknown. The missing raw frame's originating guard is unresolved.
This diagnostic correction does not claim either native condition repaired.

No native changes, rebuild or installation: private0.1.37 remains installed.
Game02 closed normally with native retirement and profiles/settings restored;
23 files compressed losslessly, all hashes preserved. Evidence: reports77/xr-game02
and worktree reports79/fixed-readiness.json. Full reload remains unaccepted.
