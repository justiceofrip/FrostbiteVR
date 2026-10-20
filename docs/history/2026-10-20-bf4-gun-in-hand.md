# 2026-10-20: BF4 gun in the hand

- BF4's first-person rig is moved to the right controller on the GPU side: the first-person
  pass's own view constants (b2) are replaced at bind time by a constant buffer of ours with
  the head-tracked render's projection and a view composed with hand x grip-at-rest^-1. Chosen
  over the native routes: the weapon offset data are shared assets (never written), BF4's
  weapon lag is a rotation only, and the first-person pose is animation in camera space, while
  this route writes nothing of BF4's and moves the whole rig rigidly at true size.
- The pass is recognised by its view rotation being exactly (within ~4.5 deg for recoil and
  camera shake) a game camera rotation the view override recorded for a recent submit, with a
  projection not at the render's scale; the FOV alone does not identify it (aiming down sights
  changes it). The hand used is the one recorded with that same submit.
- "In the hand" is the default gun mode; the turned render ("Gun follows aim: On") stays as an
  option. Aiming down sights keeps the gun in the hand by holding the receiver's hip pose
  while the game's FOV is zoomed. Known gaps: world-particle muzzle effects at BF4's own muzzle,
  BF4's crosshair at the head's centre, one rifle's grip calibration, no arm IK.
- BF4's in-level freezes (4-11 s) come from its own DirectInput controller re-enumeration on
  device changes: per attached controller it runs a WMI scan of every PnP device on its main
  thread (5.5 s here). While the adapter runs, game-controller enumerations are answered with
  no devices (in front of the Steam overlay's vtable entry, restored exactly): BF4 reads pads
  through XInput, so only DirectInput-only devices are given up during VR play.
- Superseded the same day: answering those enumerations empty also took the VR pad from the game
  (BF4 makes its XInput devices only from them; inside BF4 the Steam overlay lists its own virtual
  pads, 28DE:11FF, and answers the WMI scan for them as XInput pads, which is how the VR pad had
  reached user 0). Instead BF4's `IsXInputDevice` is answered without WMI by a code hook on an
  enrolled binding (raw-input names plus Steam's pads), the enumerations pass through, and a
  stand-in pad is offered first when none of the devices is an XInput pad (BF4 polls only one:
  offered last it was made but never polled). Live: pad polled, no stall.
- 3D by alternate eyes: the eye is chosen per frame on the render thread and applied to the
  frame's main-view constants and its first-person pass (camera half the eye distance to the
  side), not to the CPU camera per submit: BF4 draws the first-person rig into the G-buffer
  before the frame's main view, and a render frame's submit is not predictable (1-4 behind the
  newest, sometimes skipped), so per-submit eyes put the rig's G-buffer draw at the other eye.
  Pairs take the other eye's last kept frame turned onto the new pose. An option, off by default.
- Arm IK by palette rewrite (arms from the body, default): the shoulders go back to the body
  while the gun and the hands stay at the controller, by rewriting the arm bones of each bone
  palette BF4 binds in the first-person pass (GPU side, nothing of BF4's written). Chosen over
  BF4's own hand IK inputs (unbound research) because the palettes carry everything needed: the
  arms' bind-space joints are fixed, so each frame's chains and every palette's arm bones are
  found by the joints they map, whatever order a mesh keeps its bones in. A per-pass vote over
  chains near the camera picks the player's arms over the weapon palettes' stale copies.
- Reads of BF4's upload ring use SSE4.1 streaming loads: it is write-combined memory, where
  plain loads cost ~20x more (61.9 against 3.1 us per palette; the arms cost 20 % of the frame
  rate with plain loads, none measurable with streaming ones).
