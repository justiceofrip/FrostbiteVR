"""Release -> launcher contract: make_release.py output must install through the real
Installer, and gen_proxy_exports.py must reproduce a DLL's export table exactly."""
from __future__ import annotations

import shutil
import struct
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "launcher"))

from fvrlaunch.updater import Installer  # noqa: E402


def synthetic_dll(path: Path, names: dict[int, str], ordinal_only: list[int], base: int = 1) -> None:
    """Minimal PE32+ with one section holding an export directory."""
    sect_rva, sect_raw = 0x1000, 0x200
    count = max(list(names) + ordinal_only) - base + 1
    named = sorted(names.items(), key=lambda kv: kv[1])
    edata = bytearray(40)  # IMAGE_EXPORT_DIRECTORY
    afun = len(edata); edata += b"\0" * 4 * count
    anames = len(edata); edata += b"\0" * 4 * len(named)
    aords = len(edata); edata += b"\0" * 2 * len(named)
    for ordinal in list(names) + ordinal_only:
        struct.pack_into("<I", edata, afun + 4 * (ordinal - base), 0x2000 + ordinal)  # non-zero RVA
    for i, (ordinal, name) in enumerate(named):
        struct.pack_into("<I", edata, anames + 4 * i, sect_rva + len(edata))
        struct.pack_into("<H", edata, aords + 2 * i, ordinal - base)
        edata += name.encode() + b"\0"
    struct.pack_into("<IIIIII", edata, 16, base, count, len(named), sect_rva + afun, sect_rva + anames, sect_rva + aords)
    b = bytearray(sect_raw)
    b[0:2] = b"MZ"
    struct.pack_into("<I", b, 0x3C, 0x40)
    b[0x40:0x44] = b"PE\0\0"
    struct.pack_into("<HH", b, 0x44, 0x8664, 1)          # machine, 1 section
    struct.pack_into("<H", b, 0x44 + 16, 240)            # SizeOfOptionalHeader (PE32+)
    opt = 0x58
    struct.pack_into("<H", b, opt, 0x20B)
    struct.pack_into("<II", b, opt + 112, sect_rva, len(edata))  # export data directory
    sec = opt + 240
    b[sec:sec + 8] = b".edata\0\0"
    struct.pack_into("<IIII", b, sec + 8, len(edata), sect_rva, len(edata), sect_raw)
    path.write_bytes(bytes(b) + bytes(edata))


class ReleaseContractTests(unittest.TestCase):
    def setUp(self):
        self.tmp = Path(tempfile.mkdtemp(prefix="fvr-release-"))

    def tearDown(self):
        shutil.rmtree(self.tmp, ignore_errors=True)

    def test_packaged_release_installs_with_only_runtime_files(self):
        build = self.tmp / "build"
        build.mkdir()
        (build / "FrostbiteVR.Bf4.dll").write_bytes(b"adapter")
        (build / "dxgi.dll").write_bytes(b"proxy")
        (build / "nvapi64.dll").write_bytes(b"stereo shim")
        (build / "bf4.exe").write_bytes(b"game binary that must never ship")
        (build / "Bf4CoreTests.exe").write_bytes(b"test binary")
        out = self.tmp / "dist"
        subprocess.run([sys.executable, str(ROOT / "tools/release/make_release.py"), "--build", str(build),
                        "--version", "0.1.1", "--channel", "prerelease", "--out", str(out)],
                       check=True, capture_output=True)
        self.assertEqual(sorted(p.name for p in out.iterdir()),
                         ["FrostbiteVR-BF4-0.1.1.manifest.json", "FrostbiteVR-BF4-0.1.1.zip"])  # no stage left
        inst = Installer(self.tmp / "install", fetch=lambda url, dest: shutil.copyfile(url, dest))
        version = inst.install_from_urls(str(out / "FrostbiteVR-BF4-0.1.1.manifest.json"),
                                         str(out / "FrostbiteVR-BF4-0.1.1.zip"))
        installed = inst.current_dir()
        names = {p.name for p in installed.rglob("*") if p.is_file()}
        self.assertEqual(version, "0.1.1")
        self.assertTrue({"FrostbiteVR.Bf4.dll", "dxgi.dll", "nvapi64.dll", "bf4.json", "Launch FrostbiteVR.cmd"} <= names)
        self.assertFalse({"bf4.exe", "Bf4CoreTests.exe"} & names)
        self.assertTrue({"THIRD_PARTY_NOTICES.md", "OpenXR-Apache-2.0.txt", "MinHook-BSD.txt"} <= names)  # notices ship
        self.assertFalse(list(installed.rglob("__pycache__")))

    def test_published_release_requires_the_xr_host(self):
        build = self.tmp / "build"
        build.mkdir()
        for name in ("FrostbiteVR.Bf4.dll", "dxgi.dll", "nvapi64.dll"):
            (build / name).write_bytes(name.encode())
        r = subprocess.run([sys.executable, str(ROOT / "tools/release/make_release.py"), "--build", str(build),
                            "--version", "0.1.2", "--out", str(self.tmp / "dist"), "--require-xr-host"],
                           capture_output=True, text=True)
        self.assertNotEqual(r.returncode, 0)
        self.assertIn("FvrXrHost.exe", r.stderr)

    def test_updates_only_this_games_releases(self):
        # One repository, several games: the BF4 launcher must never pick another game's package.
        import json
        from fvrlaunch.updater import list_releases
        def asset(name):
            return {"name": name, "browser_download_url": f"https://example.invalid/{name}"}
        releases = [
            {"tag_name": "bc2-v1.0.0", "prerelease": False, "assets": [asset("BC2.manifest.json"), asset("BC2.zip")]},
            {"tag_name": "checkpoint-412", "prerelease": False, "assets": []},
            {"tag_name": "bf4-v0.5.0-rc1", "prerelease": True,
             "assets": [asset("FrostbiteVR-BF4-0.5.0-rc1.manifest.json"), asset("FrostbiteVR-BF4-0.5.0-rc1.zip")]},
            {"tag_name": "bf4-v0.6.0", "draft": True, "assets": [asset("x.manifest.json"), asset("x.zip")]},
        ]
        def fetch(url, dest):
            Path(dest).write_text(json.dumps(releases), encoding="utf-8")
        found = list_releases("justiceofrip/FrostbiteVR", fetch, self.tmp, "bf4-v")
        self.assertEqual([(r.version, r.prerelease) for r in found], [("0.5.0-rc1", True)])

    def test_install_local_cli(self):
        build = self.tmp / "build"
        build.mkdir()
        for name in ("FrostbiteVR.Bf4.dll", "dxgi.dll", "nvapi64.dll"):
            (build / name).write_bytes(name.encode())
        out = self.tmp / "dist"
        subprocess.run([sys.executable, str(ROOT / "tools/release/make_release.py"), "--build", str(build),
                        "--version", "0.2.0-local", "--out", str(out)], check=True, capture_output=True)
        root = self.tmp / "G-FrostbiteVR"
        res = subprocess.run([sys.executable, "-m", "fvrlaunch", "install-local",
                              str(out / "FrostbiteVR-BF4-0.2.0-local.manifest.json"),
                              str(out / "FrostbiteVR-BF4-0.2.0-local.zip"), "--root", str(root)],
                             cwd=str(ROOT / "launcher"), capture_output=True, text=True)
        self.assertEqual(res.returncode, 0, res.stderr)
        self.assertIn("0.2.0-local", res.stdout)
        self.assertTrue((root / "versions/0.2.0-local/bin/proxy/nvapi64.dll").exists())

    def test_proxy_export_generator_matches_the_dll(self):
        dll = self.tmp / "dxgi.dll"
        synthetic_dll(dll, {1: "ApplyCompatResolutionQuirking", 9: "CreateDXGIFactory", 10: "CreateDXGIFactory1"},
                      ordinal_only=[14])
        out = subprocess.run([sys.executable, str(ROOT / "tools/release/gen_proxy_exports.py"), str(dll)],
                             capture_output=True, text=True, check=True).stdout
        self.assertIn('#define FVR_DXGI_FWD "\\\\\\\\.\\\\GLOBALROOT\\\\SystemRoot\\\\System32\\\\dxgi.dll."', out)
        self.assertIn('/export:ApplyCompatResolutionQuirking=" FVR_DXGI_FWD "ApplyCompatResolutionQuirking,@1"', out)
        self.assertIn('/export:__fvr_ord14=" FVR_DXGI_FWD "#14,@14,NONAME"', out)
        # Factory functions are implemented by the proxy itself, never forwarded.
        self.assertNotIn("/export:CreateDXGIFactory", out)
        self.assertIn("Owned (not forwarded): CreateDXGIFactory, CreateDXGIFactory1, CreateDXGIFactory2", out)
        self.assertEqual(out.count("/export:"), 2)


if __name__ == "__main__":
    unittest.main(verbosity=1)
