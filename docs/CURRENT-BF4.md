# BF4 adapter: current state (2026-10-19)

The only BF4 state document; overwrite it when the state changes. BC2 state stays in
[CURRENT.md](CURRENT.md). Start with [HANDOFF-BF4.md](../HANDOFF-BF4.md); stereo detail in
[BF4-STEREO.md](BF4-STEREO.md). Branch `bf4/vr-adapter` (pushed; no PR or release).

## Evidence levels (same vocabulary as CURRENT.md)

- **Actual-game check**: unattended runs on the real bf4.exe (build 179547, sha256
  `9d9d1a3f...c6c2aa`), AMD RX 9070 XT, Steam -> EA app launch, campaign only, no headset.
- **Emulated**: Bf4EmuHost (WARP or GPU), fake OpenXR runtimes, launcher emulated sessions.
- **Headset acceptance**: theater mode (game screen on a virtual screen, laser on the
  menus) works in the player's headset (2026-10-19, SteamVR). Head tracking was seen in
  three sessions (09:16-10:19; rotation was inverted and the gun oversized, both fixed in
  0.4.16); everything built since 0.4.17 is unseen in a headset.

## At a glance (installed: 0.4.41-local)

| Feature | State | Evidence |
| --- | --- | --- |
| Theater screen + laser menus | on | headset |
| Head-tracked campaign view (6DoF head) | on | headset (earlier build); rate work emulated |
| Two frame channels + next pose in the capturing frame | on | emulated |
| Controller aim (mouse look), aim dot, two-handed aim, snap turn | on | emulated (delay/gain/gap cases) |
| Comfort vignette, walk toward body/gun | on / body | emulated |
| Holsters (keys 1/2/3, G, F) with pulses | on | emulated |
| Manual reload (magazine gesture; B tap/hold) with markers | on (player setting) | emulated |
| HUD corners (ammo, minimap insets) | on | real frame rendered offline |
| Physical crouch | off | unit-tested |
| 3D from depth (depth buffer) | off | WARP checks |
| BF4 split stereo ("3D depth", developer) | off | freezes live; watchdog armed |
| Hands / IK, gun at the hand | not built | needs live RE |

## Works on the real game (actual-game checks)

- Session-scoped `dxgi.dll` proxy loads the adapter inside bf4.exe; Present/ResizeBuffers
  hooks adopt the 1920x1080 fullscreen swapchain (format 28). Launcher restores the game
  folder, `user.cfg` block and profile key after every run (11 runs, no leftovers).
- `user.cfg` is executed (PerfOverlay.DrawFps visible in snapshots).
- **Menu pointer**: the same PointerEvent -> Win32PointerSink path the VR laser uses drove
  BF4's real menus (Campaign tab -> Battlefield 4 Campaign -> Resume) into a level.
  `GameMenuPointer` is now Enabled with that evidence.
- nvapi64.dll shim + core NVAPI emulation answer every public query BF4 makes.
- Developer research channel (developer sessions with `research_dump_image` only):
  snapshots, clicks, `dumpimage` (decrypted bf4.exe image for offline RE), background
  vtable scan, read-only `peek`. It never writes game memory (a `poke` command was removed
  after the policy guard blocked it).

## Head tracking: the world lane (2026-10-19, built; seen in a headset)

In campaign levels the headset shows the game's world from the player's tracked head
(6DoF, both eyes from the same native frame; mono unless 3D from depth is on) instead of
the theater screen. Pieces (all on the analysed build only):

- `Bf4FrameworkGlue.cpp` `TheaterLink` carries both lanes. World: the menu lane says
  Gameplay, so FvrXrHost asks for eye pairs on the frame lane (`D3D11FrameBridge`, the
  same transport BC2 uses; controller samples now arrive through it). Each host request
  (a lease) is taken at the start of a Present; its head pose goes to the camera tagged
  with a pose id.
- `Bf4ViewOverride` (slot 5 of a copied GameRenderer vtable, exact restore) now also
  writes the FOV both eyes need (`+0xB4`, derived `+0x250`, dirty `|= 0xF`), exactly as the
  game's FOV setter `+0xB89EE0` does, and counts how many submits in a row carried each
  pose id. FOV/aspect offsets are from the setter and the view setup `+0x848B00`
  (BF4-CAMERA-RE.md).
- `Bf4WorldView.h`: a frame is captured only once `settle` submits (default 2, mod menu
  1-4) carried the lease's pose, so a pair is never labelled with a pose it was not
  rendered with; a lease that cannot be met in 140 ms (170 ms with two channels) is
  cancelled. Each eye is the part
  of the frame its FOV covers (asymmetric crop), resampled to the host's eye size by
  `Bf4EyeResampler` (game device, own device-context state, raw bits kept).
- The world lane runs only while the game submits world views (loading screens stay on
  the theater panel) and falls back to the theater panel when no pair is published for
  2.5 s (retry after 5 s). The mod menu (menu button) always opens on the theater panel and
  has "In campaign levels: Screen / Head-tracked view".
- Recentring: the first head pose of the session faces forward; mod menu "Recenter view".
  With controller aim the view is anchored to the body heading (right stick); without it,
  game yaw is kept and pitch/roll/position come from the headset.
- Evidence: unit tests (crops, decisions, fallback, FOV write and exact restore, pose
  runs, lease-only camera); Bf4Emulated world phase (the emulated host receives pairs and
  each eye carries its own crop); `Run-Bf4TheaterXr.ps1 -World`: the real FvrXrHost on the
  fake OpenXR runtime submitted 165 pairs from the emulated game, 0 rejected, 0 runtime
  errors. Not yet seen in the real game: the settle depth (main thread vs render thread),
  BF4 accepting the wider FOV, pair rate in game.
- Frame shape (worked out, not yet seen): the frame is rendered tall enough for the
  taller eye (+3 %) at the window's 16:9, so it is about 1.7x wider than the two eyes need
  (Quest 3 numbers: frame tangent ~2.35 across vs ~1.38 seen). The outer part of each side
  is never shown: BF4's corner HUD (ammo bottom right, minimap bottom left) falls outside
  the eyes, and about 40 % of the horizontal pixels are spent there. A narrower projection
  aspect would fix both, but the view setup takes the aspect from the output size after
  copying the submitted block (BF4-CAMERA-RE.md), so writing it there has no effect and the
  renderer's own copy has no clean restore point. Open: a HUD route (render the UI pass
  separately) or a game window closer to the eyes' shape (the laser needs 16:9 menus).
- HUD corners (mod menu, default on): meanwhile the resampler copies the frame's bottom
  corners into framed insets inside the visible lower corners, at the same view tangents in
  both eyes, above the vignette. Measured on the 10:21 session's frame snapshot (16:9): ammo
  panel u 0.865-0.995 v 0.835-0.985 -> tangents x 0.38..0.82 y -0.64..-0.93; minimap with
  objective text u 0.008-0.142 v 0.705-0.985 -> x -0.82..-0.43 y -0.50..-0.96 (aspects kept;
  inside both eyes of a Quest 3, checked by rendering the real frame through the resampler).
  That session's eye snapshot confirms neither reached the eyes before. WARP check: a corner
  panel outside the eyes appears in the inset and nowhere else.
- Known gaps: mono until engine stereo engages (below); pair rate is bounded by `settle`
  frames per pair (two frame channels remove the request round trip, below), so the world
  updates slower than the headset (the compositor reprojects head rotation every frame); HUD and
  pause menu are drawn into the wide frame (pause menu: use the pad, or switch to Screen);
  first-person weapon not yet re-projected.

## Two frame channels: higher head-tracked update rate (2026-10-19, built; emulated only)

- One frame channel holds one request: after each pair the host had to see it, give
  feedback and ask again before the game could take the next head pose (a request round
  trip of 1-2 game frames on top of the `settle` frames). The launcher now starts
  FvrXrHost with `--frame-channels 2`: the host alternates two independent channels
  (`PipelinedFrameProvider`, own textures and ticket sessions, single-slot protocol
  unchanged) and the adapter's `TheaterLink` serves two bridges as a FIFO, so the next
  request is already queued when a pair is captured. Each pair is still labelled with its
  own request's pose (oldest request first; its pose goes to the camera).
- Next pose in the capturing frame: the camera's pose used to be set at the start of each
  Present's frame work, before that Present captured, so after a capture the next queued
  pose waited one more frame and one submit carried a pose already captured. The core now
  asks the link for its current request right after publishing and, if the front changed,
  sends that pose to the camera in the same frame (`earlyPoses`): 2 Presents per pair at
  settle 2 instead of 3. The labelling rule is unchanged (a capture still needs `settle`
  submits of the pose). Emulated test.
- Emulator note: with two channels the queued request (usually channel 2) can outlive its
  200 ms in Bf4EmuHost, where each capture is a WARP software render (25 of 139 timed out
  in one run; pairs per run vary 76-237 with the machine's load). Not expected on a GPU.
- Staggering: one channel begins per XR frame and only once the other request is 0.7 of a
  typical completion old, so the queued pose is not much older than a fresh one. If both
  complete in one XR frame the older is shown and the newer held one frame (never
  dropped). Menu/recenter hands a held pair back as not consumed.
- Requests live 200 ms with two channels (one waits behind the other); the adapter cancels
  a queued lease after 170 ms (140 ms with one channel). The host keeps both channels'
  shared textures open (two-entry cache), so alternation never reopens them.
- Second token: `logs/xr-channel-2.txt`, written before `xr-channel.txt`. An adapter or
  host without it falls back to one channel. Host report: `frame_channels`,
  `channel2_*`, `pipelined_held_pairs`. Adapter log: `theater_link ... bridges=2 queued=`.
- Evidence: `PipelinedFrameProviderTests` (real shared-memory channels: one begin per
  frame, older pair first and the newer held, stagger, suspend hands back the held pair,
  input on channel 0); `Run-Bf4TheaterXr.ps1 -World -FrameChannels 2`: 237 pairs (119 +
  118), 0 rejected, vs 47 with one channel in the same emulated run (untracked capture,
  so this measures the transport, not the settle frames). Not yet seen in the real game.

## Holsters and magazine reload as gestures (2026-10-19, built; emulated only)

- `Bf4BodyGestures.h`: input-only (no game memory). Body-relative holsters (the
  framework's `BodyFrameEstimator` + BF4 layout): over the right shoulder = primary (key
  1), right hip = sidearm (key 2), over the left shoulder = first gadget (key 3; the VR
  layout has no D-pad, so gadgets were unreachable before), left hip = grenade (grab, let
  go away from the hip to throw: G), chest with the left hand = knife (F), belt front
  left = magazine. A gadget in hand has no VR magazine: the gun's waits, fire is free. Keys are BF4's
  default PC bindings, sent as scan codes only into the focused game window, held 80 ms,
  and released on every outcome (key change, focus loss, detach). A grip press that starts
  in a holster is the gesture's, not the pad's; with holsters on, the right (gun) grip is
  never the pad's grenade button. Short controller pulses go to the XR host as feedback
  events on frame channel 1 (its haptic policy checks packet, space and deadline).
- Manual reload (Reloading: Manual) is now the magazine gesture when the virtual pad is
  usable: a tap on right B drops the magazine of the gun in hand on release (primary and
  sidearm tracked separately), fire (pad RT) is held off until one is seated, the left hand takes one from
  the belt and seats it at the magazine well (7 cm below, 6 cm ahead of the right grip,
  12 cm radius), which presses the pad's reload (X) so BF4 plays its own reload. Letting go
  before the well drops it. Holding B (0.4 s or more) holds the pad's X for as long as B
  is held: BF4's reload, or its interact / pick-up when something is in reach (the same
  pad button), so manual mode loses no game function; nothing is dropped by a hold. A magazine pushed into a gun
  that still has one is a tactical reload (same press; log `tactical=`). Custody persists across
  packets, menus and tracking loss; a level start is the lifetime transition. BF4's own
  ammunition and auto-reload are untouched (nothing native is acknowledged or written).
- Feedback without visuals of the gear itself: a light pulse when an empty hand reaches a
  holster it could use now (not the gun already in hand; the belt only while the gun is
  empty), and two markers drawn into the eye pair: an amber ring at the belt pouch while
  the gun has no magazine, a green ring at the magazine well while one is in the hand.
  Each eye places a marker by its own pose (`MarkerInEye`), so markers have true depth
  over the mono world. Log: `marked_pairs=`.
- Physical crouch (mod menu, default off; standing play): crouching 35 cm below the
  self-calibrated standing height pulses BF4's crouch toggle (pad B), standing back above
  20 cm toggles it off (`Bf4PhysicalCrouch.h`). The stance is believed, not read: it follows
  our pulses, the player's own crouch button (which is never overridden until the body
  changes posture) and BF4's own stand-ups on sprint and jump. Unit-tested; status
  `crouch=... crouch_toggles=`.
- Mod menu: "Body holsters: Off / On" (default on). Status/log: `gestures gun=...
  magazine_out=... draws= throws= knives= ejects= grabs= seats= drops= quick_reloads=`
  (logged at each change), plus `key_sends` / `haptics_sent` every 600 frames.
- Evidence: unit tests (each holster, put-back vs throw, knife hand, grip held at engage,
  holsters off; eject/hold fire/persistence/drop/seat/per-gun/quick reload/level start)
  and an emulated campaign through the whole core (keys reach the platform and go up,
  pulses, RB/LB not pressed by gesture grips, RT held off, X on seating); hover pulses and
  marker placement/parallax unit-tested; the WARP check draws a marker ring. Not yet seen in
  the real game: the default key bindings, whether BF4 mixes keyboard selection with the
  pad, holster positions for the player's body.

## Controller aiming (2026-10-19, built; emulated only)

- `Bf4MotionAim.h`: in the head-tracked view the gun follows the right controller through
  BF4's own mouse look (input only, nothing written to game memory). The headset view is
  anchored to a body heading that only the right stick turns (`HeadTrackedViewAtYaw`;
  ~150 deg/s); each frame the game's aim, read from the camera it submits (view block
  `+0x40`, read before the override), is steered toward body heading + controller
  direction by relative `SendInput` mouse counts (only into the focused game window).
  Counts-per-radian are learned per axis from what the game actually turned (sign
  included, so inverted look and aim-down-sights sensitivity are handled). If counts
  produce no turn (pause menu, cutscene) it goes quiet for 60 frames.
- The virtual pad's right stick is released while it aims (no stick look), and the left
  stick is turned back by the aim's offset so walking stays relative to the body.
- Walk toward (mod menu): Body (default) turns the left stick back by the aim's offset so
  walking follows the body; Gun leaves the stick alone. Vehicles: the left stick is
  throttle and steering there and the adapter cannot tell a vehicle from walking yet, so
  Body would mix them while the gun aims off the body heading; switch to Gun in vehicle
  sections (emulated test through the menu).
- Two-handed aim (mod menu, default on): while the left grip is held 12-80 cm in front of
  the right grip and within 35 degrees of the right controller's aim (60 to keep it), the
  gun aims along the line between the grips, steadier than one wrist. Its grip is then not
  the pad's spot button; a grip carrying a magazine or grenade never counts. Unit-tested;
  log `two_handed=` (engagements).
- Comfort vignette (mod menu, default on): while the left stick moves the player, or the
  body turns smoothly, the eye resampler darkens each eye beyond ~35 deg from its centre
  (dark by ~50 deg), in proportion to the stick and fading in fast / out slower
  (`ComfortVignette`). Unit-tested; the WARP check draws it.
- Mouse steering is now robust to the game's input delay: half the remaining error per
  frame, with the counts of the last two frames treated as in flight. The old step (0.7,
  no in-flight term) was stable only for the emulator's two-frame lag; with three frames it
  diverged (the aim would swing ever wider). Chosen by simulation (delays of 1-3 frames,
  gain off by +-40 %: settles in 7-24 frames, overshoot <= 22 %) and checked by an emulated
  test at three delays, which fails with the old step.
- Mouse gain: the first estimate is deliberately high (0.003 rad/count): a game more
  sensitive than believed overshoots every step, a less sensitive one only steers slower
  until learned (simulated: no overshoot for true gains 0.0002-0.003). Learning trusts a
  window by how far the game turned (0.2 rad: fully) rather than by counts, so one aim
  step learns the gain; learned gains survive re-engagement. Overshoot guard: when the
  error swings past zero by more than 30 % of the swing before it, the estimate grows by
  that ratio at once (a threefold sensitivity jump, e.g. leaving aim-down-sights, settles
  without running away; emulated test at two delays). Log: `guards=`.
- Aiming stays engaged across short gaps between world-lane leases (500 ms hold). Before,
  each frame without a lease released it, and re-engaging re-anchored the body heading to
  the game's aim (a view jump by the controller's offset) and reset the learned mouse gain;
  the emulated test had held a lease every frame. Regression test with a lease gap every
  third frame (fails without the hold).
- Snap turning (mod menu, default on): the right stick turns the body heading in 30 degree
  steps (one per push past 0.7, re-armed under 0.3); off: smooth ~150 deg/s. The game's aim
  follows through the mouse within a frame or two; the head view jumps with the heading.
  Unit-tested; log `snaps=`.
- Mod menu: "Aim with: Right stick / Right controller" (default right controller).
  Status/log: `controller_aim=... gain=... mutes=... mouse_sends=...`.
- Evidence: unit tests (convergence onto controller yaw/pitch, gain learning with signs,
  body turning, release, stall guard) and an emulated campaign session with a one-frame
  input delay (core + fake game). Not seen in the real game: whether BF4 mixes mouse
  look with the virtual pad, vehicles, recoil interplay.
- The gun is still the game's first-person model at the game camera (it points where the
  controller points); hands/IK are separate work.
- Aim dot (mod menu "Aim dot", default on): BF4 draws its crosshair at the centre of the
  frame, which is now where the head looks, not where the gun aims. While controller aim is
  engaged the eye resampler draws a green ring and dot where the game's aim (its own camera,
  read before the override) crosses the rendered head view (`AimInView`, `ReticleAt`;
  ~0.6 deg, same direction in both eyes of the mono frame). Not drawn in split 3D. Evidence:
  unit tests (geometry) and a WARP GPU check in Bf4Emulated (ring and dot drawn at the aim
  point, frame untouched elsewhere, plain pass unchanged). Log: `reticle_pairs=`.

## 3D from depth (2026-10-19, built; emulated only; opt-in)

Native split stereo still freezes (below), so a second route gives depth without a second
render: both eyes are cut from the one mono frame and each is shifted by its own parallax
using the frame's depth buffer (depth-image-based rendering; no simulation or render replay).

- `Bf4DepthCapture`: finds the frame's main depth buffer through the game's own
  `ClearDepthStencilView` calls (MinHook detours on d3d11.dll's implementations; the
  calls always proceed): the largest single-sample depth target with the backbuffer's
  aspect. The D3D11 runtime resolves a context's vtable entries lazily and immediate and
  deferred contexts have their own implementations, so the install resolves them first
  (one clear of a 4x4 target of ours on each) and hooks each implementation it sees,
  re-checking at every Present. Just before each clear of the main depth a copy is recorded
  on the same context (in order, even on deferred contexts). Frostbite clears depth again
  before the first-person weapon (BC2 does), so with two or more clears a frame the copy
  from before the last clear holds the world; with one, the live buffer is copied at
  Present. A target picked before the backbuffer's shape was known is dropped once it is.
- Eye resampler: each eye samples the frame at its own tangent + b/z (b = signed half eye
  distance, z = view distance from the depth with the view's near/far, +0xBC/+0xC0, read
  live as 0.06 / 20000; reversed depth when the buffer clears to 0), by a four-step
  fixed-point search; nearer than 0.4 m counts as 0.4 m (the weapon, about where a VR gun
  is held; nearer would mean a very strong per-eye shift). The aim dot is drawn where the source point under the aim lands in each eye, so it sits on the surface it covers (no double image on a near target); gesture markers keep their own per-eye placement.
- Mod menu "3D from depth: Off / On" (default off). Log: `depth_capture installed= main=WxH
  format= clear= split= ... msaa_clears= hooked=` every 600 frames, `depth_pairs=`; developer sessions
  write `vr-snapshot-depth.bmp` (near bright, far dark) the first time it works and with
  the other snapshots.
- Evidence: linearisation and per-eye shift signs unit-tested; WARP checks in Bf4Emulated:
  near content shifts each eye its own way while far content stays (shader), and the real
  detour picks a 16:9 depth over a larger square shadow map, detects two clears a frame,
  keeps the depth from before the second clear, and uses the live buffer with one clear
  (that check found the lazily resolved vtable entry and the shadow-map pick before the
  game did). Multisampled depth (BF4's deferred MSAA) is copied as it is and read at
  sample 0 through a Texture2DMS view (pixel shader now ps_4_1: SM 4.0 cannot declare a
  Texture2DMS without a fixed sample count; the WARP check caught that compile failure,
  which would have disabled the world lane); WARP checks for a 4x MSAA depth in the
  shader and in the capture. Not yet seen in the game: which target BF4 clears, its
  format, whether the weapon pass clears depth, how the parallax looks.

## Stereo: BF4's own two-eye render in the world lane (2026-10-19)

- **First headset session (0.4.13-local, 09:16)**: head tracking worked (world lane,
  ~22 pairs/s at ~97 game fps, FOV written 1.947 rad). 3D engaged as designed
  (`mode=1 on=1`, half eye 31 mm) but looked strongly zoomed and the game died within
  seconds (BF4's own handler, no Windows crash report).
- **Crash cause (static, confirmed)**: the two-eye loop `+0x8CAF70` appends one 0x1560-byte
  entry per eye to an array (object `+0x1570/+0x1578/+0x1580`) whose inline storage at
  `+0x1590` holds exactly one entry, with no bounds check; the second eye overwrote the next
  0x1560 bytes of the object. Fix: `Bf4EyeLoopOverride` points that object at a copied
  vtable (16 slots) whose slot 2 (`+0x8BE430`, the only way the loop is reached) gives the
  array room for 8 entries for the length of each call and restores the three pointers
  exactly afterwards. The object is found by a background read-only scan (class vtable
  `+0x1D24258` with its array in inline storage; exactly one expected), re-checked right
  before the one write. 3D is refused unless this override is in place.
- **Zoom cause**: in split mode the view's aspect is an eye half's (the view setup takes
  it from the output's `+0x60/+0x64` size), not the frame's; crops, the render FOV and the
  eye distance assumed the frame's (inferred from the zoom; the stereo probe now logs that size). Now the adapter reads the aspect the engine applied
  (`[renderer+0x60]` view 0 `+0xC4`) and uses it for all three.
- **Second headset session (0.4.14-local, 09:46)**: no crash, but the game stopped
  presenting ~2 s after 3D engaged (eye-loop override installed, side-by-side render
  confirmed, then no Present; the headset went black). The head-tracked mono view before
  it was reported "way too zoomed in" and a "motion sickness machine"; the pair rate was
  23-32 Hz at 100-200 game fps, render FOV 1.947 rad and view aspect 1.778 as intended.
  `waitRenderSync` (`+0x8288B0`): the main thread waits after every submit, so it runs
  at most one frame ahead and the settle-2 labelling rule holds. The zoom is not
  explained yet; the projection is rebuilt from the written FOV (view update `+0xB8C180`
  bit 1 -> `+0xB8CFC0`, matrix at view `+0x320`).
- **Third headset session (0.4.15-local, 10:19)**: the player found the cause of the
  sickness: every head rotation was inverted (look left -> turned right, look up -> looked
  down). Snapshots (`vr-snapshot-frame.bmp`, `vr-snapshot-eyes.bmp`) and the probe showed
  the renderer's view at FOV 1.95 rad with the real world camera position during head
  tracking, eye crops consistent with that FOV, and the gun/HUD drawn far too large.
  Fixed in 0.4.16-local:
  - Handedness: BF4's view space is right-handed (projection builder `+0xB7EAE0`: w = -z
    when view `+0xD9` is set, which it is live); camera rows are right, up and +Z behind
    the camera, as in OpenXR. The head pose now goes in unmirrored
    (`AdapterCore::XrPoseToBf4Camera`; unit test: head left/up turns the camera left/up).
  - Gun size: view update bit 4 (`+0xB8C910`) sets `+0x258 = tan(fov/2) /
    tan(defaultFov/2)` from the default FOV at `+0xB8`, which keeps the first-person gun
    at its normal on-screen size; with the 111 degree render it was ~3.5x too big in the
    headset. The override now writes the FOV to `+0xB8` too (restored exactly), so the
    scale is 1 and the gun is at true size. The HUD is screen space and stays magnified
    by the crop (~2.3x).
  - Split stereo image shift sign follows the right-handed view (left eye -separation).
  - Resume click: the logged press/release drifted 63 px (1920 wide), beyond the 2 % lock;
    the lock is now 6 %.
  - 3D depth now defaults to off (the split render still freezes the game).
- Evidence tools for the next session (developer): `vr-snapshot-frame.bmp` (the game
  frame) and `vr-snapshot-eyes.bmp` (both eye images) 5 s into head tracking and on every
  "Mark this moment for feedback"; a watchdog that logs every thread's stack (module +
  offset) after 4 s without a Present (Bf4Diagnostics; emulated end to end); a fault log
  (`faults.log`) armed while 3D is on; each laser click logged with its client position.
- Crash guard: a marker written while 3D is on and removed when it goes off; a session
  that starts with the marker present starts with 3D off (the mod menu can switch it on).
- The rest is as built: `Bf4EngineStereo` sets StereoEnable/StereoSplitScreenEnable and
  the stereo values through an exact-write journal and keeps the engine's eye distance at
  the headset's; 3D starts 10 s after head tracking; crops come from the halves with the
  engine's image shift removed; no capture until the layout is stable.
- Not verified: whether anything else in the split render has one-eye capacity on PC
  (the next session's log shows `eye_loop ... max_entries` and survives or not), HUD,
  cost, depth comfort. Per-eye horizontal resolution is half the backbuffer's.

## Headset path (emulated end to end)

- Theater link (`Bf4FrameworkGlue.cpp`, `FVR_BF4_FRAMEWORK_GLUE` ON): the game screen goes
  to the XR host on the existing menu lane (MenuChannel + D3D11MenuProducer), labelled
  1280x720 logical (16:9, the host's verified pointer mapping); the host's MenuControl
  comes back as the game pointer. The host writes `logs/xr-channel.txt`; the adapter
  connects whenever it appears.
- `FvrXrHost.exe` = the shared OpenXR host (BC2XrHost main) with `--game-exe bf4.exe`;
  the launcher starts it after BF4's PID is known; packaged with the OpenXR loader.
- Bf4EmuHost (CTest `Bf4Emulated`): the emulator plays the XR host; the panel carries the
  game image and the host's MenuControl clicks the emulated button exactly once.
- `tools/emulated/Run-Bf4TheaterXr.ps1` (explicit, GPU): real FvrXrHost on the theater
  fixture (FvrTheaterTestXr) + emulated game: 19 panel frames, 75 quad layers, panel
  centre pixel = the game's colour, 0 runtime errors.
- Real game + headset: theater mode works (2026-10-19). The world lane above is next.

## Camera and session signals (2026-10-19, live read-only probes done)

- Live probe: `[renderer+0x60]` entry 0 holds a rigid camera transform that follows the
  player; FOV `+0xB4` is in radians (0.79 in the prologue), near 0.06, far 20000, aspect
  1.778. Slot 19 returns `[renderer+0x70]` (not a camera).
- Session signals (R5) are wired (`Bf4SessionSignals.h`): campaign levels read as
  `Levels/SP/<name>/<name>`, the menus as `Levels/FrontEnd/FrontEnd`; isMultiplayer is
  noise outside a level, so it counts only inside a non-front-end level and a 1 must hold
  2 s before it locks (the lock is permanent). Campaign gameplay features now unlock.

## Controller input (2026-10-19, emulated)

- Virtual XInput pad from the VR controllers (BF4 default scheme, reload = right B ->
  pad X). Bf4EmuHost: the emulated host publishes a controller sample and the game's own
  `XInputGetState` call sees reload + left stick forward through the real hooks. Core
  tests: deadzones, reload gating by mode, neutral while the mod menu is open, Start pulse
  from "Game menu (pause)", pad released on lockdown. Not yet seen in the real game.
- XR host `--theater-input` (launcher passes it): gameplay input continues under the
  theater panel. Launcher: experimental features on by default for pre-release (theater
  view is a Candidate), 3D route defaults to the 3D Vision emulation with the launch-option
  note in the window.

## Manual reload research (2026-10-19, static only)

- BF4-MANUAL-RELOAD.md: the reflected weapon data (reload type, timers, threshold,
  capacity, bolt/pump, weapon classes, AnimatedWeaponBinding signals), the native firing
  state machine (`+0x9BFAC0`, ammo transfer `+0x9BC910` at the ReloadThreshold point),
  how every gun maps to the framework's mechanism model, two ways to drive the game's
  own reload, and phases P0-P8.
- Built: `Bf4ReloadClassifier` (gun data -> WeaponMechanismDescriptor; unknowns stay on
  button reload), unit-tested. Not wired to anything.
- Superseded for play by the magazine gesture above (no native observation needed);
  the driver below stays for when the firing object is found.
- Built: input-only reload driver (`Bf4ReloadDriver`): holds fire while a VR magazine is
  out / breech open / reload running / stroke owed, presses the game's own reload, and
  acknowledges only observed ammunition. Tested against an emulated firing state
  machine and the framework's ManualReload coordinator end to end; not wired into the
  core yet (needs the VR gesture side and the player's firing object).
- Built: read-only firing census (developer sessions; ~30 s in, then every ~2 min, and
  research command `census`). It logs every weapon-firing object it finds, limited-ammo
  ones first, and client/server pair candidates, so one live session can answer the
  client vs server question. Emulated end to end; never run in the game. All research and
  probes now stop at any lockdown, and a running census aborts.
- Static answer to the client/server question: every gun has a client firing object
  (client weapon `+0x8`) and a server one (server weapon `+0xA0`); acknowledgements
  should follow the server copy (BF4-MANUAL-RELOAD.md 2.3). Correction found on the way:
  the first client vtable was an abstract base; the live classes are A (`+0x1D467A0`) and
  B (`+0x1D467E0`), both owned by one client weapon entity (`+0x1D46540`: B at +0xA0, A at
  +0xA8, active at +0x98). The census classifies holders, finds the entity and its active
  class, and logs who holds the entity (emulated end to end).
- Open: the live census result; the rest of the ownership chain to the player (R8)
  before any write.

## CI and release pipeline (2026-10-19)

- Pushed to `bf4/vr-adapter`; first GitHub runs green at `440594f`: `build.yml` Windows
  x86 and x64 (all 283/282 suites incl. Bf4Core and Bf4Emulated) and `bf4-emulated`
  (launcher tests on Linux). Fixed on the way: script permissions and line endings, a
  POSIX path bug in the launcher's profile settings resolver, and the emulator's menu
  button assuming a 1280x720 client on runners with small desktops. The release workflow
  (tags `bf4-v*`) has not run: no release is tagged until a headset session passes.

- `bf4-release.yml` (tags `bf4-v*`, so BF4 releases never collide with other games here): windows-2022 like `build.yml`, actions pinned to the same
  commits. Downloads `openxr_loader_windows-1.1.61.zip` from KhronosGroup/OpenXR-SDK-Source
  (asset name confirmed via the GitHub API) and stops unless `x64/bin/openxr_loader.dll`
  hashes to the pinned `a69729a3...0fa5f`; the local loader's provenance is not recorded,
  so the first tagged run is the actual check. Builds `FvrXrHost` too, packages with
  `make_release.py --require-xr-host` (fails without the host or loader) and ships
  `licenses/` (THIRD_PARTY_NOTICES + OpenXR, JsonCpp, MinHook, BFVR texts). Local package
  check from `build/x64`: 23 files, host + loader + notices present.
- `bf4-emulated.yml` / `run_all.sh`: the portable launcher tier only. The old Linux C++
  compile globbed BC2 sources and `tools/wincheck` lacks the theater/XInput/GDI shims, so
  both were dropped; the C++ suites run through `build.yml` (Build.ps1, Windows x86/x64).
- Bf4Emulated prints SKIP for the gamepad check when `xinput9_1_0.dll` is unavailable.

## Merged design (2026-10-18)

One BF4 design now. Kept from the earlier local files: `VrPanel` + `GdiPanelRaster` (mod
menu rendering), evidence levels (now in `Capability`), the XInput pad input plan, the
installed-build facts and camera leads (BF4-ARCHITECTURE.md). Dropped as duplicates:
`Bf4Common.*`, `ReloadMode.h`, `FeatureStatus.h`, the overlay's `LaserPointer` (the host's
framework `MenuPointer` is the pointer), and `BF4-ADAPTER.md`. Originals archived in
`reports/bf4-earlier-design-archive.zip`.

## Tests (2026-10-19)

- Mod menu layout: with more items than one column holds (8 rows on the 1280x720 panel)
  the items go into two columns; before this, rows past the eighth (Recenter, holsters,
  feedback marker in developer sessions) were drawn off the panel and unclickable. Labels
  and notes were shortened to fit half width (GDI text now ends with an ellipsis when it
  does not fit). With more items than two columns of full rows hold, rows get shorter and
  the item fonts scale with them (checked by rendering the 18-item developer menu); the
  dead "Screen distance" entry is gone (the host has no way to receive it yet). Unit
  test: every item on the panel, none overlapping; a GDI render of the developer menu was
  checked by eye.
- Build.ps1 after the menu fonts: x64 284/284, x86 283/283. Bf4CoreTests 78 cases;
  Bf4Emulated pass (theater, world lane, stereo probe, census, lockdown, gestures; WARP:
  eye resampler reticle/markers/vignette/depth stereo/HUD corners, depth capture detour);
  launcher 19/19; session summary 2/2; `Run-Bf4TheaterXr.ps1` 5/5 and
  `-World -FrameChannels 2` 5/5 (76-237 pairs, machine-dependent); `-World` with one
  channel 4/4 (47 pairs).
- Installed locally: runtime/bf4 0.4.41-local (rollback 0.4.40-local). Developer
  sessions still run the read-only probes and the firing census.
- Build: VS 18, x64 RelWithDebInfo, `build/x64`. Math types: `fvr/math/Vec.h` extends the
  framework's `StereoMath.h` (`Quat` = `Quaternion`; Pose is `{position, orientation}`).

## Holsters and arm IK research (2026-10-19, static only)

- Holster gestures that select weapons through BF4's own keys are built (above); what
  follows is the native route (hide the gun model, hand IK) for later.
- BF4 can hide the first-person gun itself (`Animated1pOnlyWeaponBinding.HideWeapon1p`)
  and its character IK scene op has hand IK inputs (`InEnableLeft/RightHandIK`,
  `InLeft/RightHandIKReach`, `InHideWeapon`). Plan and unknowns: BF4-HOLSTER-IK-RE.md.

## Next live session

Checklist: [BF4-FIRST-SESSION.md](BF4-FIRST-SESSION.md). Afterwards
`tools/bf4probe/summarize_session.py` turns the adapter log into verdicts (camera,
single-player signals, client/server weapon copies, next ownership link); unit-tested in
`tests/test_bf4_session_summary.py` and checked against the emulator's real log.

## Open / blocked

- Headset check of 0.4.41 (BF4-FIRST-SESSION.md): head rotation direction, gun size,
  comfort, the pair rate (was ~17-25 Hz; expected about game fps / 2 at frame sync 2),
  controller aim (mouse look reaching the game, its learned gain), gestures, HUD corners,
  3D from depth (which depth target it finds; `vr-snapshot-depth.bmp`).
- 3D: the stall after the split render engages; the watchdog's stacks show where.
- Laser click on the campaign screen's "Resume" (main menu) did not register while other
  menu clicks did. Cause assumed: no click lock, so hand jitter while the trigger was held
  went out as a drag (that screen scrolls on drags), and the trigger also reached the
  virtual pad. Now the cursor holds its press position unless the laser moves more than
  6 % of the screen, and the pad's triggers are released while the laser points at the
  screen outside campaign play (unit-tested; live check pending; clicks are logged).
- Holsters and the magazine gesture: first live check (keys, positions, pulses).
- The census holder chain matched nothing live (27 firing objects, 0 holders): redo the
  walk from the live firing vtable `+0x1D53F18`.
- Mod menu in VR: opens with the menu button, drawn by the adapter (VrPanel + GDI) on the
  theater lane (emulated core tests, GDI render checked); not yet seen in a headset.
- Corner HUD outside the eyes (frame shape, above): insets are a workaround; the real fix
  needs the UI pass rendered separately (native RE).
- Gun at the hand / arm IK: needs the player's first-person binding and IK scene op
  (BF4-HOLSTER-IK-RE.md), i.e. live RE first.
