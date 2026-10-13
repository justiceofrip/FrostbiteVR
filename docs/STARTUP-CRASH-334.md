# Fresh campaign startup crash334

After the successful normal333 rendered-menu restart, fresh BC2 PID79772
exited about13seconds after launch. The saved crash XML changed from the
backed-up profile copy and reports21:27:28Z on2026-10-10 with EIP0. The process
started21:27:16.1708247Z. The XML has no PID; the unique launched game plus the
changed content and timestamp establish the stated association.

No VR probe, renderer, equipment, reload or pump hooks were attached. The
verified207 campaign-Continue helper did run, observed the native type4 request,
then restored its vtable entry/protection with zero remaining callbacks. This
was therefore **not an entirely unmodified game**.

No dump/module map was captured for79772. The stack resembles the earlier
zero-IP failure, but an AMD module offset cannot be assigned from another
process's address map. Neither an AMD driver defect nor a mod root cause has
been established. The old baseline276 crash XML remains stale and is not
evidence of an unmodified-game crash.

One bounded retry started PID88104 at21:29:37.4930891Z with the crash watcher
attached before the startup helper. It reached campaign with three stable
actor/vehicle reads; the40-second watcher reported zero exceptions, detached
successfully and left BC2 running. A later native setup exited the boat and
selected the rifle without desktop input, then restored its hooks. The335/336
knife diagnostics also completed and restored on that process.

Evidence:

- `ready333-start01/crash-audit.json`, `crash-after.xml`, startup identity/logs.
- `ready334-observed01/ready.json`, `exceptions/completion.json`, startup logs.
- `knife335-setup01/completion.json` and the exact native setup receipt.

The successful retry does not fix or bound the intermittent crash rate. Retain
normal333's successful restart evidence and326's failing rendered restart as
separate observations. No driver, SDK, graphics setting or user profile workaround
was installed during this investigation.
