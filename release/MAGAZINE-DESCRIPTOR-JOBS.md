# Disabled native magazine descriptor jobs

`tools/bc2_magazine_descriptor_manifest.py` consumes the existing schema1 manual-reload catalog. It emits exact configuration identities and data shaped like `ReloadConfigDescriptor`, with original float32 timing words, authored capacity, proposed bounded completion allowance and explicit deferred jobs. It reads no process or game binary and registers no runtime profile.

```
python tools/bc2_magazine_descriptor_manifest.py --catalog private/manual-reload-catalog.json --output private/magazine-descriptor-jobs.json
```

Multiple optic variants may have one native name and different exact configuration paths. They remain distinct. Identical SP/MP content retains both origins; conflicting content for one observed native name/path remains deferred. The stable key includes weapon/firing/function resource paths, GUIDs and content digests. Unknown enum translations, nonzero bolt configurations outside the currently reviewed dispatch shape, malformed identities and missing fields cannot become eligible registrations.

The optional `--reviewed-baselines` JSON array references registrations that already exist. Each row contains `key`, `descriptor_digest`, `registry_source_sha256` and `descriptor_source_sha256`. These are attribution references supplied by a source review, not authority tokens. The tool requires an exact descriptor match and rejects duplicates/deferred baselines. It still emits `Candidate` and `runtime_enabled:false` for every descriptor. No candidate can be enabled through this input, and no generated C++ runtime header is produced.

The field mapping reuses the current descriptor: AutomaticFire2, rtMagazine1, Fire8, Reload29; native timing words at +0x10 threshold, +0x14 delay, +0x18 time, +0x20 reload type, +0x24 FireLogic and +0x2c reviewed cancel-on-switch ReloadLogic0. Other symbols remain unresolved. Completion allowance is the candidate's own authored reload/delay/post time plus the existing700ms policy margin, bounded by the current10s profile limit; values outside that bound are deferred rather than clamped. This is policy data, not a native clock modification.

Authored capacity is distinct from current all-three native effective capacity. A class or rtMagazine value does not establish a physical detachable magazine, a belt/cover mechanism, a main grip, or current mesh ownership. These jobs identify missing exact geometry/grip and native-current-configuration checks. Reusing the existing executable-family proof still requires an explicit reviewed roster selection and the unchanged live owner/configuration/hold/transfer/cancellation guards. Empty-control acceptance is separate.

Run the portable tests with `python -B tests/test_bc2_magazine_descriptor_manifest.py`. Extracted catalogs, baseline references and generated job matrices remain private and are not source-package payloads.
