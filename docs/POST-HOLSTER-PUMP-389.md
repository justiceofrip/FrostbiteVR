# Continuous equipment lifetime check — checkpoint 389

This checkpoint extends the input diagnostic. It does not enable another weapon
or change player reload policy. The current gameplay fix is documented in
[SHELL-STOP-388.md](SHELL-STOP-388.md). The last human headset run (375) failed;
neither automated acceptance nor sampled captures establish headset acceptance.

## Why this sequence matters

Previous combined runs stopped after returning from the SPAS to the rifle.
The new optional sequence draws the SPAS a second time after rifle magazine
handling and firing, completes another shot and physical pump with support held,
and returns to the rifle again. The equipment, hand, ammunition, native cycle,
and holster consumers remain alive throughout. The original session deadline
also remains in force. Only the diagnostic's progression is restarted.

Both shoulder round trips require fresh ordinary draw claims. The second SPAS
visit requires the same weapon instance, its new equipped owner, exact retained
ammunition, actual paired pump rendering, a native completion, and a support
return. Missing render evidence and corrupted retained counts are explicit
negative regression cases. A final rifle draw must preserve rifle ammunition
and leave weapon actions unblocked.

## Build and deterministic evidence

- Source closure: `b4c473f5a2bf882724076d2c7465327e1f5456151c46855a17b1e853f6bcb3ad`.
- Normal x86: 246/246 suites passed, completed 2026-10-12 16:08:45 UTC.
- Normal x64: 246/246 suites passed, completed 2026-10-12 16:06:57 UTC.
- Focused composition: two shots, two native acknowledgements, two support
  returns, one inserted shell, four weapon selections, one rifle shot; 43.23 s
  simulated time. Final shotgun counts 7/23. No consumer reset.
- Status checker: ten tests passed, including rejection of missing second native
  acknowledgement, wrong counts, missing held-support evidence and missing return.
- `BC2_ORDINARY_RESOURCE_REVISIT_PROBE` defaults OFF and is explicitly OFF in
  normal checkpoint builds. It is incompatible with inspection/interruption/
  actor-replacement and other shortened diagnostic modes.

## Actual-game failure (retained)

`overnight389/native01-revisit` fails, despite completing rifle support, three
magazine operations, the first pump, shell insertion, shoulder return and rifle
fire. The second SPAS draw has fresh custody and correct 8/23 counts, but the
second pump driver stays in warmup with zero raw contacts for 963 input calls.
The unchanged 50-second active budget expires; it does not report success.
BC2 survives and native hooks drain. Trace SHA256:
`3d7e69167c2b31a62d20801780643db6be4a050c7abb0a4b6c1d0aa8596007fe`.

The suspected integration gap is `NativeCycleRequestRetired`: family selection
retains the old cancelled cycle's exact retirement in `Bc2ReloadNativePolicy`,
but the runtime gate checks only its current-family-epoch receipt, which is
cleared on a family change. This must be reproduced in a connected regression
before correction; the high-level input simulation did not model this gate.

Local evidence directory: `G:\Unity\VRMMO\bc2vr-recovery\overnight389`.
