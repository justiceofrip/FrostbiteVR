# BF4 stereo: engine routes and what bf4.exe actually does

Verified on the installed build (bf4.exe sha256 `9d9d1a3f...c6c2aa`, build 179547) from a
read-only image of the decrypted process (research channel `dumpimage`, offline Capstone
analysis) and live read-only `peek`s. RVAs are relative to image base `0x140000000`.

## The two stereo routes inside BF4

Render-device settings (Frostbite reflection, `fieldinf` records, offsets confirmed):

| Field | Offset | Type | Notes |
| --- | --- | --- | --- |
| `RenderDevice.NvApiEnable` | 0x7A | bool | |
| `RenderDevice.StereoEnable` | 0x7D | bool | settable from `user.cfg` (live peek: follows the file) |
| `RenderDevice.StereoSplitScreenEnable` | 0x7E | bool | **read-only on PC**: `user.cfg` value ignored (stays 0); string "Attempt to write read-only property %s.%s, ignored" |
| `RenderDevice.StereoDepth` | 0x44 | float | |
| `RenderDevice.StereoConvergenceScale` | 0x48 | float | |
| `RenderDevice.StereoSeparationScale` | 0x4C | float | |

Per-output stereo parameters, every frame (`+0xC12FF0`, called per output from render
device vtable `+0x1DD1870` slot 6):

```
if (StereoEnable && StereoSplitScreenEnable)          mode = 1 (split screen, no driver)
else if (device.stereoHandle && NvAPI_Stereo_IsActivated) mode = 3 (3D Vision), separation =
        GetEyeSeparation * GetSeparation * StereoSeparationScale, convergence from settings
else                                                   mode = 0 (off)
```

3D Vision initialisation, once at render-device creation (`+0xC00F90`):
`Initialize, GetInterfaceVersionString, SYS_GetDriverAndBranchVersion, EnumLogicalGPUs,
EnumPhysicalGPUs`, then **only if `StereoEnable && !StereoSplitScreenEnable`**:
`Stereo_IsEnabled` -> `Stereo_SetDriverMode(Direct)` -> device flag `+0x49 = 1`; device
setup (`+0xBF028E`) then calls `Stereo_CreateHandleFromIUnknown` (handle at device `+0x50`).
`Stereo_SetActiveEye` and `ReverseStereoBlitControl` are linked but **never called**: BF4
does not use the eye-switch style of Direct mode the first emulation assumed.

## What was tried on the real game (2026-10-18, unattended, AMD RX 9070 XT)

| Run | Setup | Result |
| --- | --- | --- |
| 1 | nvapi shim, NVIDIA vendor report | Initialize answered; 10 other queries null; no stereo calls |
| 2 | + core NVAPI answers (NvapiCoreEmu: 1 GPU, driver 425.31) | all public queries answered; no stereo calls |
| 3 | + research image dump | gate found (above): StereoEnable is off at device creation |
| 4 | + `RenderDevice.StereoEnable 1` in user.cfg | still no `Stereo_IsEnabled`: user.cfg runs after device creation |
| 5 | split route (`StereoSplitScreenEnable 1`) | frames mono; live peek: field stays 0 (read-only) |
| 6 | direct `bf4.exe -RenderDevice.StereoEnable 1` | the EA anti-tamper stub exits at once; no relaunch |

FPS overlay (`PerfOverlay.DrawFps 1`) proved user.cfg is executed. The research channel's
laser-path clicks drove the real menus (Campaign -> Resume) into a level.

## Remaining engine-stereo options

1. **3D Vision route with a launch option** (most promising): `-RenderDevice.StereoEnable 1`
   as the game's Steam/EA app launch option, so it applies before device creation, plus the
   nvapi shim + vendor report. Needs the player's OK (persistent setting in another app),
   and it is unverified whether Frostbite honours command-line settings here
   (`allowCommandlineSettings` exists in the image). Then: find how BF4 hands two eyes to
   the driver without SetActiveEye (likely a double-width image + NV stereo signature).
2. **Split route** needs `StereoSplitScreenEnable` set despite read-only: only by a game
   memory write, which is out of scope for unattended runs (blocked by policy); revisit only
   with an explicit decision and an exact-write lease.
3. Not engine stereo: head-tracked camera (R3) + alternate-eye or a verified two-pass render.

## Probe tooling (unchanged contract)

Launcher 3D off/probe/on; route `split` (default) or `nvapi`. Probe writes
`logs/stereo-probe.txt` and one snapshot. Split checks use normalised patch correlation
(SideBySide.cpp): engine side-by-side halves correlate > 0.75; real mono BF4 frames measured
0.30-0.39 (menus, loading art, dark level).
