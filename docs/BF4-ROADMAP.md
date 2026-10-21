# BF4 roadmap

Each milestone ends on evidence, recorded in CURRENT-BF4.md. Unit tests and the emulator
prove plumbing only; headset/gameplay claims need a real session.

| | Milestone | Needs | Exit evidence |
| --- | --- | --- | --- |
| M0 | Plumbing + theater view + laser menus | none (swapchain only) | game visible on a quad in the headset; main menu, campaign select and pause menu operated with the laser; MP lockdown seen in the log; game folder restored |
| M1 | Head-tracked camera (mono) | R3 camera, R5 session | 6DoF head drives the view in campaign; FOV matches the headset; restore on death/level load |
| M2 | Stereo | **3D Vision route (no RE)**, else R2 native or R3 alternate-eye | probe verdict "working"; correct per-eye depth on one mission; comfort check; frame pacing numbers |
| M3 | Controller aim + button reload | virtual XInput pad (no R6; implemented, emulated), R8 weapon transform | locomotion/fire/reload from controllers via the pad; shots land where the controller points on two weapon types; reload on button press, no repeat on hold |
| M4 | Holsters | M3 + inventory slot mapping; native first-person hide signal found (BF4-HOLSTER-IK-RE.md) | draw/stow primary, secondary, grenade from body slots; state survives frames, resets on death |
| M5 | Manual reload | M4 + magazine metadata; research and pipeline in BF4-MANUAL-RELOAD.md (classifier built) | one rifle: remove mag, insert from pouch, chamber; ButtonPress fallback still selectable; then every family by the weapon roadmap (BF4-MANUAL-RELOAD.md section 0: W0 live weapon identity -> W6 optics) |
| M6 | Arm IK, then body | rig bone mapping per build, or the game's own hand IK inputs (BF4-HOLSTER-IK-RE.md) | arms follow controllers without breaking stock animation elsewhere |
| M7 | Vehicles | seat/aim bindings | one ground vehicle and one turret |

Implemented ahead of their bindings (pure VR policy, emulated-tested): M4 body frame +
holster draw/stow with persistent ownership and lifetime reset; the in-headset mod menu;
exact-write journal for M1/M2 camera writes; camera request generation for mono,
alternate-eye and native stereo. M1's native side is built (2026-10-19): renderer and
view-submit bindings enrolled, `ApplyCamera` through a copied GameRenderer vtable
(Bf4ViewOverride), dormant until R5 supplies the campaign/local-server signals. Still
needed for M1-M4: R5, then RequestNativeReload and slot mapping.

Planned now so later work does not reshape the core: `ReloadMode` in settings and menu,
`IReloadRouter`, `IHolsterSystem`, `BodyFrame`, `IArmIkSolver`, `IRigWriter`, and the
capability entries for all of them.

Player settings planned for the mod menu (from the earlier BF4 design, merged
2026-10-18): handedness, snap/smooth turn, turn speed, stick deadzone, view mode
(auto/screen/stereo), screen distance. Also from that design: a HUD layer separate from
the eyes once the camera is head-tracked, decoupled head look vs. weapon aim (`native.aim`),
optics per the optics roadmap, full-body IK per its roadmap.

Also queued: signed manifests (minisign/Ed25519 via a bundled verifier) before the update
channel is advertised widely; currently HTTPS + SHA-256 per file.
