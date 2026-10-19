"""Package a release: stage runtime files, zip them, write the manifest next to the zip.

  python tools/release/make_release.py --build build/x64/Release --version 0.1.1 --channel prerelease --out dist

Output: dist/FrostbiteVR-BF4-<v>.zip and dist/FrostbiteVR-BF4-<v>.manifest.json, the two
assets the launcher looks for on a GitHub release. Game files are never packaged.
"""
from __future__ import annotations

import argparse
import json
import shutil
import sys
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "launcher"))
from fvrlaunch.manifest import build_manifest  # noqa: E402

RUNTIME = {  # build output name -> package path
    "FrostbiteVR.Bf4.dll": "bin/FrostbiteVR.Bf4.dll",
    "FrostbiteVR.Bf4.pdb": "bin/FrostbiteVR.Bf4.pdb",
    "dxgi.dll": "bin/proxy/dxgi.dll",
    "nvapi64.dll": "bin/proxy/nvapi64.dll",
    "FvrXrHost.exe": "bin/FvrXrHost.exe",
    # Khronos OpenXR loader (Apache-2.0); the host loads it from bin/runtime/openxr/win64.
    "runtime/openxr/win64/openxr_loader.dll": "bin/runtime/openxr/win64/openxr_loader.dll",
}
FORBIDDEN = ("bf4.exe", "bf4_x86.exe", ".sb", ".toc", ".cas", ".cat")


# Windowless Python for the launcher UI: a bundled one, then the python.org launchers
# (per-user, then all-users), then pythonw on PATH. Python is often installed without being
# on PATH, so a bare "pythonw" is not enough. Nothing found: say what is needed.
LAUNCH_CMD = "\r\n".join([
    "@echo off",
    "setlocal",
    'cd /d "%~dp0"',
    'set "ROOT=%~dp0..\\.."',
    'set "ARGS=-m fvrlaunch ui --root "%ROOT%""',
    'if exist "python\\pythonw.exe" (start "" "python\\pythonw.exe" %ARGS% & exit /b 0)',
    'if exist "%LOCALAPPDATA%\\Programs\\Python\\Launcher\\pyw.exe" '
    '(start "" "%LOCALAPPDATA%\\Programs\\Python\\Launcher\\pyw.exe" -3 %ARGS% & exit /b 0)',
    'if exist "%WINDIR%\\pyw.exe" (start "" "%WINDIR%\\pyw.exe" -3 %ARGS% & exit /b 0)',
    'for %%P in (pythonw.exe) do if not "%%~$PATH:P"=="" (start "" "%%~$PATH:P" %ARGS% & exit /b 0)',
    "echo FrostbiteVR needs Python 3.11 or newer from python.org. Install it, then run this again.",
    "pause",
    "",
])


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--build", type=Path, required=True)
    ap.add_argument("--version", required=True)
    ap.add_argument("--channel", choices=["stable", "prerelease"], default="prerelease")
    ap.add_argument("--notes", default="")
    ap.add_argument("--out", type=Path, default=ROOT / "dist")
    ap.add_argument("--require-xr-host", action="store_true",
                    help="published releases: fail unless FvrXrHost.exe and the OpenXR loader are built")
    a = ap.parse_args()

    stage = a.out / f"stage-{a.version}"
    shutil.rmtree(stage, ignore_errors=True)
    for name, rel in RUNTIME.items():
        src = a.build / name
        if src.exists():
            (stage / rel).parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(src, stage / rel)
        elif name in ("FrostbiteVR.Bf4.dll", "dxgi.dll", "nvapi64.dll") or (
                a.require_xr_host and name in ("FvrXrHost.exe", "runtime/openxr/win64/openxr_loader.dll")):
            sys.exit(f"missing required build output: {src}")
    # Redistributed third-party code (MinHook in the adapter, the Khronos OpenXR loader with
    # JsonCpp) ships with its notices.
    (stage / "licenses").mkdir(parents=True, exist_ok=True)
    shutil.copy2(ROOT / "THIRD_PARTY_NOTICES.md", stage / "licenses" / "THIRD_PARTY_NOTICES.md")
    for name in ("OpenXR-Apache-2.0.txt", "JsonCpp-MIT.txt", "MinHook-BSD.txt", "BFVR-MIT.txt"):
        shutil.copy2(ROOT / "licenses" / name, stage / "licenses" / name)
    shutil.copytree(ROOT / "launcher" / "fvrlaunch", stage / "launcher" / "fvrlaunch",
                    ignore=shutil.ignore_patterns("__pycache__"))
    shutil.copytree(ROOT / "launcher" / "profiles", stage / "launcher" / "profiles")
    (stage / "Launch FrostbiteVR.cmd").write_text(LAUNCH_CMD, encoding="ascii", newline="")

    for p in stage.rglob("*"):
        if p.name.lower().endswith(FORBIDDEN):
            sys.exit(f"refusing to package game file: {p}")

    a.out.mkdir(parents=True, exist_ok=True)
    pkg = a.out / f"FrostbiteVR-BF4-{a.version}.zip"
    with zipfile.ZipFile(pkg, "w", zipfile.ZIP_DEFLATED) as zf:
        for p in sorted(stage.rglob("*")):
            if p.is_file():
                zf.write(p, p.relative_to(stage).as_posix())
    manifest = build_manifest(stage, "FrostbiteVR-BF4", a.version, a.channel, pkg, a.notes)
    (a.out / f"FrostbiteVR-BF4-{a.version}.manifest.json").write_text(json.dumps(manifest, indent=2), encoding="utf-8")
    shutil.rmtree(stage)
    print(f"{pkg.name}: {pkg.stat().st_size // 1024} KiB, {len(manifest['files'])} files")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
