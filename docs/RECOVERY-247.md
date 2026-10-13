# Recovery247 integration and emulated tests

This is local development after the failed245 headset run. It is not a new
headset candidate or release. The last frozen246 build passed237 suites on
both architectures, but its actual-game run stopped before the pump.

## Shared recovery

`WeaponCycleConvergence` now distinguishes when native idle was first
established from its latest refresh. Previously, native callbacks arriving
between observation and processing of a neutral controller packet could make
every neutral packet look too old to begin recovery. The fix preserves the
packet's actual timestamps. Recovery authority still requires two fresh native
rounds beginning after that original neutral observation, under the same
continuous guard and ammunition counts. Busy observations, custody loss,
expired evidence and owner changes retain their existing rejection rules.

Both unchanged-policy reproductions failed. The candidate passes19 shared
convergence groups and12 native-service groups on each architecture. These are
synthetic callback tests, not claims about an actual headset disconnect.

## Combined interruption driver

`Build-Checkpoint.ps1 -OrdinaryManualCycles -NativeCycleRecovery
-OrdinaryResourceInputProbe -OrdinaryResourceInterruptionProbe` selects an
explicit finite diagnostic. It supplies controller input through ordinary
consumers: rifle magazine operations, shoulder draw of SPAS, firing with support
already held, support-to-pump transfer, a partial stroke,500ms left-hand tracking
loss, neutral/regrip/full recovery stroke, shell insertion, support return,
holsters, original rifle magazine return and firing.

The driver cannot issue native reload commands, create completion receipts,
change counts or timers, or acknowledge its own gesture. Six new groups cover
the real pump consumer/native service with synthetic Update records, both
callback orders, held support, duplicate packets and negative authority/receipt
cases. Eight original pump groups and15 combined coordinator groups also pass
on both architectures. A regression confirms that repeated input must preserve
the diagnostic's tracking-loss flags through the outer coordinator.

## Magazine publication gap and diagnostics

Actual246 first-magazine warmup failed after a native ammunition publication
expired. Missing fresh data caused the idle magazine consumer to cancel and
clear its profile publication; the next driver packet then failed its armed
profile check. Exact per-guard evidence was not in that original trace.

`Bc2MagazineResourceProbe` now saves an immutable first guard failure with the
source sequence/times, selected/expected profile and full owner identity. It
does not weaken admission or overwrite the first failure with shutdown errors.
Its complete existing controller suite plus new onset-retention cases passes
on both architectures. The conservative idle-consumer gap fix is integrated:
only an already attached magazine with the same verified family, native/physical
owner and gun token may wait. It retains identity and real input safety clocks,
but publishes no ammunition authority, magazine target or chest prop. Fire and
new reload actions remain blocked; independent valid support ownership need not
drop. Fresh native data resumes the ordinary consumer. A permanent gap still
times out in the bounded diagnostic.

Three-profile focused tests cover350–400ms gaps, fresh resumption,19 identity,
configuration/input/claim rejection cases and exclusion of active operations.
The old consumer reproduces the F2000 warmup failure with guard mask6 (missing
and changed profile), unchanged owner and zero operations. Active removed or
replacement magazine custody gaps are explicitly outside this fix and need
separate handling; they must not be presented as solved by the idle case.

The launcher requires an exact frozen build matching its requested interruption
mode. The terminal-status checker distinguishes a normal pump completion from
the requested interruption, full recovered stroke and support return. Its five
tool tests pass; terminal status does not certify native callback completeness
or headset appearance.

## Outstanding acceptance

Full composed247 passed all238 C++ suites on x86 and x64. Source digest:
`472d2c6017148c53e690e36758ab2b0de1ff7a91d987eb1c7ed9fd085ab4cd8e`.
Ten focused terminal-status tool tests also pass. The first build attempt lacked
the new test target's platform include path; after correcting the CMake target,
both complete builds and test runs passed.

Actual247 failed before the pump. One original-magazine removal/return completed
with verified server/client receipts and conserved ammunition. The second
removal reached WellEmpty with a pending discard, then cancelled when the
ammunition publication was 9.3692ms beyond its 100ms deadline. Consumer cause3
(View), request916, cycle3 and unchanged revision4298 are retained in the trace.
No pump/native-cycle controls ran. Hooks disabled and drained; BC2 remained
responsive. See [recovery248](RECOVERY-248.md) for the resulting shared fix.

First-terminal native failure journaling is integrated. The checkpoint reload
crash location remains unknown. Never describe these changes as an accepted
fix for every possible action lock or scene crash.
