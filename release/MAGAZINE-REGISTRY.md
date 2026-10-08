# Shared magazine descriptor registry

The runtime resolves immutable descriptors by the exact observed native asset name,
configuration path and every reviewed configuration value. It carries that same
descriptor through equipment geometry, native request selection, physical magazine
interaction and presentation. Different optic/configuration paths can share a name;
name-only lookup rejects ambiguity. A geometry entry still needs its own exact mesh
and rig match. A name or `rtMagazine` is never physical-mechanism evidence.

The accepted XM8 and AEK descriptors retain compatibility keys 0 and 1. Additional
rows use a 64-bit prefix of the full configuration SHA-256. Duplicate prefixes,
conflicting full digests and duplicate exact name/path rows reject selection.
The identifier conveys no permission. Disabled or unreviewed rows cannot resolve.
Accepted XM8 calibration keeps priority for its exact immutable descriptor.

Generate descriptor jobs using `bc2_magazine_descriptor_manifest.py`, then run:

```powershell
python tools/bc2_magazine_registry_header.py --jobs local/jobs.json `
  --header local/MagazineRegistry.h --receipt local/registry-receipt.json
```

This emits disabled candidates only. Unknown dispatch, nonzero authored bolt fields
and conflicting data are omitted rather than replaced with invented values. An
optional `--reviewed` JSON array can approve exact entries, with `key`,
`descriptor_digest`, `family_proof` and `evidence_sha256`. The currently supported
family proof is `bc2.automatic-magazine-zero-bolt.v1`; it names the previously
reviewed executable-wide implementation, not a new weapon-name allowlist. Reviews
must bind the exact generated descriptor and an independent review artifact.
Existing baseline registrations cannot be overridden. The emitter does not perform
that review or claim native acceptance on its own.

The optional CMake `BC2_REVIEWED_MAGAZINE_REGISTRY_HEADER` is empty by default.
An explicitly selected header is compiled once into the registry translation unit;
it cannot mutate at runtime. Missing geometry stays unavailable. The existing
experimental geometry option is separate. Native current owner/configuration,
three firing copies, counts, original deadlines, retirement and transfer checks
remain mandatory for every operation. No ammunition or chamber ledger is added.
Underbarrel reload stays native. No additional installed weapon is enabled by this
change, and static/CPU tests do not claim native or headset acceptance.

Synthetic CTest fixtures exercise same-name/different-path descriptors, disabled
rows, digest collisions, three capacities/timings, replacement and original-return
consumers and A→B→A retirement without recycling request or seat identifiers.
They contain no extracted game assets. Private generated jobs and headers are
excluded from the public source package.

The internal profile key widened from one to eight bytes. It appears only in
native-process policy/equipment/empty-control state and function arguments; no
shared-memory packet, exported DLL C ABI, launch configuration or IPC layout
contains it. Rebuild every native consumer together. Existing diagnostics keep the
numeric profile field but now serialize all 64 bits.
