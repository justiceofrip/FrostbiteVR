# Selected opening-mission knife input, checkpoint 418

The opening SP_01 knife and the ordinary carried knife share the display name
KNV-1 but have different authored attack inputs. Live, read-only inspection of
the current campaign and both native consumer branches established that
Knife_Equipable in slot 0 uses boolean action 37, while the ordinary knife in
slot 7 uses action 8. The old controller adapter always wrote firearm action 8.

The adapter now routes an already-authorized trigger request to action 37 only
for a fresh, exact selected Knife_Equipable configuration and matching actor,
item, equipment and tracking lifetime. It rereads selection before committing.
Action 8 is zero for this route; ordinary firearms and the regular knife retain
their existing action 8 behavior. Unknown or stale knife metadata grants no
attack. Gripping, drawing and rendering never create a trigger request.

The existing typed configuration reader and input-buffer transaction are reused.
No native hook, pointer layout, weapon-operation capability row, chest-knife
enable or damage write was added. The live native-playing mask was 0x1b3 and
passed the unchanged 0x103 requirement. Existing menu, tracking, neutral-arm and
owner-transition gates remain intact.

## Validation

Source closure: `6178748ef4f7decacd26f36035d05662c37df1a6a565a163264621468bc47679`.
Frozen receipt: `d145f3395acac3b945badeefdf46c80e251ae2a8017f78e49d36d7428b4be607`.

- The three focused input/adapter/action-policy suites passed.
- Full normal x86 and x64 builds each passed 286/286 C++ suites.
- The controller-to-input regression covers explicit press/release, duplicate
  packets, grip without attack, reconnect with a held trigger, and selection
  generation changes. The route tests distinguish the two same-name knives and
  reject stale identity/configuration, malformed paths and arbitrary action IDs.
- The first native input check was rejected with probe exit 10 before session
  startup: its pose-only configuration omitted the normal build's required
  physical-magazine/body-inventory configuration. No trigger pulse or native
  attack was observed. This is a test setup failure, not gameplay acceptance.
- The corrected eight-second normal configuration recorded 10 external samples
  with action 37 set, action 8 neutral throughout, and both selected-knife firing
  copies progressing 2 -> 5 -> 6 -> 2. The ordinary slot-7 knife did not advance.
  The native report records clean hook shutdown and restored view lists/callbacks.
- The receiver consumed 216 pairs before the producer ended, short of its
  requested 240. Its final result/pulse-clock record was not saved, so the
  scheduled-pulse audit remains inconclusive. Retained cache/branch observations
  are evidence of activity, not a complete timing acceptance or enemy damage.
- Root reviewed the first and last saved native stereo pairs. The scene was
  visible in both eyes, but the later pair shows a visibly misplaced/elongated
  knife/arm pose after the attack. Visual acceptance remains failed/unresolved;
  no death or restart occurred in this bounded run.
- Inspection of all 74 retained hand samples found both synthetic controller
  grips at `[0,0,0]`, collocated with the head. The plain trigger-only receiver
  did not initialize physical hand offsets. This confounds that visual result;
  it does not establish a normal-player rig failure or exercise a pose-lifetime
  transition. Correct the receiver's hand setup before another visual check.

Private evidence is under `bc2vr-recovery/overnight417/knife-route-proposal/`
(native gate and consumer proof), and `bc2vr-recovery/overnight418/`
(`application.json`, `focused/`, `normal-x86/`, `normal-x64/`, `normal-build/`).

## Remaining work

This supplies a trigger fallback, **not physical stabbing**. The selected
campaign knife does not have the chest coordinator's original-firearm/temporary
tool relationship. Its existing selected-item hand custody must independently
provide a measured knife grip/tip receipt to the shared motion recognizer.

Normal firearm aim hooks require firearm effects types; melee effects have a
different consumer. Neither a trigger bit, animation, sound nor firing-state
transition proves an enemy hit. Hand-directed hit origin/timing and authoritative
enemy damage remain unverified. Do not enable the unfinished chest coordinator
or claim damage acceptance based on this input change.

Static inspection of the installed executable also identified a gap in the
earlier melee evidence: an acquired-target attack bypasses the shared Shoot
callback used by checkpoints 330/336. That callback is used for the fallback
swing without a valid cached target. Hand-directed physical stabbing therefore
needs the native acquisition boundary, followed by the matching target attack;
changing only the fallback swing direction cannot establish a hit.

The private `MeleeHit418.md` and `melee-hit-proof418.json` record the verified
acquisition/attack interfaces, target weak handle and damage receiver. They also
verify that ServerSoldierEntity's health interface is embedded at +0x2c, with
the scalar at +0x34. These addresses are tied to the recorded executable hash;
no current target health or successful damage was measured. A future damage
check must attribute the health change to the exact attack and target lifetime.

Checkpoint 417's cycle fixes are retained, but its SPAS actual-game regression
still needs the appropriate inventory. The current saved mission is knife-only;
no campaign/loadout replacement was performed to force that fixture.

## Corrected receiver check, checkpoint 419

The isolated five-second receiver uses nonzero hand poses and saves publication
receipts independently of image count. One actual-game run passed: press/release
and a neutral tail were recorded, the external witness observed action37 with
Fire8 neutral, and both selected firing copies settled back to idle. Trace,
receiver and watcher exited 0; native hooks, lists and callbacks detached cleanly.
The eight reviewed stereo pairs retain visible scenery in both eyes. Captures
span receiver elapsed 47–422 ms, so they do not cover the complete subsequent
attack animation. No enemy damage, physical jab, death/restart or headset
acceptance is claimed. See [419 handoff](../../handoff/checkpoint419/README.md).
