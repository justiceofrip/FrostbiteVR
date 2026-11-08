Tools833/834: helper exit handling and bounded ammunition settling
November 8, 2026; source-only test-tool correction, installed0.1.77 unchanged.

Actual80/07 stopped before host/native activation when the short-lived resolution
helper exited during its image-path query. Runtime.Core now rechecks that same
retained handle after a failed query. Only a confirmed exit reaches existing
helper-exit handling; a live query failure retains its original Windows error.
PID, creation time, executable and live-input ownership checks remain unchanged.

Actual80/03's underbarrel observation stopped at394ms; a genuine fresh same-owner
idle0/7 publication survives at681ms. Preparation now permits one second of passive
settling from the original release, clipped by the existing driver deadline.
Only this observer opts into ten requests at the existing100ms cadence. Response
and native source leases, single-shot limits, custody and idle/count checks remain.
Other selected-ammo observers retain their existing three-request policy.

Checks:13 focused exit-race checks and38 existing real Windows identity checks pass.
The focused/affected Python suites run82 tests:79 pass,3 optional historical
replays skipped. Initial synthetic assertion failure is retained; its incorrect
600ms total-refusal assumption was replaced by original per-response timeout checks.
Reports: canonical reports/coordinator-20261108-feedback721/tools833/checks.json.
Independent review: coordinator WT reports/coordinator-20261108-feedback835/tool-review.

No native rebuild, repeated normal suite, new installation or public release.
Native80 remains compiled3b69bc6. Actual next reload/knife/vehicle results are still
required; these fixes do not establish weapon completion or headset reliability.
