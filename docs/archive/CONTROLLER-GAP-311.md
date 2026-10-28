# Pending native cycle input gaps311

309 failed after a controller publication gap during an established SPAS hold.
310 fixed that phase, passed both243-suite builds, then its real-game check
caught the same dependency earlier: ShotObserved, before all native own-shot
receipts had arrived.310 remains failed and is not staged.

311 extends the same bounded native-only observation through ShotObserved,
Arming and Held. At least one actual own-shot receipt, exact native owner and
configuration, and fresh verified callback/cohort reads are required. Remaining
copies still need their real own-shot receipts; a fresh snapshot never votes as
a shot or completion. Partial prediction copies may legitimately retain their
pre-shot count until those receipts arrive. Established holds keep strict
post-shot counts and safe-context checks.

During a controller gap, old hand leases are revoked and the interruption
window is fixed to the original source expiry. Neutral input from the same
owner is required to resume physical authority. Once a hold is established,
each branch can retain its own hold using its fresh whole-cohort read and safe
own context. It no longer lets its timer advance merely while waiting for an
older sibling context; publishing a physical lease still requires fresh hold
receipts from every branch. No ammo writes or synthetic completions are added.

Six focused suites pass. Regression coverage includes any of the three native
copies reporting first, SPAS/M95 pending-shot gaps, established-hold gaps,
neutral resumption and eventual exact completion. Twelve invalid owner,
configuration, count, state, context and deadline cases remain rejected.
The actual-game combined check PASSED: rifle support held763.1ms over50 unique
inputs; three magazine cycles across shoulders; one physical SPAS pump; one
shell insertion; support regrip102.94ms after completion; then rifle firing.
All six magazine commands independently match server and both client receipts.
Final rifle counts are29/180.240 stereo pairs were consumed,40 pixel compared,
with zero presentation fallbacks/ownership failures. Hooks drained and BC2
remained alive. Ten ordinary record-lock drops prevent claiming full callback
coverage; every audited critical receipt is present. No controller suspension
was triggered in this successful run: the two gap cases are proven by the
deterministic regressions, not by claiming the native hiccup repeated.

Native audit: support311-scar-native01/combined-grip-audit.json and
receipt-review.json; trace SHA256
e17d1fba26c340ca549d17015d27f8631c18e887c275ee59f65fd38a1bce9c89.
The native candidate reused the unchanged310 receiver; producer source is311.
Both normal builds pass243 suites: x86141.62s and x6438.58s. The frozen
normal player binaries have diagnostic input disabled. The native sequence
passed before these final builds; its unchanged source was verified again.

The exact310 first-failure evidence is in
bc2vr-recovery/support310-scar-native01/native-cycle-first-failure.json:
invocation8426; controller1555 expired30ms before observation; unchanged native
actor, weapon and7/24 ammunition. Hooks drained and BC2 stayed alive.

58 measured grip variants and18 manual-magazine prerequisite configurations
across10 asset names remain different capability counts. Pistol support/reload
native evidence is explicitly prior309; hip carry305 and VR-menu/checkpoint
scene evidence304 are historical. No new headset or whole-campaign acceptance.
Chest knife, third native slot, pistol chambering, belt-fed loading and broader
manual-action enrollment remain unfinished; physical grenades are deferred.

## Focused headset acceptance still required

The next check uses a fresh BC2 process and the normal311 package. Draw/stow
both shoulders; hold the rifle support grip; fire/pump/fire the SPAS; insert a
chest shell and immediately regrip; swap back and remove/return/replace a rifle
magazine, then fire. The purpose is hand placement and real gesture acceptance,
which scripted poses cannot establish. VR menu behavior has prior304 native
acceptance and must remain usable. This is not a completed-campaign claim.
