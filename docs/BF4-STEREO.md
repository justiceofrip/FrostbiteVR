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

## Split-screen stereo, traced (static, 2026-10-19)

- Render device: the 0x610-byte object the GameRenderer constructor (`+0x8391E0`) creates
  with constructor `+0xBD61D0` (vtable `+0x1DD1870`) and stores in global `+0x2738080`.
  Settings at `+0x80`; outputs at `+0x38` (stride 0x138), count at `+0x30` (vtable slot 11
  returns it; the per-frame view pipeline makes one view per output). Output fields:
  width/height u16 at `+0x58/+0x5C`, stereo mode `+0x7C`, on `+0x80`, convergence `+0x84`,
  separation `+0x88`; vtable `+0x60` of the screen manager returns `&output+0x58`.
- `+0xC12FF0` (device slot 6, per output, every frame): mode 1 when `StereoEnable &&
  StereoSplitScreenEnable`; convergence = StereoConvergenceScale x 0.1; separation =
  StereoDepth x 0.01 x StereoSeparationScale. The two flags are read in code only here and
  at device creation (3D Vision init).
- Two-eye loop `+0x8CAF70` (called from `+0x8BE430`, slot 2 of vtable `+0x1D24258`): for an
  output with `on` set, each view is built twice (`edi` = 0, 1): view `+0xD0` =
  -separation for eye 0, +separation for eye 1, `+0xD4` = convergence, dirty `|= 0xB`; in
  mode 1 eye 0 gets viewport x = width/2 and eye 1 x = 0, both width/2 wide. Under one
  condition (`[block+0x1014]` equal to a constant) the separation of every output is
  first multiplied by 0.1.
- Projection builder `+0xB7EA10` -> `+0xB7EAE0` (inputs: type `+0xB0`, fovY `+0xB4`, aspect
  `+0xC4`, near `+0xBC`, far `+0xC0`, `+0xC8/+0xCC`, separation `+0xD0`, convergence `+0xD4`,
  offsets `+0x90/+0x98/+0xA0`, flags `+0xD8/+0xD9`): row 2 x += s, row 3 x = s x c (row
  vectors, w = z). So x_ndc = x P00 / z + s + s c / z: a camera at -s c / P00 on the view's
  right axis plus a constant shift s. Eye 1 (left half, s = +sep) is the left eye.
- Half eye distance = separation x convergence x tan(horizontal half FOV). The aspect stays
  the output's, so each half is the full projection squeezed to half width.

## Why split stereo crashed on PC, and the fix (2026-10-19)

- The two-eye loop's per-eye array (eye-loop object `+0x1570` begin, `+0x1578` end of data,
  `+0x1580` end of storage) is initialised by the constructor at `+0x8B7185` to inline
  storage `+0x1590` of exactly 0x1560 bytes, i.e. one entry; the loop resets it per call
  (`+0x8CB0B6`) and appends per eye (`+0x8CB697`) without a bounds check; the consumer in
  slot 2 (`+0x8BE71E`) counts entries as (end - begin) / 0x1560. Readers of these fields
  in the whole `.text`: the constructor, the loop and slot 2 only.
- The loop `+0x8CAF70` is called only from `+0x8BE430`, which is only reachable through
  vtable `+0x1D24258` slot 2 (16 slots). Its arguments: this, an unused second, an int,
  float (xmm3), float (stack), view count, views, a pointer, two values passed on.
- In split mode the view setup takes the view aspect from the output's `+0x60/+0x64`
  size; that it is an eye half's in split mode is inferred from the zoom seen in the headset (the probe now logs it).
