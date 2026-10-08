# Native pump startup and completed-owner lifecycle correction

The private candidate originally used a sticky cancellation bit. An ordinary
startup or vehicle `ClearOwner` could therefore poison the service before its
first shot. Cancellation now advances an epoch; each serialized operation
applies each pending epoch before working. A hold decision retains its original
epoch and cannot override delta after an intervening cancellation. Cancel also
clears native dispatch targets. A read during a pending cancellation cannot
publish a held lease or an unblocked firing state.

In `Watching`, cancellation invalidates original evaluation tokens and control
without inventing native-cycle debt. A stale Update completion evaluated before
owner loss cannot enroll a new cycle after that barrier. Fresh on-foot control
can bind normally. In `Complete`, the exact ready outcome survives owner loss;
only exact acknowledgement releases the block, after which fresh owner binding
can start a later cycle. Neither path manufactures a native ready receipt.

A known unfinished shot/hold/release still becomes `Cancelled` and refuses
rebind. Callback drain or `ClearOwner` alone is not manual-cycle completion.
Verified retirement and persistent per-item action debt remain needed for
automatic recovery of that case. The private service remains disabled by
default, and this patch does not enable it.

Four additional isolated lifecycle groups cover startup clear/on-foot binding,
stale evaluated callback after clear, completed exact outcome/owner change, and
unfinished-cycle refusal. All 14 native-service groups pass x86 and x64 with
`/W4 /WX`; both ordinary/private runtime TUs compile on both architectures.
These exercise the service cancellation/control protocol with synthetic native
records; they are not a live game `Enable`/vehicle/owner test. No additional
native process, game input, or headset test was performed.

The diff is against the frozen pump222-native payload. Root should merge its
small runtime hunks while preserving intervening resource fixes.
