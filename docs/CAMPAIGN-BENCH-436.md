# Selectable campaign weapon bench

`tools/bc2_campaign_bench.py` produces an exact configuration inventory and a
small selected loadout from the saved test-range catalog. Planning never opens
BC2, mounts bundles, spawns items, or changes ammunition. The separate
`tools/Run-BC2CampaignBench.ps1` defaults to `Validate`; native operations require
`-Execute` and independently pinned process, build, resource and cleanup evidence.

This extends existing campaign maps and provisioning seams. It is not a custom
level or a room of world pickups. In particular, loading extra common bundles
remains disabled: the previous SP_03 mount experiments crashed. A catalog mount
candidate is still not proof that an asset is loaded in a running process.

## Plan and select

Use an existing Python runtime and G: output/scratch paths for this workstation.
For example, from the repository with a fresh existing output directory:

```powershell
& $Python -B tools/bc2_campaign_bench.py `
  --catalog reports/test-range-catalog-20261020.json --map sp_01 `
  --typed-report $CampaignTypedReport `
  --select objects/weapons/handheld/beach/nambu100/nambu100.dbx `
  --output G:/Unity/VRMMO/bc2vr-recovery/bench/plan.json `
  --selector-output G:/Unity/VRMMO/bc2vr-recovery/bench/selectors.json
```

Omit `--select` to inventory all catalog identities for that map. Repeat it for
at most two native inventory configurations. A full resource path must identify
one configuration; when content/GUID variants share that path, select its exact
catalog ID instead. Names and engine classes never grant mechanism support.
Missing typed firing fields prevent scanner selector generation. Ambiguous
package overlays retain their unresolved/deletion evidence instead of being
silently admitted. The emitted selector document is compatible with the existing
read-only selected-resource scanner; it does not run that scanner.

The saved catalog contains 288 exact identities, not 288 playable guns. Its
SP_01 partition has 13 mount candidates, 125 ambiguous identities and 150 outside
the map. Default source metadata joins 16 generated configurations and five
explicit built-in/calibrated identities. This describes configured support;
it does not claim all 21 pass in a current native binary. WW2 Nambu, Type 5
(native name `Garand`), Thompson and the scientist's pistol remain distinct.
Type 5/Thompson are not promoted by similarity to modern rifles or SMGs.

The planner also joins the configured body catalog and current authored support
documents named in `config/campaign-bench.json`. These joins use configuration
path, GUID and content hash; support additionally checks the configured mesh and
rig against supplied enrollment/body records. The Type 5 body/holster descriptor
is display-only and never supplies a magazine reload mechanism. Legacy props or
calibrations without an exact metadata join remain explicit gaps in this report;
that does not disable an existing runtime route.

Every row separates:

- `coverage`: any exact configured feature metadata, or none supplied;
- `feature_readiness`: separate reload, support, body-display and holster-anchor
  metadata; missing, incompatible and ambiguous bindings stay explicit;
- `manual_magazine_metadata_eligible`: an exact supported magazine descriptor
  family or the existing explicit known mechanism, never display-only geometry;
- `combined_metadata_complete`/`metadata_gaps`: whether those four metadata
  joins agree. This is not installed-cache, compiled-adapter, native, grip-fit
  or headset acceptance, and does not bypass execution preflight;
- `availability`: mounted candidate, ambiguous or outside this map;
- `native_tested`/`accepted_features`: only explicit independent native review
  for this exact resource/GUID/content and selected runtime receipt;
- `historical_native_evidence`: accepted evidence for a different runtime;
- `runtime_enabled`: always false here; this tool cannot enable an adapter.

The focused corpus regression checks all 288 saved identities, preserves the
16 generated reload configurations and five explicit mechanisms, and checks all
18 exact body records independently. It also removes/mismatches support metadata
to verify that the shared planner exposes the gap without inventing support or
revoking reload metadata. These checks do not replay per-weapon interaction tests.

Optional `--native-evidence` consumes a small independently reviewed JSON record
with schema `fvr.bc2.campaign_bench.acceptance.v1`, `accepted: true`,
`independent_native_review: true`, exact `configuration` fields `resource`,
`instance_guid`, `resource_sha256`, and scoped `accepted_features` such as
`magazine`, `shell`, `pump`, `bolt`, `support`, `holster` or `fire`. It also requires
`runtime`, `native_report`, `driver_completion` and `independent_audit` file pins
(`path`, `sha256`, optional `bytes`). It records the review; it does not replace
the native receipt audit. A completed provisioning job is never reload evidence.

## Existing native provisioning

Supply `--provision-build` and `--provision-header` to bind the existing tested
`fvr.bc2.nambu-provision426.build.v1` runner and its compiled
`Bc2ExactProvisionVariants.h`. The planner verifies both binary pins, every
prepared compilation input and the actual compiled variant table. It does not
guess selector numbers or add functions/offsets. A stale receipt fails closed;
the owner must prepare a matching build before executing it.

```powershell
pwsh -NoProfile -File tools/Run-BC2CampaignBench.ps1 `
  -Plan $Plan -PlanSha256 $PlanHash -DefinitionId $ExactId
```

`Validate` only checks saved files and prints readiness. It does not query the
game process. Subsequent explicit modes use the same plan and definition:

1. `Observe -Execute`: requires the pinned game receipt (`pid`,
   `creation_filetime`, `path`) and pinned read-only loaded-resource report.
   One exact loaded Asset/Data pair and the compiled variant must agree.
   The existing native preflight performs zero native inventory calls.
2. `Equip -Execute`: requires that successful Observe `job.json` and its hash,
   same process lifetime, at least eight native preflight observations and
   the retained provisioning DLL. The existing API gives/selects the item and
   requires client/server convergence.
3. `Restore -Execute`: requires the completed Equip job, same process and DLL.
   This restores the native inventory choice through the existing API; it does
   not claim original pointer restoration or reverse ammunition consumption.

Each native mode needs a new G: `-Output`, game receipt and hash, and appropriate
resource/prior-job receipt and hash. No retry, focus/input injection, automatic
game restart, profile restoration or process killing is added. A retained VR
module blocks a subsequent bench operation; the current wrapper therefore
requires a fresh game for each emulated episode and does not promise to Restore
after that episode. Provision-only Observe/Equip/Restore remains same-lifetime.

## Emulated controller episode

`Emulate -Execute` defaults to `-EmulationEpisode SingleMagazine`. It uses the
existing finite ordinary resource-input driver through the shared runner's
`-CampaignMagazine` mode. No second weapon or SPAS is required. SP_01 can exercise
its mounted Nambu directly; SP_02 can exercise its mounted configured rifles.
The same episode draws the selected item if needed, establishes support, fires
one preparation shot only when full, removes/returns the partial magazine,
removes it again, replaces it from the chest, reacquires support and fires once.
It does not prove a second-shoulder round trip or world pickup.

The exact selected weapon must have configured magazine metadata, a compatible
diagnostic receipt/hash, explicit Python, geometry cache/hash and process receipt.
Read-only preflight requires two unchanged observations of all three native
copies: on foot, idle, finite ammo, capacity at least 2, loaded 1..capacity and
reserve at least capacity. Asset identity is explicit, never inferred from class.
If the plan pins a runtime, that receipt must name the same diagnostic build.
`CombinedPump` retains the old full SPAS companion checks. Other mechanisms are
not substituted. The normal player DLL does not enable these finite fixtures.

The campaign diagnostic receipt requires `campaign_magazine=true`,
`native_cycle_recovery=true`, its exact `source_root`/`source_files` and binary
pins, plus `cmake_cache:{path,sha256}` for the copied compiled CMake cache.
Required flags are ordinary manual cycles, native cycle recovery, resource
magazines, ordinary resource input, magazine-only and magazine-postfire ON;
magazine-empty and support-only OFF. Other finite `BC2_*PROBE` flags must be OFF.
These checks cannot be satisfied by relabeling a normal DLL.

No raw capture is authorized by default. The existing native collector still
writes up to eight frames, so execution requires explicit `-AllowNativeImages`
and `-CaptureWidth`/`-CaptureHeight` (defaults 1920x1080), verified against the
actual game client before launch/injection. Client dimensions describe the test
conditions; storage reserves the native collector's full eight RGBA frames of
up to4096x4096 each (512MiB), plus bounded report space. Receiver files are always disabled. The default
`Validate` mode reads files only. The body cache cap remains 16 MiB/64 records.
The plan lists selected mesh/body-part requests, but does not generate or expand
a cache. There is no blanket all-guns bundle or all-guns cache.

An emulation completion is explicitly **not** native reload acceptance. Inspect
the native request/result/consumer receipts before preparing the independent
acceptance record. Reuse the existing shared 12-case resource batch tests once
per source build; do not replay those 12 simulated cases for every exact gun.
Native episodes should target previously unproven configurations or changed
mechanisms/ownership boundaries, and retain failures without automatic repeats.

## Verification and remaining work

The focused Python regressions cover exact identity, overlay ambiguity, missing
typed fields, conflicting enrollment, runtime-scoped acceptance, bounded assets,
existing-output preservation and non-G: rejection. Planning against the retained
SP_01 catalog/typed evidence generated Nambu selectors without game access.
Additional regressions cover native hexadecimal/null profile serialization,
full/partial single-magazine admission, all-three mismatch and owner changes,
insufficient reserves, stale support, missing native operations and incomplete
cleanup. PowerShell syntax is checked separately. No new native operation, world pickup,
headset behavior or all-campaign coverage is claimed by these checks.

Further work remains on per-map asset subsets, a native physical world-pickup
backend, dedicated pump/bolt/launcher/belt-fed episodes, and independent native
acceptance for each admitted configuration. Those gaps remain visible in plans
instead of multiplying per-gun implementation guesses.
