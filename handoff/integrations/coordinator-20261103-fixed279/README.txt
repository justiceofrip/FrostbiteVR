Shared selected-ammunition readiness after launcher selection

Actual277/fixed53/xr-game01 completed physical sight selection and left-hand
release. Its first selected snapshot had the exact launcher owner/configuration,
but no current ammunition view. Native reads still published the source rifle
through a458.652ms switch window. The prelude refused immediately, before firing.
This establishes a publication gap, not later recovery or a persistent native fault.

279 moves the selected-ammo observer into the shared fixed-container driver.
An absent view can use the existing read-only initial capture demand, with fresh
family/owner/input validation on every response. It sends no input while waiting.
The original1s initial demand,250ms request bounds, source deadlines and outer
timeout remain binding. Present invalid/contradictory views and identity changes
still refuse; binding loss after readiness cannot open another capture demand.
Trigger revalidation normalizes the real string nonce with the existing integer
parser; the successor prelude also normalizes its postshot admission nonce.

Root26/26 fixed-container checks and all seven affected280 prelude checks pass,
zero skips. Actual-game recovery and complete reload remain to be checked.
Installed0.1.53/native a2bedc7 and258 build/install proof remain unchanged. No new
native build or installation is needed; no launcher reload acceptance is claimed yet.

Actual277 closed normally with all six owned processes exiting0, native retirement,
exact profiles/settings restoration, and a hash-preserving14-file archive. Its
single8549-frame official tape check retains335 startup zero-layer failures.
Evidence: WT reports/coordinator-20261103-feedback279/selected-ammo/DIAGNOSIS.txt,
actual277-ammo-evidence.json, actual277-first-selected-ammo.json and root-fixed-tests01.log.
Canonical actual277/fixed53/xr-game01 and closed27701 remain preserved.
