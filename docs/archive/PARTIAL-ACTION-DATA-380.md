# Isolate authored part failures — 380

The broad379 survey found three PP2000 reload failures. They share one clip:
`a0d6898c09998a0315981061fb4784414f1768ac60a66d76276fc668284ef303`.
Its `jntWpn_12` orientation controls each have unit length, but a decoded blend
falls below the existing 0.95 norm threshold. A 120 Hz diagnostic finds a
sampled minimum of 0.918801 at 1.108333 s. This identifies the rejection; it
does not prove how native Granny interpolates this track.

The previous mechanism analyzer evaluated all parts together and discarded the
whole action when any part failed. It now isolates a failure to that part and
its actual parent dependencies. The complete wrist/finger chain remains one
indivisible contact input. If that chain fails, no partial hand is manufactured;
if the common root fails, no dependent part survives.

The curve reader, orientation tolerances and normalization rules are unchanged.
No rejected transform is replaced, clamped or borrowed from another weapon.
Successful actions retain their existing numeric results.

## Evidence and limits

The focused installed-data run produces three explicitly partial PP2000 reload
records in one content job. Each retains five valid parts and records failures
for `jntWpn_12` and dependent `jntWpn_13` at 1.1 s. It yields no accepted linear
rail or sampled hand-contact candidate. This is useful incomplete data, not a
fixed PP2000 reload or a new native admission.

All231 relevant Python checks pass, including independent-part failure, child
dependency rejection, missing hand contacts and common-root failure. The saved
26-record reload batch and all five output files remain byte-identical to the
pre-refactor result. Both full normal builds pass all246 C++ suites (x86
24.19 s, x64 13.98 s); exact receipts are preserved with the local checkpoint.
The seven-minute379 whole-catalog survey is not repeated: its427 successful
records remain evidence of that version, while380 records this focused delta.

Private evidence lives in `bc2vr-recovery/orientation380`,
`partial380-pp2000` and `action380-reload-equivalence`. Native player375 remains
unchanged; its headset files remain pinned separately from these offline tools.

## Missing mesh investigation

The G3, Garand and Thompson meshes excluded by the earlier async-only inventory
are present in the MP common archive. This explains the old `not_in_inventory`
status but does not establish an active native mesh. The package MP2 common
archive contains deleted-entry markers for the G3 and Garand mesh resources;
their effective mounted interpretation remains unverified. No fallback to old
base content is silently enabled and no manifest or game file was modified.

Read-only index and manifest context is saved in
`bc2vr-recovery/missing-meshes380`. Resolving actual package dependencies is a
separate next step from decoded geometry and native ammunition ownership.

See [the shared action pipeline](AUTHORED-ACTION-PIPELINE-379.md) and
[actual gameplay coverage](../WEAPON-COVERAGE-CURRENT.md).
