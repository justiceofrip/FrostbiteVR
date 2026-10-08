# Exact shared resource, visibility and body enrollment — 233 candidate

This package joins twelve installed configurations: M416, XM8 Compact, MG36 and
XM8 LMG, each with three exact configuration resources. It adds no per-gun
consumer, ammo helper, constructor or native hook. Their immutable registrations
reuse the reviewed AutomaticFire2 / rtMagazine1 / zero-bolt family. Ordinary
resource input remains behind `BC2_RESOURCE_MAGAZINES`; an optional registry and
geometry header must also be explicitly selected.

`profiles/resource-enrollment233/enrollment.json` binds each descriptor digest,
native profile ID, exact configuration hash/GUID, magazine geometry, complete
weapon/attachment mesh set, visibility palette and configured body prop. It pins
all seven compiled data headers. `bc2_resource_enrollment.py` rejects incomplete,
duplicate or substituted joins and only emits separately reviewed exact keys.
There is no name, category or rig-only fallback.

Visibility now checks exact configuration paths in diagnostic matching too.
The three MG36 resources intentionally share one mesh set; matching only meshes
made their descriptors ambiguous. Two older name-only M416/XM8C descriptors are
replaced by the exact rows. All new rows retain `nativeAdmitted=false`; production
requires the existing source-bound native operation capability, exact current
configuration, complete meshes and normal custody/receipt checks.

Body rendering uses twelve complete closed-pose composites, including configured
optics, and four detached magazines. Carried configuration snapshots now preserve
the path already proven by the repeated native Config read. They remain a
separate type that cannot become selected hide/reload evidence. Both selected
and carried body matching require the complete exact configured mesh set. Pair
retention rejects a changed path or path pointer.

The private cache has 38 records and 15,152,465 bytes, below the existing 64-record
and 16-MiB limits. All 22 previous records are retained byte for byte. New magazine
records trim only unused vertex-buffer ranges; every indexed skin/position hash
remains identical. Catalog-driven preparation reads installed assets directly and
reproduces SHA256 `b6c196f50b3612a636ac1fddc67a71f0ffe13ae61ba088da78f017fbf9a8293b`.
No game vertices or cache are included in source. The existing
`Prepare-BodyAmmoAssets.ps1` command automatically selects this preparation when
the reviewed catalog contains configured composites.

Focused x86 and x64 validation covers 144 shared resource cycles, twelve actual
selected hide/draw/body consumers with 204 invalid bindings, twelve carried body
pair joins with 72 invalid sources, 56 visibility descriptors, repeated carried
path changes, selected metadata reads, malformed cache input, and all 29 body
display profiles against the actual 38-record installed cache. Native callbacks
and renderer receipts in these C++ tests are explicit CPU fixtures. Eleven Python
tests check exact joins, twenty malformed enrollments, lossless compaction,
geometry constraints and reproducible compiled catalogs.

The shared native evidence is actual232 XM8/SPAS: six independent magazine
authorities, eighteen confirming branch Updates, ordinary shot/shell reload and
body custody transitions. Its overall observer audit remains **inconclusive**:
six untimestamped recording-lock drops and 34 read misses at equipment transitions
prevent a blanket completeness claim. Successful bounded operation receipts do
not erase those gaps. These are not twelve individual native or headset tests.

For an integrated build, regenerate the native-operation source/header receipt
after all source changes. The standalone tests replay the reviewed232 operation
record only to exercise admission; that record does not attest the new source.
Set the registry header to
`profiles/resource-enrollment233/ReviewedResourceRegistry.h` and geometry to
`profiles/resource-batch227/zero-bolt-batch/CombinedGeometry.h`.

Remaining actual validation is provisioning/current selection of resident exact
resources, followed by the existing input-only sequence through ordinary
consumers. The enrollment JSON supplies the twelve exact resource identities for
the independently verified generic constructor selector. It grants no constructor
permission and performs no game operations. Closed body props use the existing
flat-color renderer; MG36's frame-zero RightHand display anchor is decoded spline
data, explicitly not a native grip calibration. Headset appearance, texture/material
fidelity and per-variant runtime behavior remain unverified.
