# Magazine seat publication boundary — checkpoint 222

An automated test of the actual BC2 physical-consumer wrapper reproduced a
missing attached-magazine target on the same gather call that submitted a
physical replacement. The hand claim had been released, but the native service
had not published its admission yet. The shared support policy consequently
kept the left hand blocked. A delayed-completion case reproduced a second gap:
coherent native counts could already show the inserted rounds while the final
completion receipt was still pending.

The private resource adapter now retains the exact successfully submitted seat
request for presentation. Its original time bounds, native inventory context,
weapon metadata and operation must match the fresh current publication. This
allows an attached visual before server admission without granting ammunition,
firing or native completion. Rejection, expiry and identity changes invalidate
that visual authorization. Once dispatched, exact expected native counts may
keep the attached target visible until the real receipt arrives.

Regression coverage checks both currently enrolled XM8 and AEK profiles on
the submission frame, immediate support admission, delayed completion without
duplicate calls, and rejection of altered/expired/mismatched requests. The
original failing reports are retained in local reports. Native Updates in
these adapter tests are mocked; this is not a headset or in-game draw result.

The private candidate remains disabled in normal builds. The simulated-player
driver still needs adaptation to resource completion instead of the old stock
animation hold. Interrupted supply reservations, full gameplay arbitration and
actual BC2 controller-driven validation remain before headset promotion. No
additional weapon was enabled, and no game process was changed for this work.

Both full normal builds pass all 198 suites; the private x86 candidate passes
12 focused suites. Frozen receipts are in local recovery
`normal222-resource-adapters-20261008` and `resource-adapters222-private`.
