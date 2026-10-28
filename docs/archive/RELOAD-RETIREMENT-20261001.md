# Cancelled reload retirement

`reloadFlowRuntime::RetireRequestCycle(identity, cycle)` retires only the exact
identity/cycle retained by the native request policy. It deliberately does not
resolve the current live actor: an old cycle must remain cancellable after death
or equip changes. A wrong old token cannot cancel a newly started policy cycle.

The operation uses the same `requestEntryGate` as native callback admission and
StartRequestCycle. While that exclusion is held, the runtime cancels the exact
old policy, requires its own scope to be the only active callback, and checks an
unchanged callback revision before and after draining orphaned invocations.
New arrivals fail entry admission and cannot acquire an old held decision. Any
revision, cancellation-epoch or contention race withholds the receipt. There is
no native original call under either policy or entry exclusion.

A successful `ReloadCycleRetirement` records exact identity/cycle, a strictly
increasing event and actual observation time, and a 200ms original deadline.
Each receipt is a separate real retirement observation; old receipts are never
restamped. Cancellation/drain preserves pending/unresolved outcome and cycle
history, grants no insertion acknowledgement and never rolls back ammunition.
The supply adapter may use a genuinely fresh idle reserve observation after
retirement to rebaseline an unknown virtual shell.

`ReadReserve` now starts its observation immediately before the first of its
two actual branch/count reads. The deadline remains bounded by the original
owner deadline and 200ms from the count observation. This fixes fresh counts
being incorrectly dated before a completed native acknowledgement merely
because the owner metadata snapshot was older. Exact repeated counts, owner
identity, callback count and revision checks remain mandatory.

Runtime reporting adds retirement attempts, successes, contention, wrong-cycle
requests, unquiet attempts, withheld receipts and the latest successful event,
cycle and original times. All normal defaults remain off.

Focused validation passes on both x86 and x64: seven new retirement/read-time
groups, seventeen existing request-cycle groups, six persistent-runtime groups.
The tests exercise genuine policy hold/advance/orphan state, unknown outcomes,
completed native counts, monotonic receipts, overflow, a real denied concurrent
entry and original reserve deadline retention. The native x86 runtime compiles
and links. No game process or headset was used.
The merged read-miss + retirement runtime also compiles/links on native x86 and
passes all seven retirement groups, using the other agent's actual policy delta.

Integrate the new header/test/fixture and apply `operations.json` as exact unique
text replacements to the two runtime files. The operations are verified against
the separate read-miss candidate and intentionally avoid its helper, call-site
and report additions. `integration.patch` is also supplied against the original
normalized base. Do not overwrite newer runtime files with the isolated copies.
Full parent builds and live cancellation/rebaseline validation remain pending.
