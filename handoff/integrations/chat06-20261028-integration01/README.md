# CHAT06 source integration — October 28, 2026

CHAT06 adds nine ammunition-retention cases and four pump/bolt debt cases. The
tests prevent same-soldier generation changes from discarding retained resources
or forgiving owed work. They do not fix or establish native vehicle-exit recovery.

Included task: `tasks/chat06-20261028-vehicle-equipment-r1` at
`b8e7e122804c84ad240bb06a8f79da5af794370a`, based on accepted source
`e09d8acd335997111e13fb929a31ada1cecf41d3`. Candidate merge and tested source:
`fcb6d4fa7c9397534aca44b23e1c34e17d7e5625`. Target integration branch:
`checkpoint/436-r56-equipment-rendering`. Source push is pending final review.
[receipt.json](receipt.json) separates source integration from installation.

Both canonical normal configurations rebuilt only the two affected test targets
and passed **2/2**: `Bc2AmmoResourceService` and `Bc2NativeCycleService`.
Normal cache hashes and all55 frozen flags per architecture remain equal.
The native source guard passed before and after each build; all576 provenance
files match. Independent review verified both JUnit results, the four executable
hashes and PE architectures, and the new case output in each configuration.
Logs, XML, admissions and results: `reports/integration-chat06-20261028-01`.
The worker's isolated results and earlier failed attempts remain separately
recorded in its [evidence](../../tasks/CHAT06/20261028-vehicle-equipment-r1/EVIDENCE.txt).

Runtime source, operation records and production artifacts are unchanged. The
runtime still belongs to built source `532b75060f9bfe78d013466fa12cf6513a0a9a95`
and closure `f9d264cb17dfa88e304e433569e4d0e472adf97017f6fe3f96db2aed198f4b0b`.
Frozen receipt: `reports/integration-chat04-20261028-01/normal-runtime/build-receipt.json`,
SHA256 `f4fdcb64ef2fc1e539db12bf97dee6f338bc66719b92ae7d04e1f50ea0556950`.
All six binaries and six matching PDBs were reverified. Prior full305/305 builds
per architecture are retained, not relabeled as new CHAT06 full-suite results.

Fresh OpenXR preflight failed at instance creation, `XrResult=-2`, host exit2.
The verified frozen host stopped, with no game, XR session or installation.
Launcher0.1.12, fallback0.1.11 and settings stayed unchanged. This failure is
retained under `xr-preflight01`; it does not replace the CHAT04/CHAT05 failures.
Startup, native vehicle episodes and physical-headset checks could not proceed.
Storage admission used the unchanged40GiB/15GiB limits,256MiB for focused tests
and640MiB for preflight, including the worker root. No evidence was deleted.

CHAT06 remains **needs-native-evidence**. Follow the exact six bounded episodes
in its [diagnostic request](../../tasks/CHAT06/20261028-vehicle-equipment-r1/DIAGNOSTIC.txt)
when OpenXR is available. Capture actual actor/equipment lineage, native
transactions and fresh hand claims before changing the transition policy.
No other card was started; no public package, release or runtime was published.

Next worker prompt: Selected task: CHAT06 — Equipment through vehicle transitions.
Read COMMON and only CHAT06 from the canonical CHAT-QUEUE.txt, plus this receipt
and the selected task handoff. Continue its pending native evidence in an isolated
worktree/branch; commit and push any source change for later integration.
