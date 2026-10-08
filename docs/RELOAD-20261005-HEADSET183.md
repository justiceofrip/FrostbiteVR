# Headset183 reload investigation — October 5

User result: shoulder holsters are predictable and usable; manual reload remains
inconsistent even without weapon pickups. The mod session was ended at the user's
request and its full logs preserved. Holster implementation remains unchanged.

The tested source and native DLL are pinned by
`<local-recovery>/headset183-reload-investigation/evidence.json`.
This report supersedes the earlier183 readiness summary, not its historical evidence.

## Repairs and current validation

The shared reserve reader had two final validation sites that disagreed about
callback overlap. Both now distinguish freshly revalidated overlap from actual
owner/configuration loss; consumers wait only within original deadlines. No
cached read is promoted to new native evidence.

The magazine completion ledger also assumed client counts never roll back after
a predicted transfer. Native reserve185-02 proves client transfer, exact native
Restore to the original counts, a second client transfer, then the first server
transfer. The old ledger cancelled before the server transfer. A typed Restore
receipt now retracts only that exact client prediction, for the same configuration
and active operation, before any server transfer. Server accounting stays exactly
once and the operation deadline is not extended. This is shared magazine logic,
not a weapon-name exception.

The combined186 source builds and passes186 CTest suites on x86 and x64, including
the actual policy replay and wrong owner/configuration/expiry/duplicate-server
negatives. Earlier788 Python tests belong to the preceding checkpoint; they were
not rerun as new production validation in this investigation. A separate offline
native-report auditor passed7 test groups.

## Native checks

Evidence root: `<local-recovery>/pipeline-runtime-20261005/root-monitor`.

| Run | Result |
| --- | --- |
| magazine-reserve185-01 | Actual magazine remove/replacement/seat/completion and retirement succeeded,22/191 to30/183. Receiver diagnostic exceeded its240-pair target with257 valid pairs; reported transport failure preserved. Receiver stop condition corrected afterward. |
| magazine-reserve185-02 |240-pair transport passed. Manual operation cancelled on the proven predicted rollback/retransfer. Full final ammo alone was not accepted as success. |
| magazine-prediction186-01 |240-pair transport passed; removal cancelled with InteractionRejected/ClaimLost before seating. Prediction correction not exercised. |
| magazine-prediction186-02 | Same pre-seat ClaimLost reproduced. Hooks disabled and native readers drained. Ordinary post-hook reload independently restored22/191 to30/183. |

The zero geometry field on a cancellation record is the record's default; it is
not evidence that geometry disappeared. Claim renewal timing/rejection requires
more detailed evidence. No fresh headset readiness is claimed.

## Scaling limits and next work

The SPAS completion ledger contains similar prediction assumptions, but headset183
detailed native records end before its successful shell submissions. Its two
conserved shell transfers prove neither presence nor absence of rollback. A future
shared prediction mechanism must bind each shell's request/seat as well as its
cycle, delta and completion mode. Do not extend magazine policy to tubes merely
because the source looks similar.

Next: capture the exact failing hand claim and renewal reason, fix the demonstrated
consumer defect, then repeat actual monitor operations before requesting headset
feedback. Chamber, pump, bolt, belt-fed and underbarrel manual reloads are not
promoted by these results. Canonical deployment and release remain unchanged.
