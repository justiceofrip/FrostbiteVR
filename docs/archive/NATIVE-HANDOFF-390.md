# Native mechanism handoff after a weapon-family round trip

Checkpoint 389's new actual-game sequence reproduced a second-draw failure;
see [POST-HOLSTER-PUMP-389.md](POST-HOLSTER-PUMP-389.md). It completed the first
SPAS pump and shell insertion, returned to the SCAR, handled its magazine and
fired, then drew the same SPAS at 8/23. The second pump never received a raw
contact and the unchanged diagnostic deadline expired. This is a failed test.

## Cause and correction

`Bc2ReloadNativePolicy` already retains exact retirement facts for each policy
when changing families. That lets a cancelled, drained tube policy be revisited
without inventing another reload just to retire it again. However,
`NativeCycleRequestRetired` checked only the runtime's current dispatch-epoch
receipt. Successful family selection clears that receipt. Thus returning to a
previously used tube policy could leave the native pump selector disabled,
despite completed reload work and fresh weapon custody.

The shared policy now exposes `RetiredForNativeHandoff`. The runtime uses it
under its existing policy exclusion, preserving either the exact current
receipt or the exact per-family retirement already retained by the policy.
Starting a new cycle clears remembered retirement. Identity, cycle, selected
profile and cancelled phase must still match. Idle unused policies retain their
existing behavior.

This fact does not authorize firing, ammunition insertion, native writes or new
controller poses. The native pump/bolt selector still requires current actor
and item ownership, verified configuration, coherent idle ammunition, unchanged
callback revision, excluded native callbacks, and completed physical custody.
The change applies to their shared handoff rather than adding weapon names.

## Regression and status

The family regression now reproduces the real sequence: retire tube reload,
retire rifle reload, change back, observe the consumed dispatch receipt, then
require a permitted native handoff from the remembered retirement. It repeats
the family round trip and rejects reuse after a new cycle/equip generation,
before retirement, and without callback drain. Remembered retirement produces
no fresh insertion lease. This covers the gate missing from the earlier
high-level input simulation.

Focused family, request-cycle, native-cycle and continuous inventory suites
pass. Source closure:
`62e55991fc6f2a69992e60153dbf1129aa25fd1baca61570c8c15c9f01f9f27c`.
Both full builds pass 246/246 suites (x86 119.89 s, x64 58.26 s).
No headset acceptance.

## Actual390 validation

The unchanged 389 input sequence now passes at `overnight390/native01-revisit`:
three rifle magazine operations, first SPAS shot/pump/shell, rifle return and
fire, second SPAS draw/shot/pump, then another rifle return. Active time is
42.406 seconds within the unchanged 50-second budget. The second pump adopts
the existing support claim without a release, observes 41 paired render
receipts, and returns support. Both native releases are acknowledged; zero
control rejection or unintended hand-claim drop is recorded. All 240 stereo
pairs are consumed with zero discard or camera-restore failure; hooks drain.
Trace SHA256:
`136798ab2561ca7b9225df0e177e1c8eb590aca5c163aeaf97c30becbb036699`.

`overnight390/scene01` independently passes a checkpoint restart during an
unfinished pump through the native VR menu pointer. A new actor exits the boat,
draws the rifle, removes and returns its original magazine with matched server
and both client observations, then completes a fresh pump, shell insertion and
support return. Support returns 134.879 ms after shell completion. It consumes
240 stereo pairs, including 152 after the world rebuild, reports zero watched
exceptions, releases the menu pointer, drains hooks and leaves BC2 alive.
Trace SHA256:
`2406d955c168e3c9e640a80d42350ba2acee6342ad3d53270d7b826f8b544fc8`.

The scene audit retains its original conditions. Native retirement first
selected the replacement shotgun, so rifle-first retirement remains unverified.
One successful restart does not establish death recovery, sustained campaign
stability, or a causal fix for historical intermittent graphics crashes.
The original failed389 trace and failed human375 run remain preserved.

`overnight390/bolt01` also passes five ordinary M95 shots and physical bolt
cycles through empty. Independent audits verify original guarded native clock
progress, unchanged per-cycle ammunition, all three copies completing after each
gesture, grip return and all four visible bolt phases in paired render evidence.
Both the runtime status and stricter timing/receipt audits pass. Final magazine
refill is stock fallback, not manual-magazine or wider sniper acceptance.
Trace SHA256:
`e1325bd9bc49fbd67bcdaa5348ef1c99d892ce003997136f7ffb4a4017d95344`.

## Next player check

Use the frozen normal390 build, not any private diagnostic build, in a fresh
game. Check SCAR support and reload, SPAS firing while supported and a complete
rear/forward pump, then rifle use and another SPAS draw/pump. Repeat both
shoulders, inspect the stock/ring while pumping, and test shell loading into an
empty tube without automatic extra shells. Finally use the VR menu and restart
the checkpoint. These human checks target feel, actual controller alignment and
the previously reported visual failures; the automated outcomes above have
already been checked without a headset.
