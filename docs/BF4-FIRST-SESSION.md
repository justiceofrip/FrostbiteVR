# BF4 headset session: checklist (0.4.26-local)

What to try in the next headset session, and what each check tells us. About 15 minutes.
Everything is input-only or an exactly restored camera/FOV write, apart from the optional
3D test at the end.

## Before launching

1. Steam: if a "launch with arguments" prompt is waiting, **Cancel** it.
2. Start the headset link (Quest Link / Air Link / Virtual Desktop) so the OpenXR runtime
   is up.
3. Start the launcher: `runtime\bf4\versions\<current>\Launch FrostbiteVR.cmd`. Leave the
   developer features on: the logs, probes and the freeze watchdog come with them.

## Menus (theater screen)

- The laser clicks the game's menus, including **Campaign > Resume** (a held trigger keeps
  its press position unless the laser moves more than 6 % of the screen).
- The menu button opens the FrostbiteVR menu: two columns now, every item clickable
  (Recenter was off the panel before).
- Never open Multiplayer: the adapter locks itself off there.

## In a mission (head-tracked view)

1. Look around: the world should turn the same way as your head. Note how smooth it is:
   two frame channels should update the view more often than before (~17-25 Hz).
2. Aim with the right controller: the gun follows it, and a **green ring** shows where it
   aims (BF4's own crosshair stays at the centre of your view). Grip with the left hand in
   front of the gun to aim with both hands. The right stick turns in **30 degree steps**
   (menu: Snap turning off for smooth).
3. Walk with the left stick: the edges darken while moving (menu: Comfort vignette).
4. Holsters (grip in the zone; a light buzz when your hand reaches one you can use):
   right hip = sidearm (key 2), over the right shoulder = primary (key 1), left hip =
   grenade (grab, then let go away from the hip to throw), chest with the left hand = knife.
5. Reload by hand (Reload: Manual): **B drops the magazine** (the trigger stops firing; an
   amber ring marks the belt pouch, front left). Grab there with the left grip and push
   it to the green ring under the gun: BF4 plays its reload. Letting go early drops it.
   **Hold B** for an instant reload. A fresh magazine pushed into a loaded gun also reloads.
6. If something feels wrong, open the menu and click "Mark this moment for feedback".
7. Quit from the game's own menu, so the launcher restores the game folder.

## Optional: 3D depth (developer)

Menu > 3D depth > On, in a mission. So far the game stops presenting about 2 s later. If
it freezes, wait at least 5 s before closing it: the watchdog writes every thread's stack
to `bf4-adapter.log` (after "diagnostics: no frame presented for"), which shows where the render thread waits
(BF4-STEREO.md, "The freeze after split stereo engages"). The next session starts with 3D
off automatically.

## After

The launcher's feedback button bundles the logs. The lines that answer the open questions:

| Log line (`runtime/bf4/logs/bf4-adapter.log`) | Tells us |
| --- | --- |
| `theater_link ... bridges=2 pairs= queued=` | the two-channel pair rate in game |
| `view_override ... run=` | head tracking frame sync |
| `controller_aim=... gain= mutes= snaps=` and `mouse_sends=` | mouse aiming reached the game |
| `gestures gun=... draws= throws= ejects= seats=` and `key_sends= haptics_sent=` | holsters, keys and pulses |
| `reticle_pairs= marked_pairs=` | aim dot and gesture markers drawn |
| `diagnostics: no frame presented ...` and the stacks after it | where a 3D freeze waits |
| `laser click down/up at x,y` | menu clicks (Resume) |

`python tools/bf4probe/summarize_session.py runtime/bf4/logs/bf4-adapter.log` still gives
the camera, session-signal and weapon-census verdicts. If the headset shows nothing, send
`xr-host.log` from the same folder too.
