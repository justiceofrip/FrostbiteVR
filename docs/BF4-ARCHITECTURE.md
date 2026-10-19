# BF4 adapter architecture

Same layering rules as the rest of FrostbiteVR (AGENTS.md): VR policy/math, engine
integration, graphics, XR runtime and game rules stay separate. BF4 adds an adapter and a
few shared, game-agnostic pieces; it reuses the BC2 XR host, frame channel and IK math.

```
 launcher (Python, install drive)                 XR host (FvrXrHost.exe, x64, OpenXR)
 ┌───────────────────────────────┐                ┌─────────────────────────────────────┐
 │ update/verify/rollback         │  starts        │ head/hand tracking, action set       │
 │ session: section + proxy +     │──────────────▶│ laser pointer (MenuPointer), panels  │
 │   user.cfg block, journaled    │                │ theater quad on the menu lane        │
 │ feedback: sanitize → issue URL │                │ per-eye layers (engine stereo, M2)   │
 └──────────────┬────────────────┘                └──────────────▲──────────┬────────────┘
                │ Local\FrostbiteVR_BF4_Session                  │ frames   │ input + game-
                ▼ (key=value, 4 KiB)                             │ (shared  │ screen pointer
 bf4.exe ─ loads ─▶ dxgi.dll proxy ─ loads ─▶ FrostbiteVR.Bf4.dll│ texture) │ events
                  (GLOBALROOT forwarders,      ┌─────────────────┴──────────▼────────────┐
                   no section = no-op)         │ SessionGuard (SP only, sticky lockdown) │
                                               │ BuildIdentity + BindingTable (read-only)│
                                               │ CapabilityRegistry (evidence-gated)     │
                                               │ Present hook → FrameSink                │
                                               │ MenuBridge (pointer → cursor/clicks)    │
                                               │ ReloadRouter (button now, manual later) │
                                               │ Camera/Stereo strategy (M1/M2)          │
                                               │ Holsters / IK writers (M4/M6 seams)     │
                                               └─────────────────────────────────────────┘
```

## Files

| Layer | Path | Notes |
| --- | --- | --- |
| Shared types | `include/fvr/math/Vec.h` | helpers over the framework's `StereoMath.h` types (`Quat` = `Quaternion`) |
| Capabilities | `include/fvr/game/Capability.h` | one status table + evidence level (none/emulated/actual-game/headset) |
| Reload | `include/fvr/interaction/ReloadPolicy.h` | `ReloadMode {ButtonPress, Manual}`, router seam |
| Roadmap seams | `include/fvr/interaction/BodyModel.h` | body frame, holsters, arm IK, rig writer |
| Menus | `include/fvr/ui/ModMenu.h`, `ModMenuPanel.h`, `VrPanel.h`; `src/graphics/GdiPanelRaster.cpp` | menu model, panel layout + click rule, GDI raster (no font assets) |
| Theater link | `src/games/bf4/Bf4FrameworkGlue.cpp` | game screen / mod panel to the XR host on the menu lane; host MenuControl back as pointer |
| Engine | `src/engines/frostbite/PatternScan.*`, `Fb3CameraMath.*` | scanning, XR↔Frostbite conventions |
| Write safety | `include/fvr/runtime/ExactWrite.h` | journaled native writes, exact restore on every outcome |
| Holsters/body | `include/fvr/interaction/Holsters.h` | body frame estimator + holster policy (M4, ahead of binding) |
| Game core | `src/games/bf4/Bf4AdapterCore.*`, `Bf4PointerInjector.*`, `Bf4ModMenu.*` | all per-frame decisions, platform-neutral |
| Game glue | `src/games/bf4/Bf4Module.cpp`, `Bf4MenuBridge.*`, `Bf4FrameworkGlue.*` | Win32/D3D11/MinHook side only |
| Game data | `src/games/bf4/Bf4Profile.*`, `Bf4Bindings.*`, `Bf4Session*.*` | identity, bindings, SP guard, session block |
| Loader | `src/platform/windows/bf4/DxgiProxy.cpp` | session-scoped proxy |
| Tests | `tests/bf4/Bf4CoreTests.cpp`, `tests/bf4/emu/*` | one core suite + one emulated process |
| Launcher | `launcher/fvrlaunch/*`, `launcher/profiles/bf4.json` | game-agnostic core, BF4 profile |

## Core vs. glue

`AdapterCore` makes every per-frame decision: session guard and lockdown, recenter,
pointer events, button reload, stereo mode and per-eye camera requests, frame metadata,
status. It talks to the outside only through `IAdapterPlatform`:

```
PunkBusterLoaded()  ObserveGame()  ApplyPointer(events)  PublishFrame(meta)
ApplyCamera(requests)  RequestNativeReload()  DetachAll(reason)  Log(line)
```

`Bf4Module.cpp` implements it with Win32/D3D11/MinHook; the emulated tests implement it
with fakes and a synthetic `.text` image. The frame logic that ships is the logic that
is tested. Camera requests carry a tracking-space transform and FOV angles only; the
platform composes them onto the game's own camera anchor and builds the projection with
the game's near/far/depth convention, writing through an `ExactWriteJournal`.

## Process model

The XR host stays out of process even though BF4 is x64: one OpenXR implementation and
menu renderer shared with BC2, and a game crash never takes the compositor session down
(and the reverse). The cost is one shared-texture copy per frame. Revisit only if
latency measurements say so.

## Loading

`dxgi.dll` in the game folder is found before System32 (it is not a KnownDLL; ReShade uses
the same route on BF4). Every export is a forwarder string to
`\\.\GLOBALROOT\SystemRoot\System32\dxgi.dll`, so there is no forwarding code to get wrong.
`DllMain` starts one thread; the thread reads the session section, checks the launcher
PID is alive and loads the adapter from the install folder. Updates never touch the game
folder beyond this one file. A foreign `dxgi.dll` (ReShade) is detected and left alone.

## Frame hook

The adapter reads the `IDXGISwapChain` vtable from a throwaway swapchain and hooks
`Present` and `ResizeBuffers`. The first top-level, game-process window of at least
640×360 is adopted. If none appears within 90 s the launcher tells the player to pick
DirectX 11 (Mantle never creates a DXGI swapchain).

## Stereo strategy (M2)

`StereoNative` and `StereoAlternateEye` are separate capabilities with separate bindings.
Native is preferred: the engine's own two-pass loop with per-eye matrices from OpenXR
(`Fb3CameraMath`: mirror Z, off-axis LH projection, optional reversed-Z). Alternate-eye
is the fallback and needs motion blur off (the session `user.cfg` block already does
that). Rule from AGENTS.md: no simulation replay for a second eye, and every temporary
camera write is restored on every outcome.

## Menus with a laser pointer

Two surfaces, one pointer implementation: the XR host's `MenuPointer` (framework code,
already verified with BC2), which sends `(u, v, down, toggle)` over the menu lane:

- Game menus: in theater mode the whole game frame is a quad, so the pointer is live
  whenever the beam is on it. The host sends `(u, v, down)`; `MenuBridge` maps that to
  client pixels, answers `GetCursorPos`, swallows `SetCursorPos`, reports `VK_LBUTTON`
  through `GetAsyncKeyState` and posts `WM_MOUSEMOVE`/`WM_LBUTTON*`. If BF4's UI reads raw
  input instead, RE task R7 adds a `RawInputSink` or `EngineCursorSink` behind the same API.
  Once the camera is head-tracked (M1+), the pointer is gated on a menu-state signal.
  Verified on the real game 2026-10-18 (Campaign -> Resume driven through this path).
- The mod menu (`ModMenuModel`): reload mode, recenter, feedback marker (snap turn and
  screen distance shown disabled until the host consumes them). The controller menu
  button (MenuControl toggle) opens it; while open the adapter lays it out as a
  `VrPanel` (fresh press + release on the same widget, so a held trigger never clicks),
  rasterises it with GDI only when widgets or highlight change, and publishes that
  texture on the theater lane instead of the game frame. The game cursor is released
  and the reload button suppressed while the menu owns the laser.

## Controller input: virtual XInput pad (implemented, emulated)

BF4 imports `XINPUT9_1_0` (`XInputGetState`/`SetState`/`GetCapabilities`), so VR
controllers drive a virtual pad: `fvr/interaction/VirtualPad.h` maps the host's
`InputFrame` (frame channel, one sample per XR frame) with radial stick deadzones and
trigger deadzones; `Bf4PadLayout.h` holds BF4's default scheme; `Bf4XInputSink.cpp`
hooks `XInputGetState`/`GetCapabilities` for user 0 while active (a real pad works
otherwise). The pad is neutral while the mod menu owns input and is handed back on
lockdown. Reload goes through pad X only while button reload is the effective mode and
no native reload binding exists. The host runs with `--theater-input` so gameplay input
keeps flowing while the theater panel is up (BC2 default unchanged). Capability
`VirtualGamepad`: Candidate (emulated end to end; BF4's response to the pad unverified).
Map (handedness, turning and deadzones become mod-menu settings later):

| Control | Action |
| --- | --- |
| Left stick / click | move / sprint |
| Right stick X | snap or smooth turn |
| Right stick Y | crouch (down) / jump (up), edge-triggered |
| Right trigger / left trigger | fire / aim |
| Right A / B | jump (pad A) / reload (pad X, button-reload mode) |
| Left X / Y | crouch (pad B) / switch weapon (pad Y) |
| Right grip / left grip | grenade (RB) / spot (LB) |
| Right / left stick click | melee (RS) / sprint (LS) |
| Left menu | mod menu ("Game menu" item = pad Start); both stick clicks 1 s: recenter |

## Installed build (dev PC)

| Item | Value |
| --- | --- |
| Executable | `bf4.exe` x64, base `0x140000000`, PE timestamp `1487609024` (2017-02-20), build 179547 |
| SHA-256 | `9d9d1a3fca6ffd4c34a543b3fd7b049336ddbd43c0e6c5eee742e51431a6c2aa` |
| Protection | EA OOA wrapper (`.ooa`, `ctr` sections; `.text` encrypted on disk): scan in memory after decryption |
| Anti-cheat | no EA AntiCheat; PunkBuster (`pb/`) only on PB multiplayer servers |

## Camera discovery (M1)

Static findings and offsets: [BF4-CAMERA-RE.md](BF4-CAMERA-RE.md). Leads: view transform
setter `+0xB8A0A0`; per-view setup `+0x848B00`; a build-pinned `GameRenderer` global
candidate at `0x142672378` with the render view at `+0x60` (from the earlier design notes;
unverified). Plan: confirm read-only in a live session (peek), optionally find the writer
with a one-frame hardware write watchpoint on the view matrix (`RtlLookupFunctionEntry`
for the function start), then write only float matrices through an `ExactWriteJournal`.

## Reload, holsters, IK

- `ReloadMode` is a player setting now. `ResolveReloadMode` turns Manual into ButtonPress
  with a visible note while `ManualReload` is Disabled; the M5 implementation registers an
  `IReloadRouter` and flips the capability, with no caller changes.
- Holsters (`IHolsterSystem`) and IK (`IArmIkSolver` + `IRigWriter`) share one `BodyFrame`
  estimated from head + hands. Holster/magazine ownership is persistent state, reset only
  on lifetime transitions, per AGENTS.md. `IRigWriter::RestoreAll` is the exact-write
  restoration hook. FvrCore's existing `ArmIk`/`ArmPoleVectorMath` is the solver.

## Safety rules specific to BF4

- No writes to game memory unless the binding resolved uniquely, its validator passed and
  its capability is at least Candidate (Enabled for player sessions).
- Unknown builds may scan read-only so their fingerprint and binding report reach feedback.
- Multiplayer evidence disables everything for the rest of the process.
