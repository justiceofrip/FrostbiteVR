# Menu release and campaign restart investigation —326

Normal tracked menu button-up now sets the current VR cursor position before
dispatching the release. Previously, release used the event-only cleanup path,
allowing the native desktop mouse pump to change the release position between
VR packets. Lost tracking and ownership cleanup still release without asserting
a stale cursor position.

Both full normal builds pass244 suites (x86 128.17s, x64 36.56s). The focused
menu binding/control checks pass, including fresh release after desktop cursor
movement and refusal of expired pointer coordinates. Local normal326 is frozen
separately;318 remains preserved. No new headset acceptance or publication.

## Actual-game emulation

Private324 held an original rifle magazine and opened the pause menu, but its
first dropdown click did not advance.325 corrected the diagnostic sidecar's
handling of a Busy menu-state read: it may retain only an unexpired prior
snapshot, never extend its original deadline. The player OpenXR implementation
already had that behavior.325 advanced to confirmation but did not restart.
That run had no Busy reads, so contention is not established as its cause.

Private326 used the corrected player release path and the same325 sidecar.
The three native pointer clicks reached checkpoint restart, then BC2 crashed
before old-scene retirement or replacement-view initialization. This is a
failed campaign recovery test, not a passing menu/restart acceptance.

Evidence is retained locally in `bc2vr-recovery/scene326-native01`, including
the runner, scene journal, exception records, matching DLL/PDB build receipt
and minidumps. The fault executes address zero from `amdxx32.dll+0x1066ab`,
the same command-worker site observed in265. This identifies the failure site,
not its producer or root cause. A mapped command's first function word is zero;
unlike265, the sampled command prefix is not entirely zero. The render thread
was in native UI geometry code at the captured instant. Graphics threading and
resource lifetime need further evidence; no driver workaround is justified yet.

The latest passing combined equipment run remains311 and pistol run309.
Measured grip contacts cover59 variants/33 meshes; manual magazine prerequisites
cover18 configurations across10 asset names. These counts are not full weapon
or sustained campaign acceptance. Pump, knife, chambering and broader weapon
capability limits remain recorded in the existing handoff.
