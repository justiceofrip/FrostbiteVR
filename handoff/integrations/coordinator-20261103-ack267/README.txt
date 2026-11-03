267: passive failure tracing for the reload test driver.
The original command, owner checks and250ms acknowledgement deadline are unchanged.
Retains first plus31 tail polls and open/read/decode/validation timing in memory;
at most two failures are written after owned cleanup and observer shutdown.
Root59 driver regressions pass, with no skips. Existing native53 checks are reused.
Offline Windows32/32 sequences became visible; six state replacements failed with
WinError5. This reproduces publication contention, not actual264's timeout cause.
An explicit300ms read-denial fixture retained the original250ms refusal and214
WinError32 attempts. Its publisher also had five transient replacement failures.
Original failed outputs remain in ignored reports/coordinator-20261103-feedback267.
Actual264 closed normally with exact restoration; complete reload remains open.
No new native build or installation: private0.1.53/fallback52 remain installed.
