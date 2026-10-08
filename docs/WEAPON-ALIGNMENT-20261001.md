# Gun alignment, support hand and presence recovery — October 1

Update after the 07:26 UTC headset session: SPAS handling and XM8 grips are
accepted by the user; small orientation polish is deferred. Grenade-launcher
grip is rejected and needs its own interaction work. Recovery/recenter and
projectile impacts are not established by this feedback. See
reports/headset-grips-success-20261001-072621/acceptance.json.

Status at the original source checkpoint: revised candidate awaiting headset test. The 06:37 headset
test rejected the earlier headset-recovery candidate. Normal position and IK
improved, but the right wrist sat below the gun grip, support grip was unclear,
and redonning the headset changed alignment while shots remained above the gun.
The gun did not float away. Exact feedback is saved with that session.

## What changed

- Shared TrackedRig follows the native hand/weapon attachment during equipment
  transition, then locks it after 600 ms and 150 ms of stability. Previously a
  new weapon identity immediately captured the previous gun's still-active pose.
  A reference-space change no longer relearns that attachment from transient
  animation. The native XM8/SPAS switch demonstrates a 9.34 cm attachment change.
- Shared TrackedAimFrame derives the visible weapon orientation from absolute XR
  aim in the common eye/body reference. BC2 supplies its verified XM8/SPAS barrel
  axis (local -Z). An arbitrary initial wrist pose no longer defines gun aim.
  Native shot angles, recoil and spread remain unchanged; this is not a recoil
  removal or projectile-physics change.
- Explicit support grip now seats the published left wrist on its authored
  foregrip. Contact/steering still use the independent physical controller, so
  release, pull-away and tracking loss cannot self-latch the attachment. The free
  hand remains independent. Native animation buffers are never modified.
- The XR host enables XR_EXT_user_presence only when advertised and supported.
  Removal releases controls and invalidates retained images even if the session
  stays focused and poses remain tracked. Return waits 250 ms, then captures a
  shared upright reference. Existing tracking/focus/frame-gap recovery remains.
  The local SteamVR loader advertises the extension; real wear-event behavior
  still requires the user's headset.
- xr-events.jsonl flushes presence/reference events during the session, including
  before an intentional host stop. Hand evidence now retains the last 256 samples
  instead of dropping all evidence after the first 25 seconds.

Manual recenter remains **hold both thumbstick clicks for one second, then release**.
For support grip, place the left controller near the foregrip and squeeze the left
grip; release returns it to independent tracking. A held grip must return to neutral
after tracking loss before it can attach again.

## Evidence and limits

- x86/x64 deterministic suites: 34/33 pass. Added delayed-equipment, retained
  attachment across recenter, and independent ControllerAim versus rendered full
  orientation regressions. Five explicit test-runtime recovery scenarios pass;
  the original OpenXR presentation/controller lifecycle checks pass.
- Weapon transition: native-trace-20261001-071112-554, native-ipc-20261001-071112.
  XM8 -> SPAS -> XM8, 240 pairs, zero timeouts, settled authored grip error below
  0.352 mm; the initial old-pose/new-identity interval is observed directly.
- Supported firing: native-xr-support-20261001-070819 and
  native-trace-20261001-070819-569. Four grabs/releases, 1128 held samples, 328 fire
  samples, 503 pairs, zero timeouts or source/packing/restoration failures.
  One final outstanding pair was canceled at shutdown. Validation now accounts
  for consumed + canceled pairs and only permits this single outstanding request.
  The original strict equality check rejected that successful shutdown; retained
  logs show the original failure. Native shot angular matrices remain unchanged.
- Presence-only removal/return with all poses tracked and session focused:
  native-xr-recovery-20261001-071259 / native-trace-20261001-071300-202.
  Automatic plus manual recenter, three coherent spaces, 430 fresh pairs,
  zero timeouts, maximum resolved wrist error below 0.158 mm.
- Earlier recovery run 070950 had two transient request failures, although
  reference/hand/aim continuity checks passed. Its stricter delivery check remains
  recorded as failed; the isolated 071259 run passed. Do not claim all runs had
  zero delivery misses.
- First support run 070627 exposed an unreachable synthetic foregrip pose.
  Corrected the fixture using a reachable controller placement; did not loosen
  contact or arm-length limits. The character still has finite arm reach.
- The final extra firing check was blocked by its read-only ammunition preflight
  (reserve below 30), before injection. Continue attachment validation without
  firing; do not refill ammunition by memory writes.

All completed native runs left BC2 alive/responding, hooks disabled, and no new
observed crash. No actual projectile-impact or headset-visual acceptance occurred.
The mod is not running at the checkpoint. Preserve the user's desktop focus and
keep BC2 on the left monitor.

## Final no-fire attachment evidence and checkpoint

native-trace-20261001-071537-140 / native-ipc-20261001-071536: 240 pairs,
zero timeouts, three grabs/releases, 730 held samples. Independent matrix checks
put the attached left wrist within 0.263 mm of the native authored foregrip;
free-hand error stays below 0.156 mm. Gun attachment drift across recenter is
below 0.107 mm and barrel direction remains coherent with the reference.
The last native DLL differs from the supported-firing run only by added evidence
fields; no subsequent behavior change was made.

Summary: reports/weapon-alignment-20261001.json.
Source/binary/evidence checkpoint: reports/weapon-alignment-20261001/checkpoint.json.
Earlier checkpoints remain intact, including their recorded headset rejection.

## Next headset acceptance

Use Start-BC2VRSession.ps1 -TwoHandGrip when the user is ready. Check right palm
on both guns, grip/release, aim versus shot direction, headset off/on and manual
recenter. Reserve ammunition is low from the bounded rifle tests; ordinary game
ammo/reload may be needed. Runtime destruction still ends the host; it does not
automatically restart SteamVR.

Deferred: physical inventory, relaxed empty hands and capacitive finger poses,
minor floating red-dot reticle, LOD polish and death crash. No new thumbstick
inventory behavior was added.
