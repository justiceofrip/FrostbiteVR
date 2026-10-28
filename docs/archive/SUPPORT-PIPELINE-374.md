# Shared support contacts and independent input testing

Checkpoint374 selects the ten bounded contact variants prepared by372. The
previous59 contacts and separate SPAS anchor remain intact. There are now69
contact variants across38 meshes, plus the preserved SPAS anchor. Selection is
recorded in `profiles/support373/selection.json`; authored reference derivation
is described in [372](AUTHORED-REFERENCE-372.md).

The additions cover M416, M249, four MG3 variants, Saiga and three TOZ variants.
They reuse the existing selected-mesh binding, contact, hand ownership and
two-hand aiming path. They do not grant ammunition transfer, manual pumping,
charging, muzzle or right-hand calibration capabilities.

## Actual failure and shared correction

The373 MG3 run rendered240 stereo pairs and preserved ammunition, but failed
support acquisition. Its controller mapping was published only if magazine or
shell reload geometry existed. The private emulated player could not approach
the grip on a weapon without that ammunition enrollment.

374 captures original controller ownership, input sequence and lifetime directly
alongside the rig publication. `BuildSupportControllerMapping` validates that
evidence and converts the same independent contact wrist/body/weapon into metres.
It does not reuse a support-snapped hand, fabricate a reload lease or enable an
ammunition operation. Existing shot/support publication validation remains in
force. Normal support contact itself was already computed independently; this
fix removes the diagnostic's additional ammunition dependency.

The private support-only input mode draws the current weapon through ordinary
inventory handling, approaches the published support point, holds one genuine
support claim for at least750ms, and releases it. It does not shoot, reload or
cycle equipment. The actor/item/hand identities, original input samples, gaps
and deadline remain checked. Failures remain failures even when the trace exits
cleanly. The mode defaults off in normal builds.

## Validation

Both full normal builds pass246 suites: x86152.17s and x6449.24s. Focused checks
exercise the production mapping against the real tracked-rig transform for two
wrist calibrations, three body rotations and two world scales, with no ammo
geometry. Stale input, changed identity, missing tracking and invalid matrices
are rejected. Support-only consumer regressions check sustained ownership,
release, missing geometry, owner change and zero ammunition operations.

Two fresh, actual BC2 campaign processes used the same private374 binaries:

| Test | Fresh held inputs | Continuous hold | Longest input gap | Rendered support poses | Native ammo before/after |
| --- | ---: | ---: | ---: | ---: | --- |
| MG3_sp |46|753.229ms|20.809ms|47|100/500 unchanged |
| T194_sp |46|774.363ms|37.601ms|48|8/24 unchanged |

Each records one grab and one release,240 published/consumed stereo pairs,
zero discarded pairs, no GPU/camera-restoration failures, no rejected tracking
or palette packing/fallback failures, and zero submitted magazine operations.
The native hooks were disabled before each owned process was closed.

Private evidence is in `bc2vr-recovery/support374-{mg3,toz}-native01` beside
this repository. `support-audit.json` pins each trace and checks completion,
continuous ownership, release, unchanged counts and renderer results.

MG3 trace SHA256: `c703842231e0170f626dfd02ead47ad89e9f5c1d6f8004f2766244f1333ebe57`.
TOZ trace SHA256: `1fc5d7b8195c0b95ad5b630bda9b497164e4cd7fe304b862ef8079437b85715c`.
The failed373 trace is retained separately.

These are support integration checks, not reload or headset acceptance. They do
not establish that every contact variant was exercised in-game. The two new
families were equipped through a private exact-configuration helper; the helper
adds no normal-player ammunition capability. The TOZ's observed effective
capacity8 is distinct from its authored base capacity4. No manifest, game asset,
infinite-ammunition setting or REST/debug service was changed.

The normal374 binaries retain prior357/358 combined mechanics and369/370 menu,
depth and body-display evidence as historical tests of those paths. Human
SCAR/controller alignment, SPAS pump/stock appearance and continued campaign
play still require acceptance. LMG belt/cover handling, other pump/bolt variants,
pistol slide/chamber, third carried slot and knife integration remain open in
[the coverage index](../WEAPON-COVERAGE-CURRENT.md).
