# Checkpoint258 player check

Unreleased local integration candidate. This is not whole-roster or headset
acceptance. The latest headset245 failed; the new builds require a fresh game.

## Headset sequence

1. Rifle: remove and return its original magazine, remove it again, then use a
   chest replacement. Check that the mag stays in the hand, counts make sense,
   the support hand can regrip immediately, and firing works afterward.
2. Empty the magazine. It should require manual replacement; squeezing the
   support grip must not cause the old automatic reload to take over.
3. Holster and redraw both shoulders several times. Switch to the SPAS, fire,
   perform the full manual pump stroke, insert a shell through the bottom port,
   and regrip the front. Return to the rifle and repeat a magazine change.
4. Briefly stop tracking during a pump gesture, then return to neutral and do
   the full stroke. Report any lock, unexpected automatic action or lost grip.

M95 bolt handling has a separate actual-game emulated two-cycle check. A later
M95 player pass needs the rifle equipped and covers supporting the gun with the
left hand, releasing the right hand, gripping and cycling the bolt, then returning
the right hand to the gun. It is not inferred from the rifle/SPAS headset pass.

## Evidence and limits

Read RECOVERY-258.md for source/build pins and each actual-game result.
Emulated input uses ordinary hand/custody consumers and real native ammunition
receipts; it does not establish hand alignment, comfort or headset visuals.
Shared magazine mechanics have prior F2000 and SCAR native sequences. Enabled
configuration prerequisites are not evidence that all50 weapons work.

The shared selection read-gap correction has deterministic pump/bolt coverage.
Two fresh M95 runs completed two cycles each, but no selection read gap occurred
in them. The exact cause of the prior intermittent257 contact loss is therefore
not confirmed. Keep the new first-cause counters enabled for this player test.

Campaign checkpoint/death reload crashing remains separately unresolved. This
candidate is for the interaction check, not a claim that campaign transitions
are fixed. Pistol, belt-fed and other unaccepted mechanics are not newly enabled.

Leave the player session running until the user finishes. Stop only that mod
session afterward; preserve BC2/SteamVR unless a fresh game was requested.
