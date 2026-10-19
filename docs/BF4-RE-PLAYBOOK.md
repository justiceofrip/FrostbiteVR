# BF4 reverse-engineering playbook

Every task produces: a binding entry in `src/games/bf4/Bf4Bindings.cpp` (pattern +
evidence note), a validator, and a short note in CURRENT-BF4.md. Work on a copy of
`bf4.exe` on G:, never on the installed file. Do not paste patterns from cheat forums:
they target other builds and carry no validation.

Tools: Ghidra with `tools/ghidra/Bf4FindAnchors.py` and `Bf4MakeSignature.py`, or the
Reverse Engineer Anything MCP with the same task list as prompts. Frosty Editor (BF4
profile) for reading EBX data. x64dbg only on campaign, with the launcher's session
running so the proxy and lockdown behave as in play.

Ghidra setup that saves disk: one project on G:, analyze once, keep the default analyzers,
skip "Aggressive Instruction Finder". Expect a large project for a ~30-40 MB executable.

## R0 - imports and loader (do first, 15 min)
`dumpbin /imports bf4.exe` (or the anchors script's imports section). Confirm dxgi.dll
or d3d11.dll is imported or loaded early, and whether DirectInput8 / raw input / XInput
are present. Outcome: proxy choice confirmed (dxgi.dll by default), input sinks to try.

## R1 - build fingerprint
SHA-256 of `bf4.exe` (the launcher logs it). Enrolling a build = one `KnownBuild` row in
`Bf4Profile.cpp` with the hash, a label (store + date) and the checkpoint that validated
its bindings. Hash alone never enables a feature.

## R2 - native stereo path (highest value)
Anchors: strings `StereoSeparationScale`, `StereoConvergenceScale`, `NvAPI_Stereo*`,
`AmdDxExt*`, `nvapi64.dll`, `atidxx64.dll`. Follow xrefs from the console-variable
registration to the renderer code that reads the separation value; find the branch that
renders the scene twice and where the active eye / eye offset is chosen.
Goal: (a) the gate that enables the two-pass loop, (b) the point where each pass builds
its view/projection, (c) the render targets each pass writes. Bindings:
`Renderer.StereoEyeLoop`, `Renderer.StereoActiveEye`.
Validator idea: the separation cvar's storage address read by the function matches the
one the console writes.

## R3 - camera and render view
Anchors: `GameRenderer`, `RenderView`, FOV-related strings, `CameraData` TypeInfo.
Find the global renderer instance and the per-frame function that writes the view
transform and FOV. Confirm Frostbite's basis order and +Z forward for BF4. Bindings:
`GameRenderer.Instance`, `RenderView.Update`. Validator: vtable in .rdata; the FOV field
reads as the in-game FOV setting in radians or degrees.

## R4 - TypeInfo reflection
Locate a known TypeInfo name string (`SoldierWeaponData`) and the static TypeInfo object
that references it; walk to the list head. Binding: `TypeInfo.FirstTypeInfo`.
Then add a read-only dumper (developer sessions only) writing class/field/offset JSON to
the log folder, size-capped. Field offsets for data classes then come from the build.

## R5 - session signals
Level name (campaign is `Levels/SP_*`) and "connected to a remote server". Bindings:
`ClientGameContext.Instance`, `ClientLevel.Name`. Until these exist, the guard only knows
about PunkBuster modules, so gameplay features stay off (presentation still works).

## R6 - input concepts (button reload)
Anchors: `ConceptReload` / input-concept enum TypeInfo. Find the per-tick concept cache
the soldier reads. Binding: `Input.ConceptCache`. Button reload asserts the reload
concept for exactly one tick; fire/use follow the same route later.

## R7 - UI cursor (only if Win32 injection is not enough)
If menus ignore the injected cursor: check imports for `GetRawInputData`,
`DirectInput8Create`; find where Scaleform receives mouse position. Binding:
`UI.CursorRead`. Also find a menu-visible flag for M1+ pointer gating.

## R8 - first-person weapon transform (controller aim)
Find where the first-person weapon/camera-relative transform is composed, and the shot
origin/direction source (`ShotConfigData.InitialPosition/InitialDirection` in EBX via
Frosty). Binding: `Soldier.WeaponTransform`. Lesson from BC2 Nambu: the stock shot
origin can sit far from the displayed muzzle; measure both before claiming aim accuracy.

## Making a pattern
Cursor on the instruction you want (or the RIP-relative load of a global), run
`Bf4MakeSignature.py`, paste the printed line, set `kind/adjust/dispOffset/length`, add a
validator, rebuild, run `Bf4CoreTests` and the emulator, then one real campaign session.
