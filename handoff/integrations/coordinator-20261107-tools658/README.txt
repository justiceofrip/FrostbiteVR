Tools658 removes redundant work before simulated sight-selection publication.
652 removes a second write of the exact snapshot already retained before admission.
654 reuses four unchanged matrix inverses only within each current plan; no matrix
multiplication is reassociated and no value survives into a later plan.
656 coalesces the earlier optional poll into the transport's final prepublication
poll only for the first explicitly source-bound command with a pending response.
All current-source/owner/input/family checks, original source deadline, command
acknowledgement and safety release remain. Native/gameplay policy is unchanged.

Independent657 found that skipping the earlier poll without a pending request
could publish before consuming a newly available failure. ROOT added the pending
request condition and a portable regression. The retained unfixed candidate fails
that new control; final applied source passes28 combined affected checks.
Nine portable652/656 tests plus the657 edge are now in the shared sight-driver suite.
654's30 exact-plan/refusal comparisons and four stale-cache controls are reused
against identical applied planner bytes; original600 inversions become5 per plan.

Offline measurements: duplicate write median1.7319ms; alternating planner pairs
show median paired saving1.3525ms. Actual646 also performed duplicate request
reads, one taking6.1034ms. These numbers do not establish a live recovered deadline
or game FPS improvement. A changed actual run remains required.
Evidence: WT reports/coordinator-20261107-feedback652,654,656,657; ROOT combined
checks/source review in canonical reports/coordinator-20261107-feedback658/root-integration.
Actual646's failed result, source deadline and shutdown access violation remain;
all owned processes exited and its three profiles/settings were restored.

Installed private0.1.73/fallback72 and native source5d7d04f remain unchanged.
Actual602/run02 build/installation receipts remain authoritative, including their
original run01 source/header provenance. No native rebuild, reinstall, new complete
reload, headset acceptance or public release. Tools638/648 fixes remain included.
