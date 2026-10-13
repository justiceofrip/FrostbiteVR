# Recovery255: retain original bolt contact through a publication gap

Actual254 reached the first bolt Unlock then briefly published no physical
custody, followed by a roughly14cm raw hand/weapon displacement and Geometry
cancellation. The trace narrows the onset but does not name its exact failed
preparation condition. See [the failed run](RECOVERY-254.md).

Code review found a reproducible coordination error: a default empty render
contact was treated as a foreign native owner, invalidating the established
hand references even with live gun/mechanism claims. The regression uses the
real player coordinator and physical adapter, enters a second bolt cycle, and
removes two consecutive contact publications. It fails before this correction
and passes afterwards on x86 and x64.

The coordinator retains the last successfully mapped original contact, used
only when the new publication is entirely absent and that original evidence
has not expired. Owner, tracking space, current controls, current body mapping,
hand claims and native receipts keep their existing checks. Timestamps, contact
sequence and deadline are not renewed. A foreign publication cannot be hidden
by this cache. An expired gap requires retirement/recovery rather than reuse.
The regression also exercises resumption, immutable deadlines, expiry and a
foreign owner. No geometry tolerances, ammunition or native completion changed.

Both full255 builds pass all240 suites (124.67s x86,33.90s x64). Frozen receipt:
local `ordinary-bolt255-input-private/build-receipt.json`, source SHA-256
`9cdbc26eb5f0a39bc35d0a7b452f57caa2316d086d7b43701bc4939d9690b9ab`.

Actual `root-monitor/m95-ordinary255-01` completed the first physical
unlock/rear/forward/lock sequence, submitted exactly one native release and
returned the rifle to the right hand with its left support claim. It failed
before acknowledgement, so zero complete runtime cycles and no second shot.
Trace SHA-256:
`c81fbe9b36390f1c4a88aefb580efccb38b658c3f0c9ca36a8646c07e73a0d92`.
No physical Geometry cancellation recurred in that run, but no counter proves
the missing-contact cache branch executed. Keep that causal limit explicit.

The first-failure journal identifies native Restore3474 while Releasing.
ClientB's own Update3468 had completed8->1->2; clientA's Restore3472 then copied
that idle state and its own Update3473 confirmed idle. ClientB Restore3474
restored the original server snapshot8/next1/timer0.0241789, keeping4/45 rounds
and exact owner/snapshot bytes. The service rejects this completed-client
rewind. Original Update3475 replayed8->1->2; Restore3482 repeated the remaining
0.0115024s server tail before the server completed at3486. Preserve
`first-failure-review.json` with the original record and neighboring callbacks.
This exposes a confirmation-state assumption, not an incomplete physical stroke
or changed ammunition. Ten callback recorder rows dropped elsewhere; full
callback coverage is not established.

Next work: reproduce the original post-release client-tail replay without
minting another physical action. Revoke only provisional readiness for that
client, retain the exact submitted transaction/deadline, and require renewed
native convergence; reject changes in ammunition/owner and already-acknowledged
rollback. This is a proposed correction, not implemented in255. The two-cycle
test,252 second-grip timeout and checkpoint-reload crash remain unresolved.
BC2 stayed responsive and the hooks detached cleanly. No headset acceptance.
The known252 F2000/SPAS and SCAR/SPAS combined passes remain preserved.
