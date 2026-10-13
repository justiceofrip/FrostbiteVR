# Menu input lifetime recovery333

Normal333 fixes two related ownership defects in the native menu adapter:

- Pointer and Back/Menu cleanup previously tied an outstanding press to the
  queue's original storage. Growing that queue could strand an otherwise valid
  release. Cleanup now identifies the manager, UI and APT target as the input
  owner, while still checking current queue bounds and capacity before calls.
- A complete, stable replacement menu can retire the old press without receiving
  its release. The runtime advances its menu epoch, clears pending visibility
  work and requires fresh neutral input. Missing, blocked or inconsistent reads
  during loading retain the old obligation; they never fabricate retirement.

The pointer path also checks target replacement while a controller remains down,
even if the native menu's entered flag has not changed. The cancel path validates
both the native controller owner and APT target before retirement. Retirement
has separate counters and is never counted as a successfully dispatched up.
Normal tracked releases still position the current VR cursor before button-up;
lost-input cleanup never moves it.329's bounded Busy handling remains intact.

## Validation

Focused Binding, State and Control suites pass. Both full normal builds pass all
244 suites: x86 152.21 seconds; x64 58.27 seconds. Deterministic regressions cover
queue growth/replacement, valid replacement targets/controllers, blocked or
missing input, inconsistent reads, wrong callback authority and address reuse
after retirement. No stale release is sent to a replacement owner.

`bc2vr-recovery/menu333-recovery01/audit.json` independently verifies the normal
player build with neutral emulated controllers and all ordinary player options:

- The rendered native pointer opens the checkpoint dialog and confirms restart.
  The local actor changes from1008236928 to1309051616.
- The replacement scene's menu opens, is captured and classified, then closes.
  Three pointer downs have three matching ups, with no release failure, stuck
  button, visibility timeout or menu graphics failure.
- The receiver consumes240 stereo pairs;196 follow the replacement menu's
  return to gameplay. There are no camera restoration or reported GPU failures.
- The exception watcher records zero exceptions. All helpers exit successfully,
  hooks disable, outstanding owned references drain, and native view order
  matches the exact expected surviving order. BC2 remains running.

Two fading menu captures required another capture without input. The classifier
and command audit verify that no dependent click used either unrecognized frame.

Neither menu retirement counter fired in this game run: the relevant ownership
replacement branches are deterministic-tested, not claimed as live reproduction.
The actual-game result verifies that normal scene/menu recovery still works with
the new code. It is one bounded run, not sustained campaign acceptance.

Native trace SHA256:
`ec711dee8a04cfdbd5f1a55effae80009ecb0d8e7f9005fa2f236505b6b19e93`.
Normal source SHA256:
`bd9a55333244e1c74e6d2786fa48d2b100abb713109592297b5ab174aa2674b4`.

## Separate crash investigation and equipment limits

Input-only331 also completed a checkpoint restart with no mod rendering or
equipment hooks. It still used a native menu hook, so it is not an unmodified
game baseline.332 could not create a device with the installed D3D11 debug layer;
no SDK, driver or global graphics setting was changed. Neither result identifies
the producer of326's intermittent graphics-worker fault.

That crash remains unresolved. Historical327/328 restart and311 equipment
receipts keep their original scope.333 does not add new weapon enrollment,
player chest-knife integration, pistol chambering or a third native carry slot.
The normal player build contains no scripted diagnostic input. Headset fit,
visual acceptance and sustained campaign play still need the user.

The next combined headset check is rifle support/reload/chest ammo, both shoulder
holsters and pickup, SPAS pump/shell/immediate support, and the laser menu. Restart
is last so any remaining crash does not erase the earlier feedback.

## Follow-up on the rifle-first retirement gap

The existing scene-resume receiver sends only a vehicle-exit pulse; it does not
select a weapon. In328 the replacement checkpoint already selected SPAS before
the diagnostic could perform its shoulder draw. That reached the new-pump
selection retirement path, not `RetireNativeCycleForReplacementActor` during a
rifle-family handoff. Moving the later rifle draw earlier in the test script
would not undo that first native observation. The gap remains explicitly open.
No duplicate scene run or artificial skipping of production SPAS observations
was used to claim coverage. A different verified spawn/loadout is needed for that
specific live branch; the service-level rifle-first regressions already pass.
