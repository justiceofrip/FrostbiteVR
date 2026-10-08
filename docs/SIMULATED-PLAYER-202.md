# Diagnostic completion retention, October 8

This follow-up changes diagnostic recording. The shared gameplay correction is
documented in [201](SIMULATED-PLAYER-201.md).

The completed native 201 sequence exposed missing journal completions: Commit
2107 returned within completed Update 2103, but its diagnostic End was discarded
when another thread held the record gate. Request-mode gameplay uses separate
per-invocation evidence and had completed both magazine interactions. The strict
whole-sequence audit correctly stayed inconclusive because that End was absent.

The logger now stores an already captured End and its record id in a bounded
64-slot journal when the gate is busy. Publication never blocks or reads native
state. A single consumer holds the existing record gate while draining those
completions before normal Begin/End writes. A drained report holds that same gate
through its flush and record reads. Concurrent reports return explicit unavailable
evidence. Live reports do not flush.

The original record validation still checks id, thread, timestamp and duplicates.
Separate counters expose Begin contention, End contention, pending completions,
recovered completions, overflow and rejected drains. Overflow leaves the original
record unfinished. No missing native state is inferred or manufactured.

Native invocation completion, controller inputs, ammunition, hold policy, hooks
and capability admission are unchanged. Non-request mode retains its previous
null return when the diagnostic End gate is busy.

Six new regression groups are included in the existing runtime CTest target.
Both architectures pass those plus its 20 existing groups; x86 also passes 4,096
synthetic hook-forwarding calls. The affected production runtime compiles on both
architectures. The baseline uses a documented component reproduction of the prior
discard behavior with real record storage, rather than a full old-runtime test.

Both full composed builds pass all 186 suites (x86 70.96s; x64 23.68s).
The actual loaded probe matched SHA256
`5baf2590372607a6ee77563ea548b2de4ebda4282981db99ad7fce453ac57f01`.

## Verified native repeat

`pipeline-runtime-20261005/root-monitor/magazine-player202-01` passed the unchanged
strict sequence audit: `bounded_simulated_sequence_verified`, with no failures.
The independent startup-pulse audit also passed. Earlier failed and inconclusive
runs remain saved; the audit requirements were not relaxed.

The actual persistent consumer returned the original partly loaded magazine,
then removed it again and acquired, submitted and completed one replacement.
Ammo stayed 22/191 through the original return, then became 30/183 after the
replacement. There were zero cancellations and zero magazine palette fallbacks.
Removed, hidden and replacement visual roles were recorded as stereo pairs.
Fast carry measured 12.65cm and 0.65rad across 15 samples. No carry-wait packet
was needed in this run; that branch was observed twice in the prior 201 run.

All 7,218 retained native records have completions. Four End-contention events
were recovered by the journal, with no pending, overflow or rejected completion.
Five Begin-contention events remain explicitly counted; no rows were fabricated
for those calls. The bounded audit verifies its retained sequence and accounting,
and does not claim an exhaustive journal of every game call.

The strict audit found one exact authoritative transfer (record 6372). A preceding
client prediction restore matched its immutable snapshot, and subsequent client
and server results conserved ammo. Late client reads accepted 29 coherent samples
and rejected two non-atomic samples; the server accepted 31 with no rejection.
Those rejected reads remain in the raw evidence. The receiver consumed all 240
stereo pairs without an async timeout. Native hooks drained and restoration passed.

BC2 PID36408, started `2026-10-08T01:53:46.2422393Z`, was minimized after the run.
Its disabled DLL remains mapped; restart before another mod session. There is no
new headset session or headset-ready launcher. Headset feel, haptics, broader
gestures and other weapon configurations still need their own validation.

Consolidated evidence: `pipeline-runtime-20261005/player202-evidence.json`.
Full build logs: `player202-complete-build-logs` in the recovery root.
