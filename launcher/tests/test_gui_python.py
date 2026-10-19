"""Launcher window Python selection (fvrlaunch.gui_python): a Python without tkinter (e.g. a
project-private embedded one registered with the py launcher) must hand over to an
installed Python that has it, never to itself."""
from __future__ import annotations

import shutil
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from fvrlaunch.gui_python import find_gui_python, parse_py_list_paths, windowless  # noqa: E402

LIST_PATHS = """ -V:3.13 *        G:\\Other\\tools\\python-3.13.15\\python.exe
 -V:3.12          C:\\Users\\me\\AppData\\Local\\Programs\\Python\\Python312\\python.exe
not an interpreter line
"""


class GuiPythonTests(unittest.TestCase):
    def setUp(self):
        self.tmp = Path(tempfile.mkdtemp(prefix="fvr-guipy-"))

    def tearDown(self):
        shutil.rmtree(self.tmp, ignore_errors=True)

    def exe(self, folder: str, name: str = "python.exe") -> Path:
        p = self.tmp / folder / name
        p.parent.mkdir(parents=True, exist_ok=True)
        p.write_bytes(b"")
        return p

    def test_parses_py_launcher_listing(self):
        paths = parse_py_list_paths(LIST_PATHS)
        self.assertEqual([p.name for p in paths], ["python.exe", "python.exe"])
        self.assertIn("python-3.13.15", str(paths[0]))
        self.assertIn("Python312", str(paths[1]))

    def test_picks_an_installed_python_with_tkinter(self):
        embedded = self.exe("embedded")             # the default, no tkinter
        full = self.exe("full")
        full_w = self.exe("full", "pythonw.exe")
        missing = self.tmp / "gone" / "python.exe"  # listed but uninstalled
        has_tk = {str(full)}
        pick = find_gui_python([missing, embedded, full], current=embedded, probe=lambda p: str(p) in has_tk)
        self.assertEqual(pick, full)
        self.assertEqual(windowless(pick), full_w)  # restart without a console window
        self.assertIsNone(find_gui_python([embedded], current=embedded, probe=lambda p: True))  # never itself
        self.assertIsNone(find_gui_python([embedded, full], current=embedded, probe=lambda p: False))
        self.assertEqual(windowless(embedded), embedded)  # no pythonw next to it


if __name__ == "__main__":
    unittest.main()
