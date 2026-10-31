Feedback63: independent controller and native feed observations

User reports0.1.26 still has no functioning LMG reload. Installed and session DLL
both match the tested source2a532f8e; this was the new binary in a new BC2 process.
Session20261031-142055-690-9fea78b2 first retained attempt4982 acquired internally;
4983 advanced source8956->8957 and rejected SnapshotMismatch19/InvalidInput9.
All native/raw admission flags passed. Native snapshot sequence was not explicitly
logged, but that status has an exact same-native-sequence comparison in source.
Another attempt reached7 internal Moving rows; no pending/native dispatch occurred.
Source9006 repeated publication was retained, confirming feedback62's narrow fix;
this did not establish visible cover movement.35 earlier records were overwritten.

The fixture published native data before every controller update. Production uses
independent clocks. Shared feed policy now admits a newer controller input against
an identical, still-fresh native-bound observation. Actual native sequence, time,
deadline, counts and receipt joins remain unchanged; mutations and expiry reject.
The new fixture exercises full24-profile cycles with2:1,3:1,irregular and faster
native schedules, immediate/lagged/repeated rendering. Backend receipts are mocked;
these deterministic tests cannot establish a successful live native transfer.

Bounded telemetry adds the missing source-native sequence/time/deadline without
making publication cadence a new logging event. Validation/deployment pending;
receipt.json will record actual results. No movement result was separately reported.
Full native report saved; normal StopVR released resolution guard, game closed normally.
Agent findings and derived first300s: worktree reports/coordinator-20261031-feedback63.
Baseline matrix02 linked the new fixture against pinned0.1.26 feed/core objects and matching normal-build configuration. Legacy24-profile cycles pass; the new independent-clock matrix fails OpenCover, matching the missing timing contract. Correction validation remains pending.
