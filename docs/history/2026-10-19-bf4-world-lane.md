# 2026-10-19: BF4 head tracking goes through the world lane

Decisions worth keeping:

- AER-style reprojection of one flat frame is not VR (rejected); head tracking renders the
  game's own camera at the tracked head pose and both eyes come from the same native
  frame (mono until a native per-eye route is verified).
- The BF4 world lane reuses the framework's frame lane (`D3D11FrameBridge`, one request in
  flight) instead of a BF4-specific transport. The camera applies only poses the host
  asked for (a lease), tagged with an id; a presented frame is labelled with the lease's
  tracking only after `settle` consecutive submits carried that id. Never label pixels
  with a pose they were not rendered with; cancel instead.
- The render FOV is written in the submitted view block exactly as BF4's FOV setter does
  (`+0xB4`, `+0x250`, dirty `0xF`) and restored exactly; eyes are asymmetric crops of the
  one render.
- The world lane must never leave the headset black: it runs only while world views are
  submitted and falls back to the theater panel after 2.5 s without a pair.
- Stereo uses BF4's own split-screen two-eye render (both eyes in one native frame),
  switched on at runtime by an exact-write of its two settings flags and driven to the
  headset's eye distance; its geometry was derived from the projection builder before
  any write (BF4-STEREO.md). Rejected: submitting two views through slot 5 (each view
  renders to its own target object) and alternate-eye frames (two simulation frames).
- BF4's camera space is right-handed (w = -z projection), so headset poses go in without
  the Z mirror the generic Frostbite helper applies; the mirror inverted yaw and pitch in
  the first sessions. The FOV override also sets the view's default FOV so BF4's
  weapon-size compensation (`+0x258`) does not enlarge the gun in VR.
- Head-tracked update rate: two independent frame channels (host `--frame-channels 2`,
  `PipelinedFrameProvider`; adapter FIFO over two bridges) instead of widening the
  single-slot protocol. Each pair stays labelled with its own request's pose; the
  second request is staggered (0.7 of a typical completion) so its pose is not stale.
- Controller aim drives BF4's own mouse look from outside (no game writes). Its steering
  was chosen by simulation, not by the emulator: half the error per frame with two
  frames of counts treated as in flight, a deliberately high first gain estimate (an
  under-estimate overshoots, an over-estimate is only slow), learning weighted by the
  angle turned, and an overshoot guard. The emulator's ideal timing had hidden both an
  unstable step and a re-anchoring bug at lease gaps.
- Stereo has a second route that keeps "both eyes from one native frame": depth-image
  stereo from the game's own depth buffer, found through its ClearDepthStencilView calls
  (each lazily resolved d3d11 implementation hooked) and copied before the first-person
  clear. BF4's split-screen stereo stays developer-only until the freeze is explained.
- The projection aspect cannot be narrowed to the eyes (the view setup takes it from the
  output size), so the corner HUD is shown through insets until the UI pass can be
  rendered separately.
- Live checks without a headset use a developer OpenXR runtime (`FvrSimOpenXr`, 90 Hz,
  poses and buttons from a control file, eye snapshots) under the real FvrXrHost and the
  real game. Its pose-label check (head flipping between two poses every frame, pixels
  classified against labels) is the evidence standard for frame sync: 751/751 pairs
  matched at frame sync 1 and 2 in the prologue; the default stays 2 until a headset
  agrees.
- The aim overshoot guard judges only sign changes the game made (the target moved less
  than the game in that update). Clearing it on fast target motion was rejected after
  modelling: it disabled the guard on moving targets, which then ran away.
- BF4 draws the first-person gun into its main (4x MSAA) depth buffer with one clear a
  frame, so 3D from depth reads the buffer at Present; the pre-clear copy is kept only for
  games that clear twice.
- BF4's split-screen two-view stereo is retired as unsupported: reproduced live on the
  simulated headset, it is a crash (not a stall) because the PC world renderer is built
  for one view (one-element view-renderer array, one 0x34000-byte view block in a
  0x34400-byte object). Stereo now means 3D from depth.
- The gun is aligned with the bullets by rendering along the aim, not by moving the gun:
  BF4 draws its first-person gun at rest relative to the render camera (live experiments),
  so the frame is rendered with the camera turned to the game's aim at the head's position
  and each eye ray is rotated back to the head (exact for a rotation). The turn is latched
  per pose and limited by a FOV budget (lens-ellipse coverage).
- Three frame channels by default: +18 % eye pairs at frame sync 2 (44.8 -> 52.9/s, close
  to the game's ~59 limit there) for ~6-14 ms more request latency; fresher pairs were
  judged worth more than the latency the compositor's reprojection hides.
- Past the FOV budget the aim is held back instead of the gun: the controller's aim target
  is limited to the cone the render can turn to all the way, so the barrel stays on the
  bullets' line and stops at that edge (turning the head brings more into reach). The
  partial follow it replaces let the bullets leave the barrel (live: 15 degrees up with the
  head level, the gun a third of the way).
- BF4's own menus in a level (pause, death, mission end) are recognised by the system
  cursor being shown while the game window is in front (Win32 only, no game memory; live:
  shown in the pause menu, hidden in play) and go to the theater screen, where the laser
  works; controller aim stops sending mouse look meanwhile.
- The both-sticks recenter is recognised by the BF4 adapter itself (the shared host takes
  it only in roomscale mode, which BF4 does not use); a chord of both stick clicks never
  reaches the game, while a lone click still does after 80 ms or as a tap.
- The controller aim's mouse-gain sign changes only after a quiet probe: when the game
  turns against one-signed counts, aiming stops for a moment; a game that keeps turning is
  moving by itself (death camera, cutscene) and teaches nothing, one that holds still has
  inverted look. A window-only rule cannot tell the two apart (live: a death camera
  flipped the pitch sign and the guard then ran the yaw gain to its clamp).