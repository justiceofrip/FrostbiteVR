Tools638 fixes recovery after an unpublished controller command times out.
The old reader classified elapsed time as observed ownership interference and
prevented the tool from releasing its own held grip. Both publication and nested
command-file reads now distinguish those cases. The failed action remains refused.
Recovery requires fresh exact writer/session/command and unchanged head, hands,
controls and capture state; mismatches remain unsafe. Original source, ACK, stage
and cleanup deadlines remain unchanged. No action is replayed or lease extended.

Actual636's holster source expired before publication. Its32.8569ms uninstrumented
interval is not attributed to a particular I/O call. Six bounded in-memory timing
spans measure that path next time; this batch makes no performance-improvement claim.
The private634 duplicate-baseline correction remains separate and not game-qualified.

Independent640 review passed after fixing the nested ownership timeout case.
Author evidence retains48 earlier checks plus final installed-Python4 new checks
and6 atomic methods/8 previously ACL-blocked instances. An actual80/92/94/100-chain
replay uses saved636 snapshots and simulated I/O; it is not a live release pass.
ROOT applied3 portable regressions pass on Python3.13.16. Initial import/ACL failures
are retained. Windows line endings were normalized with exact reviewed-text equality.
Evidence: reports/coordinator-20261107-feedback638/root-integration and the matching
638/shared-publication plus640/publication-review folders in the coordinator worktree.

Installed private0.1.73/fallback72 is unchanged; native source5d7d04f/closure1e669e67
and actual602/run02 build/installation receipts remain authoritative. No native
rebuild, reinstall or public release. Source-push status is recorded separately.
Actual637 opened the campaign list in both delivered eyes;639 stopped before its
click because the older reference image expired. Both closed/restored normally.
Knife rendered/strict-Held/attack evidence, full underbarrel reload, wider weapon/
vehicle/headset reliability and measured FPS gains remain open.
