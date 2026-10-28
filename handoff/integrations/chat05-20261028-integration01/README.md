# CHAT05 source integration — October 28, 2026

Integrated `tasks/chat05-20261028-holster-replacement-r1` at
`107ea10b84b5ffbc116b2837cc4183043db5e868`, from accepted baseline
`af546c3e76830fdd2d35459ac7f97f83149c5620`. Candidate merge and tested source:
`f05043df9ab3eb2dbf4e7a0fedeefbe4bebeeb40`; target branch:
`checkpoint/436-r56-equipment-rendering`. Source publication is pending.
[receipt.json](receipt.json) records this batch independently from installation.

The change adds nine ammunition-resource replacement combinations and scoped
MP443 hip stow/draw checks. Pointer reuse cannot transfer the old original or
receipt into a new item; scoped hide/show preserves resource identity, and an
unbound production operation remains rejected. No production behavior or
capability changed. The [worker handoff](../../tasks/CHAT05/20261028-holster-replacement-r1/HANDOFF.txt)
and [diagnostic request](../../tasks/CHAT05/20261028-holster-replacement-r1/DIAGNOSTIC.txt)
retain the full native scope and acceptance limits.

Both canonical normal configurations rebuilt the two affected CPU test targets
and passed **2/2**: `Bc2BodyInventory` and `Bc2AmmoResourceService`.
Unchanged static dependencies were rebuilt as required by Ninja; no runtime
DLL/executable target was rebuilt. Normal cache hashes and operation records
remained equal. All576 native-source files match the existing runtime receipt;
independent audit verified both test executable hashes and PE architectures.
Logs, XML, admissions and result receipts: `reports/integration-chat05-20261028-01`.
The worker's seven isolated suites remain separately scoped evidence.

The runtime still belongs to source `532b75060f9bfe78d013466fa12cf6513a0a9a95`
and native closure `f9d264cb17dfa88e304e433569e4d0e472adf97017f6fe3f96db2aed198f4b0b`.
Its frozen receipt is `reports/integration-chat04-20261028-01/normal-runtime/build-receipt.json`,
SHA256 `f4fdcb64ef2fc1e539db12bf97dee6f338bc66719b92ae7d04e1f50ea0556950`.
All six binary and six PDB hashes were reverified. Prior full305/305 builds per
architecture are preserved; these are not new CHAT05 full-suite results.

Fresh OpenXR preflight failed at instance creation, `XrResult=-2`, host exit2.
The exact frozen host and current source closure were verified before probing;
the owned probe stopped. Launcher settings/active hashes stayed equal, selecting
0.1.12 with fallback0.1.11. No game, installed-payload change or native/headset
acceptance occurred. The new failure is retained in `xr-preflight01` beside the
test reports; it does not replace the earlier CHAT04 failure.

All new admissions used40GiB/15GiB limits,256MiB for focused CPU targets and
640MiB for preflight, counting the entire worker root. No evidence was deleted.
The integration lease excludes other build/game/install owners for this batch.

CHAT05 remains **needs-native-evidence**. Once OpenXR is usable, execute its
bounded transition matrix on the exact normal runtime, then physical-headset
checks. No inferred fix to right-hand-release discard or same-actor cycle debt
was introduced. Do not mark CHAT05 accepted or advance automatically to CHAT06.

Next worker prompt: Selected task: CHAT05 — Holstering pickups and replacement.
Read COMMON and only CHAT05 from the canonical CHAT-QUEUE.txt, plus this receipt
and the selected task handoff. Continue the pending evidence/repair in your own
worktree/branch; commit and push the task source branch for later integration.
