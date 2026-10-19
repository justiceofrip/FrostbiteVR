#!/bin/sh
# Type-check every Windows-only BF4 translation unit against the declarations-only shim.
# Catches typos, missing includes, wrong signatures and type errors before a Windows
# build. It does NOT prove the code links or runs on Windows.
set -e
R=$(cd "$(dirname "$0")/../.." && pwd)
for f in src/games/bf4/Bf4Module.cpp src/games/bf4/Bf4MenuBridge.cpp src/games/bf4/Bf4GpuVendor.cpp \
         src/platform/windows/bf4/DxgiProxy.cpp src/platform/windows/bf4/NvapiShim.cpp \
         tests/bf4/emu/Bf4EmuHost.cpp tests/bf4/emu/FakePunkBuster.cpp; do
  g++ -std=c++20 -fsyntax-only -Wall -Wextra -Wno-unknown-pragmas -Wno-unused-parameter -Wno-cast-function-type \
      -I"$R/tools/wincheck/include" -I"$R/include" "$R/$f" && echo "typecheck ok: $f"
done
