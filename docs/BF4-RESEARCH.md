# Battlefield 4 VR: research notes (2026-10-18)

Facts are marked **verified** (external source or this repo's tests) or **hypothesis**
(needs an RE task before any feature depends on it).

## The game

- **verified** Steam app 1238860 starts through the EA app. Install under test:
  `<Steam library>\steamapps\common\Battlefield 4`.
- **verified** Frostbite 3, Direct3D 11 (Mantle optional; the adapter requires DX11).
- **hypothesis** The folder holds `bf4.exe` (x64) and `bf4_x86.exe`. Only x64 is supported;
  the launcher's PE check rejects 32-bit images either way.

## Anti-cheat and the single-player boundary

- **verified** BF4 multiplayer still uses PunkBuster (client modules, kicks reported through
  2024-2026) plus server-side FairFight. EA Javelin (kernel) covers newer titles; nothing
  found shows it was added to BF4.
- **verified** PunkBuster historically kicked for extra Direct3D DLLs (ReShade reports).
- Design consequence: campaign only, enforced three ways.
  1. The proxy is copied into the game folder only for a VR session and removed after.
  2. Without the launcher's session section the proxy loads nothing.
  3. In-process, `SessionGuard` locks the adapter off on any MP evidence (PunkBuster module
     loaded, non-campaign level, remote server) and never re-arms in that process.

## The big lever: Frostbite already renders stereo

- **verified** DICE added stereoscopic 3D to Frostbite 2 for BF3 by rendering each frame
  twice, once per eye (NVIDIA 3D Vision, AMD HD3D).
- **verified** BF4's console still exposes `RenderDevice.StereoConvergenceScale`,
  `RenderDevice.StereoSeparationScale` and `RenderDevice.StereoSoldierZoomConvergenceScale`.
- **hypothesis** BF4 keeps a working per-eye render loop gated on a driver stereo check.
  If that loop can be forced on and fed per-eye view/projection from OpenXR, the mod gets
  synchronous stereo from the engine itself: no alternate-eye artifacts, no second
  simulation. This is RE task R2 and the preferred M2 path. NVIDIA dropped 3D Vision from
  drivers in 2019 and the dev machine has an AMD GPU (BC2 crash notes mention an AMD
  queued callback), so the driver check must be satisfied or bypassed inside the game.
- Fallback: alternate-eye rendering through the camera override (StereoAlternateEye).
- **verified** BF4 shipped with 3D Vision support (benchmarked with 3D Vision on/off), and
  DICE described Frostbite stereo as the engine rendering each frame twice under its own
  control: the 3D Vision Direct pattern (SetDriverMode Direct, SetActiveEye per eye).
- **verified** NVIDIA publishes NVAPI's headers and function-ID table under MIT, explicitly
  to enable re-implementations; the shim's IDs and enums are taken from that table.
- Consequence: the shortest route to stereo is emulating the 3D Vision driver, not
  reverse-engineering the renderer. See BF4-STEREO.md.

## Data and reflection

- **verified** Frosty Toolsuite has full BF4 support (profile `BF4SDK.dll`). Use it to read
  EBX data (weapon `ShotConfigData`, camera data, UI assets) while planning bindings.
  Licence is CC BY-NC-ND: reference only, never vendored or redistributed. Its mod manager
  writes a ModData copy of game data: avoid it on space-limited drives.
- **hypothesis** BF4 carries Frostbite's runtime TypeInfo reflection. A read-only in-process
  dumper (RE task R4) turns data-class field names into offsets, so field layouts come
  from the running build instead of hand-copied structs.
- BC2 lesson carried over: `InitialDirection (0,0,1)` means +Z is forward; confirm for BF4
  before the camera feature leaves Disabled.

## Engine settings for VR sessions

- **verified** BF4 reads `user.cfg` in the game folder; commonly used, documented keys:
  `WorldRender.MotionBlurEnable`, `WorldRender.MotionBlurQuality`,
  `WorldRender.MotionBlurMaxSampleCount`, `RenderDevice.VSyncEnable`,
  `RenderDevice.TripleBufferingEnable`, `RenderDevice.RenderAheadLimit`, `GameTime.MaxVariableFps`.
- The launcher adds a marked block for the session and removes it after; the player's own
  lines are untouched.

## Prior art

- vorpX ran BF4 on the Rift in Z-buffer 3D (depth-reprojected, not true stereo, no motion
  controls). Useful as a floor, not a model.

## Sources

- FrostbiteVR repo, CURRENT.md, AGENTS.md, CMakeLists.txt, launcher 0.1.6 notes (github.com/justiceofrip/FrostbiteVR)
- EA: Enable stereoscopic 3D in Battlefield 3 (ea.com/news/enable-stereoscopic-3d-in-battlefield-3)
- BF4 console variable list (gist.github.com/BETLOG/cd6d32e8015aea960c70a48b8b5b40b0)
- BF4 user.cfg examples (gist.github.com/xiaohutai/7426417; forums.ea.com BF4 cfg threads)
- EA Javelin progress report, April 2025 (ea.com/security/news/anticheat-progress-report)
- Steam community + review summaries on PunkBuster kicks (steamcommunity.com/app/1238860; vaporlens.app)
- ReShade forum on PunkBuster and injected D3D DLLs (reshade.me)
- Frosty Toolsuite docs and releases (github.com/CadeEvs/FrostyToolsuite)
- Road to VR on vorpX + BF4 (roadtovr.com)
- NVIDIA NVAPI SDK, MIT (github.com/NVIDIA/nvapi: nvapi_interface.h, nvapi_lite_stereo.h)
- 3D Vision Direct DX11 sample (github.com/bo3b/3D-Vision-Direct)
- 3D Vision GPU benchmark incl. BF4 (overclockers.ru)
