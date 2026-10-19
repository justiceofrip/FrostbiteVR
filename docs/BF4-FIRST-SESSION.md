# BF4 headset session: checklist (0.4.39-local)

What to try in the next headset session, and what each check tells us. About 15 minutes.
Everything is input-only or an exactly restored camera/FOV write, apart from the optional
3D test at the end.

## Before launching

1. Steam: if a "launch with arguments" prompt is waiting, **Cancel** it.
2. Start the headset link (Quest Link / Air Link / Virtual Desktop) so the OpenXR runtime
   is up.
3. Start the launcher: `runtime\bf4\versions\<current>\Launch FrostbiteVR.cmd`. Leave the
   developer features on: the logs, probes and the freeze watchdog come with them.

Sharpness: each eye is cut from the game's one wide-angle frame (about 110 degrees tall),
so the game's own resolution sets how sharp the headset looks. If the GPU has headroom,
raise BF4's Video > Resolution Scale (150-200 %) before judging blur.

## Menus (theater screen)

- The laser clicks the game's menus, including **Campaign > Resume** (a held trigger keeps
  its press position unless the laser moves more than 6 % of the screen).
- The menu button opens the FrostbiteVR menu: two columns now, every item clickable
  (Recenter was off the panel before).
- Never open Multiplayer: the adapter locks itself off there.

## In a mission (head-tracked view)

1. Look around: the world should turn the same way as your head. Note how smooth it is:
   two frame channels and the earlier next pose should update the view much more often
   than before (~17-25 Hz; expected about game fps / 2 at frame sync 2).
2. Aim with the right controller: the gun follows it, and a **green ring** shows where it
   aims (BF4's own crosshair stays at the centre of your view). Grip with the left hand in
   front of the gun to aim with both hands. The right stick turns in **30 degree steps**
   (menu: Snap turning off for smooth).
3. The ammo panel and minimap appear as small framed insets in your lower right and left
   (menu: HUD corners); BF4 draws them outside what the headset shows. Note whether they
   show the right part of the HUD.
4. Walk with the left stick: the edges darken while moving (menu: Comfort vignette). In a
   vehicle, set the menu's "Walk toward" to Gun (the stick is throttle and steering there).
5. Holsters (grip in the zone; a light buzz when your hand reaches one you can use):
   right hip = sidearm (key 2), over the right shoulder = primary (key 1), over the left
   shoulder = gadget (key 3), left hip = grenade (grab, then let go away from the hip to
   throw), chest with the left hand = knife.
6. Reload by hand (Reload: Manual): **tap B to drop the magazine** (the trigger stops
   firing; an amber ring marks the belt pouch, front left). Grab there with the left grip
   and push it to the green ring under the gun: BF4 plays its reload. Letting go early
   drops it. A fresh magazine pushed into a loaded gun also reloads. **Hold B** to reload
   at once, or to use / pick up things (it holds the pad's X, BF4's interact button).
7. Optional, standing: menu > Physical crouch > On, then crouch for real.
8. If something feels wrong, open the menu and click "Mark this moment for feedback".
9. Quit from the game's own menu, so the launcher restores the game folder.

## Try: 3D from depth

Menu > 3D from depth > On, in a mission. Each eye is shifted by the game's depth buffer, so
near things should stand out. It needs BF4's Video > Antialiasing Deferred set to Off (a
multisampled depth buffer is refused; the post-process antialiasing is fine). Look at something close (a wall, a crate) and something far.
If it looks wrong (doubled edges, the gun at a strange depth, no depth at all), switch it
off and mark the moment; `vr-snapshot-depth.bmp` and the `depth_capture` log line show what
it found.

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
| `controller_aim=... gain= mutes= snaps= guards=` and `mouse_sends=` | mouse aiming reached the game; its learned sensitivity |
| `gestures gun=... draws= throws= ejects= seats=` and `key_sends= haptics_sent=` | holsters, keys and pulses |
| `reticle_pairs= marked_pairs= depth_pairs=` | aim dot, gesture markers, 3D from depth drawn |
| `depth_capture ...` | the depth buffer 3D from depth found |
| `diagnostics: no frame presented ...` and the stacks after it | where a 3D freeze waits |
| `laser click down/up at x,y` | menu clicks (Resume) |

`python tools/bf4probe/summarize_session.py runtime/bf4/logs/bf4-adapter.log` still gives
the camera, session-signal and weapon-census verdicts. If the headset shows nothing, send
`xr-host.log` from the same folder too.
