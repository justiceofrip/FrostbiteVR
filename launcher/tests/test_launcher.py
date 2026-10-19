"""Launcher regressions. No network, no game, no headset: releases are local files served
through an injected fetch function. One test per behaviour; no permutations."""
from __future__ import annotations

import json
import os
import shutil
import sys
import tempfile
import unittest
import urllib.parse
import zipfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from fvrlaunch.feedback import collect, issue_url, sanitize  # noqa: E402
from fvrlaunch.games import load_profile  # noqa: E402
from fvrlaunch.manifest import build_manifest, load_manifest  # noqa: E402
from fvrlaunch.session import (CFG_BEGIN, Journal, SessionError, apply_cfg_block, pe_machine,  # noqa: E402
                               prepare_game_folder, restore_game_folder, session_text)
from fvrlaunch.settings import Settings  # noqa: E402
from fvrlaunch.updater import Installer, UpdateError  # noqa: E402


def make_release(work: Path, version: str, files: dict[str, bytes], tamper: bool = False) -> tuple[Path, Path]:
    src = work / f"src-{version}"
    for rel, data in files.items():
        p = src / rel
        p.parent.mkdir(parents=True, exist_ok=True)
        p.write_bytes(data)
    pkg = work / f"FrostbiteVR-BF4-{version}.zip"
    with zipfile.ZipFile(pkg, "w") as zf:
        for rel in files:
            zf.write(src / rel, rel)
    man = build_manifest(src, "FrostbiteVR-BF4", version, "prerelease", pkg)
    if tamper:
        man["package"]["sha256"] = "0" * 64
    man_path = work / f"FrostbiteVR-BF4-{version}.manifest.json"
    man_path.write_text(json.dumps(man), encoding="utf-8")
    return man_path, pkg


def local_fetch(url: str, dest: Path) -> None:
    shutil.copyfile(url, dest)


class LauncherTests(unittest.TestCase):
    def setUp(self):
        self.tmp = Path(tempfile.mkdtemp(prefix="fvr-launcher-"))
        self.root = self.tmp / "install"
        self.root.mkdir()

    def tearDown(self):
        shutil.rmtree(self.tmp, ignore_errors=True)

    def test_update_hardlinks_unchanged_files_then_rolls_back(self):
        inst = Installer(self.root, fetch=local_fetch)
        base = {"bin/FrostbiteVR.Bf4.dll": b"adapter-v1", "bin/proxy/dxgi.dll": b"proxy", "bin/FvrXrHost.exe": b"host"}
        self.assertEqual(inst.install_from_urls(*map(str, make_release(self.tmp, "0.1.0", base))), "0.1.0")
        hotfix = dict(base, **{"bin/FrostbiteVR.Bf4.dll": b"adapter-v1.1"})
        self.assertEqual(inst.install_from_urls(*map(str, make_release(self.tmp, "0.1.1", hotfix))), "0.1.1")
        old, new = self.root / "versions/0.1.0", self.root / "versions/0.1.1"
        self.assertTrue(os.path.samefile(old / "bin/proxy/dxgi.dll", new / "bin/proxy/dxgi.dll"))  # shared bytes
        self.assertEqual((new / "bin/FrostbiteVR.Bf4.dll").read_bytes(), b"adapter-v1.1")
        self.assertEqual(inst.rollback(), "0.1.0")
        self.assertEqual(inst.state(), {"current": "0.1.0", "previous": "0.1.1"})
        # A third version prunes down to current + previous.
        inst.rollback()
        inst.install_from_urls(*map(str, make_release(self.tmp, "0.2.0", base)))
        self.assertEqual(sorted(p.name for p in (self.root / "versions").iterdir()), ["0.1.1", "0.2.0"])
        self.assertEqual(list((self.root / "tmp").iterdir()), [])  # no leftovers on disk

    def test_corrupt_or_unsafe_packages_change_nothing(self):
        inst = Installer(self.root, fetch=local_fetch)
        with self.assertRaises(UpdateError):
            inst.install_from_urls(*map(str, make_release(self.tmp, "0.1.0", {"a.dll": b"x"}, tamper=True)))
        self.assertIsNone(inst.state()["current"])
        evil = self.tmp / "evil.zip"
        with zipfile.ZipFile(evil, "w") as zf:
            zf.writestr("../../escape.dll", b"x")
        man = {"name": "x", "version": "9.9.9", "package": {"file": "evil.zip", "sha256": "0", "size": 1},
               "files": [{"path": "../../escape.dll", "sha256": "0", "size": 1}]}
        with self.assertRaises(ValueError):
            load_manifest(json.dumps(man))
        self.assertFalse((self.tmp / "escape.dll").exists())

    def test_settings_recovery_and_reload_option(self):
        path = self.root / "settings.json"
        path.write_text("{not json", encoding="utf-8")
        s = Settings.load(path)
        self.assertIsNotNone(s.recovered_from)
        self.assertTrue(s.recovered_from.exists())
        self.assertEqual(s["reload_mode"], "button")
        s["reload_mode"] = "manual"
        with self.assertRaises(ValueError):
            s["reload_mode"] = "telekinesis"
        s.values["future_key"] = 7
        s.save()
        again = Settings.load(path)
        self.assertEqual((again["reload_mode"], again.values["future_key"]), ("manual", 7))
        text = session_text(adapter=Path("G:/FVR/bin/FrostbiteVR.Bf4.dll"), log_dir=Path("G:/FVR/logs"),
                            exe_sha256="ab" * 32, host_pipe="p", reload_mode=again["reload_mode"], developer=False,
                            launcher_pid=1)
        keys = {l.split("=", 1)[0] for l in text.splitlines() if "=" in l and not l.startswith("#")}
        # Must match the C++ parser (src/games/bf4/Bf4SessionInfo.cpp).
        self.assertEqual(keys, {"protocol", "adapter", "log_dir", "exe_sha256", "host_pipe", "reload_mode",
                                "developer", "launcher_pid", "stereo", "stereo_route", "stereo_separation",
                                "spoof_gpu_vendor"})
        self.assertIn("reload_mode=manual", text)

    def test_game_folder_changes_are_journaled_and_undone(self):
        profile = load_profile("bf4")
        game = self.tmp / "Battlefield 4"
        game.mkdir()
        (game / "user.cfg").write_text("GameTime.MaxVariableFps 144\n", encoding="utf-8")
        proxy = self.tmp / "dxgi.dll"
        proxy.write_bytes(b"our proxy")
        journal = Journal(self.root / "session-journal.json")
        prepare_game_folder(profile, game, proxy, True, journal)
        self.assertTrue((game / "dxgi.dll").exists())
        cfg = (game / "user.cfg").read_text(encoding="utf-8")
        self.assertIn(CFG_BEGIN, cfg)
        self.assertIn("GameTime.MaxVariableFps 144", cfg)
        apply_cfg_block(game / "user.cfg", profile.engine_cfg_lines)  # idempotent
        self.assertEqual((game / "user.cfg").read_text(encoding="utf-8").count(CFG_BEGIN), 1)
        # Simulated crash: a new launcher start restores from the journal alone.
        restore_game_folder(Journal(self.root / "session-journal.json"))
        self.assertFalse((game / "dxgi.dll").exists())
        self.assertEqual((game / "user.cfg").read_text(encoding="utf-8"), "GameTime.MaxVariableFps 144\n")
        # A foreign dxgi.dll (ReShade) is never overwritten or deleted.
        (game / "dxgi.dll").write_bytes(b"reshade")
        with self.assertRaises(SessionError):
            prepare_game_folder(profile, game, proxy, False, journal)
        restore_game_folder(journal)
        self.assertEqual((game / "dxgi.dll").read_bytes(), b"reshade")

    def test_pe_check_rejects_32bit(self):
        def pe(machine: int) -> Path:
            b = bytearray(512)
            b[0:2] = b"MZ"
            b[0x3C:0x40] = (0x80).to_bytes(4, "little")
            b[0x80:0x84] = b"PE\0\0"
            b[0x84:0x86] = machine.to_bytes(2, "little")
            p = self.tmp / f"{machine:x}.exe"
            p.write_bytes(b)
            return p
        self.assertEqual(pe_machine(pe(0x8664)), 0x8664)
        self.assertEqual(pe_machine(pe(0x14C)), 0x14C)

    def test_feedback_is_sanitized_and_url_is_bounded(self):
        home = os.path.expanduser("~")
        log = self.tmp / "bf4-adapter.log"
        log.write_text(f"opened {home}\\AppData\\x\nmail me: someone@example.com\ntoken=abc123\n"
                       f"gh: ghp_{'a' * 36}\npeer 203.0.113.7\n" + "x" * 200_000, encoding="utf-8")
        report = collect("Black screen in menu", "Pressed launch.", {"reload_mode": "button"},
                         {"launcher": "0.1.0", "mod": "0.1.0"}, [log], extra_terms=[("Rigby", "<user>")])
        blob = json.dumps(report)
        for secret in (home, "someone@example.com", "abc123", "ghp_aaaa", "203.0.113.7"):
            self.assertNotIn(secret, blob)
        self.assertLessEqual(len(report["logs"]["bf4-adapter.log"]), 64 * 1024)
        url = issue_url("justiceofrip/FrostbiteVR", report, None)
        self.assertLess(len(url), 20000)
        self.assertEqual(urllib.parse.parse_qs(urllib.parse.urlparse(url).query)["template"], ["vr-feedback.yml"])


if __name__ == "__main__":
    unittest.main(verbosity=1)
