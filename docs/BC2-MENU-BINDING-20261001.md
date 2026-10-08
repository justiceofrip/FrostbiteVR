# Native BC2 menu pointer binding â€” October 1

Static executable discovery and deterministic dispatch tests pass. No live menu
input, hooks, OS cursor changes, or game-memory writes were performed. Native
menu-state and XR panel integration remain necessary for usable headset menus.

## Proven route

Executable: `D:\Games\Battlefield Bad Company 2\BFBC2Game.exe`, SHA256
`3911fcc8914b0158c434cee36f19295316e715d4c9a0e70c857e8c39e3d54258`.
Addresses below are preferred-base VAs (`0x400000`); implementation returns RVAs.

| Binding | Evidence |
| --- | --- |
| Mouse pump `0x909290` | `__thiscall(self, inputNode)`, ret4 at `0x9094C2`; caller `0x941E33` pushes inputNode, loads self into ECX, calls at `0x941E36`. Self owns wheel accumulator `+0x60`, and is **not** the manager. Candidate owning-thread boundary, requiring native observation before enabling. |
| Manager `0x19A4CA8` | Same global is loaded before cursor and button calls. |
| Cursor `0xF82050` | `__thiscall(manager, int x, int y)`, callee pops8; resolves `manager+0x14`, then `ui+4`, and calls `0x12EBC20`. |
| Event `0xF82020` | `__thiscall(manager, const uint32_t event[3])`, callee pops4; same target chain, wrapper `0x12EBC60`. |
| APT readiness | Sinks `0x1309260` / `0x13092A0` require `0x1BEA910 != 0`, `0x1BEA704 == 0`, target's `+0x18` queue. These gates do **not** prove menu-open. |
| Queue | `0x1304B50` checks count `+0x28` against capacity `+8`, de-duplicates consecutive identical words, appends through buffer `+0x2c`. |
| Canvas | Conversion `0x909060` scales to **1280 Ã— 720**, constants `0x143BB60` / `0x143BB58`, after native display-aspect/viewport conversion. |

The original pump positions the cursor before a related mouse event. First
button down/up cases construct `{0,0,1}` / `{0,1,1}`. `0x1304BC0` packs these as
`0x5` / `0x405`; `0x130DB50` decodes button/state/device. Mouse dispatchers
`0x130D8F0` / `0x130D740` map state0 to mask`0x10`, state1 to`0x20`.
The APT wrappers select and restore their native target themselves. Invoke those
wrappers; do not patch target globals or write the queue directly.

## Implemented interface

`src/games/bc2/Bc2MenuBinding.h/.cpp` supply:

- `DiscoverMenuBinding`: unique executable signatures, pump/cursor/event call
  relationships, first-button case table, canvas constants, APT readiness,
  target restoration and queue encoding. Actual executable passes; a mutated
  wrapper fails. A build hash alone is insufficient evidence.
- `MenuBindingCodeSpans`: derives nine complete native function/code ranges from
  the verified call graph, including the pump switch table, wrappers, sinks and
  queue encoding. Runtime must compare all nine with the loaded image before
  enabling. Mutating each entry in an offline copy rejects discovery.
- `ReadMenuTarget`: bounded read-only manager â†’ UI â†’ target â†’ queue resolution,
  then an exact second snapshot. Rejects invalid readiness, null relationships,
  bad queue bounds, unreadable endpoints and address overflow. Its 16,384-entry
  ceiling is a conservative read bound, not an asserted native queue size.
- `MakeMenuPointerCommand`: finite normalized **logical-canvas** UV to integer
  coordinates, preserving generation, menu epoch and original deadline. Raw
  desktop/surface UV needs the correct canvas/letterbox conversion first.
- `DispatchMenuPointer`: explicit native UI-thread, verified menu-owned, focus,
  tracking, matching epoch and unexpired-command guards. Revalidates identity
  and capacity, calls cursor first, then revalidates before optional primary
  down/up. At most two callbacks; no arbitrary keys or script commands.
- `MenuOwnedPress`: non-copyable evidence minted only after successful down;
  normal up requires it, duplicate down rejects, successful up consumes it.
- `ReleaseMenuPointer`: cleanup up only, with no cursor movement. Requires the
  original epoch, exact target/queue identity, original binding/base, native UI
  thread, ready queue and space. Current focus/tracking/menu-owned may be lost.
  Failures retain the token for bounded runtime retry; changed targets never
  receive the old up. Forget only when the original UI/session is retired.

Callbacks let runtime invoke the proven `__thiscall` wrappers; tests substitute
fixtures. `Invoked` means a wrapper call, not confirmed menu activation. The
channel/interaction owner must consume edges once, implement neutral rearming
and cancellation, and prevent old clicks replaying after reconnect/menu changes.
Queue checks are not a cross-thread reservation: dispatch on the original UI
thread, never the XR thread. A ready APT target can still be the gameplay HUD.

## Integration and acceptance

1. Join the independent native menu-state binding and its epoch. Manager
   existence, APT readiness and gameplay inactivity cannot authorize clicks.
   `EntryAction::Menu(32)` differs from `InputConceptMenu(57)`; no guessed toggle
   is enabled. Unknown startup state remains disabled until established.
2. Observe the original pump's thread/target and use one hook to consume commands,
   preserving native device processing. Wire the typed cursor/event callbacks.
3. Put the actual active menu in a separate XR panel; map laser UV to the native
   canvas, including letterboxing. Preserve normal world stereo separately.
4. Gate gameplay actions while the verified menu owns interaction. Test native
   hover/select, checkpoint/death menus, held-trigger entry, tracking loss,
   reconnect, stale messages, target replacement between calls and cleanup.
5. Require native/UI evidence and headset feedback before claiming menu support.
   Accepted support grips and sight mechanics are outside this binding.

`tests/Bc2MenuBindingTests.cpp` covers ownership races, stable reads, capacity,
overflow, UV bounds, no calls without every guard, cursor-before-button ordering
and exact event triplets. Cleanup tests cover no owned down, duplicate up, lost
focus/tracking/menu, wrong epoch/binding/target, queue-full retry and call failure. Its optional executable argument checks real static
signatures and a mutated-copy rejection. Standalone test artifacts are recorded
in `reports/menu-binding-static-20261001.json`; runtime/CMake wiring belongs to
the integrating agent.
