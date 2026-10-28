# Support grips306

The user's SCAR reload succeeded while the front support grip was missing.
Pistols must remain usable in the right hand alone, with an explicit left-hand
support grip when the hands meet. Reload success alone is not grip acceptance.

## Implementation

- Shared `SupportGrip` contacts now select either two-point aiming or primary-hand
  aiming. Primary-hand support keeps both controller poses unchanged, including
  aim, and permits close or coincident hands. It retains the existing contact,
  fresh-input, neutral-before-grab, tracking and hand-ownership checks.
- The exact MP443_sp authored left-hand contact is enrolled through the existing
  support-data generator. Its right-hand transform and native firing capability
  are unchanged. The new behavior is selected by measured profile data, not by
  broad native weapon classification; BC2 also labels this pistol `wcSmg`.
- The previously extracted SCAR_sp/_s, F2000, XM8 Compact, MG36, XM8 LMG and SPAS
  contact data remain intact. Long-gun aiming continues to use the two-point mode.
- The magazine-only native diagnostic now requires a support grab and release
  before its reload and post-refill shot. Its synthetic controllers cannot grant
  a support claim or write native ammunition. Both must come from the actual
  production consumers and verified native adapter.
- The diagnostic's body-anchor lookup accepts the pistol hip as well as the
  shoulders. It does not add a third native inventory slot.

## Verification

Five focused x64 suites passed, including close/coincident hands, unchanged
primary aim, duplicate packets, missing support, tracking loss, recenter,
ownership conflicts and clean release into magazine handling. The support-data
generator's ten checks also passed. Both complete normal builds passed all243
suites: x86 147.92s and x64 42.32s. A compiler/linker stall was recovered; the
completed full-build reports are separate from the preserved interrupted logs.

Source: `609c8d43b4703ed6545124e5fdea2484d912750f79c2cf823c6929f047b835db`.
Normal binaries and exact profile hashes are frozen in
`bc2vr-recovery/normal306-player-checkpoint/build-receipt.json`.

The actual-game pistol run failed before support acquisition: its measured
support contact was present within0.3mm, but the magazine consumer acquired the
overlapping squeeze first. The two contacts also used different wrist bases.
No intentional diagnostic reload or shot completed. BC2 remained alive and all
hooks drained. Evidence is retained in
`bc2vr-recovery/support306-pistol-native01/support-failure-review.json`.
Candidate307 addresses this conflict;306 is not the accepted grip checkpoint.
The prior305 hip-carry checkpoint remains preserved. No new headset acceptance is claimed. This does
not enable unmeasured pistol contacts, pistol slide/chamber mechanics, physical
grenades or the chest knife.
