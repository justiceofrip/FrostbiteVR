# Checkpoint272: keep automated input independent of evidence writes

The failed271 F2000/SPAS run retained a support reset with the specific reason
`stale_input`. Its receiver logged generation1759 at105023765ms and generation1760
at105023890ms, a125ms gap against the100ms input lifetime. The same iteration
saved pixel-checked pair120. Large synchronous image files were written on the
controller-publication loop. The earlier271 audit missed the specific reset
reason; the original failed run remains preserved.

The diagnostic receiver now queues immutable pixel buffers to a dedicated file
writer. Four waiting pairs plus one being written bound the queue. GPU access
stays on its original thread. Queue overflow and write failure are reported and
fail evidence acceptance after controller publication ends. No stale input
deadline is extended; GPU readback or scheduling stalls can still happen.

The simulated pump player also handles one loss of support before firing. It
waits for fresh native/renderer evidence of the same unshot Watching state,
returns to Approach, releases its grip, and acquires a new support claim. A
second loss, changed owner, blocked firing, or a changed cycle/shot rejects the
retry. It cannot clear native debt, retry an active pump or fabricate a completed
action. Reports expose the retry count and loss timestamp/sequence; the runtime
checker distinguishes recovered pre-shot input from an uninterrupted pre-shot
sequence. This changes the diagnostic player, not normal gameplay grip policy.

The new125ms-gap regression fails with phase_timeout on271 and passes on272.
It uses the actual controller mapper, hand arbiter, physical-pump consumer,
support policy and native cycle service with synthetic original callbacks.
Negative cases reject repeat loss, blocked firing, changed cycle, changed shot,
active native debt and changed owner. Writer tests verify bounded queue behavior,
off-thread ordered drain, failed writes/exceptions and exact saved bytes.
Seven Python status tests include visible retry reporting and invalid proof.

Both full normal builds passed all241 suites per architecture. Normal binaries
and matching symbols are frozen in `bc2vr-recovery/normal272-player-checkpoint`.
The operation-source digest is
`d78656d55305cf4a96db48d35a6e8b78930c885411d8e4ebc0254c33f5ce9ffc`.

Actual `resource-f2000272-01` completed the F2000/SPAS sequence with all six
verified magazine-command receipts, conserved original returns/chest replacement,
shoulder round trip, held support, interrupted manual pump, shell7/24 to8/23,
support return134.1785ms after shell completion and final rifle firing to28/180.
No pre-shot regrip was needed and no forced support reset occurred. The maximum
logged input-attempt gap was46ms across3857 samples; no gap reached100ms.

All40 queued image pairs were written without rejection or failure. The audit
checks80 full-resolution files and hashes,240 consumed stereo pairs and40
pixel-compared distinct pairs. There were no recorded GPU or camera-restore
failures; BC2 detached alive and responsive. Twelve callback recorder entries
were dropped, so full callback coverage is not claimed. This runner has no
exception watcher. Native trace SHA-256:
`1677723d2ae450ea1ca9b9e065b1886c61f4378ef374d3fb7c0a29704932e7da`.

The actual run did not force a pre-shot loss: that recovery branch is verified
by the fail-before offline regression. One successful run does not prove that
every scheduling/tracking interruption is fixed. This checkpoint improves the
emulated test system; normal player grip/ammunition behavior is unchanged.
Headset feel, visuals and whole-roster acceptance remain separate. No new headset
session was requested. Normal271 and its staged headset package remain preserved.
