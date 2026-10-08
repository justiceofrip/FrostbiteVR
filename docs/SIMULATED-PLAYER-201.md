# Magazine startup pulse boundary, October 8

No new headset acceptance. This change addresses the second-cycle failure found
by the repeated real-game synthetic-controller sequence in200.

## Recorded failure

`magazine-player200-01` returned the original22-round magazine while preserving
22/191. Cycle2 started at input611. About31ms later, native branches entered
Holding while the original100ms Reload pulse remained active. The following
branch0 callback carried Reload and failed the existing native Safe check with
Timing. Physical input613 then received KeepAlive rejection, and the scripted
driver failed afterward. No replacement was acquired, submitted or completed.
Its final30/183 came from ordinary native fallback and remains a failed sequence.

## Shared correction

Physical Start passes an immutable typed proof containing the exact original
control and its100ms pulse end. Physical output uses the same end. The native
magazine policy cannot enter Holding until that pulse ends and all three safe
native contexts have genuine source timestamps at or after the end. The timestamp
is sampled before the existing context read; later policy processing cannot make
an older context fresh. KeepAlive cannot change the proof or move its deadline.

Native Safe, state, profile, timing, ownership and restoration checks are retained.
Invalid context evidence can wait during Arming; it cancels an established hold.
A short or expired physical pulse rolls back only the unstarted local request,
without a native cancellation or fabricated retirement. Native Start separately
rejects expired/forged proof. Registration inspection must match the original
proof. Explicit diagnostics that send no start pulse retain their prior API path.

This is shared magazine policy, including reviewed XM8 and AEK configurations.
It changes no individual weapon geometry, native offsets, ABI, ammunition-write
scope or capability admission. It does not qualify additional weapons by itself.

## Validation

Both architectures pass10 native-cycle,42 physical-consumer and18 diagnostic
fixture groups. The old policy fails the premature-holding case. Added checks
cover mixed-age contexts, stale reads processed late, source loss after Holding,
immutable proof across renewed input, forged identity/cycle/deadline, expired
pulse, integer-limit bounds, and local rejection followed by a valid retry.
Both affected native translation units compile on x86/x64.

The combined build also required the shared body-render test fixture to accept
and validate the typed Start proof. This test-only amendment does not change the
reviewed production source hash. Seven independent Python audit regressions pass;
the new audit supplements the existing full sequence audit rather than replacing
its native accounting or cleanup requirements.

Both full composed builds pass all 186 suites (x86 68.98s; x64 26.73s).
The loaded native module was checked against probe SHA256
`04ca5140d22dbac60f00a969f5ca5da4ccb120cf7075f4f5731e46befc53d3e4`.

## Actual native repeat

`pipeline-runtime-20261005/root-monitor/magazine-player201-01` ran on fresh
BC2 PID41740, started at `2026-10-08T01:36:46.3522251Z`. The actual consumer
returned the original partly loaded magazine, then acquired, submitted and
completed one replacement. Counts stayed22/191 through the return and became
30/183 after the replacement. There were zero cancellations and zero magazine
palette fallbacks, with all three paired visual roles recorded. The receiver
consumed240 stereo pairs without an async timeout. Both late readers accepted
31 samples without rejection. Hooks retired, and BC2 was minimized.

The second cycle actually exercised17 fast-carry samples,12.65cm of displacement
and0.65rad of rotation, including two bounded carry-wait packets. This provides
live coverage of the diagnostic carry-wait correction that200 did not reach.
Saved native eye images show the original removal, replacement below the well,
and attached magazine afterward; headset optics and haptics were not tested.

The independent startup-pulse audit passed. The original pulse ended at
46063481324800ns, all three context observations followed it, and first Holding
occurred at46063506631700ns. No native hold preceded the boundary; recorded hold
writes were restored.

The unchanged strict sequence audit remains **inconclusive**, with one reason:
`first_native_counts_or_owner_changed`. The offending Commit record2107 has
its original22/191 entry but no diagnostic End record. Its parent Update2103
completed normally. The trace reports five record-lock misses, no capacity drops
and no nesting misses. Code inspection confirms that the independent native
invocation finishes before a nonblocking diagnostic lock; a busy lock discards
the journal completion while returning the independent result to gameplay.
This is an evidence gap, not proof of a native count change. The raw failed audit
is retained; it has not been loosened or relabeled as a full sequence pass.

A separate telemetry-only follow-up will retain exact captured completions across
that contention without blocking game threads or altering native authority.
Do not infer headset comfort, all-weapon coverage or release readiness from this run.
