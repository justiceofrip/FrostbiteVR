# Checkpoint396: menu and grenade evidence; recovery replay pending

Both normal architectures pass all258 suites. Native source closure:
`9d6cad4f3e45ab701f82b2262855d1d701b4da83e033cc97cea2cbbc1dcdae9a`.
The frozen normal binaries and source snapshot are under
`../bc2vr-recovery/overnight396`. Human390 remains the latest headset result,
and it failed. No whole-campaign acceptance is claimed.

## Integrated behavior

- Fresh native menu ownership crosses input protocol3 / control channel7.
  Older control peers fail closed. Player grenade policy consumes that same
  ownership, rather than treating missing menu state as gameplay permission.
- Shared campaign vehicle admission joins current native entry, input mapping,
  camera/gun components and dispatch consumers. Exact PBL handling remains first;
  generic ground/mounted-gun coverage requires its own successful admission.
- A finite diagnostic stops both input and tracking publication for five seconds
  during an owed pump, then returns in a new tracking space. It is off normally.
- Player grenade gesture, native acknowledgement and original-weapon return are
  implemented. Actual testing uses a separate private mode; normal396 does not
  yet enable the binding.

## Actual-game results

| Check | Result and limits |
| --- | --- |
| Native menu | `menu01` passes. Verified pause capture, pointer click on Resume, subsequent pixel-captured world pair,240 pairs, original actor retained, all children exit0. No OS input. Physical headset ray appearance remains untested. |
| Player grenade | `player-grenade01` sends exactly one successful press/release, consumes one grenade in all three native copies and returns to the original rifle without changing its22/191 rounds. Return input497 and subsequent neutral498 retain original source times. |
| Grenade publication audit | Original strict report remains failed:773 attempts include771 successful publications and2 Busy results. Both Busy rows are later neutral updates, after release363; the proven publisher returns before writing on Busy. Separate offline functional adjudication passes. Two native read gaps remain unknown; complete observation is not claimed. |
| Full receiver gap | Native state retains7/24 and owed pump debt throughout921 missing-input observations; old leases expire and a fresh gun hold is acquired after the tracking-space change. The driver then rejects an already valid recovery grant because it incorrectly uses its later200ms handoff as the neutral boundary. Sequence completion failed. Correct the driver and perform one changed replay; do not call this a production recovery pass. |
| Vehicle admission | Three coherent read-only snapshots admit the actual PBL driver/gunner and revalidate dispatch/backlinks. This does not prove another vehicle works. |
| Death rendering | Previous395 grenade damaged health100 to53.6414, with28 paired captures and no death. Actual death remains untested. |

The gap-result checker also exposed an empty-cycle indexing error. Its correction
must preserve failed native evidence and emit an ordinary failure report instead
of crashing. An offline re-audit needs no gameplay replay.

## Campaign gaps found from installed data

SP_02 truck passenger seat3 and SP_009 free-fall seat2 use soldier-weapon entries,
not ordinary mounted-gun entries. Their current native camera, firing owner and
attachment routing still require observation. SP_005 forces the player into a UAV
whose authored input mapping differs from ground vehicles. Helicopter segments
inspected in SP_004/SP_007 assign the human to gunner seat1, with Flynn in pilot
seat0; this does not justify a generic helicopter-pilot adapter.

Physical chest knife custody is being implemented separately. Physical magnified
scope rendering and broader manual mechanism coverage remain incomplete.
Unregistered reload families retain their documented stock fallback;258 passing
suites are not evidence that every weapon has manual interactions.

## Startup and operational limits

Fresh game06 exited before VR injection with an access violation at the same AMD
dispatch return site seen in earlier loading/restart failures. MSAA0 therefore
does not fix that intermittent startup failure. Game07 and game08 subsequently
loaded with the existing bounded native Continue path. The cause of the corrupt
dispatch pointer is not established; no speculative driver/settings fix is made.

Tests preserve failed artifacts, source/binary hashes and exact process identity.
Finished native probe DLLs remain resident until their owned BC2 process closes;
hooks drain normally, but the DLL cannot be reused for another injection session.

