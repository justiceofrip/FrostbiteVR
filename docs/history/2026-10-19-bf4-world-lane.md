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
- Stereo next: BF4's split-screen stereo route (render device `+0x2738080`, settings
  `+0x80`, read-only flag `+0x7E`) needs its per-eye geometry found before use.
