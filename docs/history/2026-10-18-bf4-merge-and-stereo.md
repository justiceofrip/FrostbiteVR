# 2026-10-18: BF4 designs merged; stereo gates found

- Two BF4 designs had been started in parallel (local files and the claude.ai overlay).
  Decision: the overlay is the base (built, tested, run on the real game); the earlier
  design contributes VrPanel/GdiPanelRaster (mod menu rendering), capability evidence
  levels, the XInput pad input plan and camera leads. Duplicates removed; originals in
  `reports/bf4-earlier-design-archive.zip`.
- bf4.exe stereo: split-screen stereo is read-only on PC; the 3D Vision route is gated on
  `RenderDevice.StereoEnable` at render-device creation (user.cfg too late). Detail:
  BF4-STEREO.md.
- Do not start bf4.exe directly with arguments: the EA stub forwards them to Steam, whose
  confirmation prompt then blocks later launches.
