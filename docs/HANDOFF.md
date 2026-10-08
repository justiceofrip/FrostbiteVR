# Fork handoff

Local checkpoint [205](RELOAD-RECOVERY-205.md) extends the reload motion driver
to repeated episodes without resetting persistent consumers. Offline tests now
cover 30 tracking/focus/input interruption cases followed by recovery, repeated
reloads and rejection of old evidence. Native responses and renderer receipts
are mocked in this matrix. All 186 C++ suites pass on both architectures.
`Build-Checkpoint.ps1` selects the 205 operation
receipt with the same 202 calibration headers. No new live interruption mode
or headset acceptance is claimed. The user is independently testing unmodified
BC2 launch switches; do not restart, inject into, focus or close those sessions.
The x86 205 output is `build/x86-checkpoint205`, because the old DLL at
`build/x86/BC2NativeProbe.dll` remained locked. Do not select the old path by
habit. The new offline build receipt pins the separate output directory.

Local development now adds [204](COMBINED-INVENTORY-RELOAD-204.md), a bounded
combined shoulder-switch/chest-reload controller sequence. All 186 suites pass
on x86 and x64. The corrected live run passed its strict combined, reload and
startup-pulse audits: 20/191 to 30/181 ammo, zero cancellations/fallbacks, 240 pairs
and clean restoration. At the end of that run BC2 was minimized with its disabled
DLL still mapped. That historical PID is not authority to close any later game
session; verify the current process before future tests. No GitHub push was
made; the public repository remains the user's fork snapshot.

The prior [checkpoint 203](RELOAD-TRANSITIONS-203.md) corrected a reproduced
cross-profile magazine-retirement deadlock in the shared consumer.
All 185 public C++ suites passed on each architecture, and the rebuilt module passed
the existing strict native original-return/replacement sequence. Cross-profile
active-reload interruption recovery itself is CPU-tested; 204 adds ordinary
holster/switch/reload coverage without interrupting an active reload.
The prior 203 receipt is retained for its exact historical source.

This public source snapshot starts at checkpoint 202. Read [STATUS.md](STATUS.md)
for current acceptance and [../BUILDING.md](../BUILDING.md) for ordinary and
checkpoint builds. The latest human headset feedback predates the automated fixes.

The shared magazine startup pulse now has immutable original input timing and
requires genuinely fresh native contexts before Holding. A bounded diagnostic
completion journal retains exact End data when the record gate is contended.
The actual original-magazine return then replacement sequence passed its strict
native audit. See [SIMULATED-PLAYER-202.md](SIMULATED-PLAYER-202.md).

Do not revive old absolute launch paths or treat historical research notes as
current release instructions. Raw traces, media, game content and local config
are intentionally excluded. The small checkpoint calibration headers and exact
source-bound operation receipt are in `profiles/checkpoint202`.

Next work is live interruption/pickup/vehicle/empty-ammo transition coverage,
followed by mechanism integration and exact-package headset acceptance. There are no agents
or local game sessions that a fork needs to coordinate with.
