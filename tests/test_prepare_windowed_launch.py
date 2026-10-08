"""Offline tests only: temp config fixtures; no process or real settings access."""
import importlib.util
from pathlib import Path
import tempfile
import unittest
SPEC = importlib.util.spec_from_file_location("prepare_windowed_launch", Path(__file__).resolve().parents[1] / "tools" / "prepare_windowed_launch.py")
mod = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(mod)


class WindowedPreparationTests(unittest.TestCase):
    def test_preserves_unrelated_text_encoding_and_newlines(self):
        text = "; native prefs\r\n[WindowSettings]\r\nWidth=1920\r\n Fullscreen = TRUE ; keep\r\nVSync=true\r\n[Graphics]\nFullscreen=true\nTexture=high"
        expected = text.replace("TRUE ; keep", "false ; keep")
        for encoding, bom in [("utf-8", b""), ("utf-8", b"\xef\xbb\xbf"), ("utf-16-le", b"\xff\xfe"), ("utf-16-be", b"\xfe\xff")]:
            with self.subTest(encoding=encoding, bom=bom):
                actual, codec, changed = mod.planned_bytes(bom + text.encode(encoding))
                self.assertEqual(actual, bom + expected.encode(encoding))
                self.assertEqual(codec, encoding)
                self.assertTrue(changed)

    def test_legacy_comment_and_idempotence(self):
        raw = b"; caf\xe9\r\n[WindowSettings]\r\nFullscreen=true\r\n"
        actual, codec, changed = mod.planned_bytes(raw)
        self.assertEqual(actual, raw.replace(b"=true", b"=false"))
        self.assertEqual(codec, "cp1252")
        self.assertTrue(changed)
        self.assertEqual(mod.planned_bytes(actual), (actual, "cp1252", False))
        self.assertEqual(mod.planned_bytes(b"[WindowSettings]\nFullscreen=FALSE")[2], False)

    def test_ambiguous_values_rejected(self):
        cases = ["[WindowSettings]\nFullscreen=maybe", "[WindowSettings]\nFullscreen=true\nFullscreen=false",
                 "[WindowSettings]\nFullscreen=true\n[WindowSettings]\nWidth=2", "[Graphics]\nFullscreen=true",
                 "[WindowSettings]\n; Fullscreen=true", "[WindowSettings]\nFullscreen=true garbage",
                 "[WindowSettings]\nFullscreen=true\0"]
        for value in cases:
            with self.subTest(value=value), self.assertRaises(mod.PreparationError):
                mod.planned_bytes(value.encode())

    def test_dry_run_then_backup_atomic_apply_then_idempotence(self):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / "settings.ini"
            original = b"[WindowSettings]\r\nWidth=1920\r\nFullscreen=true\r\n[Graphics]\r\nMSAA=3\r\n"
            path.write_bytes(original)
            report = mod.prepare(path, path, lambda: [])
            self.assertFalse(report["applied"])
            self.assertEqual(path.read_bytes(), original)
            self.assertEqual(len(list(Path(folder).iterdir())), 1)
            report = mod.prepare(path, path, lambda: [], True)
            self.assertTrue(report["applied"])
            self.assertEqual(Path(report["backup"]).read_bytes(), original)
            self.assertEqual(path.read_bytes(), original.replace(b"Fullscreen=true", b"Fullscreen=false"))
            self.assertFalse(mod.prepare(path, path, lambda: [], True)["applied"])
            self.assertEqual(len(list(Path(folder).iterdir())), 2)

    def test_live_game_and_wrong_path_refuse_without_writes(self):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / "settings.ini"
            original = b"[WindowSettings]\nFullscreen=true"
            path.write_bytes(original)
            with self.assertRaises(mod.PreparationError):
                mod.prepare(path, path, lambda: [{"ProcessId": 1}], True)
            with self.assertRaises(mod.PreparationError):
                mod.prepare(path, path.with_name("other.ini"), lambda: [], True)
            self.assertEqual(path.read_bytes(), original)
            self.assertEqual(len(list(Path(folder).iterdir())), 1)

    def test_launch_during_preparation_preserves_original(self):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / "settings.ini"
            original = b"[WindowSettings]\nFullscreen=true"
            path.write_bytes(original)
            calls = []
            def processes():
                calls.append(1)
                return [{"ProcessId": 1}] if len(calls) == 3 else []
            with self.assertRaises(mod.PreparationError):
                mod.prepare(path, path, processes, True)
            self.assertEqual(path.read_bytes(), original)
            backups = list(path.parent.glob("*.bak"))
            self.assertEqual(len(backups), 1)
            self.assertEqual(backups[0].read_bytes(), original)
            self.assertFalse(list(path.parent.glob("*.tmp")))

    def test_concurrent_settings_edit_is_not_overwritten(self):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / "settings.ini"
            original = b"[WindowSettings]\nFullscreen=true"
            path.write_bytes(original)
            calls = []
            def processes():
                calls.append(1)
                if len(calls) == 3:
                    path.write_bytes(original + b"\nWidth=1280")
                return []
            with self.assertRaises(mod.PreparationError):
                mod.prepare(path, path, processes, True)
            self.assertEqual(path.read_bytes(), original + b"\nWidth=1280")
            self.assertFalse(list(path.parent.glob("*.tmp")))


if __name__ == "__main__":
    unittest.main()
