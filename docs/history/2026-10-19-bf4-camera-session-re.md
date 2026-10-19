# 2026-10-19: BF4 head tracking built (dormant); session signals located

- Head tracking uses a copied GameRenderer vtable with slot 5 (the per-frame view submit)
  replaced, not a code patch: one restorable 8-byte write on a heap object, no change to
  bf4.exe's code pages. View block 0 is overridden only for the duration of the call and
  restored exactly; the copy is never freed because a call may be in flight.
- The first static note was wrong about GameRenderer slot 19 (`[rcx+0x70]`, not `+0x60`).
  Probes now read getter offsets from the code instead of assuming them.
- Alternate-eye stereo needs a frame-to-eye signal (Renderer.StereoActiveEye) as well as
  the camera: requests are consumed one or two frames after they are made.
- R5 signals come from the game's own status message producer (`+0x10438A0`); keep its
  null-means-multiplayer default. The menus run as level `Levels/FrontEnd/FrontEnd`,
  which the guard must read as the front end, not lock on.
