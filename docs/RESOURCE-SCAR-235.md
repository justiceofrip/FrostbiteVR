# Exact SCAR SP enrollment

Two installed single-player configurations now join the existing resource,
selected/carried identity, visibility and whole-weapon body pipelines:

| Configuration | Native registry id | ProvisionHarness4 variant |
| --- | --- | --- |
| `Objects/Weapons/Handheld/UL_rif_FNSCARL/SP_rif_FNSCARL` (`SCAR_sp`) | `c73120ea25cc6946` | 18 |
| `Objects/Weapons/Handheld/UL_rif_FNSCARL/SP_rif_FNSCARL_Scoped` (`SCAR_sp_s`) | `924645a13919d598` | 19 |

Both exact DBX descriptors use AutomaticFire2, rtMagazine1, capacity30 and zero
bolt/post-reload time. They reuse the reviewed executable-wide native resource
class and ordinary consumers. Each configuration retains its own path, GUID,
digest, timing words, meshes and rig checks. No native hook or per-gun policy was
added. Twelve previously reviewed MP registrations and AEK/XM8 data remain intact.
`profiles/resource-enrollment235-scar/enrollment.json` pins all ten compiled data
headers, including the unchanged visibility descriptors. Runtime native code,
owner, current configuration, source-bound operation review and actual receipts
remain mandatory; offline descriptor admission grants none of those receipts.

The shared authored-contact extractor found a stable0.2s wrist/magazine/finger
interval in the exact SCAR reload. Wrist translation is2.77mm, relative angle
0.00160rad, maximum finger angle0.000817rad and measured contact19.98mm. Existing
contact thresholds are unchanged. The rigid magazine has25.46mm thickness but
166.02mm axial extent. Its first withdrawal samples are36.13mm and66.55mm from
the closed pose. A thickness-limited probe could not admit two samples with the
required20mm travel. Failed measurements are retained in local evidence.

The generator preserves every successful prior witness. Only an insufficient
thin-part witness may retry within100mm and the measured projected rigid extent,
with the existing rotation, cadence, travel and direction checks. An existing
receiver-distance tie can use the independent withdrawal direction only among
the original endpoints within10mm of the nearest one. SCAR's Y+ insertion end
is26.50mm from the receiver versus112.08mm for Y-, with direction agreement
0.999987. A closer side corner at17.22mm explains the earlier tie. Unknown/far
axes, ambiguous opposite ends and inconsistent direction still reject. Native
trajectory and in-game contact acceptance are not inferred from authored samples.

The installed catalog contains42 records and16,101,705bytes, below the unchanged
16MiB limit. Its SHA256 is
`154d78ec7f9207ae8882139f56bfd7af5219efd4f166e6b3304e0b57e94a7e60`.
All38 old record bytes remain intact. Two detached magazines and two complete
body composites are appended; the scoped body includes its exact ACOG mesh.
An independent run of the ordinary installed-asset compiler reproduced the cache
byte for byte. Private vertices remain outside source. The old name-only SCAR
body fallback is superseded by its exact configuration row.

Eight C++ suites pass on x86 and x64:14-profile/168-cycle resource tests, exact
body/visibility joins and238 malformed bindings,84 invalid carried sources,
selected/carried readers, cache parsing and30 whole-weapon display rows. Actual
ordinary policies exercise hand release, support reacquisition, immediate full
magazine removal, delayed publication, interruptions, holster and draw. Native
Updates and GPU receipts in these deterministic tests are CPU fixtures. Fifty-nine
Python groups cover the new measured export,16 bad SCAR joins,20 preserved batch
join faults and existing geometry/contact/drum behavior.

Select `CombinedRegistry.h` and `CombinedGeometry.h` in this directory for the
checkpoint build, regenerate the source-bound operation receipt after integration,
and prepare the new local cache. Default unreviewed builds retain their admission
boundaries. Existing native hand/weapon pose fallback remains in use for SCAR;
this enrollment does not add an authored right-grip or native aim calibration.
An actual fresh-process held-start run with Harness4 variant18 or19 and the
ordinary mode7 consumer is the next validation step. No SCAR native test, headset
acceptance, global observer-completeness or all-weapons coverage is claimed here.
