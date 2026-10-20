# BF4 headset session: checklist (0.4.54-local)

What to try in the next headset session, and what each check tells us. About 15 minutes.
The game side of everything below already ran live without a headset (simulated headset,
CURRENT-BF4.md): this session is about how it looks and feels.
Everything is input-only or an exactly restored camera/FOV write.

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
   about 45 new views a second at frame sync 2 (measured live; it was ~17-25 before).
   Then try menu > Frame sync (developer) > 1 frame (~52 a second; every view matched its
   head pose in the live check) and note whether the world swims or looks smoother.
2. Aim with the right controller: the game's aim follows it, a **green ring** shows where
   shots go, and the gun itself now points there too (menu: Gun follows aim): turn the
   controller and the barrel swings onto the ring. Note whether it stays steady and sharp;
   looking far away from where the gun points, it follows only part of the way. Grip with the left hand in
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
near things should stand out (with BF4's deferred antialiasing on too: the depth is then
read from its first sample). Look at something close (a wall, a crate) and something far.
Some doubled outlines at depth edges are expected (seen live); if it looks wrong otherwise
(the gun at a strange depth, no depth at all, discomfort), switch it off and mark the moment; `vr-snapshot-depth.bmp` and the `depth_capture` log line show what
it found.

BF4's own split 3D (the old developer "3D depth") is retired: it crashed BF4's one-view
world renderer (found live without a headset; BF4-STEREO.md). Its menu item is shown
disabled.

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
| `diagnostics: no frame presented ...` and the stacks after it | where a freeze waits |
| `laser click down/up at x,y` | menu clicks (Resume) |

`python tools/bf4probe/summarize_session.py runtime/bf4/logs/bf4-adapter.log` still gives
the camera, session-signal and weapon-census verdicts. If the headset shows nothing, send
`xr-host.log` from the same folder too.
