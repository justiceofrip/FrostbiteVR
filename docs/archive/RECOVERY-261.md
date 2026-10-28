# Exact tube prediction Restore follow-up —261

This candidate follows the failed actual260 combined check. See
[260](RECOVERY-260.md) for its preserved result. The shell physically applied,
but client1 restored a matching pre-transfer snapshot before the server transfer.
The tube completion policy treated that temporary rewind as a State failure.
PhysicalReload's later keepalive rejection was a consequence.

Magazine and tube adapters now share the typed prediction Restore evidence.
The runtime supplies it only from the existing signature-verified Restore hook,
an immutable snapshot match, retained identity and exact current configuration.
For a pending tube insertion, a verified pre-server Restore withdraws only the
affected client's provisional transfer. Its subsequent transfer must occur
after that Restore. The original request, completion deadline, server credit
and ammunition counts do not change. Generic count reversals still fail.

The actual260 sequence has a failed-before regression. Focused completion and
request-policy tests cover invalid, repeated, stale, foreign and server Restore
evidence. All four focused suites pass on x86 and x64 (round completion,
request cycle, family dispatch and magazine regression). The source is pinned
at `9844a0cd5031631eef5d31f0c871273c64e73254a2a75fdede3f23da4f4e5c3f`.
Full builds were stopped before completion when ordering review found a
second defect: clearing the restored client's valid context timestamp made
a server-first update cancel State. Actual260 used that order. The timestamp
must remain original and subject to its existing freshness bound; only the
old hold/publication should be withdrawn. Follow-up262 adds this regression.
No261 binary was injected and no261 headset candidate exists.

Earlier259/260 changes remain included: keep committed empty hands through an
input gap, measured SCAR support enrollment, natural pump motion and aiming
continuity, and same-packet processing time for shell support return. Latest
headset258 remains unaccepted. GitHub245 remains unchanged.
