Shared ordinary-XR reload stimulus — November2,2026

Problem: asynchronous diagnostic snapshots repeatedly interrupted a physical
controller gesture before its native result could be tested. Actual169 stopped
on a pre-acquisition gate count;170 stopped during sight selection. Both failed
runs remain preserved, normally closed and exactly restored.

Added a separate reusable ordinary-input sender and geometry/boundary adapter for
feed, pump/shell, fixed/underbarrel and sight. The installed mod still owns native
input, claim, movement, custody and ammunition checks. Only bounded left pose/grip
and initial left-pose setup are sent. Asynchronous observation gaps are recorded;
fresh typed acquisition/custody/stage boundaries are still mandatory. No old
nonauthorizing plan is relabeled authorized, and old diagnostic tools are intact.

Combined47 offline tests PASS; all24 feed registrations covered across96 stages
plus24 pickup/attach paths. Pump/fixed/sight cases use portable synthetic fixtures.
One focused integration review found and resolved a missing imported-helper pin
and the narrow absent-feed observation wait; no open review findings remain.
Evidence: canonical reports/coordinator-20261102-feedback176/combined-offline-tests01.log;
worktree reports174,175,177; reusable root session envelope reports176/stimulus-session.
Tool guide: tools/xr-sim/RELOAD-STIMULUS.txt.

Native source/build/installation unchanged: private0.1.46,45 fallback; delivery147
still binds source d99d1e59bae44d3041c0776cc411e199fead38f1 and both342/342 builds.
No native rebuild was needed for these Python tools/tests/docs. No public release.
Actual complete reload, player behavior, destruction and performance remain open.
Next: one bounded actual-game LMG sequence using the new shared runner.