# Fleet reference audit and bounded support authoring — 371–372

Player checkpoint 369 is preserved. This work adds an offline reference-bound
calculator and an opt-in support-data authoring path. It does not select a new
runtime grip header or change native reload, pump, bolt, firing or menu behavior.

## Installed-data audit

The audit follows exact configuration, skeleton, animation and mesh hashes.
It revalidates 82 configuration references and all 59 compiled support entries,
then produces 42 assembled-pose previews. The inventory contains 40 unique mesh
contents; four references initially lack a constant assembled pose under the
older world-ancestor classification. They cover AN94 variants and M249.

All 82 exact weapon → firing → function chains resolve from the installed
single-player and multiplayer common archives. Their typed `InitialDirection`
values are `(0, 0, 1, 0)` in the raw authored basis. This is not proof of a live
projectile transform. Several clips leave `jntWpn_Flash` near the rear of the
weapon, so that marker must not become a universal muzzle-position heuristic.
The seven geometry gallery pages were inspected; geometry remains private.

## Full-curve reference bounds

`tools/bc2_authored_pose_bounds.py` bounds an entire decoded animation relative
to the weapon, using every control rather than an endpoint or sparse sample.
Position splines remain inside their control hulls. Quaternion normalization
is bounded using the hull's distance from zero. Rotation and translation bounds
propagate through the two unshared hierarchy branches; common ancestry cancels.
Identity scale is required across the full dependency chain. Missing tracks,
antipodal/poorly bounded quaternion controls, nonfinite data and scale/shear fail
explicitly. A small, separately recorded numerical allowance covers this tool's
double-precision evaluation.

These are bounds for the repository's decoder, not a verification of native
Granny interpolation. Existing `static_authored_pose` classification and the
default strict support generator remain unchanged. `derive_bounded` recomputes
the envelope from exact Clip/Skeleton objects, checks their content identities
and original reference matrix, and labels the output `bounded_decoded_reference`.
It does not accept a supplied pass flag or retarget the right hand.

The installed batch measures 88 hand references across 44 unique clips. Using
an explicit review tolerance of 0.25 mm and 0.05 degrees, 14 previously nonconstant
references fit the conservative bounds. Three do not: both AN94 hands and the
MP443 right hand. No failed reference is enabled. Samples are only a separate
sanity check, never the constancy proof.

| Prepared support family | Exact configurations | Maximum position bound | Maximum angle bound |
| --- | ---: | ---: | ---: |
| M416 | 1 | 0.0594 mm | 0.01293° |
| M249 | 1 | 0.1284 mm | 0.00741° |
| MG3 | 4 | 0.000112 mm | 0.0000200° |
| Saiga S20K | 1 | 0.000154 mm | 0.0000292° |
| TOZ T194 | 3 | 0.000106 mm | 0.0000147° |

The ten candidate contacts are in `profiles/bounded-support372/`. They are not
selected by `Build-Checkpoint.ps1`; the normal header remains support312 with
59 entries plus its preserved SPAS contact. Do not count these candidates as
completed weapons, accepted reloads, native interaction tests or headset passes.

The same hierarchy reasoning recovers eleven fixed AN94 parts and twelve M249
parts relative to their moving weapon ancestor. Both recovered previews were
inspected. This corrects an offline reference gap; it does not suppress a game
animation or admit the AN94 hand poses.

## Validation and evidence

The 133 authored-reference Python checks pass, including 16 new bounds/authoring
cases. They cover interior motion hidden by equal endpoints, rotation lever arms,
moving reference frames, noncommuting rotations, common-parent cancellation,
large world origins, invalid scale, antipodal quaternions, source tampering and
preservation of the strict generator. Both full architecture builds pass all246 suites
(x86:23.52s; x64:13.92s), and the unselected generated header passes syntax
checks on both architectures. The initial restricted build stalled before
compilation output; only that owned Ninja process was stopped, and the authorized
retry outside the process sandbox completed.

Private evidence under `bc2vr-recovery`:

- `authored-axes371-fleet/`: exact geometry/pose receipts, galleries, firing chains
  and hierarchy analysis. Initial four rejected geometry rows remain recorded.
- `authored-bounds372/`: analytical envelopes, recovered geometry, build logs and
  tests. The interrupted sandbox build is retained separately from its retry.

No new native gameplay or headset run was needed for this offline change.
Player369 retains its own body-display and menu-restart evidence; human SCAR
alignment, SPAS feel and sustained campaign acceptance remain pending.

Next integration should preserve the current 59 contacts, select the new ten
only with their bound receipts, and exercise representative support ownership
through the existing emulated input path. Native ammunition and mechanism
enrollment remain independent; this tool is not a shortcut around those checks.
