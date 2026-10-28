# Selected knife delivery and opening loading text — 431

## Reproduced failures

The 430 emulated physical jab was recognized correctly: input 649, approximately
0.999 m/s, 0.063 m travel, alignment 0.999996. The adapter prepared and published
one intent. However, none of the three observed native firing copies received
Fire, and neither native acquisition nor attack ran. The intent ultimately
became unknown. This was a delivery failure before target selection, not a
melee miss. The separate trigger check reached native acquisition and returned
a clean out-of-range miss.

The native input cache is replaced by every original Gather. The adapter set
Fire only when recognition first emitted an intent, then cleared it on the next
Gather while the same intent was pending. A native consumer between those
updates could see Fire; a later consumer saw only the cleared cache.

The missing SP01 text is a separate presentation problem. Its loading screen
owns `UIManager+0x82b28`, independently of interactive MainMenu/Pause. The old
menu lane required an entered interactive menu. A baseline Entire Mission
restart observed the typed native loader for about 39 seconds while the VR
world captures remained dark. Desktop captures made after mirror composition
cannot establish what the original loading raster contained.

## Corrections

The selected-knife adapter retains the same published Fire level across valid
Gathers until the exact server firing callback consumes it or acquisition
starts. It never extends the intent's original deadline, changes its serial,
replays a completed gesture, or writes target health. Current input, hand,
selection, menu and native ownership checks still apply. Native readiness is
needed to admit a new attack; prediction leaving idle does not erase an already
admitted attack. Invalid or reordered input revokes delivery. A failed later
publication cannot erase the ambiguity of an earlier successful publication.

BC2's native loading-owner reader verifies construction, retirement, queued
transition and resource-path evidence. It is read-only and publishes a passive
menu raster with no input controller. The XR presenter displays that raster to
both eyes and suppresses world/HUD submission while it is active. Passive
screens cannot produce laser clicks, Back or menu-toggle edges. Entering and
leaving loading retires the old image/control epoch.

Present samples loading ownership before the desktop mirror. A contended or
unknown observation preserves the native backbuffer instead of using a cached
Gameplay state to overwrite the first loading frame. The producer, IPC channel
and XR consumer all require matching image mode/epoch and original deadlines.

## Validation and remaining acceptance

- The repeated-Gather regression uses the real melee policy and native input
  cache adapter. It demonstrates the old erased Fire level and checks bounded
  retention, predicted-state changes, consumption and rejected continuations.
- The earlier 430 renderer/calibration regression covers recognition surviving
  independently recalculated geometry for one immutable controller packet.
- Five focused passive-display suites passed, including the actual OpenXR host
  against a test runtime, pixel transfer, held controls, expiry and return to
  fresh world frames.
- The native loading reader passed its bounded mocked-memory cases and static
  verification against the installed executable.

Both complete normal builds passed 294/294 suites. Frozen runtime receipt:
`6e5206be9444ae87bf8e3b1d6200d9c1be4df28a62f1ea1a43d4f2229115769e`.
Native source:
`c8bfcc271268727b7195f473a17d34068587bf65d16fbf091f8afde64a255c0a`.

Actual-game `motion43101` delivered exactly two motion intents, with zero
trigger/button input. Each retained Fire across three cache publications for
about 10 ms, then reached native acquisition within the original 100 ms
deadline. Each native attack returned once; both were out-of-range misses.
Pending returned to zero and the second gesture rearmed after the first
completed. The independent audit found no duplicate native attack or owner
failure. This is delivery and miss-recovery evidence, not enemy-damage proof.

The actual-game `intro/candidate431-01` restart captured nine passive surfaces.
Root image review confirms all three SP01 narrative cards on that native
game-to-menu-raster path, with fresh matching mode/epoch and unexpired source
deadlines. One Entire Mission restart completed, fresh world pairs resumed,
and the native hooks/view graph detached cleanly. This was not a physical
headset compositor capture.

Further navigation's `approach43107` delivered both requested strokes, but only
the first became an attack. A native-read gap during the second reset recognition
into settling. This remaining recognition issue is separate from retained Fire
delivery and is under investigation. Guard damage/objective progression remains
unverified. Installed launcher 0.1.10 is unchanged. This document does not
establish human headset or full-campaign acceptance.
