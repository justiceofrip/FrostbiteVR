# Building and staging the single-player preview

The source archive is an explicit allowlist of C++ source, headers, tests, build files, launch tools, vendored headers/source and license notices. It excludes developer reports, captures, machine-local configuration, caches, compiled outputs and game content. It does not contain the OpenXR loader binary. The binary preview contains only launch requirements, public docs/licenses and exact manifests.

Required build tools: Visual Studio C++ x86/x64 tools and Windows SDK, CMake 3.24 or newer, Ninja, PowerShell, and Python 3.9+. Use `Build.ps1 -Architecture x86` and `Build.ps1 -Architecture x64` to compile and run CTest. These deterministic suites do not start BC2 or SteamVR. Hardware/native/headset checks are separate.

To build an isolated preview from the current source tree:

```powershell
.\tools\Build-SingleplayerPreview.ps1 -WorkRoot 'D:\Builds\bc2vr-preview-001' -Version '0.1.0-preview.1' -LoaderPath 'D:\Dependencies\openxr_loader.dll'
```

Use a new, nonexistent WorkRoot. The script takes an immutable source snapshot, copies the verified OpenXR loader into that snapshot, builds/tests both architectures there, and records source and binary hashes before assembling archives. It never starts a game or headset runtime, modifies the original source tree, publishes a release, or pushes to a remote. The loader must match the pinned supplied version; obtain it from an existing verified dependency or the project maintainer. The script does not download unverified dependencies. Do not substitute a fake XR test runtime.

The output includes `source-manifest.json`, `binary-manifest.json`, a public feature matrix, source and binary ZIPs, and their SHA-256 digests. ZIP paths, ordering, timestamps and metadata are fixed, so identical inputs produce identical archives. This does **not** claim MSVC compilation is byte-for-byte reproducible across machines or toolchains. Build logs remain outside the distribution; a sanitized attestation records only test counts and hashes. The staging tool detects a changing source tree and refuses to silently mix it with another build.

`tools/package_preview.py --root <tree> --out <new-directory> --version <tag> --source-only` stages the source without building. Without `--source-only`, an unstamped binary snapshot can be assembled for local review. It is explicitly marked `build_verified: false`; use `--require-build-verified` for distribution candidates so stale or mismatched output is rejected. Build-SingleplayerPreview uses this required mode.

The source and build attestation prove what was compiled and which offline suites passed. They do not grant release approval or prove a campaign playthrough. Before publication, test the **exact packaged payload** on a clean extraction, verify launch/stop/reconnect and supported campaign transitions, then record headset acceptance. Keep unaccepted flags off. The themed installer, VR-first title environment, VR settings, full-body IK, and multiplayer remain separate work.

## Optional registry-wide empty-reload regression

The two private-fixture coverage targets are opt-in through the CMake cache
variable `BC2_MANUAL_EMPTY_COVERAGE_FIXTURE_DIR`. Point it at a local directory
containing the separately reviewed FixtureRegistry.h and DisabledRegistry.h.
The headers contain private configuration fixtures and are not distributed.
Without this option, ordinary public builds omit those two targets. The fixture
is test-only: it does not set `BC2_REVIEWED_MAGAZINE_REGISTRY_HEADER` or enable
production weapons. This distinguishes mechanism regression coverage from
native/headset admission of a new gun configuration.

`tools/report_empty_step.py` analyzes captured Step journals, and
`tools/audit_empty_fire.py` audits the dedicated input-only monitor fixture.
The audit requires native zero-ammunition evidence and cleanup; a normal headset
trace without that fixture is inconclusive, not a monitor pass.

## Offline development tools

`tools/report_empty_step.py <native-trace.json> --output <new-report.json>`
summarizes a drained schema 1 empty-Step journal. Missing, stale or overwritten
evidence is explicit; the tool does not infer a fix from aggregate counters.

`tools/audit_pump_capture.py` joins the bounded native pump capture with the
reviewed hold audit. Its `--help` lists the required trace/receiver inputs.
Passing synthetic tests or a capture audit does not enable manual pumping.

`tools/bc2_magazine_pipeline_coverage.py` compares exact descriptors with supplied
measured contact batches and reconciles standalone consumer probes. Generated
fixture headers are private, mocked-native test inputs and must never be supplied
to the native game build as reviewed registry headers.

The ordinary `Bc2BodyDisplayBatch` CTest needs no game assets. To additionally
check a locally prepared private cache, run the built
`Bc2BodyDisplayBatchTests.exe <path-to-body-ammo-assets.fvrprop>`. The cache stays
outside source and runtime archives. Regenerate it from the user's installation
with `tools/Prepare-BodyAmmoAssets.ps1` when using a changed catalog.
