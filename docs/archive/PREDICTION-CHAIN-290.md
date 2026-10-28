# Candidate290: complete the directed native prediction chain

Native289 retained hand custody and completed three pumps. On the fourth,
clientB Update8313 completed7->8->1->2 with4/24 ammunition. ClientA Restore8317
then copied that idle state while the server still had roughly15ms on its tail.
288 required server completion directly for both SPAS clients, so it rejected
this valid clientB-to-clientA transition. The same route was already measured
for M95 in239. The game stayed alive and diagnostic hooks drained.

290 models the evidenced directed graph: server -> clientB -> clientA. A source
branch must have finished its ordered native tail or its already-proven forward
copy followed by its own idle Update. That source's vote must remain current,
with conserved counts and original deadlines. A snapshot alone cannot vote
ready or act as a source for the next client. Each receiving client needs its
own neutral idle Update; the server independently finishes its actual tail.
Backward prediction still withdraws only the affected provisional vote.

Exact289 callback fields join the287 fixtures. A connected regression fails on
289 for clientB-to-clientA ordering. Loaded/empty tests cover both routes and a
full two-hop chain, including rejection before the intermediate idle Update.
This change is shared cycle synchronization, not another gun-specific reload.
Native acceptance, full normal builds and headset acceptance remain separate.

Failed289 trace: `pipeline-runtime-20261005/root-monitor/resource-empty289-01`.
SHA256: `e53f6477eaa1a4dcb4f7820d71fc82bff53d96a1a0529dba913217e50d3b7f37`.

## Subsequent290 validation

The actual-game controller-emulation run completed eight SPAS shots and physical
pumps (8/24 to 0/24), inserted a shell (1/23), returned support grip, swapped
shoulder weapons, removed/reinserted the full F2000 magazine (30/210 to 0/210
to 30/210), and fired afterward. Support returned 30-48 ms after pumps and
132 ms after shell insertion. The independent ammunition/callback audit passed.
All pump and magazine presentation samples had zero fallbacks. Hooks drained;
the game stayed alive. This is a bounded native result, not headset or complete
campaign acceptance. Nine recorder lock drops mean full callback coverage is
false; all required shot, hold and completion proofs were present.

The shared fixes preserve controller custody independently of older render
poses, protect in-flight completion reads, inhibit the last-shot automatic
reload, and recognize the directed server -> clientB -> clientA completion
chain. The successful run used local tails; forward routes are covered by exact
failed287/289 fixtures and connected regressions, not claimed as live290 hits.

Both normal player builds passed 243 suites. Normal290, headset290 and a local
source checkpoint are being frozen from the same source receipt. Diagnostic
drivers are disabled in the player build. No publication or headset host launch.
Earlier279 native menu/pointer checkpoint recovery and280 loaded M95 bolt
evidence remain preserved without redundant repeats. Last-round M95, native
pistol and belt-fed reloads, broader enrollment, sustained campaign/death
recovery and headset visual acceptance remain unfinished. Seventeen compiled
magazine prerequisites do not mean seventeen accepted weapons.

Evidence: `pipeline-runtime-20261005/root-monitor/resource-empty290-01/empty-tube-receipt-review.json`.
Source: `fb11a03db956d779a22dc1b50f557ab49cc4ccb2ecf48f5b25dad2cff66d1bc7`.
Trace: `4661e1a2b9a5ebb9ede4ff293bc085b08afbccbe75d411aa9b45b123015f8277`.
See [prediction chain](PREDICTION-CHAIN-290.md) and
[controller custody](CONTROLLER-CUSTODY-289.md).

