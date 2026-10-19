# BF4 adapter: current state (2026-10-19)

The only BF4 state document; overwrite it when the state changes. BC2 state stays in
[CURRENT.md](CURRENT.md). Start with [HANDOFF-BF4.md](../HANDOFF-BF4.md); stereo detail in
[BF4-STEREO.md](BF4-STEREO.md). Nothing here is committed yet (branch `bf4/vr-adapter`).

## Evidence levels (same vocabulary as CURRENT.md)

- **Actual-game check**: unattended runs on the real bf4.exe (build 179547, sha256
  `9d9d1a3f...c6c2aa`), AMD RX 9070 XT, Steam -> EA app launch, campaign only, no headset.
- **Emulated**: Bf4EmuHost (WARP or GPU), fake OpenXR runtimes, launcher emulated sessions.
- **Headset acceptance**: none yet.

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

## Stereo (see BF4-STEREO.md)

- Not working yet. BF4 has a split-screen route (`StereoSplitScreenEnable`, read-only on
  PC) and a 3D Vision route gated on `RenderDevice.StereoEnable` **at render-device
  creation** (user.cfg is too late). A launch argument reaches BF4 through Steam only after
  the player confirms Steam's "launch with arguments" prompt.

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
- Not done: a real-game run with the host (the last attempt was blocked by the Steam
  prompt below), and anything in a headset.

## Camera (2026-10-19: static RE done, override built, nothing run in the game)

- Static RE traced the per-frame view pipeline: camera manager fills view blocks, the
  game submits them through GameRenderer vtable slot 5, which copies them before
  returning (BF4-CAMERA-RE.md). Correction found on the way: slot 19 returns
  `[renderer+0x70]`, not `+0x60`; the read-only probe now parses the getter and samples
  both. The probe runs automatically in the next developer session on this build.
- Head tracking (mono) is built: `Bf4ViewOverride` points the renderer at a copied vtable
  whose slot 5 writes the head-tracked transform into view block 0, calls the game, then
  restores the bytes exactly. No game code is patched. Game yaw is kept, pitch/roll come
  from the headset. Unit-tested in-process (install checks, exact restore, foreign write
  kept, stale pose expiry, non-rigid refusal, detach rules). Bindings GameRenderer.Instance
  and RenderView.Update are enrolled (unique in the decrypted `.text`).
- Dormant in the real game: the core asks for a camera only in Campaign mode, which needs
  R5 (level name + local server). FOV is still the game's; matching the headset FOV is
  next after the first live check. Alternate-eye stereo now also requires
  Renderer.StereoActiveEye (frame-to-eye association).

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

- Build.ps1 after the weapon-entity link: x64 283/283, x86 282/282. Bf4CoreTests 49 cases / 366 checks
  (view override, front-end level, reload classifier, census rules, reload driver); Bf4Emulated now also runs the census; Bf4Emulated
  pass; launcher 16/16; theater XR script 5/5 (last run 2026-10-18). One earlier parallel
  x64 run failed Bf4Emulated's panel-pixel check once; not reproduced in four later
  parallel runs, cause not captured (the emulator now prints busy/error copy counts).
- Installed locally: runtime/bf4 0.4.6-local (rollback 0.4.5-local). Its developer
  sessions run the read-only camera and session probes and the firing census
  automatically.
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

- **Steam prompt pending on the player's PC** (since 23:19): "launch BF4 with
  `-RenderDevice.StereoEnable 1`?" It came from the EA stub after a direct-launch test and
  blocks further launches until answered (Cancel is the clean choice).
- Player to set (in Steam, by hand) BF4's launch option `-RenderDevice.StereoEnable 1`
  for the 3D Vision route; agreed 2026-10-18, not yet done.
- Session signals (R5) block every gameplay feature, head tracking included. Static
  candidates found 2026-10-19 through the game's own status message (BF4-SESSION-RE.md):
  level name `[[+0x27A1918]+0x50]`, isMultiplayer `[[[[+0x2670D80]+0x28]+0x38]+0xA0]`
  (null means multiplayer, as in the game). A read-only session probe logs both in the
  next developer session; nothing consumes them yet. SessionGuard now treats
  `Levels/FrontEnd/*` as the front end (it would have locked on it). Controller aim (R8)
  and button reload (R6, now covered by the pad) not started. Game-memory writes for the camera need an
  exact-write lease; the player authorised them (2026-10-18), but the writer waits for
  the read-only probe's evidence.
- Mod menu in VR: opens with the menu button, drawn by the adapter (VrPanel + GDI) on the
  theater lane, reload mode / recenter / feedback marker work (emulated core test); not
  yet seen through the real host or in a headset.
