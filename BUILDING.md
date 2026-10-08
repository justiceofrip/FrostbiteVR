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

`requirements-dev.txt` pins NumPy and Pillow for offline geometry and image tests.
Use a virtual environment if you keep other projects' Python dependencies separate.

## Current checkpoint configuration (203)

```powershell
.\Build-Checkpoint.ps1 -Architecture x86 -Jobs 2
.\Build-Checkpoint.ps1 -Architecture x64 -Jobs 2
```

This builds the shared retirement correction described in
[checkpoint 203](docs/RELOAD-TRANSITIONS-203.md), using calibration headers retained
from 202. The omitted registry fixtures affect two test targets only.
`profiles/checkpoint202` contains mod-authored contact/rail values and identity
hashes, not game meshes or animations. Some header comments retain their original
"private" wording from local development. Experimental geometry is labeled as
such; it is not automatically measured or accepted for every weapon.

The current operation header and source receipt are in `profiles/checkpoint203`.
The older 202 source receipt remains historical; it intentionally will not verify
against changed source. To reproduce the initial publication, use commit
`a820f76b30a2451014c2bd1cb9a9460ab7d685d3` and its build script.

The operation header is pinned to an exact source dependency closure. Its receipt
is byte-sensitive, so `.gitattributes` disables automatic line-ending conversion.
Changing covered native code makes verification fail. Review and validate the
changed adapter before updating its operation receipt; changing a hash alone is
not new native evidence. Default builds remain available for policy development.

Build caches retain options. To return to defaults, use a fresh checkout/build
directory or explicitly clear the optional CMake variables listed in the script.

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
