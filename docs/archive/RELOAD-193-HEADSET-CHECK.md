# Next focused reload check

Candidate193 passed both full builds (186 suites each) and one actual-game
monitor reload with zero palette fallbacks. It is not yet headset-validated.
Use a fresh campaign instance with ammunition; the monitor DLL remains resident
after its hooks stop, so do not reuse that game process for the headset session.
Keep the headset run open until the user finishes it.

Prepared launcher: `pipeline-runtime-20261005/Launch-Headset193.ps1` under the
recovery root. Its `-ValidateOnly` check verified seven pinned inputs without
opening the game or starting VR. The launcher rejects an existing probe in the
game process. Native evidence is recorded in
`pipeline-runtime-20261005/render-lifetime193-evidence/evidence.json`.

## User checklist (about five minutes)

1. With the XM8 partly loaded, remove its magazine, take a replacement from the
   body and insert it. Watch for the magazine snapping back or disappearing.
   Check whether firing and the support grip become available after seating.
2. Empty the XM8 while holding the foregrip. It must wait for manual reloading.
   Reload once and fire again. Report any automatic reload separately from a
   visual animation or delayed ammunition transfer.
3. Stow and draw the rifle, then repeat a magazine removal. This checks the shared
   inventory/hand ownership boundary that previously broke reloads.
4. If a ground weapon is available, replace one rifle and repeat that same reload
   once. Do not spend the session checking every weapon.
5. With the SPAS equipped, confirm a shell still spawns and can be loaded. This is
   a regression check;193 does not add manual pumping.

Stop at the first reproducible failure and describe the last successful action.
No menu, vehicle, scope or new weapon-class feature is included in this test.

## Evidence to collect

- Exact loaded binary and build receipt, input/owner transitions and native
  magazine start/seat/completion/cancellation counts.
- Magazine fallback journal and the original geometry/claim deadlines.
- Native ammo counts before and after a physical insertion; animation completion
  or a full HUD alone is not evidence that the manual transaction completed.
- Whether the same action fails before or after a holster/pickup boundary.

## Optional Blender calibration workflow

A live Blender MCP connection can make visual profile work easier. Import the
existing captured skeleton/weapon data with explicit scale, coordinate basis and
rig identity. Show grip anchors, magazine/shell insertion rails, pump/bolt travel,
launcher hinge axes and body holsters in one reusable scene. Export measured
profile data through the existing weapon pipeline with provenance and validation.

The MCP connection supplies scene inspection, screenshots and scripted edits; it
does not decode Frostbite assets or validate native reload state. A Blender Python
script can provide the same importer/exporter without making MCP a runtime
dependency. No Blender component has been installed or enabled by this work.
