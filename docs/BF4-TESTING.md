# BF4 testing

Cheapest tier first; run what the change touches.

| Tier | Command | Covers | Needs |
| --- | --- | --- | --- |
| Portable (any OS) | `tools/emulated/run_all.sh` | the launcher row below | Python 3.11+ |
| Core + headless sessions (Windows) | `Bf4CoreTests` (ctest `Bf4Core`) | engine-stereo routes (emulated 3D Vision driver + core NVAPI, split-screen pair check, eye boundaries, probe verdicts, snapshot BMP, init gate, 3D theater + lockdown), scan, bindings → capabilities + evidence, reload policy, VR panel clicks (fresh press rule), mod menu, virtual gamepad mapping + reload gating + neutral-while-menu, research command parsing, camera-probe plausibility rules, head-tracked view override (vtable copy install checks, exact restore, foreign write kept, stale-pose expiry, non-rigid refusal, detach rules), manual-reload classifier (every family, descriptors valid against the framework contract, unknowns stay on button reload), input-only reload driver against an emulated firing state machine (magazine/empty/bolt/single-shot flows, stranding guards, weapon switch, full coordinator plan), session guard (incl. the front-end level), camera math, PE identity, session parsing, exact-write restore, holsters/body frame, full emulated sessions (front-end click, campaign stereo + recenter + reload, alternate-eye fallback, mid-click MP lockdown, PunkBuster cadence, wrong-arch refusal) | MSVC + Windows SDK |
| Launcher | `python -B -m unittest discover -s launcher/tests` | update verify/hard-link/rollback/prune, corrupt + zip-slip rejection, settings recovery, journaled restore (user.cfg and PROFSAVE keys), foreign dxgi.dll protection, feedback redaction, emulated full VR session, release package → installer contract (runtime files only, notices ship, `--require-xr-host` refuses a package without the host), proxy-export generator vs synthetic DLL | Python |
| Emulated process (Windows) | ctest `Bf4Emulated` | real proxy forwarders + owned factory exports, session section, adapter load, real Present hook, the emulator playing the XR host (theater token, panel texture consumed, MenuControl click into a real window), VR controller samples reaching the game's `XInputGetState` (SKIP if xinput9_1_0.dll is absent), the developer firing census over the real heap scan (planted client/server-shaped pair found, unlimited AI copy marked, planted client/server weapon owners classified, the client weapon entity found with its active class, and its holder located; the emulated session claims the analysed build hash for this), real nvapi64.dll shim driving red-left/blue-right eye capture + snapshot, PunkBuster lockdown | Windows, WARP |
| Real | campaign session | headset/gameplay claims | game + headset |

CI: `build.yml` runs `Build.ps1` on Windows x86/x64 for every push; the BF4 adapter is in
the default CMake build, so `Bf4Core` runs on both and `Bf4Emulated` on x64. `bf4-emulated.yml`
runs the portable tier on Ubuntu. `bf4-release.yml` (tags `v*`) downloads the Khronos
OpenXR 1.1.61 Windows loader and refuses to continue unless its x64 `openxr_loader.dll`
matches the pinned SHA-256, builds the adapter, proxies and `FvrXrHost`, runs ctest
`^Bf4` and the launcher suite, then packages with `--require-xr-host` (notices under
`licenses/`). The download and hash match have not been exercised on a runner yet.

`tools/wincheck` (declarations-only SDK shim) predates the theater link, XInput and GDI
code and no longer covers the adapter; it is not run by CI.

Mutation checks were run on the emulated sessions (recenter sign, lockdown release), the
stereo route (NVIDIA eye enum swap, post-present capture rule) and the launcher
(restore-on-exit): each broken behaviour was caught, then restored.

Disk: the existing x64/x86 build trees; the portable tier writes nothing outside a temp dir.

## Explicit GPU check: theater link through the real XR host (2026-10-18)

```powershell
pwsh tools/emulated/Run-Bf4TheaterXr.ps1
```

Runs the real `FvrXrHost.exe` on the fake OpenXR runtime `FvrTheaterTestXr` (theater
fixture: campaign pacing, quad layers accepted, panel centre sampled) with `Bf4EmuHost`
as the game (`FVR_BF4_EXTERNAL_XR_HOST=1`, hardware GPU so the shared texture can cross
devices). Passes when the host shows game frames on the panel and the compositor's quad
layer carries the game's colour with 0 runtime errors. Not part of CTest (needs a GPU).