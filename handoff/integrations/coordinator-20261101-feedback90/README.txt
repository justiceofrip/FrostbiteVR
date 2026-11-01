Feedback90: preserve observation while acknowledging a final feed release.

Actual40 intentionally published its final left-hand release, then received an
unclaimed, blocked, pending native result before xr-sim's acknowledgement. The
driver incorrectly required movement admission at that point and stopped.
Claim20/root18 had renewed through the last held input; observed gate refusals
were zero. This is a diagnostic sequencing defect, not supported claim-loss proof.

Only a successfully published exact final release enters acknowledgement and
observation handling. Before publication, original source/owner/config/input/
lease admission remains required. Intermediate motion and SupplyPickup retain
their existing handling. Acknowledgements/counters still cannot prove native work.

26 focused observation tests pass. Of14 affected readiness/planning tests,13
passed initially; the remaining mock needed the new optional argument and its
unchanged ownership test then passed. Setup-only invocation failures are retained.
Native source closure remainsc9776d83; both341-suite normal builds and private40
package/install validation are reused. No rebuild, new installation or public
release. Complete native reload/muzzle/headset acceptance remains open.

Evidence: canonical reports/coordinator-20261101-feedback90; exact actual40 input
and snapshots under reports88/feed40/xr-game01/live-driver-c88c33bb6227487c88a933f33c913ef4.
