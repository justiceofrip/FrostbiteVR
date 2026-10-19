# BF4 adapter: current state (2026-10-19)

The only BF4 state document; overwrite it when the state changes. BC2 state stays in
[CURRENT.md](CURRENT.md). Start with [HANDOFF-BF4.md](../HANDOFF-BF4.md); stereo detail in
[BF4-STEREO.md](BF4-STEREO.md). Branch `bf4/vr-adapter` (pushed; no PR or release).

## Evidence levels (same vocabulary as CURRENT.md)

- **Actual-game check**: unattended runs on the real bf4.exe (build 179547, sha256
  `9d9d1a3f...c6c2aa`), AMD RX 9070 XT, Steam -> EA app launch, campaign only, no headset.
- **Emulated**: Bf4EmuHost (WARP or GPU), fake OpenXR runtimes, launcher emulated sessions.
- **Headset acceptance**: theater mode (game screen on a virtual screen, laser on the
  menus) works in the player's headset (2026-10-19, SteamVR). Nothing else yet.

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

## Head tracking: the world lane (2026-10-19, built; first headset check pending)

In campaign levels the headset now shows the game's world from the player's tracked head
(6DoF, both eyes from the same native frame, mono: no parallax yet) instead of the theater
screen. Pieces (all on the analysed build only):

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
  rendered with; a lease that cannot be met in 140 ms is cancelled. Each eye is the part
  of the frame its FOV covers (asymmetric crop), resampled to the host's eye size by
  `Bf4EyeResampler` (game device, own device-context state, raw bits kept).
- The world lane runs only while the game submits world views (loading screens stay on
  the theater panel) and falls back to the theater panel when no pair is published for
  2.5 s (retry after 5 s). The mod menu (menu button) always opens on the theater panel and
  has "In campaign levels: Screen / Head-tracked view".
- Recentring: the first head pose of the session faces forward; mod menu "Recenter view".
  Game yaw is kept, pitch/roll/position come from the headset (aim pitch stays the game's).
- Evidence: unit tests (crops, decisions, fallback, FOV write and exact restore, pose
  runs, lease-only camera); Bf4Emulated world phase (the emulated host receives pairs and
  each eye carries its own crop); `Run-Bf4TheaterXr.ps1 -World`: the real FvrXrHost on the
  fake OpenXR runtime submitted 165 pairs from the emulated game, 0 rejected, 0 runtime
  errors. Not yet seen in the real game: the settle depth (main thread vs render thread),
  BF4 accepting the wider FOV, pair rate in game.
- Known gaps: mono until engine stereo engages (below); pair rate is bounded by one request in flight (host asks again
  only after the previous pair is consumed) plus `settle` frames, so the world updates
  slower than the headset (the compositor reprojects head rotation every frame); HUD and
  pause menu are drawn into the wide frame (pause menu: use the pad, or switch to Screen);
  first-person weapon not yet re-projected.

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

- Build.ps1 after the eye-loop fix: x64 283/283, x86 282/282. Bf4CoreTests 55 cases / 447
  checks; Bf4Emulated pass (theater, world lane, stereo probe, census, lockdown); launcher
  19/19; session summary 2/2; `Run-Bf4TheaterXr.ps1` 5/5 and `-World` 4/4 (157 pairs).
- Installed locally: runtime/bf4 0.4.15-local (rollback 0.4.14-local). Developer
  sessions still run the read-only probes and the firing census.
- Build: VS 18, x64 RelWithDebInfo, `build/x64`. Math types: `fvr/math/Vec.h` extends the
  framework's `StereoMath.h` (`Quat` = `Quaternion`; Pose is `{position, orientation}`).

## Holsters and arm IK research (2026-10-19, static only)

- BF4 can hide the first-person gun itself (`Animated1pOnlyWeaponBinding.HideWeapon1p`)
  and its character IK scene op has hand IK inputs (`InEnableLeft/RightHandIK`,
  `InLeft/RightHandIKReach`, `InHideWeapon`). Plan and unknowns: BF4-HOLSTER-IK-RE.md.

## Next live session

Checklist: [BF4-FIRST-SESSION.md](BF4-FIRST-SESSION.md). Afterwards
`tools/bf4probe/summarize_session.py` turns the adapter log into verdicts (camera,
single-player signals, client/server weapon copies, next ownership link); unit-tested in
`tests/test_bf4_session_summary.py` and checked against the emulator's real log.

## Open / blocked

- First headset check of the world lane (head tracking): does the world follow the head
  without swimming (else raise "frame sync" in the mod menu), does BF4 render the wide
  FOV, pair rate (`theater_link ... pairs=` and `view_override ... run=` lines every 600
  frames in `bf4-adapter.log`).
- Zoom/motion sickness in the head-tracked view: compare the snapshots with the eye
  FOVs; then raise the pair rate (one request in flight is the limit).
- 3D: the stall after the split render engages; the watchdog's stacks show where.
- Laser click on "Resume" reported not to work (which screen is not known yet). Controller aim (R8) not started;
  button reload (R6) is covered by the virtual pad.
- The census holder chain matched nothing live (27 firing objects, 0 holders): redo the
  walk from the live firing vtable `+0x1D53F18`.
- Mod menu in VR: opens with the menu button, drawn by the adapter (VrPanel + GDI) on the
  theater lane, reload mode / recenter / feedback marker work (emulated core test); not
  yet seen through the real host or in a headset.
