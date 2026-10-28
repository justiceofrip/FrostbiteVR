# Checkpoint392: targeted input-expiry and two-shell evidence

The last human run remains failed390. This checkpoint provides additional
automated evidence for391's corrections; it is not campaign or headset acceptance.

## Actual-game result

A single input-driven SCAR/SPAS session completed three shots and manual pumps
(8 -> 7 -> 6 -> 5), two distinct shell gestures (5/24 -> 6/23 -> 7/22), shoulder
return, rifle magazine handling and subsequent rifle fire. The diagnostic held
one original accepted pump-control packet and submitted it once after its actual
deadline, without renewing any pose, timestamp, ownership or action authority.
The packet was rejected1.3897 ms late, with the complete pending mechanism and
last accepted input preserved. Twenty native updates elapsed; the next fresh
packet was accepted and the pump completed. Legacy cancellation is established
by source and regression comparison, not by deliberately running old code live.

Independent review of the same trace passes64 direct callback checks. Both shell
transactions bind their native server invocations and conserved ammunition
changes. All three native stop branches complete; no diagnostic ammunition or
callback-context writes are used. Both inactive shotgun client copies remain
idle at7/22 in31 observations spanning3.000244 seconds after detach. Expected
pump tails are retained, but five callback begin-lock drops mean coverage is not
exhaustive. Repeated prediction-branch updates are retained as repeats, not
counted as additional ammunition transfers.

Trace SHA-256:
`669517d4d9257709090821e3630e74d76f9a3a2b913f4d60f95f1f6811e5fdb3`.

The original validator rejected the two shells because it required distinct item
IDs. The shared supply contract uses `(id, generation)`; the two actual shells
have one namespace ID and generations1 and2, with distinct claims and requests.
The corrected validator preserves all other conservation and authority checks.
Its old failure remains saved; a separate corrected artifact audit passes, with
no additional gameplay run. Regressions reject duplicate or missing generations
and unintended extra filling.

## Host diagnostics and test coverage

Normal builds add a bounded in-memory host diagnostic ring. It distinguishes
missing world imagery, menu-only presentation, zero-layer submission, input
publication failure, retained-source identity/age and slow frame stages. It
writes at shutdown, not every frame. Input freshness and retained-image limits
are unchanged. These observations can narrow the next streaming interruption;
they do not establish a Steam Link or Wi-Fi cause.

Both normal architectures completed their full build and248-suite run. One
outdated test still rejected the deliberately supported three-pump diagnostic;
only that test was corrected, rebuilt and rerun on both architectures. All248
distinct suites now pass; the original failed run is retained. Two explicit
fake-XR tests also pass after test-only corrections: reference-space reset
precedes the fixture's expected age expiry, and GPU fixture allocation must
precede the IPC request deadline. The host executable did not change during
these test corrections. Menu-only layer classification has separate CPU tests;
the menu-transition fixture does not establish real rasterized menu visibility.

Native source closure:
`31740bb4e696f1947ca93444e90b67e09c3ef330ddf7ae613eac9d56e0b7193c`.
No new weapon, native hook or native write target is admitted by392.

## Eye-transition investigation and remaining work

An optional, default-off observer records bounded native view state and source
targets; a private receiver saves correlated eye pixels outside the game. It
does not modify native view fields or run an extra simulation. The first launch
was rejected before hooks because its arguments omitted the compiled ordinary
resource-session requirements. No checkpoint action occurred in that attempt.
The ordinary resource-mode attempt then correctly waited for controller input
in the supported boat and produced no pairs; no checkpoint action occurred.
A separately compiled graphics-only observer avoids that dependency. Its single
native-menu checkpoint restart passes:24 exact paired-pixel correlations span
the old and replacement worlds, with one independently observed replacement
actor, restored views/hooks and no GPU or camera-restoration failures. The native
view RGB state diverges during this transition (primary zero, extra eye one);
pixel and consumer analysis continues before any native correction is admitted.
This is not an actual death test or headset acceptance.

Pump animation acceptance, sustained headset streaming, death recovery and
full-campaign playability remain unresolved. Ordinary grenade/melee bindings and
vehicle coverage beyond the verified PBL boat driver are also incomplete.
Private evidence is under `bc2vr-recovery/overnight392` and
`bc2vr-recovery/headset391-investigation`; game assets stay outside the repository.
