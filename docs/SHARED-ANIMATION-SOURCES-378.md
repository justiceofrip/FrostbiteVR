# Shared animation sources — 378

The magazine/mechanism extractor previously assumed the reload clip was in the
same archive as the selected hand pose. SPAS references its reload and pump
clips from its authored animation tree, but their bytes live in the common
level archive. The shared lookup now searches the already verified hand-pose
and skeleton/tree sources and records the source actually used.

Every supplied archive must match its index receipt. Conflicting contents for
the same resource, ambiguous duplicates, wrong resource kinds and stale indexes
are rejected. Identical contents are reusable; an index match alone is not a
payload match. The lookup does not guess the game's mount precedence or mount
additional bundles.

## Results

- The existing mechanism batch processes 26 exact configuration records in
  five distinct content jobs with zero errors or missing reference gaps.
- All 18 previous records remain identical. Eight SPAS configuration records
  are added from common-archive clips; these are variants, not eight new guns.
- The action survey resolves the exact authored tree and AnimationAsset GUID
  before selecting the BoltAction replacement. Four shotgun models yield eight
  linear-part candidates. Motion alone does not identify the part's function.
- SPAS fore-end `jntWpn_4` has 1,176 rigid triangles and decoded travel from
  -1.658 to +94.662 mm relative to its hand-pose reference. Its positive peak
  differs by 0.027 mm from the historical native calibration's 94.635 mm peak.
  The reference is not assumed to be the native minimum stop.
- SPAS `jntWpn_5` is a separate 240-triangle moving part with a different
  excursion. The current fixed-stock list is `jntWpn_6/12/13`; a moving part
  must not be added to that list merely to remove animation.
- The four SPAS reload/pump resources are byte-identical in the selected SP
  and MP common archives. The existing 30 Hz contact extractor finds seven
  stable candidate windows on the fore-end; this is sampled contact evidence,
  not a full-curve bound. The authored reference and historical native closed
  translation differ by 0.018 mm.

The clip source correction is general. No weapon-name special case, copied
hand offset, native ammunition operation, or new player capability is added.
The recovered data supports later mechanism enrollment and presentation review.

## Validation and preserved player build

All 219 relevant Python tests pass, including source conflicts, absent clips,
wrong types, stale indexes, repeated-source payload conflicts and source bounds.
Both full normal builds pass 246 C++ suites (x86 22.68 s, x64 14.86 s).
Native source identity remains
`8b2f2b4ec35fbd5fb938d16021817cb1e3d7900a178f246bf71ca516a88320c8`.
Normal player375 and its headset package are preserved; no game was launched
for this offline checkpoint and no new headset acceptance is claimed.

Private evidence is under `bc2vr-recovery/shared-clips378`,
`shared-mechanisms378` and `mechanism-rails378-verified`. The batch run receipt
retains its earlier `recovered_mechanisms377` schema label; its recorded inputs,
hashes, 26 records and checkpoint378 location identify this run. It is not the
18-record checkpoint377 result. Original game assets remain local and are not
included in the source snapshot.

Remaining work includes native admission for additional mechanisms, paired
contact review, controller/weapon alignment and human visual acceptance. Decoded
authored curves do not prove native state selection, chamber completion or
headset feel. See [player375](SHARED-PUMP-GEOMETRY-375.md) and
[actual coverage](WEAPON-COVERAGE-CURRENT.md).
