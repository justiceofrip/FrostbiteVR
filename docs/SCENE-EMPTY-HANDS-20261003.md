## Current follow-up: pause recovery passes, restart remains open

Native022310 and both-eye review24/72/96/144 verify SPAS remaining stowed through
an actual pause/resume. The fix spans three boundaries: missing fresh paired
visibility, exact same-owner input epoch, and ControllerActions' inactive rearm
output rejected by InputOverride. All require new suppression and paired hide
before free-hand authority resumes. A bounded reason journal distinguishes
semantic cancellation from input rearm and final source/session retirement.
This monitor check did not restart the mission, cause death, reload or use XR.
See HANDOFF.md and recovery/empty-pause-native-audit/audit-022310.json.

# Default empty hands and checkpoint coverage — October 3

The user requested initially stowed weapons and empty hands after leaving a
vehicle. Production BodyInventory now queues a normal Holster intent after the
first verified on-foot actor observation, a changed native actor, or an observed
vehicle-to-foot transition. The selected, coherently observed carried item must
have an assigned slot and an accepted hide profile. Controller input must be
neutral; reload/support/ammunition claims retain priority. This is a semantic
request, not an acknowledgement or a new native visibility mechanism.

Actual input-cache suppression, a fresh paired private hide, and release of the
current right GunHold still precede Empty. On vehicle exit, old presentation
first retires through the current-owner Show path, then the queued stow obtains
new hide proof. Ordinary draw does not requeue automatic stow. Input gaps and
recenter alone do not count as actor entry. An observed item replacement or
explicit body selection supersedes a queued default. Unknown native actor
lifetime changes that reuse every observed pointer remain unproven.

SPAS and exact scoped XM8_sp_s with current XM8/ACOG mesh evidence are admitted
in explicit BodyInventory mode after native hide/draw/firing review. Other guns,
pistols and gadgets remain unsupported. This does not complete all-weapon empty hands
or remove the existing selection fallback. BodyInventory itself is still
controlled by the existing launcher/session selection.

The receiver-only cross-draw fixture keeps left squeeze at 0.4 for its initial
three seconds, below grab acquisition and above default-stow neutrality. Right
squeeze retains its original neutral/press edge. This is a deliberate fixture
prelude, not fully neutral-input evidence. At 3s the ordinary right-shoulder
gesture supersedes the queued default; the fixture still has exactly two right
press edges and no trigger, stick, or button actions.

Deterministic checks cover initial stow through real consumer phases, missing
suppression/pairs, reload and ammunition claims, input gaps, current GunHold,
same-pointer item replacement, unsupported scoped XM8, ordinary draw, initial
seated state, and an already-empty seat-entry/exit recovery. Both architectures
pass 33 coordinator, 42 existing inventory, 15 automatic-stow, and 11 diagnostic
groups. The x86 Gameplay and x64 receiver translation units compile freshly.

## Restart blackscreen remains an open user report

There is no newer timestamped headset run proving a cause. Earlier two-checkpoint
monitor checks used neutral controls without a committed holster or active reload.
They do not settle the user's restart/death report. This change makes the existing
feature-enabled neutral fixture exercise a real initial Empty state with SPAS:

1. Start a fresh process with SPAS selected on foot. Run the existing
   tools/Test-GameplaySceneRecovery.ps1 -ExpectedPid <pid>.
2. Before the first actual checkpoint restart, require a committed Empty with
   fresh native suppression, paired hide, and both free hands. Save both eyes.
3. Perform two actual native checkpoint restarts. Require separate scene-owner
   retire/rebuild chains and advancing visible stereo pairs in all three scenes.
4. If a checkpoint returns to the boat, prove infantry claims retired and current
   driver camera/control ownership resumed. If it returns on foot with an admitted
   profile, require a new current-owner default hide receipt.
5. Require strict final hook/camera/graph cleanup, no observed exceptions, and
   post-detach game stability. Headset, death, and active reload cancellation are
   separate remaining tests.

The staged audit-empty-scene.py is a saved-evidence supplement for initial Empty
and later retirement. It requires the separate full recovery/exception/visual
audit and never declares the unrecorded headset blackscreen fixed. Its 17 synthetic
schema/rejection checks are tests of the auditor, not native acceptance.

Read-only investigation found two possible gates worth observing if reproduction
still stalls: native Gameplay remains pinned to its first input thread, and a
known seated camera waits for a fresh vehicle view lease. Neither is established
as the cause, so neither authority check was relaxed. Native menu mode with no
fresh surface can also submit no headset layer; monitor world recovery alone
cannot establish OpenXR menu behavior during a real headset restart.

