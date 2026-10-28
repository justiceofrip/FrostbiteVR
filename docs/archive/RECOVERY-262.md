# Tube replay ordering and combined validation —262

Continuation of [261](RECOVERY-261.md). The exact shell prediction Restore
handling now withdraws the affected client's old hold and publication while
retaining its original context timestamp. Its existing freshness bound remains
unchanged. A server-first update can therefore continue under a valid context;
an expired one still rejects. No request, deadline or server transfer is renewed.

This fixes the ordering used by actual260: Restore12131, server Update12132,
then client1 Update12133 and its replayed transfer. The earlier regression
covered client-first only. Both orders now have explicit coverage, with
failed-before evidence for server-first. The four focused suites pass on both
architectures. Source pin:
`457763dbf0c77da437f9f03c21bb06b9641868df1ffe48ea1a4ecac357545af3`.
Both full architectures passed240 suites. Actual run262 failed before reaching
the tube/pump stage: SCAR support succeeded, but the original magazine return
request789 was rejected by the native resource service before native dispatch.
Hand ownership stayed valid; the physical terminal cancellation was downstream.
The first failed request's admission reason was not retained. Follow-up263 adds
bounded diagnostics before changing policy. Trace SHA-256:
`bab5b0fb33fcb26176c540609fd57b7205fe1e9329935be3a5cd9cc58596d29d`.
The game remained responding and hooks detached. No headset candidate is ready.

Included earlier changes: committed empty hands survive temporary input gaps,
SCAR front support uses a measured authored binding, pump travel accepts a
natural continuous stroke, gun aiming correction survives mechanism handoff,
and completed shell support return uses the same packet's current processing
time. None of these imply headset placement acceptance.

GitHub remains245. Campaign checkpoint/death reload crashing and broader weapon
enrollment remain open. Do not launch an old candidate under this checkpoint.
