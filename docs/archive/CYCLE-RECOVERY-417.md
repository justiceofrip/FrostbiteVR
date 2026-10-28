# Cycle recovery 417: completed strokes and late native prediction

**Candidate; release blocked. No checkpoint 417 actual-game or headset acceptance.**
Source closure: `41489d7d8bb268ab7dae50f1a856716c0622000a6ec3a44cb449d31f80edf2db`.
The focused tests and full x86/x64 builds passed. Frozen normal runtime receipt:
`d9c4c6c5891f5957639ac5d4515763299f5181bb856ad3df9856a1cd09e68005`.

## What failed and what remains unknown

The 416 headset session retained a first failure at native invocation **8748**:
SPAS cycle/shot 3 changed `Complete -> Cancelled`, failure `Restore`, with 5 loaded
and 24 reserve rounds. Controller focus/tracking were present; recorded policy
contention was zero. Its persistent `blocksFire` also blocks holsters and vehicle
Use. That explains the shared lockout, not the rejected callback's cause.

BC2 exited before the full trace finalized. The final `native-trace.json` is
empty; 8748's native boundaries, prediction snapshot and recovery mode are
unavailable. This patch must not be described as a reconstruction of that event.
Original evidence remains in
`reports/native-trace-20261017-133551-422/native-cycle-first-failure.json`.

## Implemented behavior

- An exact acknowledged stroke can survive a later client ready-to-tail replay
  for the same native weapon, operation, counts and prediction snapshot. The
  ordinary path retains its original release deadline and verifies completion
  of the replayed native tail. It does not issue another gesture or Ready receipt.
- The shared recovery path retains the original outcome and acknowledgement,
  temporarily blocks firing, and requires **two new guarded idle rounds from all
  three native branches**. It covers SPAS recovery and concurrent M95 bolt
  recovery. A long pause does not invent an overall expiry; each native callback
  still needs its original bounded, fresh guard and exact lifetime/count proof.
- An ordinary Update already issued while Complete may overlap an independent
  client's replay. Only that retained invocation can drain. Its unchanged
  original idle result is not a guarded vote. The last drain discards earlier
  votes; completion requires the pending callbacks to drain and two fresh rounds.
  The native Step exception verifies the exact caller, TLS frame, context and
  invocation. It executes the existing Step/empty control unchanged.
- Wrong ownership, stale chronology, altered ammunition/snapshot/context,
  unproven shots and failed restoration remain explicit failures. A proved next
  own-Update shot starts new debt; prior payment cannot satisfy it.
- The first-failure journal now copies the associated callback when available
  and policy state before/after cancellation, including recovery mode and ACK
  time. This is one immutable bounded record per module lifetime; callback code
  does no file I/O. It cannot recover missing evidence from earlier sessions.

Pump audio playback/cache, physical calibration and custody rules are unchanged.
The production audio ledger is used in the integration regression to check one
release/ACK and exactly two accepted pump cues across native replay.

## Evidence and limits

Actual checkpoint 413 recorded SPAS invocation 17625 changing ready `2/1/2` to
tail `7/2/8` with unchanged ammunition, followed by original tail completion.
Existing M95 fixtures also establish the selected tail shape. **Scheduling that
shape after global completion and ACK is synthetic**: it is the regression that
failed the previous implementation, not a measured occurrence of 8748.

Private evidence root: `G:\Unity\VRMMO\bc2vr-recovery\overnight417`.

| Evidence | Result |
| --- | --- |
| `cycle-regression-red-pinned/validation.json` | Baseline service fails the new paid ordinary replay assertion; journal passes |
| `cycle-regression-recovery-red-pinned-r2/validation.json` | Ordinary fix passes; unchanged recovery implementation fails the paid recovery assertion |
| `cycle-regression-green-pinned/validation.json` | Service, recovery service and failure journal all exit 0 with source hashes retained |
| `normal-x86/validation.json` | 286/286 C++ suites pass for the source closure above |
| `normal-x64/validation.json` | 286/286 C++ suites pass for the same source |
| `normal-build/build-receipt.json` | Four normal binaries frozen; no installed promotion |

Regressions cover SPAS and M95, empty/positive ordinary cycles, both client
replays, exact next-shot debt, a 61-second recovery pause, physical pump/audio
integration, overlapping original callbacks and contradictory/stale evidence.
Existing shared convergence tests were not weakened. An earlier unpinned attempt
and a source-changing recovery-red attempt remain retained but are not evidence
of a valid red/green build.

Recorded coordinated commands are `RunCycleRegression417.ps1 -Stage green`,
then `BuildNormal417.ps1 -Architecture x86` / `x64` under that private root.
They enforce storage/source pins and preserve prior outputs; the regression
wrapper accepts a new `-Attempt` for a deliberate rerun. After a matching built
tree is verified, the three focused suites can also be selected with:

```powershell
ctest --test-dir build/x86 --output-on-failure -R '^(Bc2NativeCycleService|Bc2NativeCycleRecoveryService|Bc2NativeCycleFailureJournal)$'
```

Do not infer campaign playability, every weapon's support, reconnect/death
acceptance or headset comfort from these synthetic tests. The bounded actual-game
check and subsequent headset acceptance remain separate gates.
