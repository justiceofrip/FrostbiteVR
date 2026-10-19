#!/bin/sh
# Portable emulated tier (Linux/macOS/WSL): the BF4 launcher's update/rollback, emulated VR
# session and release contract. The C++ BF4 suites need the framework and Windows SDK
# headers now (theater link, XInput, GDI), so they run through CMake/CTest on Windows
# (build.yml, or locally: Build.ps1 / ctest -R ^Bf4).
set -e
R=$(cd "$(dirname "$0")/../.." && pwd)
cd "$R/launcher" && python3 -B -m unittest discover -s tests -p "test_*.py" 2>&1 | tail -1
