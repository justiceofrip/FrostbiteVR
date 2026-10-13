# Candidate286: drain original native reads across cycle completion

The285 run completed and acknowledged all eight SPAS pump strokes, then failed
before returning the final support grip. The first failure was Restore12608,
immediately after Complete. ClientA Update12603 began under the completion
observation grant. Another branch published Complete while that Update was
still running. ObservationAuthority revoked its original read; its nested Step
also reacquired a regular owner with a different revision domain. That Step
entered stock reload2->10, then prediction restored10->2 and correctly triggered
the contradiction check. No ammunition had transferred. This exposed another
timing-dependent cause of empty-weapon reload/holster lockup.

286 preserves only an already-issued observation token through completion of
the same cycle and native owner. Complete grants no new observation watch.
Every read still retains its original observed time and deadline. Explicit
owner cancellation, selection, a new cycle or expiry invalidates it. Nested
Steps prefer their exact original Update's read, verified by thread-local
invocation/firing/context identity, and continue to recheck structural ownership,
family/configuration and the live boundary adjacent to the scoped byte write.
No global context, state, timer or ammunition write is introduced.

The overlapping-branch regression fails on285 at the original read's retention
assertion. It covers loaded SPAS, empty SPAS and loaded M95, both before and after
acknowledgement, and proves owner cancellation still retires observation while
preserving any unacknowledged outcome. Native and full-build acceptance are
pending. This does not enroll last-round M95 or new reload families.

Failed native run: `pipeline-runtime-20261005/root-monitor/resource-empty285-01`.
Trace SHA256:
`976c169f115da61fb04ca60907e95bf7137153b9a05e2ef87ce385bfb620cb2d`.
The earlier284 pump/shell pass remains its historical result; it does not erase
the subsequently observed completion race. Normal280 is still the last frozen
player candidate until a later full normal build and combined native run pass.
