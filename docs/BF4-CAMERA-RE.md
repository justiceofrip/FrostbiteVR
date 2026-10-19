# BF4 camera / render view (RE task R3): findings so far

Evidence level: **static analysis only** of the decrypted research image of bf4.exe build
179547 (sha256 `9d9d1a3f...c6c2aa`), RVAs relative to `0x140000000`. Nothing here is a
binding yet; every item needs a live read-only check before any feature uses it, and any
camera write needs an exact-write lease plus the player's explicit go-ahead.

## Settings that shape the view (reflection-confirmed offsets)

`GameRenderSettings` (settings container "Render", console `Render.*`):

| Field | Offset | Notes |
| --- | --- | --- |
| NearPlane | 0x40 | read by the view setup, abs() |
| ViewDistance | 0x44 | far plane = ViewDistance + view near term |
| ForceFov | 0x48 | if non-zero the view FOV is forced (degrees, scaled) |
| FovMultiplier | 0x4C | the options menu writes `FieldOfViewVertical (clamp 50..105) / 55` here |
| ForceOrthoViewEnable / ForceSquareOrthoView | 0xA6 / 0xA7 | ortho path |
| ForceOrthoViewSize | 0x50 | |

So the player's vertical FOV = 55 deg x `Render.FovMultiplier`, and `Render.ForceFov` is
the candidate for matching BF4's FOV to the headset (to verify: settable via user.cfg,
and which projection it produces).

Options apply code (`+0x390C40`): reads profile values, gets live settings containers by
name through `0x688C40(settingsManager, "Name")` (`settingsManager` = `[+0x261E930]`
global), writes Brightness -> "PostProcess" +0x114, FOV -> "Render" +0x4C.

## Per-view setup (`+0x848B00`)

`this` caches the "Render" settings at `[this+0x48]` (set in `+0x8392DB`). For each view:
copies the source view state with `+0x83A710` (three 0x4E0-byte blocks via `+0xB79380`
plus trailing fields), then applies settings through small view setters:

| Setter | Field | Meaning |
| --- | --- | --- |
| `+0xB89F40` | +0xBC | near plane |
| `+0xB89EC0` | +0xC0 | far plane |
| `+0xB89EE0` | +0xB4 | FOV-Y in degrees; derives +0x250 from tan(fov) and aspect (+0xC4) |
| `+0xB89FB0` / `+0xB89F90` | +0xC8 / +0xCC | ortho width / height |
| `+0xB89FD0` | +0xB0 | ortho mode |
| `+0xB89F70` | +0xA0 | 16-byte vector |
All mark `+0xE0` dirty (flag 0xB / 0xF).

## View transform setter (`+0xB8A0A0`), verified 2026-10-19

Leaf function, `rcx` = view, `rdx` = 16-byte-aligned 0x90-byte block (movaps). Compares
rows `+0x00..+0x30` and `+0x80` on x, y, z only (mask 7: LinearTransform rows are padded
Vec3) and returns if unchanged; otherwise copies all 0x90 bytes and sets
`[view+0xE0] |= 0xB`. Callers fill the block as {transform, the same transform again,
a vector that is zero on the camera path}. 26 direct callers.

## Per-frame view pipeline (`+0x828090`), static, 2026-10-19

1. Up to four stack view blocks of 0x1020 bytes each (main view at +0x000, a secondary
   view at +0x9C0 with an enable byte at +0xEA0, e.g. a scope picture-in-picture).
2. Camera manager global `+0x2673D80`: `+0x8AAF90(manager, index, 1)` returns camera
   `index`; `+0x8A9160(camera, viewBlock)` fills that block from the camera's view state
   (`camera+0xEF0`) and calls the setter. One block per local player view; the count
   comes from `[+0x2738080]` vtable `+0x58`.
3. Optional override: `+0x820920(this, outBlock)` returns true with a replacement block
   (settings- and soldier-dependent), applied through the setter to block 0.
4. Submit: GameRenderer vtable slot 5 (`+0x83F0E0`), signature verified from the call
   site and prologue: `(renderer, float, float, uint32 viewCount, viewBlocks*, sync*,
   ptr)`; rdx is unused. It calls the view setup `+0x848B00(renderer, count, blocks)`,
   which copies each block into the renderer's own buffer at `[renderer+0x60]` and
   applies the "Render" settings (near/far/FOV setters). The caller then waits on
   "waitRenderSync".
5. A six-iteration variant of step 4 with per-iteration rotations (a cube-map capture
   path) sits behind a flag; the normal frame submits once.

GameRenderer: `+0x83EF10` allocates 0x148 bytes, constructor `+0x8391E0`, vtable
`+0x1D15A10` (23 slots), "Render" settings at `+0x48`, per-frame view buffer at `+0x60`,
another view state at `+0x70`; **slot 19 is `mov rax,[rcx+0x70]; ret`**, not `+0x60` as
first assumed (the probe now reads the getter's offset from the code and samples both).

### Head-tracking injection point (built 2026-10-19, dormant until R5)

Copy the renderer's vtable (23 slots plus the RTTI locator before it), replace slot 5 and
point the renderer object at the copy: one 8-byte heap write, restored exactly, with no
game-code patch. In the replacement: save block 0's 0x90 bytes and `+0xE0`, write the
composed transform (game camera with the headset pose relative to the recentred
reference) into both transform copies, set `+0xE0 |= 0xB` as the setter does, call the
original slot 5 (the view setup copies the block synchronously), then restore the saved
bytes. The copied vtable is never freed (another call may be in flight). On detach, put
the original vtable pointer back only if the global still names the same object and it
still points at our copy. Mono only at first: the request is made on the Present thread
and consumed one or two frames later on the main thread, so an alternate-eye label would
be wrong half the time until frame association is solved.

Implementation: `src/games/bf4/Bf4ViewOverride.*` (portable; ViewOverrideTests drive it
through a fake renderer laid out like bf4.exe's) and `Win32Platform::ApplyCamera`. The
platform installs it on the first mono camera request, only on the analysed build, only
when both bindings resolve, the live object's vtable lies inside bf4.exe and its slot 5 is
the bound submit. Per submit it composes only onto a rigid transform (otherwise stock
view, counted `notRigid`), and a pose older than 100 ms expires (stock view). Status line:
`view_override=on|off|refused submits applied restored foreign stale`. Bindings
(`Bf4Bindings.cpp`): GameRenderer.Instance = the global load in the normal view-submit
path of `+0x828090`; RenderView.Update = slot 5's prologue; both unique in the decrypted
`.text`. Alternate-eye additionally needs Renderer.StereoActiveEye now.

Blocked on R5: the core requests a camera only in Campaign mode, which needs the
level-name and local-server signals bound, so nothing is installed in the real game yet.

Multi-view lead: slot 5 already takes a view count (local-player views). Rendering two
eye views in one frame through this count is a possible native stereo route; untested.

## Live read-only probe (built 2026-10-19, not yet run)

`Bf4CameraProbe.h` + `CameraProbeTick` (Bf4Module.cpp), developer sessions on build
`9d9d1a3f...c6c2aa` only, every ~2 s (first minute) then ~10 s, VirtualQuery-checked reads:
global `+0x2672378` -> object; logs the slot-19 getter offset parsed from its code and,
for both `[object+0x60]` (view buffer, entry 0 = main view) and `[object+0x70]`, whether
the matrices at `+0x00`, `+0x40`, `+0x420` are rigid transforms (unit orthogonal rows;
w lanes ignored) with translation/forward, and FOV/near/far/aspect plausibility
(`+0xB4/+0xBC/+0xC0/+0xC4`). Expected if the notes hold: entry 0 at `+0x60` holds a rigid
transform at `+0x00` (and the same at `+0x40`) that follows the player, FOV = 55 x
FovMultiplier. Only then does a binding get enrolled and the override get built.
