# BF4 first live session: checklist

One developer session answers most open questions (camera, session signals, client/server
weapons, the next ownership link). Everything the adapter does in it is read-only except
the theater view, laser clicks and the virtual gamepad. About 15 minutes.

## Before launching

1. Steam: if a "launch with arguments" prompt is waiting, **Cancel** it.
2. Steam > Battlefield 4 > Properties > Launch Options: `-RenderDevice.StereoEnable 1`
   (for the 3D Vision stereo test; remove it later to go back to normal).
3. Start the headset link (Quest Link / Air Link / Virtual Desktop) so the OpenXR runtime
   is up.
4. Start the launcher: `runtime\bf4\versions\<current>\Launch FrostbiteVR.cmd`. The
   pre-release build has developer features on; leave them on for this session.

## In the game

1. In the headset you should see the game on a screen. Check:
   - the laser pointer clicks the game's menus;
   - the controller menu button opens the mod menu (reload mode, recenter, game menu);
   - sticks move, trigger fires, right B reloads.
2. **Campaign > Resume.** Never open Multiplayer: the adapter locks itself off there, and
   the session's research would end.
3. Play about 5 minutes in a mission:
   - walk around and look around;
   - empty a magazine and reload; reload once with rounds left;
   - switch weapons a few times; pick up a shotgun or a bolt-action rifle if one is near.
   The weapon census runs about 30 s in and then every 2 minutes.
4. Quit from the game's own menu, so the launcher restores the game folder.

## After

Summarise the log (from the repository folder):

```
python tools/bf4probe/summarize_session.py runtime/bf4/logs/bf4-adapter.log
```

It prints verdicts for the camera, the single-player signals, client/server weapon copies,
the active client weapon class and the next ownership link. Paste that output (or use the
launcher's feedback button, which bundles the logs) and the next steps follow from it:

| Verdict | Unlocks |
| --- | --- |
| camera CONFIRMED at `[renderer+0x60]` | head tracking (enrol after the session signals) |
| front end and campaign read correctly | the single-player gate: head tracking, manual reload, holsters |
| client and server copies CONFIRMED, active class | manual reload acknowledgements, the reload driver |
| next link (vtable + offset) | the path to the player's own weapon: holsters, aim, IK |

If the headset shows nothing, the launcher's log folder has `xr-host.log`; send that too.
