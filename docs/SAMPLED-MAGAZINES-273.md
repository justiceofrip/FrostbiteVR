# Shared sampled magazine paths — local273

The previously isolated sampled-path agent work is integrated into the common
`ReloadInsertion` and `DetachableMagazine` consumers. A profile can now supply
measured poses from the withdrawn position to the exact seated position. The
same path handles original removal/return and replacement insertion. Profile
data supplies the differences; there is no M9- or MP443-specific gesture code.

The measured M9 and MP443 profiles contain twelve poses each, derived from the
authored reload and tied to exact configuration, mesh, skeleton and contact
evidence. They remain experimental geometry, **not enabled native pistols**.
Slide/chamber handling and review of the pistol native firing/reload class are
still required. The normal player registry remains seventeen magazine
configuration prerequisites, not seventeen accepted weapons.

Integration preserves the later272 ownership, pending-receipt and recovery
changes. Existing straight rails select the original arithmetic. Curve progress
uses actual fresh hand motion, bounded displacement, local orientation and
ownership; elapsed time or a duplicate input cannot seat an item. Ambiguous
path projections, jumps and loss of ownership reject instead of guessing a
different point on the path. Original magazine ammunition remains its own
resource until a matching native outcome completes the return.

The first full integration build exposed stack exhaustion in large registry
fixtures (Windows `0xc00000fd`), and an additional x86 physical-consumer fixture
failed. The candidate embedded 32 poses in every profile, even straight rails.
Paths now own only their supplied samples; ordinary profiles have empty storage.
Validation still bounds paths to32 points and requires a matching sample count.
Copies own independent samples. The original failing fixtures are unchanged,
and their failed logs and one pre-fix executable are retained privately.

The controller diagnostics now also use the shared path-to-hand transform.
The legacy transform's multiplication order is preserved exactly. The older
private animation-hold detached adapter is not a sampled-path acceptance target;
new sampled native enrollment must use the current resource consumer.

The extraction helper, its source tests and measured fixtures are included in
source packaging. Runtime helper dependency checks reject an omitted sampled
path helper. No installed game assets or cache are added to the source package.

## Validation

- The final focused x64 run passes seven suites, including both new sampled-path
  suites, existing insertion/magazine behavior and the ordinary resource driver.
- The measured consumer exercises twelve original return cycles across empty,
  partial and full counts, plus two discard/replacement cycles, using real
  hand ownership, supply and inventory policies with simulated native receipts.
- Extraction and packaging checks pass; the packaged source runs the measured
  path tests without relying on the original workspace.
- Full normal x86/x64 builds pass243 suites each. The101 affected Python tests
  pass, including packaging and exact re-emission of15 existing geometry rows.
- Native273-01 fails before any ammunition command with an idle magazine-view
  cancellation. The retained record does not establish its precise cause.
- Fresh unchanged273-02 completes the first two magazine cycles (four native
  commands reported completed) and interrupted pump recovery, then fails shell
  pickup. It records one shell acquisition but no native shell start/submission.
  Full shoulder-return/final-firing acceptance was not reached.

Both failures and their traces are retained under
`<local-recovery>/pipeline-runtime-20261005/root-monitor/resource-f2000273-01`
and `resource-f2000273-02`, with first/second-failure-review.json respectively.
Normal273 binaries and symbols are frozen in `normal273-player-checkpoint`.
The native regression is **failed**, and headset272 remains the staged candidate.
The exact native failure causes still need investigation.

No new pistol native or headset acceptance follows from these tests. The
seventeen existing normal registry entries and their live evidence are separate.

## What still needs implementation or acceptance

1. Pistol native reload admission and a shared slide/chamber operation.
2. Belt-fed cover, feed and charging sequence connected to native completion.
3. Broader exact weapon enrollment, bolt last-round behavior and varied
   holster/weapon-switch interruption coverage.
4. Sustained campaign/death recovery beyond the single scene-rebuild sequence
   accepted in271; rifle-first actor retirement still lacks actual-game evidence.
5. Headset hand placement, optics/reticle and body/equipment visual polish.

Underbarrel automatic reload remains the agreed interim behavior; physical
underbarrel loading is deferred. Multiplayer is outside the single-player
release checkpoint.
