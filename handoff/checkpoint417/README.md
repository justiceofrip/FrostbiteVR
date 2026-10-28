# Checkpoint 417 handoff — cycle recovery candidate

**Do not publish or promote this candidate.** The 416 headset run failed after
the SPAS initially worked. Checkpoint 417 has deterministic validation only;
actual-game and headset acceptance remain pending.

Native source closure:
`41489d7d8bb268ab7dae50f1a856716c0622000a6ec3a44cb449d31f80edf2db`.
Source receipt/header: `profiles/checkpoint417/`. Full x86 and x64:
**286/286 passed each**. Frozen normal runtime receipt:
`d9c4c6c5891f5957639ac5d4515763299f5181bb856ad3df9856a1cd09e68005`.
Probe SHA256: `10af105b4d3551a39a04a5c1dfbbe511300e976fe406b1ddd9c084cc680d00b3`.

The [cycle recovery note](../../docs/archive/CYCLE-RECOVERY-417.md) records the exact
policy changes, native evidence, synthetic scheduling and reproducible commands.
The focused green receipt pins all ten changed service/runtime/test files.

Implemented:

- Preserve a completed physical stroke across an exact late native prediction
  tail; settle native work without another stroke, Ready, ACK or pump sound.
- Reconcile acknowledged SPAS/M95 recovery through two fresh guarded native
  rounds, including safe draining of an already-issued ordinary callback.
- Preserve strict identity, ammunition, snapshot, chronology and restoration
  checks; a genuine next shot still creates new debt.
- Retain the triggering callback and before/after policy state in the bounded
  first-failure record so a future late failure need not depend on the full trace.

The original invocation **8748** is not reconstructed. Only its
`Complete -> Cancelled / Restore` summary survived the game exit, with loaded 5,
reserve 24 and zero recorded contention. The post-ACK replay ordering is an
emulated regression based on previously measured native transitions; its
red-to-green result does not establish 8748's cause.

Private validation artifacts are under
`G:\Unity\VRMMO\bc2vr-recovery\overnight417`:

- `cycle-regression-red-pinned/`: ordinary baseline red.
- `cycle-regression-recovery-red-pinned-r2/`: recovery baseline red, ordinary fix green.
- `cycle-regression-green-pinned/`: all three focused suites pass.
- `normal-x86/`: full x86 logs, capabilities and source-pinned validation.
- `normal-x64/`: full x64 logs, capabilities and source-pinned validation.
- `normal-build/`: four frozen normal binaries and provenance, no PDB/tree copies.
- `source-red/`, `source-recovery-red-r2/`, `source-green/`: preserved source receipts.

Prepared native-check scripts are preparation only, not a gameplay result.
Keep the accepted 414/415 pump-audio rollback and 416 failure evidence intact.
Pump audio assets, playback and physical hand calibration are unchanged here.
The reopened existing SP_01 save contains only knives; no rifle/SPAS fixture was
injected and the save/loadout was not replaced. Run the bounded actual-game
validation when its real inventory prerequisites are present, before asking for
another headset check. No claim of a playable whole campaign or fleet-wide weapon
acceptance is made. The installed desktop remains runtime 414 / launcher 0.1.2.
