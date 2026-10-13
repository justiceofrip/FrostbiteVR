# Candidate388: stop a held tube reload without resuming stock filling

The October12 headset375 failure remains the latest human result. Campaign
playability and the stock/pin animation are not accepted.

## Actual387 evidence

Both normal387 architectures pass246 suites. The private input-only game run
completes eight SPAS shots, eight physical pumps, eight native acknowledgements
and eight immediate support returns. It manually inserts one shell into the
empty tube:0/24 becomes1/23. No unintended hand-arbiter drop occurs; that does
not resolve the intermittent385 claim loss.

At `381829478662600ns` the driver enters ShellSettle and requests cancellation
of the held shell reload. Physical retirement succeeds. Native abort reports
zero arms/calls. The server remains in11->12 and transfers a second shell after
719ms, then continues stock filling. The combined driver times out after5s,
before returning to the rifle. Native counts eventually reach8/16. This is an
actual unwanted automatic reload, not evidence of a successful combined test.

Local evidence:`../bc2vr-recovery/overnight387/native01/native-trace.json`, SHA256
`6c03947c542d31f82233cfa977218c0c0cf65193395d30e6e1d5e52ca670c315`.

## Correction

Previously explicit cancellation could arm the native stop helper only from an
immediately available all-held insertion lease. Such leases require no open
native callback and a fresh quiet-cohort publication. Cancelling between those
opportunities still released the hold, allowing the engine to resume filling.

Candidate388 separates a stop intent from insertion authority. A held SPAS
policy with delivered outcomes, recent verified own-branch holds and fresh
original controller authorization can request stopping with its committed
counts. No pending/advancing/unresolved transfer qualifies. An open callback or
an expired quiet publication cannot alone erase this intent.

The intent retains the original controller deadline. Each helper call still
requires its own fresh native Update, exact owner/configuration/revision/counts,
neutral input, permitted reload state, adjacent memory recheck and exact helper
and original-Update postconditions. It is not an ammunition lease, cannot
submit a shell, cannot renew a deadline and cannot roll back a completed shell.
Magazine cancellation and native capability admission are unchanged.

## Validation status

Focused request-cycle, native-abort and persistent inventory composition suites
pass. The added connected policy/cleanup regression covers one shell from an
empty tube, delivered acknowledgement, stale quiet publication while all own
updates continue, cancellation during an open callback, three independent
native-stop decisions, and rejected changed counts/owner/expired/replayed work.
Both full builds pass246 suites per architecture (x86121.06s; x6441.25s). Mocked helper results do not
establish actual-game success or visual correctness.

## Actual388 result

`overnight388/native01` completes the whole SCAR/SPAS empty-tube sequence.
Trace SHA256`79032d26c6662e87f036a59298846c65f26bc63fe5b4d04a2909ff2ecb145ae7`.
One stop intent completes mask7 with no failures: client0 is already idle;
client1/server each execute the helper with exact postconditions, unchanged
context and retained owner. No additional server shell transfer is recorded.
Eleven later inactive-weapon samples show both clients still idle at1/23.
335 moving pump poses produce670 paired copies with no fallback or rejection.

`overnight388/native02-inspection` completes the F2000/SPAS sequence with three
magazine cycles, rifle support, both shoulders, one pump/shell and rifle fire.
Trace SHA256`47b988fafbbbfaeb1ddee94c9c2c2d6988744507b04eced7cc049c1ad6fc2864`.
The private side-on input pose enables inspection; it uses post-shot acquisition,
not the eight-cycle run's held-support-at-shot scenario.122 moving pump poses
produce244 paired copies with no fallback or rejection. Twelve sampled frames
show the stock/ring attached; gaps remain between captures. Final F2000 counts
are28/180 after its bounded automatic pulse fires twice.

Both runs consume240 stereo pairs, discard zero and restore all camera overrides;
hooks drain and BC2 survives. Full callback coverage and headset acceptance
remain false. The older failed traces are preserved.

The copied freezer retained387 in two display-label fields of the initial
388 receipts. Original receipts remain unchanged. `build-receipt-corrected.json`
beside each normal/private receipt documents this metadata correction; source
closure, compiled provenance, binaries and source archive correctly identify388.
The inspection receipt has the correct388 labels from creation.
