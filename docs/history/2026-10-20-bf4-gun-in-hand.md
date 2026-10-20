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
