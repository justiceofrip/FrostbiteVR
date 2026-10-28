# Actor replacement retirement266

Both normal player builds passed all240 test suites on October9. Source digest:
`fbf5725d1e8ec63dd1baa868c19aaa9863f291115063d2fb4c873cb39edaffa0`.
The frozen build receipt is in `bc2vr-recovery/normal266-player-checkpoint`.
Automated input drivers are disabled in these player binaries.

An unfinished manual pump or bolt cycle survives ordinary cancellation. That
also prevented selecting a new player after a genuine checkpoint actor
replacement: the old native cycle stayed Cancelled/blocked, and the physical
adapter prevented requesting a new selection. A deterministic reproduction
failed before this change and now passes.

The new explicit retirement requires changed client and server actors,
increasing actor generation, exact prior cycle identity, a fresh coherent idle
replacement and a verified callback exclusion/drain. The runtime revalidates its
published owner before committing. It then reconstructs physical adapters for
the accepted new lifetime. Rejected selection changes no physical state.

Retirement retains the old outcome and any unconsumed Ready/release/recovery
evidence. Sequence high-water marks survive; an old input or callback cannot
become the new player's action. Same-actor tracking, equipment and space changes
still cannot discard owed action or invent completion. No ammunition write or
native pump completion is introduced by retirement.

Tests cover invalid/replayed proofs, same-actor debt preservation, retained
terminal evidence and higher sequence values on the replacement's next cycle.
These use synthetic native receipts. Actual checkpoint/on-foot reacquisition
and headset behavior remain unverified. This is not the fix for the separate
graphics-worker crash captured by scene265; see SCENE-RECOVERY-265.md.

Frozen264 remains available as the prior player candidate. Do not silently
replace its headset stage with266, or publish a fleet/readiness claim from these
build results. The latest scene comparison still uses frozen diagnostic264 so
its menu-consumer-lifetime experiment changes only that one variable.
