# Building the developer snapshot

Use 64-bit Windows, PowerShell 7, Visual Studio C++ tools with x86/x64 support and
the Windows SDK, CMake 3.24+, Ninja, and Python 3.11+. CMake, Ninja and Python must
be on PATH. `Build.ps1` imports the installed Visual Studio compiler environment.

## Default source build

```powershell
.\Build.ps1 -Architecture x86 -Jobs 2
.\Build.ps1 -Architecture x64 -Jobs 2
python -m pip install -r requirements-dev.txt
python -B -m unittest discover -s tests -p 'test_*.py'
```

Builds and logs go to ignored `build/` and `reports/` directories. Ordinary CTest
does not launch BC2, select the installed XR runtime or run optional hardware GPU
probes. The x86 native module matches BC2's architecture; the x64 OpenXR host is a
separate process. A successful x64 build does not provide a native BF3/BF4 adapter.

Use the existing architecture build trees. If a running BC2 session maps a DLL
from a build tree, preserve the live module and arrange a fresh game process
before replacing it; a stopped mod can remain mapped. An explicitly bounded
alternative build directory is supported, but do not create a full build tree
for every checkpoint. Generated data and process-local scratch belong on G: in
this development workspace; see [storage policy](docs/STORAGE-BUDGET.md).

`requirements-dev.txt` pins NumPy and Pillow for offline geometry and image tests.
Use a virtual environment if you keep other projects' Python dependencies separate.

<a id="checkpoint413-player-composition"></a>
<a id="checkpoint414-player-composition"></a>
<a id="checkpoint426-player-composition"></a>
<a id="checkpoint436-r56-player-composition"></a>

## Checkpoint436 R56 player composition

Use the existing build directories and run the architectures sequentially:

```powershell
.\Build-Checkpoint.ps1 -Architecture x86 -Jobs 2 -OperationProfile checkpoint436-r56 -OrdinaryManualCycles -NativeCycleRecovery -BodySceneDepth -PlayerGrenades -PlayerKnife -ContextualTools
.\Build-Checkpoint.ps1 -Architecture x64 -Jobs 2 -OperationProfile checkpoint436-r56 -OrdinaryManualCycles -NativeCycleRecovery -BodySceneDepth -PlayerGrenades -PlayerKnife -ContextualTools
```

The script selects the exact source-bound operation header and receipt in
`profiles/checkpoint436-r56`, which is also the default operation profile.
The reviewed header and receipt are byte-for-byte exports of the qualified R56
normal build; their 576 source paths are repository-relative. Opening426 still
supplies the authored grip, support,
magazine geometry/registration and exact candidate Nambu muzzle enrollment,
while preserving the prior profile entries. Finite synthetic input drivers
remain OFF. The switches above match the normal player feature composition;
omitting them builds a different configuration. Runtime feature opt-ins are
selected by the portable launcher; compiling a capability is not gameplay or
headset acceptance.

The frozen normal build uses native source
`af13daab2acb73ac73273f1a7719515b7befdbb89d3537db2b25eeba2c6f59e8`
and passed 305/305 C++ suites on each architecture. R56's focused custody suites
and five diagnostic-reader regressions also passed. The later M95 native check
completed five manual bolt cycles through empty across all three firing copies;
missing-render-frame continuation and physical-headset acceptance remain open.
See [current state](docs/CURRENT.md) for the separate scoped native results and
preserved failures, and the [profile provenance](profiles/checkpoint436-r56/README.md)
for exact exported input hashes. A new build on another compiler may differ in
binary bytes; it must pass source verification and relevant tests.

Verify the exported source closure without compiling or touching build caches:

```powershell
python -B tools/bc2_native_operation_receipts.py verify-source --root . --reviewed-header profiles/checkpoint436-r56/Bc2NativeOperationCapabilities.generated.h --receipt profiles/checkpoint436-r56/source-with-header.json --expected af13daab2acb73ac73273f1a7719515b7befdbb89d3537db2b25eeba2c6f59e8
```

The Nambu muzzle descriptor remains Candidate. A passing build does not promote
it to NativeVerified, prove projectile impacts, or settle all restart failures.
Older checkpoint profiles retain historical evidence; their receipts are not
substitutes for the current source. Do not change a receipt hash to bypass the
source guard.

The script saves compiled capability output under
`reports/<architecture>-compiled-capabilities.json`. Its count describes exact
magazine configurations with compiled prerequisites, not completed guns or live
acceptance. See [current weapon coverage](docs/WEAPON-COVERAGE-CURRENT.md).
Profiles contain mod calibration and identity metadata; they do not supply game
meshes or animations. The `handoff/checkpoint245/pending` payloads are historical
snapshots, not patches to apply on top of390. Current implementations are in the
normal source directories.

The operation header is pinned to an exact source dependency closure. Its receipt
is byte-sensitive, so `.gitattributes` disables automatic line-ending conversion.
Changing covered native code makes verification fail. Review and validate the
changed adapter before updating its operation receipt; changing a hash alone is
not new native evidence. Default builds remain available for policy development.

Build caches retain options. To return to defaults, use a fresh checkout/build
directory or explicitly clear the optional CMake variables listed in the script.

CTest runs in parallel using `-Jobs`; run the architecture builds sequentially
to keep memory and storage bounded. Tests using the standard reload-flow flags
share `Bc2ReloadFlowRuntimeTestObjects`; tests with distinct definitions retain a
private copy. The static CRT is selected once before targets through
`CMAKE_MSVC_RUNTIME_LIBRARY`, not through scattered per-target overrides.
GitHub Actions builds the default composition on x86 and x64, and runs Python
and launcher/source-package checks on x64. Local checkpoint results and default
CI results describe different feature compositions.

## Running development code

You need your own compatible 32-bit BC2 installation and its Direct3D 11 path.
The locally tested executable SHA-256 is
`3911fcc8914b0158c434cee36f19295316e715d4c9a0e70c857e8c39e3d54258`.
Other builds require compatibility investigation; the game is not included.

```powershell
.\Setup-BC2VRPreview.ps1 -GamePath 'D:\Games\Battlefield Bad Company 2'
```

This creates ignored `config/local.json`. Native observers also accept a
`BC2_GAME_PATH` environment variable naming the game directory or executable.
The optional disassembly tool requires `pefile` and `capstone`; screenshots
require Pillow. Ordinary launch-helper preflight uses the standard library.

One Python test reads the installed game executable to check native signature
discovery. It is skipped by default and in CI. To opt in, configure the game path
and set `FVR_TEST_INSTALLED_BC2=1` for that test process. This test reads the file;
it does not launch or attach to the game.

The x64 host loads `runtime/openxr/win64/openxr_loader.dll` relative to its
executable. Source builds do not bundle that binary. Supply the matching Khronos
OpenXR loader under the source tree's same relative path before building; CMake
copies it beside the host. The historical preview packager pins loader 1.1.61
by SHA-256 (see its script and third-party notices). A test XR DLL is not a
replacement for an installed headset runtime.

Prepare body-display assets from your own installation when needed:

```powershell
.\tools\Prepare-BodyAmmoAssets.ps1 -GamePath 'D:\Games\Battlefield Bad Company 2' -Python python.exe
```

The generated `.fvrprop` cache stays local and must not be committed. Start with
the launchers' parameter help and the feature-specific research notes; the older
preview documentation is historical and is not a claim that every launch option
matches an accepted player build. Restart BC2 after changing the native DLL,
because a stopped mod can leave its disabled DLL mapped in the process.

## Automated development tests

`BC2NativeIpcProbe` supplies bounded synthetic tracking/controller fixtures to
the real adapter; the native trace records actual operations, accounting and
cleanup. A fixture's final ammo count or process exit alone is not a pass: normal
game reload can produce the same count after cancellation. See
[checkpoint 202](docs/SIMULATED-PLAYER-202.md) and the source tests for the evidence
required. These tools are development diagnostics, not an unattended campaign bot.

This source checkpoint does not replace the separately published
[BC2VR 0.1.15](https://github.com/justiceofrip/BC2VR/releases/tag/v0.1.15) player
release, which retains checkpoint434 native binaries. The R56 runtime remains
unreleased; publishing it requires separate package and native/headset validation.
