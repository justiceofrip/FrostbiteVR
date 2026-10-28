# Shared reload timing and established hold recovery —264

The263 actual-game diagnostic passed SCAR support, both initial magazine cycles,
and the natural interrupted pump stroke. It then failed shell insertion before
submission. The first native magazine rejection was null; the262 intermittent
magazine rejection is not proved to have the same cause as any new fix.

Three independently reproduced defects are corrected:

- Physical magazine intent keeps its original controller deadline. Construction
  proof records the native observation valid when the input was consumed;
  admission requires fresh exact current authority, without retries or renewed
  input timestamps. Legacy requests keep strict source freshness.
- Shared shell/magazine guidance measures continuity between observation times.
  Actual263 had a94.0121ms geometry gap plus22.6264ms processing delay, incorrectly
  exceeding the100ms limit. Current input deadlines and claim expiry remain
  independent. Genuine large observation gaps still require recapture.
- Once a shell's native hold has been established, a brief missing publication
  cannot reuse the three-second startup timeout. A fixed maximum50ms recovery
  window preserves existing custody only; absent native evidence authorizes no
  hold or insertion. It cannot be renewed by later controller packets.

Each regression failed before its fix and passes on x86/x64. Focused service,
resource consumer/probe, insertion adapter and physical reload checks pass.
The combined functional candidate still requires actual-game testing and full
player builds. These checks do not establish headset feel or fleet acceptance.

Earlier259 fixes remain included: committed empty-hand intent survives tracking
interruptions; measured SCAR front support; natural pump stroke and continuous
right-hand aim. Off-body squeeze no longer synthesizes a draw in the production
consumer regression. This is not yet confirmed by a new headset test.

Evidence: private `resource-scar263-01/native-trace.json`, SHA-256
`c2f9afe99a364c10d77bac62e7b0294d81d7dc5e75f0a0dcf73d68b4b83ad82f`;
`insertion264/diagnosis-and-tests.json`; focused logs in
`test-temp/resource-intent264-*` and `test-temp/hold264-*`.

Public GitHub245 remains unchanged.

## First actual-game result

Fresh SCAR/SPAS run `resource-scar264-01` passed the complete ordinary controller
input sequence. Three magazine cycles produced six unique native command
receipts, including returning the original full magazine after shoulder swapping.
Natural pump scenario4 retained a0.34775rad two-hand aiming correction, recovered
after tracking loss, and completed exactly once. One shell inserted with matching
native ammunition changes; support returned133.8038ms after completion. The final
rifle shot succeeded. Game remained responsive; diagnostic hooks detached.

Trace SHA-256: `645883067704f7a372fdd76ad562a2cc7988cbd7c5d02afc803b08d48662ada0`.
Receipt/expanded audits passed. Recording had10 callback lock drops, so this is
functional completion evidence, not complete callback coverage. Headset feel
is unverified.

## Second family result

Fresh F2000/SPAS run `resource-f2000264-01` passed the same complete sequence
using the identical frozen diagnostic DLL and source. It also produced six
unique magazine command receipts and one shell completion, recovered a natural
interrupted pump, and returned support134.2128ms after shell completion. The
final rifle firing check succeeded. Game remained responsive; hooks detached.

Trace SHA-256: `4037ab24903b570734ba0f1a05a895686960c7223bee630d0e8ad56180619b8d`.
The second receipt/expanded audit passed; seven callback lock drops prevent a
complete-recording claim. These two runs establish these shared interaction
sequences on SCAR/F2000 plus SPAS, not all enrolled guns or headset acceptance.

## Player build and checkpoint

Both complete normal player builds passed all240 C++ suites: x86 in137.29s,
x64 in36.70s. Emulated input drivers are disabled in the player build; ordinary
manual cycles and native recovery remain enabled. Frozen source pin:
`bd0b0b19cbf6ed2c8f4db02569a47245df89ed2f340d997e93c2c112a0807408`.

Private player binaries are frozen under `normal264-player-checkpoint` and
staged under `headset264-20261009`, with a local source archive under
`checkpoint264-local`. The staged launcher validates file hashes and requires a
fresh game. Current BC2 contains the detached diagnostic DLL, so it must restart
before the headset check. No headset session has been started.

Next check: off-body grip stays empty, real shoulder draw works, rifle support
and magazine handling, natural SPAS pump and hand alignment, shell insertion
and immediate support return. [Player checklist](CHECKPOINT-264-PLAYER.md).
Checkpoint/death reload crashing, broader weapon enrollment, pistol slides and
belt-fed reloading remain separate unfinished work. No public publication.
