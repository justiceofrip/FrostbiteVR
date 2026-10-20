# Main-menu startup and aspect-ratio correction — checkpoint 429

The October 20 tester archive identifies public 0.1.9, the supported executable,
VirtualDesktopXR / Quest 3, an X: installation and a 1024×768 game raster.
It contains one session. Later failed retries were reported by the tester but
are not separately recorded in this archive.

## Observed failures

- The host remained focused, published 2,503 input frames, copied 468 menu
  surfaces and recorded no graphics failures. The native menu read 1,881
  control packets but dispatched no pointer moves or button events.
- `OpenXrMenu::Frame` explicitly disabled the right aim pose for a raster
  other than 16:9. This explains the visible 4:3 menu without a pointer.
- The launcher's readiness check required the world-render `input_samples`
  counter. No world draw callback occurs in this front-end-only session.
  After roughly 35 seconds the launcher declared the connection unready and
  stopped its host. This was not an observed VirtualDesktopXR crash.
- Native cleanup drained successfully, leaving BC2 open with the adapter
  module retained until game exit. Restarting BC2 is required for a new
  session. Reinstalling the mod is unnecessary.

## Implementation

The launcher now accepts a fresh menu-specific handshake independently of
world-render telemetry. It requires a current-epoch focused input packet, a
published menu raster, a live native menu observation and no graphics failures.
The input lease expires and is cleared on menu/owner retirement. A newer
unready telemetry record cannot be overridden by an older ready record.

XR pointing describes the entire captured raster. The native UI callback uses
BC2's original pixel-to-APT conversion at `0x909060`, called by the original
mouse pump at `0x90939C`. Its ABI is cdecl with output/input float-pair pointers.
The complete 560-byte function, referenced constants, three display-class
vtable layouts and their five read-only getters are verified. Live display,
dimensions and viewport state are read repeatedly around the conversion.
Only the existing verified UI callback may dispatch the resulting cursor or
button event. Invalid or changing state does not create a new press.

Retry checks now detect the still-open selected game before opening another
headset session. A failed supervisor still finishing cleanup is reported as
cleanup pending, rather than “already running.” The launcher preserves the
explicit Restart BC2 choice and uses normal window close; it does not delete
caches or forcibly terminate the game.

## Regression coverage

- Launcher startup tests cover menu-only readiness, stale/incomplete records,
  missing input/raster, graphics failure and newer unready records.
- Retry tests execute the entry/session scripts with process/runtime fixtures,
  including cleanup pending and a retained game after failure.
- The actual OpenXR menu presenter is exercised with GPU textures and fake
  swapchains at 1024×768, 1280×800, 1920×1080 and 2560×1080. It must retain
  panel, cursor and beam, send off-center pointer input, and release/rearm
  buttons through focus loss. Restoring the old aspect guard fails this test.
- Native dispatch tests cover all three verified display classes, viewport
  changes, denied callback context and original mapped coordinates for down/up.
- A private instruction-level fixture executes the original mapper/getter
  machine code across 72 aspect/viewport/point combinations. All retain the
  cdecl stack and callee-saved registers, with writes confined to stack/output.
  This is ABI evidence, not a headset test.

Both full normal architectures passed 293/293 C++ suites. Separate PowerShell
checks passed 33 startup, 24 retry, 15 session and 42 portable-runtime assertions.
The actual-game `menu02` check observed 45 seconds of menu-only operation with
fresh launcher readiness beyond the former 35-second timeout. World input stayed
zero; the original mapper converted raster UV (0.25, 0.25) through pixels
(480, 270) to native logical coordinates (320, 180). It sent no clicks, menu
toggle, keyboard/mouse input or gameplay action. Both helpers exited zero,
hook cleanup passed, and the root-owned game subsequently closed normally.
The retained menu raster was visually inspected as the intact BC2 main menu.

This live renderer was 1920×1080. A prior nonactivating window resize changed
the client area to 1024×768 but did not change the internal D3D11 dimensions;
that attempt is inconclusive for actual-game 4:3 acceptance. Other aspects have
presenter/GPU and original-instruction coverage, not a live 4:3 game claim.
The first live harness invocation omitted the normal build's required resource
session flags and was rejected with code 10 before native initialization. Its
evidence is retained; the corrected invocation used the unchanged frozen binary.

The retained tester text, instruction proof, build receipts and bounded live
menu results are under private `bc2vr-recovery/startup429`. Raw game data,
tester paths and captures are not distributed with the public package.

Actual Virtual Desktop headset acceptance, complete campaign playability and
death/reload crash recovery remain separate, unresolved acceptance items.
