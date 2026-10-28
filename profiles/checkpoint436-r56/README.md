# Checkpoint436 R56 reviewed operation profile

This profile exports the qualified normal R56 build's reviewed operation header
and exact source receipt without changing either file. It enables reproducible
source verification from a clean checkout; it does not add native operation
records or qualify additional gameplay or headset behavior.

The original files are retained under the local evidence prefix
`bc2vr-recovery/performance436/root-custody-normal-r56-prep/build01/provenance/`.
They contain no game asset payloads or machine-specific dependency paths.

| Item | SHA-256 |
| --- | --- |
| Exact native source closure | `af13daab2acb73ac73273f1a7719515b7befdbb89d3537db2b25eeba2c6f59e8` |
| `Bc2NativeOperationCapabilities.generated.h` | `0c83dd120b2de468ba9cc2d5114bd2ddd94ae738907820159d575f8f6b21d11d` |
| `source-with-header.json` | `62fd58d126cfcc06888202a4abcc8fac2b0242373a5a7ed87ab0d7a41bbfbb93` |
| Original normal runtime build receipt | `1bbce04a887c1b9c022d0d4aeb6f58e5df0dd1faa28b4c23fb79db8f2e7db002` |

The receipt covers 576 source files and separately hashes four generated metadata
headers already present in `src/games/bc2`. These headers describe mod calibration
and identity metadata; generated body geometry and game files remain excluded.
All receipt source paths are repository-relative. CMake resolves this profile's
header to the checkout's absolute path when generating its local build config.

Use the full normal switch recipe in [BUILDING.md](../../BUILDING.md). The profile
alone does not select normal feature flags. Its original build passed 305/305
C++ suites on both x86 and x64. Source edits must pass the exact provenance guard;
do not change hashes merely to accept different native code. Preserve historical
profiles as evidence for their original source versions.
