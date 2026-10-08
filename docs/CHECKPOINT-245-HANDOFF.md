# Checkpoint 245 — failed headset acceptance, October 8, 2026

Start here when continuing this repository. This is the latest development
source checkpoint, not a playable release. The user exhausted their usage and
requested this GitHub handoff. Do not infer readiness from the older README or
historical checkpoint sections.

## Immediate failures to address

The headset user fired the SPAS and lost holster/reload/other interactions.
They then attempted a mission checkpoint reload; the game crashed. Treat the
interaction lock and subsequent scene-transition crash as separate failures.

The final native report and BC2 crash XML were empty. Surviving telemetry had
143 samples and 3,989 submitted stereo pairs; it does not expose the first pump
cancellation. Scene ownership changed and stereo views rebuilt before exit.
The probe thread returned `0x80000003`; no exception address or stack survived.
Do not claim the exact crash cause or first interaction failure is known.

There is a confirmed recovery gap in the current code:

- `Bc2NativeCycleService::Control` rejects `Cancelled`; `View` continues to
  block actions and `YieldForReload` cannot retire that active state.
- `Bc2PhysicalPump` retains blocking after rejected controls/cancellation.
- `Bc2Gameplay` uses pump debt to block holsters and shell loading as well as
  firing. A failed interaction can therefore strand the other controls.

Do not fix this by erasing ammunition/cycle debt, manufacturing acknowledgements
or resetting on every frame. Reproduce loss/release/regrip and delayed callbacks
through persistent pump, holster and shell consumers. Retain durable per-weapon
state while recovering usable input after verified native convergence.

## What is implemented and what was tested

The latest player build passed all 230 C++ suites on x86 and x64. The tooling
run passed 874 tests with one skip. The post-test launcher-only change also
passes `tests/Test-PreviewLaunchers.ps1`: a locked crash XML no longer prevents
writing the terminal `completion.json`. These checks do not override the failed
headset test.

The ordinary-input F2000/SPAS test completed magazine original return,
discard/chest replacement, shoulder inventory changes, physical/native pump,
shell insertion, immediate support return and rifle reload after holstering.
Six native magazine operations completed. Its final assertion still failed:
the 102.9 ms trigger pulse fired two rounds, 30→29→28, instead of the driver's
exact-one-shot expectation. Server and both client copies independently record
both shots. Preserve the original failed overall diagnostic status; correct the
driver assumption separately. The trace also has callback-recording gaps.

Earlier SCAR/SPAS performed the combined sequence, and a finite M95 diagnostic
completed two bolt cycles. Ordinary M95 input later succeeded at startup, one
shot and custody transfer, but lost the right Mechanism claim during Unlock.
It is excluded from headset acceptance; its first cancellation cause is unknown.

The common magazine fix retains native completion across repeated tracked input
packets until the physical consumer can receive it. Regression cases cover XM8,
AEK and F2000. This is shared behavior, not three separate weapon implementations.

Seventeen exact magazine configurations have compiled prerequisites, including
campaign XM8/AEK/SCAR/F2000 and multiplayer M416/XM8 Compact/MG36/XM8 LMG variants.
That is not seventeen accepted guns, campaign compatibility for MP variants,
or completed belt-fed LMG reloads. Pistols, slide/chamber actions and belt-fed
cover/feed integration remain unfinished. Underbarrel automatic loading is
retained by user request; physical underbarrel reloads are deferred.

## Reproduce the source build

```powershell
./Build-Checkpoint.ps1 -Architecture x86 -Jobs 4 -OrdinaryManualCycles
./Build-Checkpoint.ps1 -Architecture x64 -Jobs 4 -OrdinaryManualCycles
python -B -m unittest discover -s tests -p 'test_*.py'
./tests/Test-PreviewLaunchers.ps1
```

The receipt is `profiles/checkpoint245/source-with-header.json`; source digest:
`fde0371f517635960c48c476d39503daaebfccf5364c3f8c403b7c3cfcf4f107`.
The reviewed registry/geometry are in `profiles/resource-enrollment240-f2000`.
Omitting `-OrdinaryManualCycles` leaves those experimental native mechanisms off.
Do not use a diagnostic-input build for a human headset session.

`Run-OrdinaryResourceInputProbe.ps1` and `Run-M95OrdinaryBoltInputProbe.ps1`
exercise persistent normal consumers with simulated controller input. Their
local loadout provisioning, binary receipts and geometry-cache paths need to be
prepared on the receiving machine. Raw traces, videos, meshes, compiled binaries
and installed-asset caches are intentionally not published.

## Unmerged agent work

See `handoff/checkpoint245/pending/README.md`. Frozen source payloads are saved
there separately, with original manifests and checksums. They are not integrated
into checkpoint245, are not headset accepted, and may need rebasing. In particular,
native recovery depends on the physical recovery payload and remains default OFF.
Do not overwrite the main tree with entire historical candidate files blindly.

The next useful work is interrupted-pump recovery plus durable first-failure
telemetry/crash capture, then scene-transition investigation. Avoid another
headset run just to reproduce a lock already reported by the user. Keep native
engine evidence separate from mocked tests, and every gun's measured data
separate from the shared interaction and engine operating-class code.
