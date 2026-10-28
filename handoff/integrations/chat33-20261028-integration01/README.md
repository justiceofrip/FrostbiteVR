# CHAT33 source integration - October 28, 2026

Integrated task `tasks/chat33-20261028-steam-launch-r1` at
`7b48b7c24e7118a358e66cfbc0303eb0252e4f2c` into the accepted baseline
`a16f75c34d63cd773962730d38b6a6787db4d028`, with no conflicts or unrelated edits.
Candidate merge: `7d3673eb073639a6a92b4b8b2f14de07c3ebf3b0`.
Validated combined source: `03dd44d2f77cd19a613829a5097971b7a0f06b20`.
Integration target: `checkpoint/436-r56-equipment-rendering` at the verified
FrostbiteVR origin. Source push is pending final publication of this receipt.

The [task handoff](../../tasks/CHAT33/20261028-steam-launch-r1/HANDOFF.txt) explains
the two separate reported failures. Steam's recognized executable remains refused
until its native signatures and ABI are verified; the report now preserves the
enabled compatibility setting and identifies the profile mismatch. EA's accepted
executable reached asset preparation, which refused protected launcher placement.
Setup and Play now catch that placement early and explain a fresh separate launcher
installation. Game files, saves, native gates and the Python cache guard are preserved.

Canonical deterministic checks pass: Startup89, Storage40, Feedback48,
Steam Python11, managed-cache Python3.12 13, packaged setup/update/rollback26,
and PortableRestart30. The extra restart suite initially failed because its old
fake process handle reached the real Windows resolver. The test-only correction
replaces the OS observation boundary, preserves real ownership comparisons, and
adds stale PID/path/exited-owner cases. The initial failure and corrected result
remain separately recorded; the six passed suites were not repeated for this fix.
Evidence is local under `reports/integration-chat33-20261028-01`.

Native source closure, all576 provenance files, both normal build configurations,
and all six binary/PDB pairs match the CHAT04 receipt. No native rebuild was needed.
Built-source identity remains `532b75060f9bfe78d013466fa12cf6513a0a9a95`;
the prior x86/x64 305/305 and CHAT06 2/2 results retain their original scope.
[receipt.json](receipt.json) pins the reused artifacts and new validation separately.

Fresh local SteamVR preflight failed at OpenXR instance creation (`XrResult=-2`,
host exit2). The owned host stopped; no game, XR session, installation or equipment
smoke occurred. Active launcher0.1.12, fallback0.1.11 and exact settings bytes remain
unchanged. This local failure is separate from the EA report's successful
VDXR/Quest3 preflight. No new package or public release was published.

Remaining: restore local runtime availability before installation and actual Play,
stereo/input/equipment/shutdown validation. The EA reporter must retry from a fresh
separate launcher folder. Steam needs exact-executable file-only signature/discovery
and ABI evidence before a reviewed compatibility change; do not bypass its gate.
Physical-headset acceptance remains pending. No unrelated recovery task was started.

Next integration prompt: Read INTEGRATE.txt and this receipt. Resume only the
pending CHAT33 local installation/Play smoke once OpenXR preflight is available;
preserve settings/fallback and record exact candidate identities. Do not publish.
