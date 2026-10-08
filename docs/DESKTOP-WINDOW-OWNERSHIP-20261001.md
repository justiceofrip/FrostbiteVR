# BC2 desktop window ownership — 2026-10-01

BC2 owns a desired fullscreen setting and a cached windowed style. Changing
DXGI state alone changes neither. Static code proves the configuration path,
style capture and style restoration. Parent's independent read-only snapshot
now confirms that the active renderer's cached style lacks WS_VISIBLE and
matches the hidden HWND's current style.

A next-launch preparation tool is ready. This investigation changed no real
settings or live game state. Initial hidden-launch attribution remains unknown.

## Evidence

Inspected executable: D:\Games\Battlefield Bad Company 2\BFBC2Game.exe.
SHA256: 3911FCC8914B0158C434CEE36F19295316E715D4C9A0E70C857E8C39E3D54258.
Preferred base: 0x400000. All addresses below are VAs for this build, not portable
Frostbite offsets. Exact code-span bytes/hashes and source hashes are in
[the static evidence report](../reports/desktop-window-static-20261001.json).

Existing capture
[native-trace-20261001-145328-952](../reports/native-trace-20261001-145328-952/native-trace.json):

| Phase | DXGI windowed | HWND visible | Foreground is game |
|---|---:|---:|---:|
| First Present | 1 | 1 | 0 |
| First stereo pairs | 0 | 1 | 1 |
| Two seconds | 0 | 1 | 1 |
| Eight seconds | 1 | 0 | 0 |
| Retired | 1 | 0 | 0 |

All 2,889 Presents succeeded; all five final textures contained colored game
content. The HWND stayed noniconic, responsive and without a ghost replacement.
The window-mode helper saw an initially windowed chain and made zero transition
attempts. The record establishes native state changes after that observation.

Parent's [live owner snapshot](../reports/window-native-settings-20261001-1510.json)
matches the verified renderer (403372032), vtable (21272480), exact swapchain
(400090040), and HWND (18354612). Desired Fullscreen is 1. Cached and current style
are both 80216064 (0x04C80000), without WS_VISIBLE (0x10000000). This corroborates
the native cached-hidden-style restoration mechanism. It does not identify
which original launch action or earlier transition created the cached style.

## Native configuration path

- String initializers 0x1372A90 / 0x1372AF0 create WindowSettings / Fullscreen
  key strings at globals 0x1570FA8 / 0x1570FCC.
- Load routine 0x7DBD60, at 0x7DBE50..0x7DBE82, reads this exact section/key through
  GetPrivateProfileStringA (IAT 0x14051D8), parses with native bool parser
  0x512400, and stores the result in the graphics-options object's +0xAC.
- Startup 0x570410..0x57044D constructs options (0x7E44B0), loads them, applies
  defaults/writeback if load fails, and calls apply 0x7DE030.
- Apply checks/clears its +0xC0 dirty flag. At 0x7DE3EB..0x7DE423, it passes the
  +0xAC bool and exact name DX11DisplaySettings.FullScreen to typed setter
  0x559560 through registry global 0x155CFDC. That setter serializes a bool and
  routes it through 0x4F4C00; this is not yet a verified standalone graphics-thread
  mode-change API.
- Save routine 0x7DD1F0, specifically 0x7DD307..0x7DD32F, writes the same +0xAC
  byte back to the INI via WritePrivateProfileStringA. Editing while the game is
  alive can therefore be overwritten.
- Global 0x1570EA0 is the options object used by native update
  0x5D1685..0x5D1693 before dirty apply. Exact live owner/type and calling-thread
  proof are still required before considering runtime calls.

The read-only settings observation found [WindowSettings] Fullscreen=true.
A direct DXGI transition does not alter this engine preference.

## Exact active renderer fields

Use only the existing discovered/live-verified D3D11 renderer and exact
swapchain/HWND ownership.

| Field | Offset | Static evidence |
|---|---:|---|
| Display settings pointer | renderer +0x40 | Present 0x9EEA4B |
| Desired FullScreen byte | settings +0x35 | Present 0x9EEA4E |
| Swapchain | renderer +0x88 | Job setup 0x9E117B |
| HWND | renderer +0x160 | Job setup 0x9E116B |
| Cached windowed GWL_STYLE | renderer +0x164 | Capture 0x9FBED1, job 0x9E1171 |
| Native current-fullscreen byte | renderer +0x184 | Present 0x9EEB73 / 0x9EEB9B |
| Related presentation flag | renderer +0x185 | Existing wrapper; no new semantic claim |
| Pending reset byte | renderer +0x186 | Present 0x9EEA51 / 0x9EEAEB |

Initializer 0x9FBEB0 calls GetWindowLongA(HWND,GWL_STYLE) at 0x9FBEC6 and stores
that value at renderer+0x164.

Mode method 0x9E1150 copies swapchain, HWND, cached style, and requested fullscreen
bool into a temporary native job. It schedules 0x9D7FD0 at 0x9E11E9, or invokes
it directly at 0x9E1264, respecting the native affinity path.

The job restores the supplied cached style using SetWindowLongA(HWND,-16) at
0x9D8004 when entering windowed mode, then calls the exact swapchain's virtual
slot +0x28 (SetFullscreenState). Present 0x9EEA30 reads desired FullScreen,
updates native +0x184 and calls this mode method with false at 0x9EEB89 or true
at 0x9EEBB8. This explains why a COM-only windowed change can be undone.

A hidden interactive launch could explain how a nonvisible style was cached;
that origin remains a hypothesis. Other ShowWindow imports include unrelated
child/dialog windows and cannot be blamed merely from their presence.

## Concrete next normal launch

[prepare_windowed_launch.py](../tools/prepare_windowed_launch.py) defaults to a
read-only plan. With --apply it:

1. Refuses any running BFBC2Game.exe; process-query failure also refuses.
2. Accepts only the current user's exact Documents/BFBC2/settings.ini, without
   a redirected path.
3. Requires one unambiguous WindowSettings section and Fullscreen true/false.
4. Preserves encoding/BOM, mixed newline styles, comments and all other keys.
5. Creates an exact uniquely named backup before atomic replacement.
6. Rechecks process absence and unchanged source before replacement.
7. Is idempotent when already false.

After normal game exit, the intended invocation is:

    python <local-workspace>\tools\prepare_windowed_launch.py --apply

The next interactive game launch should be normal/visible, followed by existing
nonactivating left-monitor placement. Do not apply -WindowStyle Hidden to the
game when a visible desktop game is requested. Background helper processes keep
their hidden launch behavior. The tool does not launch BC2, manipulate input,
restore fullscreen later, change rendering quality, or run a visibility timer.

Process checks cannot lock out another application starting BC2 in the interval
after the last check. Backups and hashes make this bounded operation reviewable.
The intended next-launch correction still requires native validation through
the previous eight-second failure interval, focus changes, menu roundtrip,
VR attach/detach and shutdown. No live cache mutation was implemented.

## Validation

Seven offline test methods pass: UTF-8/BOM/UTF-16/legacy comments, mixed newline
preservation, duplicate/malformed entry rejection, dry run, backup integrity,
atomic edit, idempotence, running-game refusal, wrong-path refusal, game-start
race and concurrent settings edits. Tests touch temporary fixtures only.
Native render/menu/runtime candidate files remained frozen.
