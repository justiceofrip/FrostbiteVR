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

## Checkpoint415 player composition

Use the existing build directories and run the architectures sequentially:

```powershell
.\Build-Checkpoint.ps1 -Architecture x86 -Jobs 2 -OrdinaryManualCycles -NativeCycleRecovery -BodySceneDepth -PlayerGrenades -PlayerKnife -ContextualTools
.\Build-Checkpoint.ps1 -Architecture x64 -Jobs 2 -OrdinaryManualCycles -NativeCycleRecovery -BodySceneDepth -PlayerGrenades -PlayerKnife -ContextualTools
```

Checkpoint 415 retains the normal414 runtime composition. Player knife, grenade and
contextual-tool code is compiled, but those runtime opt-ins remain disabled in
the current personal launch profile. Synthetic controller drivers remain OFF.
Omitting these switches builds a different feature composition. The script selects
`profiles/checkpoint414` for the operation header and exact source receipt,
`profiles/aim339` for gun-hand calibration, `profiles/support373` for support
contacts, and `profiles/resource-enrollment295-pistol` for magazine registration
and geometry. Older profile numbers identify retained data, not the current build.

The build integration combines checkpoint414 with main's build/test optimizations.
Both architectures passed 284/284 suites and native source verification; all
four rebuilt runtime binaries match frozen414 byte-for-byte. Python/launcher
checks also passed. See the [415 handoff](handoff/checkpoint415/README.md) and its
[validation record](handoff/checkpoint415/validation.json). These are local
integration results; no remote CI pass for the combined merge is claimed.
The [414 handoff](handoff/checkpoint414/README.md) retains the earlier full build
results and actual-game placement evidence. The desktop launcher remains on
the exact frozen414 profile; no new game/headset run is needed for this merge.
Headset acceptance and whole-campaign stability remain pending. Tags
`checkpoint-414` and `checkpoint-413` retain their prior source/instructions,
and `checkpoint/410-headset-rollback` remains available.

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

No player release archive is published with this source snapshot. Publishing a
player build requires separate clean-package and headset acceptance.
