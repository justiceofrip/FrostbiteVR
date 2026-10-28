# CHAT04 original-magazine source integration — October 28, 2026

Integrated task `tasks/chat04-20261028-tracking-custody-r1` at
`19e95afe694c2afc7486d06c020a4b40a1ff8b64` on accepted baseline
`e013ae21edd5c4f2ab661ac36513ea71ab68a435`. Merge commit:
`4c60f9801ea7bd8ba44774771e98353eea74abfe`; exact built source:
`532b75060f9bfe78d013466fa12cf6513a0a9a95`. The target branch is
`checkpoint/436-r56-equipment-rendering`; source-push status is recorded in
[receipt.json](receipt.json). Later documentation commits preserve this runtime.

The [task handoff](../../tasks/CHAT04/20261028-tracking-custody-r1/HANDOFF.txt)
describes the bounded fix: input loss suspends an original magazine without
discarding its resource or rounds. Fresh same-owner evidence and a new hand claim
are required to resume. Partial extraction still needs a real pull; exact native
unseat receipts settle once, and deliberate release retains its ordinary policy.
Replacement-magazine custody and M95 lost-left-root recovery remain unfinished.

Both canonical normal builds passed305/305 (x86 attempt02; x64 attempt01),
including combined knife/grenade/gadget/reload regressions. The worker's eleven
portable suites also passed. First x86 attempt01 compiled but passed303/305:
`Bc2ResourceBatch` and `Bc2ResourceScarConsumer` still expected focus loss to
record cancellation. Their shared test now verifies preserved custody/ammunition
through repeated loss, then separately verifies explicit cancellation diagnostics.
The initial failure and its stale prior-day `build.json` are retained and rejected
as qualification evidence. The wrapper now omits stale success receipts on failure.

Local evidence is under `reports/integration-chat04-20261028-01`. Native closure:
`f9d264cb17dfa88e304e433569e4d0e472adf97017f6fe3f96db2aed198f4b0b`.
The reviewed operation header changed only its source binding; all normal feature
settings remain unchanged. No new native capability or actual-game qualification
was granted. `normal-runtime/build-receipt.json` has SHA256
`f4fdcb64ef2fc1e539db12bf97dee6f338bc66719b92ae7d04e1f50ea0556950`.
All six binaries and six PDBs are frozen there, with independent PE/PDB GUID+age
matches in `symbol-audit.json` and source/cache pins in the build receipt.
Prior R56 binaries remain in their frozen runtime; the five formerly mutable
fallback PDBs were preserved and hashed before relinking (`fallback-symbols.json`).

The default40GiB storage admission refused the required3GiB build reservation.
After an initial decision to leave integration pending, the user explicitly
authorized42GiB. Each normal build used fresh42GiB admission while retaining the
15GiB free floor and3GiB build reserve. Repository defaults remain40GiB.
No evidence was deleted to make room. BuildCandidate.ps1 preserves preparation,
attempt logs and admissions; FreezeRuntime.py verifies source, tests and symbols.

Installation remains pending. ProbeRuntime.ps1 ran the exact frozen host with
the pinned loader against the configured SteamVR OpenXR runtime. Instance creation
failed with `XrResult=-2` (host exit2); no system or XR session was created.
The owned host exited, and launcher settings/active-version hashes stayed equal.
`xr-preflight01/preflight-receipt.json` preserves the failure. The installed
`G:\Unity\VRMMO\BC2VR-Launcher` still selects0.1.12 with fallback0.1.11.
No game, installed payload switch, equipment smoke or public release occurred.
INTEGRATE.txt permits source/validation completion when required runtime evidence
cannot be established. Run the established complete-payload installation and
startup/equipment smoke once OpenXR is available; retain the current fallback.

CHAT04 is not complete. Its [native check request](../../tasks/CHAT04/20261028-tracking-custody-r1/NATIVE-CHECKS.txt)
retains the original-magazine tracking and missing-render matrix, replacement
custody, support/pump, M95 bolt/root recovery and physical-headset checks.
Earlier M95 five-cycle evidence does not qualify these gaps. Continue CHAT04;
do not automatically advance to CHAT05 or reopen deferred crash tasks.

Next worker prompt: Select CHAT04 — Tracking gaps and persistent weapon state.
Read COMMON and only CHAT04 from the canonical handoff/campaign-chat-queue/CHAT-QUEUE.txt.
Continue the remaining scope from this integration handoff in a new isolated
worktree/branch; commit and push the task source branch and return its exact
commit/task-specific handoff for integration. Shared builds/game/install remain
owned by the integrator.
