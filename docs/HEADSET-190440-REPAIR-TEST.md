# Shared empty-reload headset test

SPAS and XM8 passed bounded monitor firing checks. This candidate has no new
headset acceptance; AEK's intermittent failure remains an explicit check.
Use a fresh BC2 process, load campaign with reserve ammo, connect SteamVR and
launch `headset-190440-test-readiness/Start-Focused-Test.ps1`. Keep the session
running until the user finishes. This launcher retains manual reloads, sight
interaction, body inventory and boat head aim/fire; it never enables synthetic
monitor firing.

1. Draw SPAS. Fire to empty while holding support grip, release the trigger and
   wait three seconds. It must stay empty. Take a chest shell, use the bottom
   loading rail and confirm deliberate reload and firing still work.
2. Repeat with XM8 and AEK, one-handed and two-handed. At zero rounds neither
   gun should refill itself. Remove the empty magazine, return that same empty
   magazine once (still zero), then insert a supplied magazine. Confirm firing
   and immediate support-grip reacquisition after normal completion.
3. Stow/draw from both shoulders, swap guns and pick up a gun with both weapons
   holstered. Release the trigger during pickup, then grip/fire/reload normally.
   If available, enter/exit the boat and recheck manual reload after drawing.

Report the weapon, support-grip state and preceding swap/vehicle/pickup when a
failure occurs. These are regression checks on the shared mechanisms; new SMGs,
LMGs, pistols and snipers are not enabled in this candidate. Unknown/underbarrel
automatic reload is intentional for now. Manual pumping remains unfinished.
Hold both thumbstick clicks for one second and release to recenter.

When the user finishes, stop only the owned mod session and collect the final
trace. Keep BC2/SteamVR open unless the user requests otherwise. Run
`tools/report_empty_step.py` for retained Step evidence; the monitor-only audit
requires its dedicated fixture and must not label an ordinary headset run a pass.
