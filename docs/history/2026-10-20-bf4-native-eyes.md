# 2026-10-20: BF4 native stereo, each eye drawn by BF4

- The player wants native VR (each eye rendered from its own position every frame) and rejected
  stand-ins (depth reprojection, alternate eyes, the NVIDIA driver route). Those are out of the
  menu; "3D: Native (each eye)" is the default.
- Route: BF4's simulation and its world build run once per frame; the world renderer's draw
  (vtable slot 3, render thread) runs twice, each with the main views' constants moved to one
  eye. Doubling the build (slot 2) or the GameRenderer view submit was a dead end: BF4
  double-buffers per-view state per build, and a submit only builds data.
- A draw uses up a few per-frame fields; the second draw gets back only fields verified in the
  code that uses them (`+0x1DE24F8 +0x200`, `+0x1DE2420 +0x108`), restored exactly after.
  Putting back every field a draw cleared corrupted thread stacks, re-ran job continuations and
  handed out pool chunks still in use; a generic re-arm is not safe in this engine.
- The HUD is drawn once (after the second draw) and copied onto the other eye where it changed
  the presented frame.
