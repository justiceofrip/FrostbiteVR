# Handoff: Battlefield 4 VR adapter for FrostbiteVR

> **Status 2026-10-20: the current state is docs/CURRENT-BF4.md, and the next headset session's
> checklist is docs/BF4-FIRST-SESSION.md.** What follows is the original hand-over of
> 2026-10-18 and is history: the frame glue is no longer a stub (the XR host shows the theater
> screen and a head-tracked 6DoF view), BF4's own split / 3D Vision stereo is retired in favour
> of alternate eyes and 3D from depth, and the game side (gun in the hand, controller aiming,
> holsters, manual reload, laser menus, the controller scan) runs live on a simulated headset.
> Not yet seen in a real headset since 0.4.16; arm IK and vehicles remain open.

Written 2026-10-18 at the end of a claude.ai chat session (sandboxed Linux, no access to
the player's PC, the game, Windows or a headset). Read this first, then AGENTS.md, then
docs/CURRENT-BF4.md and docs/BF4-STEREO.md.

## What the player asked for
- A BF4 VR mod built on github.com/justiceofrip/FrostbiteVR (their BC2 VR framework).
- Game: Steam BF4 (starts through the EA app) in a Steam library (`<library>\steamapps\common\Battlefield 4`).
- "Move fast and break things"; emulated testing; no redundant tests; don't waste disk
  (C: is small: everything on G:, per AGENTS.md storage rules).
- A VR launcher / updater / feedback sender for GitHub releases, so hotfixes can ship.
- Reload option: button press now; manual reload selectable but not built yet.
- Architecture that plans for manual reload, IK and holsters.
- All menus usable with a laser pointer in VR.
- Wants stereo as early as possible ("skip to stereo").
- Ghidra / Reverse Engineer Anything MCP are welcome for RE work.

## What exists (this tree overlays FrostbiteVR; intended as new files only)
- Platform-neutral core: capability registry; reload mode + button router; laser pointer;
  capability-driven in-headset menu; pattern scanner; XR<->Frostbite camera math; PE
  identity; campaign-only session guard; session block; exact-write journal; body frame +
  holster policy; `AdapterCore` (all per-frame decisions behind `IAdapterPlatform`).
- Stereo route (see docs/BF4-STEREO.md): `nvapi64.dll` shim emulating a 3D Vision Direct
  driver so BF4 renders both eyes itself; eye capture at eye boundaries; probe verdict +
  side-by-side snapshot; opt-in "report an NVIDIA GPU" for AMD/Intel.
- Windows glue: session-scoped `dxgi.dll` proxy (owns CreateDXGIFactory*, forwards the
  rest), adapter `FrostbiteVR.Bf4.dll`, Win32 pointer sink, PunkBuster lockdown.
- Emulated Windows host `Bf4EmuHost` (WARP): proxy -> adapter -> Present hook -> laser
  click -> stereo red/blue capture -> lockdown.
- Launcher (bundled-Python style like BC2VR): update/verify/rollback with hard-linked
  hotfixes, journaled game-folder changes, feedback -> pre-filled GitHub issue,
  `install-local` for PC-built packages, 3D off/probe/on.
- Release tooling + CI (`bf4-emulated.yml` Linux, `bf4-release.yml` Windows), issue form,
  Ghidra helper scripts, docs.

## What was validated, and where
- Linux g++ 13: 30 test cases / 204 checks pass (core, headless emulated sessions,
  holsters, menu, stereo route). Mutation checks caught deliberate breaks.
- All 7 Windows sources type-check against a declarations-only SDK shim
  (`tools/wincheck`): types only, NOT a real MSVC compile or link.
- Launcher: 14/14 Python tests incl. emulated full VR session and release contract.

## Never done (do not claim otherwise)
- No contact with bf4.exe. No headset. (Steps 1-3 below were completed on the player's
  PC on 2026-10-18: MSVC x64 build and `Bf4Core`/`Bf4Emulated`/launcher tests pass; see
  docs/CURRENT-BF4.md.)
- The 3D Vision route is a well-supported hypothesis (BF4 shipped 3D Vision support; DICE
  described engine-side two-pass stereo; NVAPI IDs are from NVIDIA's MIT SDK), not a result.

## Known risks to check first
1. **File collisions.** The chat could not list the FrostbiteVR tree. Paths like
   `include/fvr/math/Vec.h` or `include/fvr/interaction/*.h` may already exist. Unzip
   without overwriting and report every skipped file before going further:
   `tar -xf FrostbiteVR-BF4-stereo.zip --keep-old-files` (or 7-Zip "skip existing").
2. **Proxy exports.** `DxgiProxyExports.generated.h` is a generic Win10/11 list. Regenerate
   from the actual machine: `python tools/release/gen_proxy_exports.py C:\Windows\System32\dxgi.dll`
   then check `dumpbin /exports` on the built dxgi.dll (GLOBALROOT forwarders + the three
   owned CreateDXGIFactory* exports).
3. **Framework glue is a stub.** `Bf4FrameworkGlue.cpp` counts frames but does not send
   them to the XR host; the BC2 XR host (`BC2XrHost`) has not been generalised to
   `--game bf4`. The chat never saw those sources. Nothing appears in the headset until
   this is done; the stereo probe does not need it.
4. **MSVC-only details** not exercised by the shim: `#pragma comment(linker, "/export:...")`
   string concatenation, `__uuidof`, x64 MinHook target `FvrMinHook64` (hde64).
5. **Stale claims elsewhere.** README-BF4.md says "no existing file is replaced"; true only
   if step 1 finds no collisions.

## Next steps, in order
Run `tools/StorageBudget.ps1` before builds (AGENTS.md). Keep TEMP/TMP on G:.

1. Unzip without overwriting (risk 1). Add `include(cmake/Bf4Adapter.cmake)` to the root
   CMakeLists.txt after FvrCore.
2. Build x64 Release in the existing x64 tree:
   `--target FrostbiteVR.Bf4 Bf4DxgiProxy Bf4NvapiShim Bf4CoreTests Bf4EmuHost`.
   Fix compile errors with minimal changes.
3. `ctest -R ^Bf4 --output-on-failure` (Bf4Core + Bf4Emulated), then
   `python -B -m unittest discover -s launcher/tests -p "test_*.py"`.
4. **STOP and report to the player.** Everything up to here is safe unattended.
5. With the player present: regenerate proxy exports (risk 2), rebuild, package with
   `tools/release/make_release.py --build <x64 Release dir> --version 0.2.0-local --out dist`,
   install with `python -m fvrlaunch install-local dist/...manifest.json dist/...zip --root G:\FrostbiteVR`.
6. With the player present: stereo probe (docs/BF4-STEREO.md "Run the probe"). On AMD/Intel
   tick "Report an NVIDIA GPU". Read `logs/stereo-probe.txt` and look at
   `logs/stereo-probe-sbs.bmp`. Follow the verdict table.
7. Then: XR host `--game bf4` with per-eye layers + FrameSink/InputProtocol glue
   (`FVR_BF4_FRAMEWORK_GLUE`), IPD calibration for engine stereo, RE task R3 (camera) for
   head tracking. Update docs/CURRENT.md per AGENTS.md.

## Rules that must hold
- Campaign only. Any multiplayer evidence locks the mod off for the process; the shim then
  reports stereo off. Never weaken this.
- Proxies are copied into the game folder only for a session and removed after; never
  overwrite a foreign dxgi.dll/nvapi64.dll (ReShade etc.).
- No game binaries/assets or credentials in the repo or packages.
- Features stay Disabled until specific evidence; Manual reload resolves to button press
  with a visible note until roadmap M5.
