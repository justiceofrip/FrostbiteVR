# FrostbiteVR: Battlefield 4 adapter (bootstrap)

Campaign-only VR for Battlefield 4 (Steam/EA app, x64, DirectX 11), built as a second
adapter on the FrostbiteVR framework. This tree overlays the FrostbiteVR repository:
same paths, new files only, plus one `include()` line.

## Merge into FrostbiteVR
1. Copy this tree over a FrostbiteVR checkout (no existing file is replaced).
2. In the root `CMakeLists.txt`, after FvrCore: `include(cmake/Bf4Adapter.cmake)`.
3. Create the `BF4VR` releases repo (or edit `launcher/profiles/bf4.json`), and copy
   `.github/ISSUE_TEMPLATE/vr-feedback.yml` there too.

## Build and test (x64 only)
```
cmake -S . -B build/x64 -G "Visual Studio 17 2022" -A x64 -DBUILD_TESTING=ON
cmake --build build/x64 --config Release --target FrostbiteVR.Bf4 Bf4DxgiProxy Bf4NvapiShim Bf4CoreTests Bf4EmuHost
ctest --test-dir build/x64 -C Release -R ^Bf4 --output-on-failure
python -B -m unittest discover -s launcher/tests -p "test_*.py"
```

## Ship a hotfix
Bump the patch version, tag `v0.1.1` (or `v0.1.1-rc1` for testers), push. CI builds,
runs the emulated tests, packages, and publishes the zip + manifest. Launchers pick it
up from "Check for updates"; unchanged files are hard-linked, the previous version stays
for one-click rollback.

## Read next
`docs/BF4-RESEARCH.md` · `docs/BF4-ARCHITECTURE.md` · `docs/BF4-ROADMAP.md` ·
`docs/BF4-STEREO.md` · `docs/BF4-CAMERA-RE.md` · `docs/BF4-SESSION-RE.md` · `docs/BF4-MANUAL-RELOAD.md` · `docs/BF4-RE-PLAYBOOK.md` ·
`docs/BF4-TESTING.md` · `docs/CURRENT-BF4.md`

No Windows box handy? `tools/emulated/run_all.sh` runs the portable launcher tier on Linux/WSL;
the C++ suites need Windows (`Build.ps1`, or ctest `-R ^Bf4`).

Independent community project; not affiliated with EA or DICE. Single-player only: the
adapter turns itself off on any multiplayer evidence.
