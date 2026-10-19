"""Launcher settings with recovery: a malformed file is backed up, never silently lost,
and unknown keys from newer versions are preserved on save."""
from __future__ import annotations

import json
import os
import time
from pathlib import Path

DEFAULTS: dict = {
    "game": "bf4",
    "game_dir": None,            # None -> profile default
    "reload_mode": "button",     # "button" | "manual" (BF4: magazine gesture through the virtual pad)
    "apply_engine_cfg": True,    # session-scoped user.cfg block, removed after the session
    "channel": "prerelease",     # "stable" | "prerelease"
    "developer": True,           # pre-release: allow Candidate features (theater view, pad); off for stable
    "openxr_runtime": "system",  # honour the active runtime; never force SteamVR
    "stereo": "off",             # "off" | "probe" | "on": BF4's own stereo renderer
    "stereo_route": "nvapi",     # "nvapi": emulated 3D Vision driver | "split": engine side-by-side (read-only on PC)
    "stereo_separation": 100.0,  # percent reported to the engine
    "pretend_nvidia": False,     # stereo only: report an NVIDIA GPU so BF4 takes its 3D path
    "research_dump_image": False,  # developer RE only: research channel + on-demand image copy in <root>/research
    "developer_cfg_lines": [],     # developer sessions only: extra user.cfg lines (e.g. PerfOverlay.DrawFps 1)
    "launch_args": [],             # developer sessions only: start the exe directly with these arguments
    "xr_loader": "",               # developer sessions only: OpenXR loader/runtime DLL for the host (emulated XR)
}

STEREO_MODES = ("off", "probe", "on")
STEREO_ROUTES = ("split", "nvapi")

RELOAD_MODES = ("button", "manual")


class Settings:
    def __init__(self, path: Path):
        self.path = path
        self.values: dict = dict(DEFAULTS)
        self.recovered_from: Path | None = None

    @classmethod
    def load(cls, path: Path) -> "Settings":
        s = cls(path)
        if not path.exists():
            return s
        try:
            data = json.loads(path.read_text(encoding="utf-8"))
            if not isinstance(data, dict):
                raise ValueError("settings root is not an object")
        except (ValueError, OSError):
            backup = path.with_name(f"{path.name}.bad-{int(time.time())}")
            os.replace(path, backup)
            s.recovered_from = backup
            return s
        s.values.update(data)
        if s.values.get("reload_mode") not in RELOAD_MODES:
            s.values["reload_mode"] = DEFAULTS["reload_mode"]
        if s.values.get("stereo") not in STEREO_MODES:
            s.values["stereo"] = DEFAULTS["stereo"]
        if s.values.get("stereo_route") not in STEREO_ROUTES:
            s.values["stereo_route"] = DEFAULTS["stereo_route"]
        return s

    def save(self) -> None:
        self.path.parent.mkdir(parents=True, exist_ok=True)
        tmp = self.path.with_suffix(".tmp")
        tmp.write_text(json.dumps(self.values, indent=2, sort_keys=True), encoding="utf-8")
        os.replace(tmp, self.path)

    def __getitem__(self, key: str):
        return self.values[key]

    def __setitem__(self, key: str, value) -> None:
        if key == "reload_mode" and value not in RELOAD_MODES:
            raise ValueError(f"reload_mode must be one of {RELOAD_MODES}")
        if key == "stereo" and value not in STEREO_MODES:
            raise ValueError(f"stereo must be one of {STEREO_MODES}")
        if key == "stereo_route" and value not in STEREO_ROUTES:
            raise ValueError(f"stereo_route must be one of {STEREO_ROUTES}")
        if key == "stereo_separation" and not 0.0 <= float(value) <= 100.0:
            raise ValueError("stereo_separation must be 0..100")
        self.values[key] = value
