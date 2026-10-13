# Magazine admission diagnostics —263

Actual262 failed at full SCAR magazine return before the pump/shell stage.
The right-hand claim remained valid, and the left-hand release matched the
committed seat. The native service rejected the request before dispatch; the
physical coordinator then cancelled. See [262](RECOVERY-262.md).

This diagnostic adds an immutable first-rejection record containing the exact
request, source and current binding/snapshot clocks, processing time, counts
and rejection category. Existing admission and ownership rules are unchanged.
Focused service tests pass on x86/x64, including the captured timeline with
32.8ms remaining on the old publication, expiry categories, and preservation
of the first rejection across later failures.

Source pin: `b95d06e3f9e513dd4f9ee430023a3fd93a85fa5e673a11603c9de0df167b869b`.
Only the affected x86 native diagnostic targets are rebuilt for evidence
collection. The unchanged262 IPC receiver is reused with exact binary/source
provenance. This is not a new full build or a headset-ready candidate. Both
architectures last passed all240 suites at262. A functional change still needs
full builds and actual-game validation.

The suspected short admission window is not yet the established root cause.
Do not relax checks or label a fresh retry a fix without the diagnostic record.

## Actual result

Run263 did not reproduce the native magazine rejection. SCAR support, two
magazine cycles and the interrupted pump passed. Shell guidance then lost its
capture on a false timing gap; a later missing hold publication reused the
startup timeout and cancelled the interaction before submission. The cause of
the missing source rows is unrecorded. Focused fixes and subsequent validation
are documented in [264](RECOVERY-264.md). The game remained responsive and hooks
detached; this run was not a complete combined pass.
