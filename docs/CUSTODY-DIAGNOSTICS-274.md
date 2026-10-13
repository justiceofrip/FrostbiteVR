# Reload failure evidence — local274

This development checkpoint adds observation only. It does not change native
ammunition policy, hand ownership, reload acceptance, or weapon enrollment.

Two273 native failures need distinct evidence. An idle magazine cancellation
was hidden behind the expected startup input cancellation, and a carried SPAS
shell was lost before native reload started. The old shell cancellation record
only covered active native cycles.

The magazine consumer retains the first cancellation of each cause, including
the typed resource-read reason and same-owner check. Shell logging now retains
pre-start cancellation with the original item/claim (event11; physical cancel
reason), and loss inside the supply policy (event12; AmmoSupplyReason). The
32-row cancellation log distinguishes observed cancellations from actual native
cycle cancellations. The existing rolling availability log remains available.
No logging result grants custody or native operation authority.

Focused regressions exercise loss before native start and verify that hundreds
of subsequent idle samples do not erase it. Separate rifle coverage starts with
an expected input cancellation, then confirms that the first idle resource-read
failure survives later failures without submitting ammunition commands.

Both full builds pass243 suites, including the focused custody regressions.
The first launch stopped before injection because SPAS was selected. The existing
setup helper selected F2000 but reported a timeout; independent read-only checks
then verified both inventory slots, idle state and full rifle ammunition.

Native274-02 completed two F2000 magazine cycles, interrupted SPAS pump recovery,
one shell insertion and support-hand return133.721ms after completion. Independent
receipt review checks four magazine commands, ammunition conservation, all three
recorded pump branches, and shell counts7/24 to8/23. Callback recording has ten
lock drops; this is partial evidence, not complete callback coverage.

The full sequence **failed** during the final rifle warmup. The driver retained
equipment generation3; the same rifle/actor/space later published generation4.
The native input-gap event immediately precedes the guard failure. Gameplay.cpp
advances this observation epoch after a native update pause greater than150ms.
Receiver input continued with maximum32ms spacing; this is not the prior272
receiver image-write stall. The cause of the native pause remains unproved.

The earlier273 idle magazine rejection and pre-start shell loss did not recur;
logging changes do not establish fixes for either. Next work must separate
input recovery from persistent equipment identity and verify interrupted actions
without adopting old commands or fabricating native receipts. Do not merely
ignore the diagnostic owner guard or extend input freshness.

Evidence: `<local-recovery>/pipeline-runtime-20261005/root-monitor/resource-f2000274-02`
contains instrumented-review.json, partial-receipt-review.json and input-epoch-review.json.
Normal builds, symbols and source are frozen locally. Headset272 remains staged;
274 has not passed the full native sequence or a headset check.
See [the retained273 failures](SAMPLED-MAGAZINES-273.md).
