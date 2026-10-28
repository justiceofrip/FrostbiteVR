# Checkpoint 412 headset failure — October 16, 2026

The user reports two SPAS failures: firing, pumping and holstering stopped,
while grip still worked. Pump audio also failed to follow the physical movement.
This supersedes the earlier description of412 as headset-untested. No new
candidate was deployed by this investigation. The subsequent413 deployment and
its separate validation are recorded in [CHECKPOINT-413.md](CHECKPOINT-413.md).

## Build and session identity

At the time of this run, desktop launcher0.1.2 selected normal412. The actual loaded
`BC2NativeProbe.dll` was read from BC2's module list and hashed, matching
`f8ed8cd0d363825e0fb8a6135391fb54437d8ed922325e674bd0bfd880daa48f`.
The session's probe/trace/host hashes also match the frozen412 build receipt.
This is a current-build failure, not merely a stale launcher label.

The successful session began at03:34:50 EDT (07:34:50 UTC), with trace
`native-trace-20261016-073451-790`. The preceding03:33 session failed during
OpenXR instance creation (-2), before native injection; it is not a second
recorded gameplay failure. Do not equate the two launch folders with the user's
two observed weapon failures.

The existing-campaign retry measured9.38s through launcher attachment: storage
6.17s, campaign readiness0.64s, attach2.36s. This is not a cold-start headset
latency measurement and does not time the first perceived eye image.

## Mechanical evidence

- First journaled failure: cycle/shot4, Held to Cancelled with Control failure.
  Loaded4, reserve24, capacity8; input was fresh, focused and both hands tracked.
  One native-cycle gate-contention failure was recorded. This preceded the first
  recorded focus loss, so a headset disconnect does not explain the first failure.
- A later verified actor replacement allowed cycling to resume. The latest
  physical-pump instance began four cycles and completed three releases,
  acknowledgements and support returns. Cycle8 did not finish.
- Final native state: Cancelled with Input failure, cycle/shot8,
  `blocks_fire=true`. No guarded recovery steps or recovery releases occurred.
  Physical-pump control rejection count was2435. Geometry rejection also occurred
  during cycles6 and8;6 recovered after release/regrab,8 did not complete.
- The holster itself ended Held, with four requests/four commits/no cancellations
  and no pending transition. Inventory recorded14219 blocked observations.
  `Bc2Gameplay.cpp` propagates unresolved pump debt into inventory `reloadBusy`;
  this explains the shared fire/pump/holster lockout while grip remains usable.
- No physical shell insertion transaction started during this run.

The current `WithNativeCycle` wrapper increments its cancellation epoch whenever
its policy lock fails, even for an internal observation. The next successful
holder applies Cancel(Control). The lock budget is250 microseconds. This is
consistent with the first journal entry, but its failed callsite was not recorded.
Do not globally remove that cancellation: missed native Evaluate/Finish/Commit/
Restore callbacks can leave native changes unobserved. Pure observation and
pre-dispatch retries need a separate non-destructive busy result; durable physical
submissions need confirmed retry/receipt handling before using that policy.

The detailed callback window ends before the first failure. It does not identify
which access contended or the exact unsafe native context behind final Input
failure. Both failure paths require investigation; treating them as one proven
root cause would overstate the evidence.

## Audio and outcome

The current manual pump controls visual bone travel, not BC2's audio timeline.
The exact SPAS sound asset supplies one fixed0.601333s sample. No manual mechanism
audio dispatcher or verified selective stock-sound suppression exists. See
[audio investigation](../PUMP-AUDIO-INVESTIGATION.md). Shared future event semantics
should cover other mechanism classes, but this run does not prove they all fail.

The user authorized ending only the mod. It ended cleanly, flushed its logs,
drained callbacks and recorded no new crash report. BC2 remained responsive and
SteamVR remained running. One world recovery completed successfully. No new
headset session, game input, build or native code change was made for this audit.
