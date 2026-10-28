# 0.1.12: opening knife progression and narrative text

Published 2026-10-20 at 22:42 UTC to
[justiceofrip/BC2VR](https://github.com/justiceofrip/BC2VR/releases/tag/v0.1.12),
release 418953565, documentation commit
`c99f02485ae3b4a3545701d5501d149b7cdea84d`.

Normal checkpoint 434 passed both full builds (294/294 each). Source:
`2e693ca5bbe9e8e333e582675fc43dd74a001b241b896db42b8f3c672274c738`.
Frozen runtime receipt:
`d7214c076cbca9544cb9ad7ddba472a96a03f9aa5b92fa7d0f969c88aee34c3b`.

The candidate retains 431/433 native knife delivery/recognition fixes and adds
translation-invariant LOCAL blade sampling with current head-relative reach
validation. It includes passive opening text, the corrected menu clock used by
crouch input, left-stick toggle crouch/Y sprint, and the 0.1.11 cache fix.

Actual-game emulated input reached the opening guards continuously after one
Entire Mission start. approach43404 serial1/input485 delivered native damage to
target1041955984 (12.5 -> 0), pending0. Root-reviewed native Objectives pixels
then showed Reach Trenches. No restart, death, teleport, direct damage write or
objective skip intervened. The same candidate's three distinct opening story
cards were reviewed. Native hooks and view cleanup passed. The diagnostic game
closed normally; SteamVR was unchanged.

Private release012 acceptance SHA:
`2dd62364c66af3d97a37203640dd731768b34b0f621b0e818fad0ac74d5f1b21`.
Package receipt SHA:
`34241321ad62d373c2267a2f88fd74d9dbedf92bed931d2584be1fcb870d2d14`.
Install fixture validation SHA:
`28327783051a2e07c77fe18fb822db3867ff4148591bf1b96fc3c5ed1285fd47`.

Package checks: 12 passed. Actual installer fresh/update/validate/rollback
checks: 18 passed. Local current012/previous011 validated, settings unchanged
(`be6f984f6bd44dcc62aab0a2ac1a63acaa2d3221d2919f2a44a4aeeef2b1f7cf`).
All three published asset sizes and GitHub digests match local artifacts.
Game assets, debug symbols and diagnostic receiver are excluded.

Limits: no physical headset check of this candidate; no full-campaign acceptance;
no accepted death/reload crash fix. The broad pipeline and weapon limitations
remain. Only the named opening path and native narrative pixels are accepted.
Raw captures stayed bounded; idle compiler compression preserved hashes and
lengths. No user recordings or source were deleted.
