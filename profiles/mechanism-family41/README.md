# Shared weapon-family enrollment

This source batch enrolls 24 belt-feed configurations across PKM, M249, Type 88,
M60 and MG3, and five slide/magazine configurations across M9, M1911 and MP443.
The existing M60_sp and MP443_sp mechanism registrations are preserved; generated
tables add 23 feed and four slide rows. Magazine tables preserve the opening426
composition and add four native registrations and three geometry profiles.

`source-review.json`, `admissions.json` and `mechanism-receipt.json` record the
exact offline authored-data and native-adapter reviews. They do not assert
actual-game, controller-calibration or headset acceptance.

When integration is authorized, configure both existing canonical build trees
with `cmake -S . -B build/<architecture> -C profiles/mechanism-family41/Enrollment.cmake`,
then run the required normal `Build.ps1` checks. Do not configure a canonical
tree against the coordinator worktree. Existing cache values are replaced only
when this enrollment file is explicitly applied.

The matching 57-record cache is staged locally at
`reports/coordinator-20261030-family-enrollment41/body-ammo.fvrprop`; its identity
and size are in `enrollment-receipt.json`. It preserves all 49 prior records with
lossless compaction. The cache is excluded from Git and must accompany the new
catalog at installation. Source enrollment does not perform installation.

Both architectures passed the production registry consumer and actual cache CPU
loader checks. Evidence: `handoff/integrations/coordinator-20261030-pipeline34/CHECKS.json`.
