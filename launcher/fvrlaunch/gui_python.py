"""A Python that can show the launcher window.

The launcher window uses tkinter, which some Python installs leave out (embedded or
project-private Pythons registered with the py launcher, for example). If the Python we
were started with has no tkinter, find an installed one that does and restart there; if
there is none, say so in a message box instead of exiting silently under pythonw.
"""
from __future__ import annotations

import os
import re
import shutil
import subprocess
import sys
from pathlib import Path
from typing import Callable, Iterable

RELAUNCH_ENV = "FVR_LAUNCHER_RELAUNCHED"  # one hop only: never bounce between interpreters
_LIST_LINE = re.compile(r"^\s*-V:\S+\s+\*?\s*(?P<path>.+?\.exe)\s*$", re.IGNORECASE)


def has_tkinter() -> bool:
    try:
        import tkinter  # noqa: F401
    except ImportError:
        return False
    return True


def parse_py_list_paths(text: str) -> list[Path]:
    """Interpreter paths from `py --list-paths` output."""
    return [Path(m["path"]) for line in text.splitlines() if (m := _LIST_LINE.match(line))]


def _py_launcher() -> str | None:
    found = shutil.which("py")
    if found:
        return found
    for base in (os.environ.get("LOCALAPPDATA"), os.environ.get("WINDIR")):
        if base:
            for candidate in (Path(base) / "Programs" / "Python" / "Launcher" / "py.exe", Path(base) / "py.exe"):
                if candidate.is_file():
                    return str(candidate)
    return None


def candidate_interpreters(run: Callable = subprocess.run) -> list[Path]:
    launcher = _py_launcher()
    if not launcher:
        return []
    try:
        out = run([launcher, "--list-paths"], capture_output=True, text=True, timeout=20,
                  creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0)).stdout
    except (OSError, subprocess.SubprocessError):
        return []
    return parse_py_list_paths(out)


def _probe(python: Path) -> bool:
    try:
        return subprocess.run([str(python), "-c", "import tkinter"], capture_output=True, timeout=20,
                              creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0)).returncode == 0
    except (OSError, subprocess.SubprocessError):
        return False


def find_gui_python(candidates: Iterable[Path], current: Path, probe: Callable[[Path], bool] = _probe) -> Path | None:
    current = Path(current).resolve()
    seen = set()
    for c in candidates:
        key = str(c).lower()
        if key in seen or not c.is_file() or c.resolve() == current or c.resolve() == windowless(current).resolve():
            continue
        seen.add(key)
        if probe(c):
            return c
    return None


def windowless(python: Path) -> Path:
    """pythonw.exe next to python.exe when there is one (no console window)."""
    w = python.with_name("pythonw.exe")
    return w if w.is_file() else python


def show_error(text: str) -> None:
    if sys.platform == "win32":
        import ctypes
        ctypes.windll.user32.MessageBoxW(None, text, "FrostbiteVR launcher", 0x10)
    else:
        print(text, file=sys.stderr)


def ensure_gui_python(root: Path, package_parent: Path) -> bool:
    """True: this process can show the window. False: a restart was started or an error shown."""
    if has_tkinter():
        return True
    if os.environ.get(RELAUNCH_ENV) != "1":
        alt = find_gui_python(candidate_interpreters(), Path(sys.executable))
        if alt:
            env = dict(os.environ, **{RELAUNCH_ENV: "1"})
            subprocess.Popen([str(windowless(alt)), "-m", "fvrlaunch", "ui", "--root", str(root)],
                             cwd=str(package_parent), env=env)
            return False
    show_error("The FrostbiteVR launcher window needs Python 3.11 or newer with tcl/tk (tkinter).\n\n"
               f"This Python has no tkinter:\n{sys.executable}\n\n"
               "Install Python from python.org with the \"tcl/tk and IDLE\" option, then start the launcher again.")
    return False
