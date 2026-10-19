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
