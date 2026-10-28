# R49 private build lifetime and failed game103 episode

The first real combined boat-exit/reload/holster attempt used private build03.
Its setup reached the actual SP_03 PBL driver, with the existing XM8 selected
and coherent22/191 ammunition in all three copies. The ordinary setup used E
to exit, keyboard1 to select XM8, then the visible E Enter prompt. Keyboard2
selected the SPAS and was corrected before native injection; that failed
precondition remains recorded.

The native episode is **failed**, not reload/holster acceptance. Bootstrap
observed remote-thread exit0xc0000005, then BC2 exited. The receiver exited0
normally. Raw native-trace.json is14,172,160bytes and ends inside shell row104384,
far beyond the192-row array. It also contains24 unquoted nonfinite tokens.
No repaired trace is used for acceptance. Independently complete original
prefix objects record zero native ammo operations, disabled hooks, and drained
reload flow with zero in-flight operations. These limited facts do not turn the
failed remote exit into a successful detach or sequence.

The actual build03 and restore logs omit compilation of both changed BC2Camera
translation units: Bc2ResourceInventoryProbe.cpp and Bc2InventoryReloadProbe.cpp.
Gameplay/NativeProbe did compile. The private wrapper copied staged files with
Copy-Item, preserving older timestamps, so Ninja could reuse incompatible object
layouts. Current disk hashes and a compiler command graph alone did not prove
that an object had been compiled from those bytes. Build03 is withdrawn from
further execution. The correction must invalidate exactly the affected objects
and prove their compilation on both overlay application and normal restoration.
Do not treat serializer clamping as a correction for incompatible code layouts.

Evidence: performance436/boat-exit-reload-r49-prep/run01/independent-audit01.json,
SHA2560b4e8f8360f0f5b48fab7b7acfc87afc550d9bba40b6a1a54cc9ce6e1d4ebfd4.
Root confirmed no owned game/helpers remained; scene103/Finish.ps1 restored all
three saved profile files exactly. No forced game/receiver termination was used.

Separately, R50 camera observation's isolated13-case vehicle suite passed, and
the destruction-payload callback's WARP integration passed with4,211,349bytes of
evidence. It verifies paired buffer capture, preserved predication, cancellation
and owner-thread cleanup. The source-reviewed composed overlay is still private;
neither test proves that the destruction visual bug or restart timeout is fixed.

No public/installed native build was changed by this attempt. Public0.1.15
remains the launcher hotfix with the previous native gameplay binaries.

## Corrected build and separate startup rejection

Build run04 compiled all four affected units during diagnostic application and
normal restoration, restored exact normal outputs, and produced receipt
88fc576528bdd7e2df490d91c9a4215f843477ba9858befbdd9617998d57b46b.
RunBuilt rejects the older build before process lookup. Native run02/game104
then wrote complete valid JSON and detached with bootstrap/receiver exit0,
disabled hooks and drained reload flow. It performed zero ammo operations:
the emulated boat fixture rejected its first input before any exit pulse.
Reason101 combined several readiness checks, so it did not establish which
predicate failed. A bounded initial wait with exact rejection telemetry is the
successor; actor/seat checks and original input lease durations are unchanged.
Its standalone startup/terminal regression passed once. Native acceptance is
still pending. Root closed game104 normally and restored all three profile
files; the future scene cleanup now parses receipt timestamps as strings so
PowerShell cannot discard UTC fractional precision when checking process birth.

Storage maintenance compressed retained symbols and 61 existing raw captures
losslessly, checking every content hash. Raw capture compression reclaimed
107,216,896 bytes; no evidence was deleted and storage limits were not raised.
