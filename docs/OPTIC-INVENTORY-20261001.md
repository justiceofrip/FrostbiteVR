# Offline optic capture inventory

2026-10-01. The new [inventory tool](../tools/optic_capture_inventory.py) audits
saved BC2 reports without opening a game process, installing hooks, sending input
or changing source captures. This is useful preparation for the single-player
optics work in [OPTICS-ADS-ROADMAP](OPTICS-ADS-ROADMAP.md), not a scope renderer or
an automatic weapon classifier. XM8/ACOG optics and the runtime are unchanged.

## Use

From the project root, using the project's Python interpreter:

```powershell
python -B tools/optic_capture_inventory.py --trace reports/native-trace-20260928-123847-523 --reflection reports/launcher-reflection-20261001.json
```

Repeat `--trace` for multiple captures and `--reflection` for saved reflection
reports. Output is JSON on stdout by default. An explicit `--output` creates only
that named file, requires its parent to exist, and refuses to overwrite any
existing file, including source evidence. No output file is created implicitly.

Exit 0 means the available evidence parsed; it does **not** mean optics-ready.
Malformed evidence returns exit 2 with structured validation errors. Missing or
incomplete evidence remains explicit in the report. Output/write errors also
return 2. `status` is `valid_inventory`, `partial`, or `invalid`; classification
remains `unknown`, runtime enablement is false, and phase comparison remains blocked.

## Actual schemas supported

- `manifest.json`: game/probe SHA-256 identities and explicit `pass_evidence`.
- `pass-buffer-evidence.json`: the native array of eye/pass/sequence/kind/count,
  VS/PS/target/depth pointers, complete/overflow state, and four buffer descriptors.
  Buffer slots 0/1 correspond to VS0/VS1; slots 2/3 correspond to PS0/PS1.
  Completed blobs must exist with the declared byte size; verified blobs receive
  a reproducible aggregate hash. Advertised, bound, done and verified counts differ.
- `projection-bindings.json`: caller, frame, gather-eye and finite 16-value matrices.
- `camera-context-evidence.json`: context roles, valid 36-value snapshots, patch
  and restoration counters. Numeric roles are not renamed as optic roles.
- `tracked-camera-evidence.json`: declared frame/mask/steps. The tool does not
  claim to decode or validate the separate camera matrix blobs.
- `native-trace.json`: world-color target descriptors and observed weapon-profile
  asset names. These are not joined to unrelated pass samples by proximity or order.
- Explicit reflection files: saved type/field arrays. Aiming/camera/HUD/FOV/shader-set
  fields are listed as metadata leads, not verified runtime consumers or units.

JSON source hashes are retained. Invalid shapes, truncated JSON, duplicate keys,
nonfinite matrices, duplicate pass/slot identities and inconsistent buffer
completion are rejected. Collector overflow, unavailable buffers, incomplete GPU
captures and recorded context restoration failures produce partial evidence.

## Counts and limitations

The reviewed native collector selects the first shader/target/depth tuple per eye
and filters to one 1920x1080 viewport. Those are **source-contract observations**,
not a hash-based attestation of every historical probe. Legacy pass JSON does not
export viewport dimensions. The tool distinguishes optional explicitly recorded
width/height from world-color target dimensions and from the source filter;
it never fills missing per-pass dimensions using either of the latter.

Tuple repetition is counted in the saved samples and flagged against the legacy
first-tuple contract. Cross-eye sharing is reported separately. Native draw totals
and native repeated-draw totals remain unknown; a maximum sequence number or
sampled index count cannot supply them. Overflow uses the maximum recorded counter
per eye, not a sum of repeated cumulative values. Shader/resource pointers are
capture-local; equality across captures cannot establish a shared optic/material.

## Verified saved-capture results

| Capture | Pass inventory | Other parsed evidence | Optic/phase conclusion |
| --- | --- | --- | --- |
| `native-trace-20260928-123847-523` | 336 complete samples, 168 unique tuples per eye, no repeat/overflow; 672 constant blobs verified | 39 projection rows, 20 camera contexts; six relevant reflection leads when the launcher reflection file is supplied | Exact optic asset/mode/ADS phase unknown; not comparison-ready |
| `native-trace-20261001-113015-478` | Explicitly disabled; zero pass samples | 51 projection rows, 20 camera contexts; observed weapon assets remain unlinked to pass samples | No conclusion about presence or absence of an optic |

Both inventories parse without validation errors. These results only describe
saved evidence; no new native session or headset test occurred.

## First new capture requirements

To compare hip view with ADS, future capture metadata must bind exact game and
collector builds, physical item/attachment configuration, native optic/aiming mode,
actor/equip generation, native frame and explicit phase to the relevant draw
samples. Current `comparison_key` preserves unknown asset/mode/phase fields even
if only one asset appears elsewhere in the trace. Version 1 deliberately does not
accept a guessed phase from a folder name, controller press, or array position.

The missing evidence list also calls out texture/SRV and material identities,
blend/depth/stencil, sampler/mip/postprocess state, reticle/HUD draw identity,
native ADS acknowledgement, and complete viewport/draw coverage. Those additions
belong to a separately verified bounded collector experiment; this offline tool
cannot recover data that was not captured. They are needed to distinguish the
reported fullscreen optic from the separate ADS-triggered blur/detail change.
Removing manual left-trigger ADS is independent runtime work and is not reversed
by this tool. The future proximity-driven ADS path still requires proof that its
authoritative gameplay state can be separated from unwanted visual effects.

## Tests

```powershell
python -B -m unittest discover -s tests -p test_optic_capture_inventory.py -v
```

All 16 tests pass. The fixtures reproduce the inspected native schemas and cover
sample-versus-draw semantics, absent versus explicit viewport dimensions,
per-eye repetition, disabled/empty/unknown captures, overflow, pending/missing/
truncated buffers, malformed JSON and schemas, reflection provenance, missing
metadata, default read-only behavior and output-overwrite rejection. Windows test
execution needs permission to create ordinary temporary fixture files.
