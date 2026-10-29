# Coordinator runtime02 — October 28–29, 2026

The shared foreground blocker is now separated into two observed conditions.
The tool sandbox uses another Windows desktop; its foreground query returns no
window. The new `game_window.py --desktop-context` records desktop/input ownership
and refuses game operations before process access when that ownership is unproved.
The same diagnostic on the active Default desktop passes. Package preflight remains
independent of desktop/game access. Native source and the batch01 binaries are unchanged.

Window helper tests pass33/33; three targeted package checks pass. The first test
interpreter lacked Pillow and had a package-specific `_pth`; its five skipped image
tests and two package-import failures are retained. The available Pillow interpreter
and canonical full Python resolve those environment issues without source changes.
Independent source review found no actionable issue.

Runtime02 scene01 used a fresh original process and preserved all three profiles.
Direct activation failed; a manual-reply recheck found ChatGPT foreground, and a
bounded45s wait found the browser foreground. The existing launcher activation helper
then passed one burst/four stable observations with both input queues detached and no
keyboard/mouse events. A later window/foreground postcheck refused capture; a
read-only follow-up found another application foreground and unchanged game geometry.
The position-only alignment recheck initially reused a transformed coordinate; that
private-wrapper error was corrected and retained as a failed attempt. No menu image,
gameplay input or native attachment was accepted. Scene01 closed normally and restored
all three original profiles. Launcher0.1.12 and settings remain unchanged.

Windows has one foreground window across monitors. The user is using the other
monitor; continue offline work and request a short uninterrupted test window only
when the diagnostic is ready. Do not repeatedly activate BC2 while the user works.

Saved SteamVR logs join the prior `XrResult=-2` episode to no connected HMD. Prior
successful sessions used Steam Link/vrlink. VirtualDesktop was already blocked during
those successful sessions, so no driver or OpenXR registration change is justified.
The user confirms the headset is unavailable; no new XR preflight is warranted.

Lossless compression of66 current generated symbol files reclaimed669319168 bytes;
every content hash stayed unchanged. Build admission retains the3GiB reserve and
unchanged40GiB/15GiB limits. The Nambu preparation passed actual batch01 validation.

Local evidence: `reports/coordinator-20261028-runtime02`. Scene recipes bind the
current integration lease, exact game lifetime, fresh names, bounded captures and
profile restoration. The scoped Nambu diagnostic build is pending;
its existing fixture needs three compile-time probes and excludes grenade/knife/
contextual opt-ins. Its result cannot qualify CHAT07 combined normal interactions.
No new installation, native equipment acceptance or headset acceptance is claimed.
