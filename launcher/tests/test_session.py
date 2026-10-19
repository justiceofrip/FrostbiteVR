"""Emulated VR session through the real run_session: a fake OS/game replaces Steam,
tasklist and the XR host; the game folder, journal, proxy and user.cfg are real files."""
from __future__ import annotations

import shutil
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from fvrlaunch.games import load_profile  # noqa: E402
from fvrlaunch.launch import NO_SWAPCHAIN_HINT, SessionEnv, run_session  # noqa: E402
from fvrlaunch.session import CFG_BEGIN, SessionError  # noqa: E402
from fvrlaunch.settings import Settings  # noqa: E402


def x64_exe(path: Path) -> None:
    b = bytearray(1024)
    b[0:2] = b"MZ"
    b[0x3C:0x40] = (0x80).to_bytes(4, "little")
    b[0x80:0x84] = b"PE\0\0"
    b[0x84:0x86] = (0x8664).to_bytes(2, "little")
    path.write_bytes(bytes(b))


class EmulatedGame:
    """Starts `start_delay` polls after launch, adopts a swapchain (or not), runs, exits."""

    def __init__(self, game_dir: Path, log: Path, start_delay=2, run_polls=3, adopt=True, starts=True,
                 documents: Path | None = None):
        self.game_dir, self.log, self.documents = game_dir, log, documents
        self.start_delay, self.run_polls, self.adopt, self.starts = start_delay, run_polls, adopt, starts
        self.launched = False
        self.polls_since_launch = 0
        self.clock = 0.0
        self.seen_while_running: list[tuple[bool, bool]] = []
        self.reports: list[str] = []

    def env(self) -> SessionEnv:
        return SessionEnv(find_pids=self.find_pids, start_game=self.start_game, start_xr_host=self.start_xr_host,
                          sleep=self.sleep, monotonic=lambda: self.clock,
                          documents_dir=lambda: self.documents)  # never the real Documents

    def start_game(self, profile, game_dir, report):
        assert (game_dir / "dxgi.dll").exists(), "proxy must be in place before the game starts"
        self.launched = True

    def start_xr_host(self, version_dir, profile, pid, log_dir, report):
        assert pid == 4242 and self.launched, "the host starts once the game PID is known"
        self.host_started = True
        report("xr host (emulated)")
        return None

    def find_pids(self, exe):
        if not self.launched or not self.starts:
            return []
        self.polls_since_launch += 1
        running = self.start_delay <= self.polls_since_launch < self.start_delay + self.run_polls
        prof = self.documents / "Battlefield 4" / "settings" / "PROFSAVE_profile" if self.documents else None
        if running and prof and prof.exists():
            self.stereoscopy_seen = "GstRender.Stereoscopy 1\n" in prof.read_text(encoding="utf-8")
        if running:
            cfg = (self.game_dir / "user.cfg").read_text(encoding="utf-8")
            self.stereo_cfg_seen = "RenderDevice.StereoEnable 1" in cfg
            self.split_cfg_seen = "RenderDevice.StereoSplitScreenEnable 1" in cfg
        if not running and self.polls_since_launch == self.start_delay + self.run_polls and prof and prof.exists():
            # BF4 rewrites the whole profile on exit; the player changed FOV in-game meanwhile.
            prof.write_text(prof.read_text(encoding="utf-8").replace("Vertical 55.000000", "Vertical 70.000000"),
                            encoding="utf-8")
        if running:
            self.nvapi_seen = getattr(self, "nvapi_seen", False) or (self.game_dir / "nvapi64.dll").exists()
            if getattr(self, "probe_text", None):
                (self.log.parent / "stereo-probe.txt").write_text(self.probe_text, encoding="utf-8")
            self.seen_while_running.append(((self.game_dir / "dxgi.dll").exists(),
                                            CFG_BEGIN in (self.game_dir / "user.cfg").read_text(encoding="utf-8")))
            if self.adopt:
                self.log.write_text("12:00:00.000 adopted swapchain 2560x1440 fmt=28 windowed=0\n", encoding="utf-8")
        return [4242] if running else []

    def sleep(self, seconds):
        self.clock += seconds


class EmulatedSessionTests(unittest.TestCase):
    def setUp(self):
        self.tmp = Path(tempfile.mkdtemp(prefix="fvr-session-"))
        self.game = self.tmp / "steamapps" / "common" / "Battlefield 4"
        self.game.mkdir(parents=True)
        x64_exe(self.game / "bf4.exe")
        (self.game / "user.cfg").write_text("GameTime.MaxVariableFps 144\n", encoding="utf-8")
        self.root = self.tmp / "FrostbiteVR"
        self.version = self.root / "versions" / "0.1.0"
        (self.version / "bin" / "proxy").mkdir(parents=True)
        (self.version / "bin" / "proxy" / "dxgi.dll").write_bytes(b"proxy")
        (self.version / "bin" / "proxy" / "nvapi64.dll").write_bytes(b"stereo shim")
        (self.version / "bin" / "FrostbiteVR.Bf4.dll").write_bytes(b"adapter")
        self.documents = self.tmp / "Documents"
        self.prof = self.documents / "Battlefield 4" / "settings" / "PROFSAVE_profile"
        self.prof.parent.mkdir(parents=True)
        self.prof.write_text("GstRender.FieldOfViewVertical 55.000000\nGstRender.Stereoscopy 0\n", encoding="utf-8")
        self.settings = Settings(self.root / "settings.json")
        self.settings["game_dir"] = str(self.game)
        self.profile = load_profile("bf4")

    def tearDown(self):
        shutil.rmtree(self.tmp, ignore_errors=True)

    def run_game(self, game: EmulatedGame):
        run_session(self.profile, self.settings, self.root, self.version, game.reports.append, env=game.env())

    def assert_restored(self):
        self.assertFalse((self.game / "dxgi.dll").exists())
        self.assertEqual((self.game / "user.cfg").read_text(encoding="utf-8"), "GameTime.MaxVariableFps 144\n")
        self.assertFalse((self.root / "session-journal.json").exists())

    def test_full_session_lifecycle(self):
        game = EmulatedGame(self.game, self.root / "logs" / "bf4-adapter.log", documents=self.documents)
        self.run_game(game)
        self.assertTrue(game.seen_while_running and all(p and c for p, c in game.seen_while_running))
        self.assertIn("Battlefield 4 is running. Put on the headset.", game.reports)
        self.assertTrue(game.host_started)
        self.assertNotIn(NO_SWAPCHAIN_HINT, game.reports)
        self.assertEqual(game.reports[-1], "Game folder restored.")
        self.assert_restored()

    def test_stereo_probe_installs_shim_and_reports_the_verdict(self):
        self.settings["stereo"] = "probe"
        self.settings["stereo_route"] = "nvapi"
        self.settings["pretend_nvidia"] = True
        game = EmulatedGame(self.game, self.root / "logs" / "bf4-adapter.log", documents=self.documents)
        game.probe_text = ("FrostbiteVR stereo probe\nverdict: working\n"
                           "next: Engine stereo works: separate left and right images every frame.\n")
        self.run_game(game)
        self.assertTrue(game.nvapi_seen)  # shim in the game folder while BF4 ran
        self.assertIn("Stereo probe: working. Engine stereo works: separate left and right images every frame.",
                      game.reports)
        self.assertFalse((self.game / "nvapi64.dll").exists())
        self.assertTrue(game.stereoscopy_seen)  # BF4's own 3D option was on while it ran
        self.assertTrue(game.stereo_cfg_seen)   # the render-device gate (StereoEnable) too
        # Only our key is put back; the player's in-game FOV change survives.
        self.assertEqual(self.prof.read_text(encoding="utf-8"),
                         "GstRender.FieldOfViewVertical 70.000000\nGstRender.Stereoscopy 0\n")
        self.assert_restored()

    def test_split_screen_stereo_needs_no_nvapi_shim(self):
        self.settings["stereo"] = "probe"
        self.settings["stereo_route"] = "split"
        self.settings["pretend_nvidia"] = True  # ignored on this route
        game = EmulatedGame(self.game, self.root / "logs" / "bf4-adapter.log", documents=self.documents)
        self.run_game(game)
        self.assertTrue(game.stereo_cfg_seen and game.split_cfg_seen)
        self.assertFalse(getattr(game, "nvapi_seen", False))
        self.assert_restored()

    def test_stereo_off_never_touches_nvapi(self):
        game = EmulatedGame(self.game, self.root / "logs" / "bf4-adapter.log", documents=self.documents)
        self.run_game(game)
        self.assertFalse(getattr(game, "nvapi_seen", False))
        self.assertFalse(getattr(game, "stereoscopy_seen", False))
        self.assertFalse(getattr(game, "stereo_cfg_seen", False))
        self.assertFalse(any(r.startswith("Stereo probe") for r in game.reports))

    def test_mantle_or_no_swapchain_gets_a_clear_hint(self):
        game = EmulatedGame(self.game, self.root / "logs" / "bf4-adapter.log", documents=self.documents, run_polls=200, adopt=False)
        self.run_game(game)
        self.assertIn(NO_SWAPCHAIN_HINT, game.reports)
        self.assert_restored()

    def test_game_that_never_starts_still_restores_the_folder(self):
        game = EmulatedGame(self.game, self.root / "logs" / "bf4-adapter.log", documents=self.documents, starts=False)
        with self.assertRaises(SessionError):
            self.run_game(game)
        self.assert_restored()


if __name__ == "__main__":
    unittest.main(verbosity=1)
