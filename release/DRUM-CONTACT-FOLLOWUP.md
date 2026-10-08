# Authored drum contact candidates

MG36 and XM8 LMG now produce complete magazine-contact candidates through the
shared extractor. Six exact configuration definitions reduce to two equivalent
body/geometry results. They are not six different weapons, and **no new native
or headset capability is enabled**.

| Native asset | Same-frame interval | Drum travel in interval | Maximum relative wrist drift | Insertion direction |
|---|---:|---:|---:|---|
| `MG36` | 1.533–1.733 s | 60.4 mm | 3.9 mm | Local +Y, from receiver geometry and initial withdrawal |
| `XM8 LMG` | 2.650–2.850 s | 58.8 mm | 5.1 mm | Local +Y, from receiver geometry and initial withdrawal |

The predecessor check required more than 100 mm of separation from the closed
magazine pose throughout a stable 200 ms interval. These drums have complete
wrist, all 15 finger transforms and item motion near insertion/removal, so that
distance rule rejected useful authored contact. The original carried-window
route retains first priority and unchanged output. Only when it finds no window,
the shared alternative requires net item motion above four times the existing
10 mm wrist-error bound within the same stable interval. Every existing wrist,
finger, proximity, sample-rate and duration threshold is retained. A static
support pose or a sliding hand is insufficient.

The drum's widest dimension is horizontal, which does not identify its insertion
axis. The alternative requires an exact static closed baseline, a contiguous
near-seat withdrawal segment with consistent direction and little rotation, and
a unique principal end nearest the receiver. Actual withdrawal must corroborate
that end. Both models select vertical Y. Missing, ambiguous or conflicting evidence
rejects the candidate; there is no weapon-name axis branch.

Wrist-to-item and all fingers come from one decoded authored frame. The attached
magazine pose stays static and exact; rail depth, travel and tolerances remain
shared VR policy. The linear rail is an assist approximation to authored motion,
not a measured native constraint. Body ammunition placement also remains shared
policy, not animation-derived. Spline interpolation has not been independently
validated against the game. CPU previews show plausible contact with residual
mesh intersections; they are not a zero-clipping or headset result.

## Reproduce

Run the [LMG extraction workflow](LMG-AUTHORED-MECHANISMS.md) with `--asset MG36`
and `--asset "XM8 LMG"`. Its `magazine-candidates.json` now includes the two drum
results. For private C++ output, use `bc2_authored_magazine_geometry.py` with those
exact bindings and explicit `--paired-reload-grasp` and `--header-asset` names.
Keep generated matrices, geometry, headers and previews local.

```powershell
python -B -m unittest discover -s tests -p test_bc2_drum_grasp.py
python -B -m unittest discover -s tests -p test_bc2_authored_magazine_geometry.py
```

Validation covers 33 deterministic checks, private header validation on x86 and
x64, and exact installed predecessor comparison: accepted AEK and scoped-XM8
profile dictionaries, digests and generated headers remain identical. Tests
reject stationary parts, unrelated finger motion, wrong closed baselines,
ambiguous receiver ends and malformed motion receipts. Native family admission,
render binding, full reload/restore and headset feel remain separate gates.
