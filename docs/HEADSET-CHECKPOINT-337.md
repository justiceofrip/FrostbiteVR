# Headset checkpoint337 — prepared SCAR/SPAS session

This is a test-readiness checkpoint using the unchanged normal333 player build,
not a new weapon implementation. All35 staged file hashes and the compiled
source manifest are checked again. The full244-suite x86/x64 results remain
applicable because production source and binaries have not changed.

The preserved311 actual-game emulation covered SCAR support, three magazine
cycles across shoulder swaps, a SPAS pump and shell insertion, immediate support
return, and subsequent rifle firing. Normal333 separately passed rendered laser
menu interaction, checkpoint restart and replacement-scene rendering. These are
automated results; hand placement and real player gestures still need acceptance.

## Prepared session

BC2 was restarted on the left monitor without OS input or requested activation.
The native Continue helper loaded the saved campaign. The bounded setup helper
exited the boat and the existing provisioning path equipped SCAR alongside SPAS.
Setup/provisioning hooks are removed and their code restored. The startup
exception watcher detached after40 seconds with no exceptions. No VR mod session
has started, and no scripted controller driver is enabled in the player build.

Local readiness evidence and the identity-checked launcher are referenced by
`bc2vr-recovery/checkpoint337-local/checkpoint.json`. Installed game-derived
geometry remains private and is not included in the source archive.

## Headset check — about5–10 minutes

1. Hold both stick clicks for one second to recenter. Draw and stow each shoulder
   weapon, then draw SCAR. Gripping away from a holster must not equip a weapon.
2. Hold SCAR with both hands and fire. Remove and return its magazine; then
   replace one from the chest. Check hand alignment and immediate front regrip.
   Fire after seating; neither the removed magazine nor a replacement should
   appear in the gun before insertion.
3. Draw SPAS, fire, pull the pump back and forward, then fire again. Insert a
   chest shell and immediately grip the front. The pump must actually rearm the
   gun, and it must not strand holstering, reloads or the other weapon.
4. Swap between both shoulder holsters and fire again. Check that chest ammo
   remains visible and grabbable after swapping.
5. Hold X+Y for half a second, release both, and use the visible laser to interact
   with the menu. Resume and verify controls. Try checkpoint restart last; the
   intermittent startup/restart crash remains unresolved despite bounded passes.

Stop the sequence at the first persistent failure and describe the last action;
there is no need to complete later steps in a broken state. The mod session stays
running until the user finishes. Startup and diagnostic preparation are not
headset acceptance or a claim of a fully playable campaign.

Chest knife, pistol chambering and complete belt-fed loading remain unfinished
and are not part of this test. The private336 knife result is preserved separately.
