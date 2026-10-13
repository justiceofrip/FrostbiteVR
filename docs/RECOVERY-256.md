# Recovery256: replay the original native tail after one physical action

Actual255 completed its bolt stroke but rejected clientB Restore3474: that
client had reached idle, then received the server's still-running original
tail. The recorded snapshot kept the exact owner and4/45 rounds. Its own
Update replayed8->1->2, another Restore repeated the shorter remaining tail,
then the server completed. See RECOVERY-255.md and the preserved original
M95TailReplay255Fixture.h; recorder IDs are not native invocation IDs.

The shared cycle service now withdraws only that client's provisional ready
vote when this exact original snapshot transition occurs while a physical
release is pending. It retains the original release, debt, counts, deadline,
other branch receipts and fire block. The client must complete its own ordered
tail and all three branches must agree before readiness. It does not issue a
second physical gesture, restore an old hold, change ammunition or extend time.

The selected mechanism supplies the boundary, so the same policy covers the
supported positive/empty pump and bolt tails. Tests exercise both clients,
repeated replays, an interleaved forward-ready client, immutable release and
deadline, and missing history, completed/acknowledged cycles, changed owner,
changed counts, changed snapshot, expiry and concurrent-Update rejection.
The service regression fails before the correction and passes afterward in
both architectures; the other six focused mechanism suites also pass.

Both full builds pass all240 suites:129.87s x86,35.06s x64. Source SHA-256:
`5692f7f8baa928869710223637ea14712bea6049888c40375078f019a5103394`.
Frozen receipt: local `ordinary-bolt256-input-private/build-receipt.json`.

Actual `root-monitor/m95-ordinary256-01` completes and acknowledges the first
physical/native bolt cycle, with original clientB tail replays3418 and3426
before native Ready. The rifle returns to the right hand and retains4/45.
The second shot consumes one more round; its grip and Unlock both succeed.
After entering Rear, original contact disappears at input742. The driver then
times out; the later native Owner cancellation is not the demonstrated onset.
Only one of two requested cycles completed. The mod detached cleanly and the
game remains responsive with3/45; no headset acceptance.
Trace SHA-256:
`ac7b57d657a1ca1be7ef4f0b29deae15e4b31fb9f8fde769c3b01c84865e42cc`.
`receipt-review.json` preserves both original replays, first-cycle receipt and
second-cycle transition timeline. Six callback recorder rows dropped; complete
callback coverage is not established.

Next: isolate why contact publication disappears while the second gesture and
gun custody remain active. Renderer pose rejection, publication freshness and
player retirement need distinct evidence. Combined rifle/SPAS retest and the
separate scene/checkpoint-reload crash remain outstanding.
