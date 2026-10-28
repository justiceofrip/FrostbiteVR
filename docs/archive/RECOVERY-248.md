# Recovery248: original-magazine publication gaps

Local development after failed headset245 and actual-game247. Not headset
accepted. Shared BC2 resource consumers use exact enrolled weapon geometry;
these changes do not introduce another per-weapon interaction implementation.

## Reproduction and implementation

Actual247 completed one original-magazine removal/return. On the second removal,
the consumer reached WellEmpty with a pending discard, then cancelled when the
native publication was 9.3692ms beyond its 100ms deadline. The pump never began.

The channel now returns the reason for its single guarded read together with
the optional view. Busy/missing/revision/future/expired publications permit
custody-only deferral. Owner and invalid-publication failures do not. Existing
native and input deadlines are unchanged. Same physical/native/family identity,
gun token and tracking epoch are independently required.

Original-magazine states can keep existing ownership and pending requests
through that gap. The shared interaction still checks fresh controller safety,
real release and original transaction timeouts. It cannot acquire a hand, use
old rail geometry, dispatch a native operation or invent an acknowledgement.
Deferred output has no resource authority, target or chest prop. Replacement
supply and submitted seats remain excluded; visual continuity across a gap is
not yet proven.

A confirmed original return now retires its submitted-seat record. The composed
second-cycle regression found that the old record blocked gap recovery. An exact
pre-dispatch rejection of a released-original discard may settle and resubmit
disposal of that same token against a fresh view. This never retries a native
remove/refill/return call; uncertain outcomes and changed ownership remain blocked.

## Validation

Focused x64 resource adapter, controller driver and channel suites pass. Tests
cover five deferred read reasons, 300–400ms custody gaps, real release, delayed
removal acknowledgement, preserved rounds on reinsertion, expired queued discard
followed by replacement, and rejection of lifetime/input/timeout violations.
The full controller repeats original return then discard/refill through a short
expired publication, retaining its separate 100ms missing-view watchdog.
Full x86/x64 compositions pass all240 C++ suites per architecture (127.65s and
34.60s). Ten status-tool tests also pass. Frozen source digest:
`5d29b9bcd9ea84c8b76f964af374454a24cbaa3a6bb9f096463731505e23b0ba`.
Actual248 failed before removal: magazine palette fallback, diagnostic failure11.
Three fallback records show unchanged source/owner and coherent tracked input;
the reserve publication alone expired before drawing. No magazine commands or
pump controls ran, so248 did not exercise its new active custody path. Hooks
disabled/drained and BC2 stayed responsive. All42 scene milestones flushed with
zero reported drops/overflow or write errors. See [249](RECOVERY-249.md).

## Scene-transition diagnostics

Integrated the reviewed scene247 milestone payload. Bounded atomic records track
native rebuild and first post-rebuild gameplay/render callbacks, plus remembered
local-owner validity changes. An existing worker flushes them to
scene-milestones.jsonl; no hook performs file I/O or additional native reads.
Missing completion markers alone cannot locate an exception. This is evidence
collection, not a checkpoint-reload crash fix.
