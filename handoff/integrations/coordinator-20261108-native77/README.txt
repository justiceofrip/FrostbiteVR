Private native77 delivered: vehicle camera evidence and launcher publication

Vehicle camera recording previously exhausted its first16 entries during early
head motion. It now retains the first and latest15 samples at a500ms cadence,
with immediate owner/space transitions and exact per-frame eye joins. Retention
and incomplete pairs are reported explicitly. This changes observation only;
it does not establish a vehicle blackout fix or complete ride coverage.

The bounded launcher status reader now closes its file before parsing/emitting
the snapshot. A real two-process reproduction showed the old reader preventing
atomic replacement through the original250ms limit; the proposed reader allows
replacement while preserving its complete old snapshot. Publication failures
retain path/stage/error context. The failed721 startup lacks that original path,
so its precise cause is not established by this reproduction.

ROOT evidence: reports/coordinator-20261108-feedback721/vehicle-focused01 and
native77. Agent drafts/checks are WT reports727/vehicle-camera and
reports730/json-publication (both dated20261108). Native77 is installed and
setup-validated with76 fallback; both architectures actually compiled5342101.
Normal352x86/349x64 and18focused suites passed; source is pushed, settings/cache
preserved and the stable77 headset entry verified. native77/run01 retains build,
freeze, push, package and installation receipts. No public release followed.
Actual feed77/xr-game01 reached campaign Empty then refused grip release before
LMG draw/reload. All5owned processes exited normally, native retired and exact
profiles/settings restored. No LMG, headset, vehicle-ride or performance pass.
