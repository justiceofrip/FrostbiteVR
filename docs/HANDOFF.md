# Fork handoff

Local development now adds [204](COMBINED-INVENTORY-RELOAD-204.md), a bounded
combined shoulder-switch/chest-reload controller sequence. All 186 suites pass
on x86 and x64. The corrected live run passed its strict combined, reload and
startup-pulse audits: 20/191 to 30/181 ammo, zero cancellations/fallbacks, 240 pairs
and clean restoration. BC2 is minimized with no active mod/host session; its
disabled DLL remains mapped, so restart BC2 before the next injection.
`Build-Checkpoint.ps1` now selects the 204 operation receipt and 202 calibrations. No GitHub push was
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
