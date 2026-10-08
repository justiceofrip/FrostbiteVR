# Current weapon coverage — October 8, 2026

**The current checkpoint integrates two detachable-magazine profiles: scoped
XM8 and AEK971_sp. It does not enable manual magazine reloads for the whole gun
roster.** SPAS shell loading and the scoped-XM8 launcher sight/mode are separate
integrated paths. Wider asset extraction, generated descriptors and offline
consumer tests are preserved, but most are not enrolled in the current runtime.

The follow-up extraction now prepares **21 exact configurations across seven
additional weapon models**. All 21 pass the shared consumer's partial/empty
reload and identity-denial checks on x86 and x64 with mocked native responses.
Their production registrations remain disabled. See the completed variant batch
below; it resolves the 14 geometry gaps found in the initial audit.

The often quoted **35 configurations, 20 two-hand grip records and 10 magazine
contact records are prepared data counts**, including SP/optics variants. They
are neither distinct gun counts nor completed manual-reload coverage. The
35-configuration visibility/holster path is also independent of magazine
registration. Sharing its native operation proof does not admit ammunition,
chambering, pump, bolt or belt-feed operations.

This is a source/data audit of the local 206 composition, followed by a separate
offline coverage-tool correction. It performs no builds, process inspection or
game actions. Final build/native acceptance
belongs to [checkpoint 206](RELOAD-RECOVERY-206.md). Its shared hand-geometry fix
repairs existing interactions; it adds no weapon profiles. No all-gun percentage
is meaningful until exact configurations and independent capabilities are joined.

## What the current checkpoint selects

[Build-Checkpoint.ps1](../Build-Checkpoint.ps1) selects the calibration headers
from `profiles/checkpoint202` and the operation receipt from `profiles/checkpoint206`.
The inspected `build/x86-checkpoint206/CMakeCache.txt` and
`build/x64/CMakeCache.txt` agree with these inputs. Cache inspection establishes
configuration. The subsequent serial 206d builds completed successfully with
186 tests passing on each architecture; their frozen evidence is under
`<local-recovery>/recovery206d-build-logs`, with receipt
`pipeline-runtime-20261005/normal-recovery206d-build-receipt.json`. Successful builds do not establish the
identity of a loaded DLL or a fresh native/headset acceptance result.

| Current source/configuration | Count and meaning |
| --- | --- |
| [Native magazine registry](../src/games/bc2/Bc2MagazineNativeRegistry.cpp) | Two enabled builtins: exact scoped XM8 and AEK971_sp descriptors. `BC2_REVIEWED_MAGAZINE_REGISTRY_HEADER` is empty in the checkpoint script and both inspected caches. No wider generated magazine registry is selected. |
| [Magazine geometry lookup](../src/games/bc2/Bc2MagazineGeometryProfile.cpp) | Builtin XM8 calibration plus one selected experimental AEK profile. Native registration, geometry and live owner/configuration evidence remain separate requirements. |
| [Authored grip header](../profiles/checkpoint202/Bc2AuthoredGrips.generated.h) | Two rows: AEK971_sp and XM8_sp_s. Accepted-baseline replacement is OFF, so a row's presence does not mean it overrides the accepted XM8 handling. |
| [Authored support header](../profiles/checkpoint202/Bc2AuthoredSupports.generated.h) | One SPAS12_sp support-contact row. Support-only data is not a new full weapon grip or pump implementation. |
| [Visibility descriptors](../src/games/bc2/Bc2VisibilityDescriptorData.h) | 46 geometry rows, of which 35 have exact configuration paths; 11 lack those backlinks. The selected reviewed operation class can qualify compatible exact rows under live binding checks. Legacy XM8/SPAS handling has its own accepted route. This is not magazine coverage. |
| [Body equipment profiles](../src/games/bc2/Bc2BodyEquipmentProfiles.h) | 19 render-only rows, including aliases. Body-model/cache presence does not establish reload or native hide/show authority. |
| [Sight adapters](../src/games/bc2/Bc2SightAdapter.h) | XM8 sight has `nativeSightAccepted=true`; measured AEK/GP30 subtree profile remains false. The corresponding AEK/GP30 mode family also remains unaccepted. |

The three entries in [Bc2WeaponProfiles.cpp](../src/games/bc2/Bc2WeaponProfiles.cpp)
describe evidence for aim/support/muzzle handling of SPAS, scoped XM8 and its
launcher. That table is not the complete runtime capability catalog: authored
grips, magazine profiles, visibility, body rendering and sights have separate
registries.

The freshly built x86 206d capability output,
`test-temp/compiled-capabilities206.json`, directly confirms **two enabled,
resolved, magazine-ready registrations**, `chamber_known=false`, and 46 visibility
rows with **two `production_stow_admitted=true` and 44 false**. The two true rows
are scoped XM8 and SPAS. This probe leaves its synthetic snapshot's
`operationBinding` empty, so it exercises their legacy admission and does not
establish the configured-operation-class route for the other exact descriptors.
Report that route as untested by this output; do not convert either the old 35
qualification count or the current 44 false results into an all-weapon claim.
The generated `test-temp/weapon-coverage206.json` joins the supplied 35-package
index to this compiled report and finds two compiled magazine prerequisite rows.
Both reports are also preserved in `recovery206d-build-logs`. The report's
`production_stow_profile_not_admitted` wording inherits the missing-binding
limitation above; it is not evidence that the live operation-class route fails.

## Prepared exact-configuration data

`<local-recovery>` below means the local sibling directory
`G:\Unity\VRMMO\bc2vr-recovery`. These private artifacts are evidence, not
distributed game assets or instructions to select an old build.

Direct inspection confirmed:

- `pipeline-audit-20261005/assets/integrated-package-index.json`: **35 rows and
  11 exclusions**, with zero rows marked runtime-enabled. SHA-256:
  `2d997615d5c48422d0284eb76c79796e6d9b6e0cabee372ad731e5d8216b7b9c`.
- `pipeline-runtime-20261005/component-closure-verified.json`: the same package
  set, with **20 two-hand grip and 10 detachable-magazine contact evidence
  records**. All remain metadata prerequisites without native admission.
  SHA-256: `55fff72b8c007effef6fdc6a75ade995c2839d9bf84888e6be6c5c9c57f6b4fc`.

The closure records their exact inputs: the package index, configured visibility
evidence, `runtime-authored-grips/bindings.json`,
`runtime-authored-magazines/geometry.json`, and the SPAS support-only metadata.
The base index alone contains visibility; its name does not mean the later
component records or a runtime consumer were integrated into each row.

| Base asset grouping in this package set | Exact configurations | Joined two-hand grips | Joined magazine contacts |
| --- | ---: | ---: | ---: |
| AEK971 | 5 | 5 | 5 |
| XM8 | 5 | 5 | 5 |
| F2000 | 5 | 5 | 0 |
| SCAR | 5 | 5 | 0 |
| AKS74u | 5 | 0 | 0 |
| AN94 | 5 | 0 | 0 |
| UZI | 3 | 0 | 0 |
| MP443 | 2 | 0 | 0 |
| **Total** | **35** | **20** | **10** |

These are eight asset groupings, not eight fully supported guns. Zero means
absent from this particular exact package closure; separate extraction batches
may already contain useful data. For example, the wider magazine batch below
contains AKS74u/UZI contacts outside this closure. Do not add counts from the two
overlapping sets. A magazine contact also does not supply the visible ammo cache,
native transfer proof or chamber state.

## Where the wider magazine work is, and why it is still disabled

The reusable registry and consumer code is present in the main source. Missing
runtime enrollment is more specific than an unimplemented registry:

1. `<local-recovery>/magazine-registry-followup-120602-candidate/private/InstalledDisabledMagazineRegistry.h`
   contains **63 generated rows, zero enabled**. Its adjacent
   `disabled-registry-receipt.json` records no review input and no native test
   claim. Header SHA-256:
   `1e683e6ce829299d95808e8ddf3d4fdb9908275fbf1511fb5340b746d4889cfa`.
2. `<local-recovery>/automatic-magazine-stock-bolt-20261003-candidate/private/`
   contains `DisabledRegistry.h`, `FixtureRegistry.h` and `MeasuredGeometry.h`.
   Its parent directory's `coverage.json` records **21 descriptor paths across seven asset names**,
   tested on both architectures with real shared consumers but mocked native
   receipts; separate disabled-registry tests reject all 21. New runtime
   admissions are zero. Coverage SHA-256:
   `f55ae31fe1e1116d5ad2e2fefb58da5824f9cb97469b4321bbf5240435ca5137`.
3. `FixtureRegistry.h` deliberately fabricates admission for standalone tests.
   It is explicitly marked **never enable in game**. It is not a reviewed native
   registry and cannot be used to promote that batch.
4. The saved `MeasuredGeometry.h` predates the current exact-path join: it has
   no `configurationPath` entries, and its seven underlying saved JSON profiles
   also lack the `weapon` configuration backlink. Current `MagazineGeometryMatchesConfiguration`
   permits pathless compatibility only for the two immutable XM8/AEK builtins.
   These wider geometry rows therefore need regeneration and exact configuration
   component joins before reuse with today's consumer. Merely selecting the old
   headers cannot complete the integration.
5. The current exporter accepts a reviewed enablement document only for
   `bc2.automatic-magazine-zero-bolt.v1`. It explicitly refuses enabling a
   stock-bolt candidate using that review. Missing live configuration, native
   owner/cycle, render and empty-control evidence remains separate from the
   configuration-path incompatibility.

The seven-name batch divides into these reusable groups:

| Prepared definitions | Exact paths in the batch | Reuse and remaining boundary |
| --- | ---: | --- |
| M416, XM8 Compact | 6 | Automatic magazine, zero-bolt descriptor shape matches the existing reviewed class in principle. Regenerate exact component joins, review the selected native configuration/three-copy lifecycle, then enroll data in the shared consumer. |
| MG36, XM8 LMG | 6 | Prepared detachable-drum contacts and the same zero-bolt dispatch shape. They are magazine-feed candidates; their inclusion does not implement belt-fed LMGs. |
| 9A91, AKS74u, UZI | 9 | Automatic magazine route with nonzero authored bolt fields. Static proof shows ordinary AutomaticFire2 bypasses stock bolt states, while preserving those fields. The broader admission contract and native evidence still need review; no manual charging-handle implementation follows from this proof. |

The wider descriptor audit also marks exact AEK/XM8, F2000, SCAR and other
automatic-magazine definitions as dispatch-compatible where their values match.
That is eligibility for shared review, not live enrollment or a guarantee that
their complete configuration components are joined. Its AN94 and M16/M16k
definitions instead report unreviewed `fltSingleFire` dispatch. Those require a
shared native dispatch-family review; they must not be forced into the automatic
class because their category or visual magazine resembles it. Handguns and
several self-loading rifle definitions have the same unresolved distinction.

See [the stock-bolt evidence and limits](AUTOMATIC-STOCK-BOLT-DISPATCH.md) and
[the exporter](../tools/bc2_magazine_registry_header.py). The review unit is the
native mechanism/configuration family. Reusing compatible authored components
and testing representative families avoids a separate controller routine or a
fresh manual calibration campaign for every gun name.

## Completed offline tooling correction and exact outputs

The current [authored geometry exporter](../tools/bc2_authored_magazine_geometry.py)
already writes `g.configurationPath` from validated `weapon` metadata, checks
profile digests, and rejects duplicate asset/configuration pairs. It does not
need a new per-weapon exporter. The corrected
[bc2_magazine_pipeline_coverage.py](../tools/bc2_magazine_pipeline_coverage.py) now:

1. Joins descriptors to geometry through exact configuration resource/hash/GUID
   and the original `grip_binding_digest` backlink, with selected mesh/LOD and
   rig, skeleton and static-pose evidence checked. It reuses the existing
   component-closure identity/hash helpers; full package resource closure remains
   a separate prerequisite.
2. Recovers a legacy profile's missing `weapon` only from its verified original
   backlink. Preserves the old profile digest as source provenance and computes
   a new derived digest; never stamp three variant paths onto one name match.
3. Reports missing or ambiguous exact joins as unavailable. It accepts original
   source metadata with `--bindings`; legacy `--header` input is provenance only
   and its C++ content is ignored.
4. Passes the validated metadata to the existing `cpp_header` emitter and keeps the
   generated registry disabled. Reports descriptor-eligible paths, exact geometry
   joins, and zero new native admissions separately. Selected-build enrollment
   and native acceptance are explicitly not observed by this offline tool.

The first target is the **12 zero-bolt paths** for M416, XM8 Compact, MG36 and
XM8 LMG. The historical name-joined fixture does not establish that all 12 can be
regenerated from the four saved geometry rows alone. This is a data/tooling task,
not authorization to enable native support.

The corrected tool was run against the saved seven-profile batch and its original
source bindings, writing only a new private directory:
`<local-recovery>/weapon-pipeline206-exact`. Its `coverage.json`, disabled registry,
private mock fixture and regenerated `MeasuredGeometry.h` contain **seven exact
joins, zero enabled production rows**. The immutable input files were preserved.

Root also compiled the generated geometry and disabled registry against the
completed x86 and x64 core libraries. Both standalone checks validate all seven
one-to-one configuration matches, insertion configurations and rigid hand/part
poses, with native registrations still disabled. This is data/compiled-policy
validation; it does not execute the native reload adapter or accept hand feel.

| Result against historical 21-path fixture | Exact joins recovered | Other variants still missing exact geometry |
| --- | ---: | ---: |
| Zero-bolt: M416, XM8 Compact, MG36, XM8 LMG | 4 | 8 |
| Nonzero stock bolt: 9A91, AKS74u, UZI | 3 | 6 |
| **Total** | **7** | **14** |

The seven joined paths are the base paths for M416, XM8C, MG36, XM8 LMG, AKS74u
and UZI, plus the 9A91 Kobra path. The original binding files are
`arsmg-paired-contact-batch-candidate/all/grip-bindings.json` (66 records) and
`lmg-authored-mechanisms-candidate/all/reference-bindings.json` (30 records).
Having more source bindings does not authorize cloning a measured magazine role
onto every configuration using the same asset name. The subsequent extraction
below resolves the 14 variants through their own assets and configuration
bindings. Independent native family proof remains separate.

## Completed exact variant extraction and shared-consumer batch

`<local-recovery>/weapon-pipeline206-variant-extraction` contains fresh extraction
of all 14 previously unresolved variants using the existing paired-grasp
extractor and each variant's original configuration, mesh and animation data.
The serial extraction took about 97 seconds. No hand positioning or controller
state machine was written separately for these weapon names.

The combined output now contains 21 unique exact paths: **12 zero-bolt and nine
stock-bolt candidates**, with zero requested extraction gaps and zero enabled
production registrations. The seven models are M416, XM8 Compact, MG36, XM8 LMG,
9A91, AKS74u and UZI, each with three configurations. The manifest records source
and output hashes and exact reproduction inputs. Independent validation checks
35 file hashes, 14 new profile digests/original bindings and the emitted path set.

Root compiled the existing standalone magazine-consumer batch against the final
21 headers and the current 206d libraries, sequentially on x86 and x64. Both
architectures pass all 21 disabled-registry checks and all 21 mock-native consumer
rows. Each consumer row runs partial and empty magazine reloads, checks transferred
counts, and rejects mismatched path/capacity/bolt timing. `consumer-audit.json`
joins the exact output paths and retains source/library hashes.

These checks execute the shared C++ consumer and measured geometry; native calls
are mocked. They do not test game hooks, live configured meshes, GPU rendering,
headset feel, full package closure or production admission. The current runtime
still enables only scoped XM8 and AEK971_sp magazine profiles. The remaining
integration unit is the native mechanism family and its package composition,
not a new per-weapon interaction implementation.

The new `manifest.json` lists all five data-input hashes, tool/output hashes,
the seven exact and 14 unresolved paths, and reproduction arguments. SHA-256:
`daa24de33004267e3593fc63b9faefff9da550c7b8bf7f454f15d0bdcdb1e9d4`.
These are content hashes, not signatures. The baseline regression failed with
two name-joined fixtures where zero were justified. After correction, 89 focused
Python tests passed, including corrupt/missing bindings, configuration/mesh/LOD/
rig/pose changes, ambiguity, exact variant separation, preserved source metadata,
disabled output and CLI input-overwrite rejection. No generated header was built
or selected by a native checkpoint. `focused-python-tests.log` in the new
artifact records the completed test-output transcript.

A complementary report change should distinguish legacy stow acceptance from
configured-operation-class acceptance and label absent synthetic operation
binding as untested. This prevents a report from implying either 35 complete
guns or 44 definitively unsupported configurations.

## Mechanism readiness and the next integration step

| Mechanism/category | Integrated or prepared now | What brings the next family online |
| --- | --- | --- |
| Detachable rifles/SMGs | One persistent removal/original-return/replacement consumer; XM8 and AEK profiles selected. Wider batches above are disabled. | Finish the shared recovery regression, regenerate exact package/header joins, supply an independently reviewed native family admission, and exercise partial/empty reload plus equip/pickup/holster transitions through the same consumer. |
| Tube/shell loading | SPAS has actual shell-supply, insertion, native hold/transfer and presentation consumers. | Bind other exact shell-feed configurations to that shared path where semantics match. Prove their native entry/transfer/cleanup; a shotgun label does not select tube loading or pumping. |
| Pump action | Portable `WeaponCycle` pump recognition and a bounded SPAS `PumpPart` presentation planner exist. They are not an ordinary gameplay pump consumer. | Wire the shared after-shot/after-feed action requirement, exact mechanism claim/part pose, fire-readiness gate, native completion and cancellation. Use actual chamber knowledge rather than loaded-count guesses. |
| Bolt-action rifles | Ordered unlock/back/forward/lock policy exists offline. | Supply the native action/chamber/readiness adapter and wire it to the weapon's independently selected feed. Magnified optics remain a separate capability. |
| Magazine-fed handguns/self-loading rifles | Magazine infrastructure is reusable; MP443 has two configuration/visibility rows in the package set. That does not grant handgun reload. | Review the correct native fire/reload dispatch family and bind existing/extracted components. Requested pistol slide/rack/release and chamber behavior need their own shared recognizer and conserved-ammunition/native-ready contract. |
| Revolver/other sidearms | No integrated cylinder/round mechanism established by this audit. | Bind their actual feed/action model; do not treat every sidearm as a detachable-magazine pistol. |
| Belt-fed LMGs | Authored parts/resource work and portable `FeedMechanism` ordering/contact queries exist. | Add the production cover/latch/feed/container interaction coordinator, resource accounting, native hold/commit/cancel and readiness acknowledgements. A query result or `rtMagazine` transfer category does not prove a detachable box-magazine mechanism. |
| Scoped-XM8 underbarrel | Sight manipulation/mode changes are integrated; ammunition remains the native attachment's own pool. | Preserve this baseline. Manual underbarrel reload remains deferred; stock reload is the current scope. |
| AEK/GP30 sight | Measured horizontal hinge, generic subtree/finger presentation helpers and exact family data are present; native acceptance flags are false. | Complete verified family dispatch/current-owner publication, paired subtree/hand presentation and release/equip/reload transition checks. No new per-gun sight state machine is needed. |
| Standalone launchers | Shared supply/insertion/request primitives and authored research are reusable; no admitted general manual launcher consumer is established. | Bind the actual one-round/breech mechanism, independent pool and native completion. This is distinct from rifle magazine or SPAS shell loading. |

`WeaponMechanism`, `WeaponCycle` and `FeedMechanism` being compiled/tested types
does not mean BC2 gameplay instantiates the full mechanism workflow. The current
native loaded count does not establish a separate chamber count. A tactical
reload retaining a chambered round and an empty reload must eventually select
different action requirements without inventing rounds or capacity-plus-one.

## What the acceptance evidence actually covers

- [202](SIMULATED-PLAYER-202.md) and [204](COMBINED-INVENTORY-RELOAD-204.md) establish
  bounded actual-game XM8 original return/replacement and the combined XM8/SPAS
  shoulder/chest sequence. The latter passed its strict audit with conserved
  ammunition and no consumer cancellation or magazine palette fallback.
- [203](RELOAD-TRANSITIONS-203.md) and [205](RELOAD-RECOVERY-205.md) add persistent
  transition and interruption/recovery coverage with mocked native/renderer
  boundaries. A synthetic second profile is not a newly accepted weapon.
- [200](SIMULATED-PLAYER-200.md) records two actual SPAS shell insertions with
  conserved ammo. That run's overall audit still failed the separate ordinary
  palette-fallback coverage requirement. Shell insertion is not manual pumping.
- [206](RELOAD-RECOVERY-206.md) records the delayed-neutral-geometry reproduction
  and live recovery work. The instrumented pre-fix run completed the interaction
  chain but had two display-transfer timeouts; it is not a strict combined pass.
  The subsequent serial 206d x86/x64 builds each passed all 186 tests. This
  completes offline build validation; fresh corrected-build native recovery and
  human headset acceptance remain separate from that result.
- The latest recorded human headset feedback remains failed rifle reloads and
  unrequested draw in 193. Later native/CPU fixes do not replace a new headset
  acceptance check, nor establish every gun, pickup, vehicle or empty-ammo case.

The historical 183 status in [WEAPON-SYSTEM-PIPELINE.md](WEAPON-SYSTEM-PIPELINE.md)
and the old active-agent queue in [WEAPON-MECHANISM-NEXT.md](WEAPON-MECHANISM-NEXT.md)
are not current operating status. The inspected private `ACTIVE-WORK.json` was
a 205 snapshot; its old paths, next steps and process/agent fields must not be
used to infer what is running now. This audit reports no active process or worker.

## Reproduce the configured-binary report

The root ran the report successfully using the 206d build and the exact index
below. `Build-Checkpoint.ps1` now also emits the raw compiled-capability report
automatically. These manual probe/report commands are provided for reproduction
and were not executed by this audit. Use a fully completed matching build; the
probe opens no game process.

```powershell
$sourceRoot = 'G:\Unity\VRMMO\FrostbiteVR'
$coverageBuild = Join-Path $sourceRoot 'build/x86-checkpoint206'
$packageIndex = 'G:\Unity\VRMMO\bc2vr-recovery\pipeline-audit-20261005\assets\integrated-package-index.json'
& (Join-Path $coverageBuild 'BC2WeaponCapabilityProbe.exe') |
    Set-Content -LiteralPath (Join-Path $coverageBuild 'compiled-capabilities-current.json') -Encoding utf8
py -3.12 -B (Join-Path $sourceRoot 'tools/bc2_weapon_capability_report.py') `
    --package-index $packageIndex `
    --registry-receipt (Join-Path $coverageBuild 'compiled-capabilities-current.json') `
    --cmake-cache (Join-Path $coverageBuild 'CMakeCache.txt') `
    --out (Join-Path $coverageBuild 'weapon-capabilities-current.json')
```

The standalone probe reports its actual linked magazine registry and synthetic
visibility/holster policy inputs. Its cache metadata records selected header
paths/hashes; it does not prove all NativeProbe-only grip/support headers were
consumed by that standalone executable, or that a deployed DLL matches it.
Refresh the package index and its separate component closure when adding newly
joined components. Native transaction and headset acceptance remain separate
evidence fields throughout.
