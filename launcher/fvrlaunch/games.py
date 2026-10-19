"""Game profiles. A profile says where the game lives, how to start it, which proxy DLL
carries the adapter in, and which engine settings are applied (and removed) per session."""
from __future__ import annotations

import json
from dataclasses import dataclass, field
from pathlib import Path

PROFILE_DIR = Path(__file__).resolve().parent.parent / "profiles"


@dataclass(frozen=True)
class GameProfile:
    id: str
    title: str
    exe: str
    default_dir: str
    steam_app_id: str | None
    proxy_name: str
    session_section: str
    stereo_proxy_name: str | None
    release_repo: str
    engine_cfg_file: str
    # Several games share one repository: only releases tagged <prefix><version> are this game's.
    release_tag_prefix: str = "v"
    engine_cfg_lines: tuple[str, ...] = field(default_factory=tuple)
    unsupported_exes: tuple[str, ...] = field(default_factory=tuple)
    profile_settings_file: str | None = None  # %DOCUMENTS% is expanded at session start
    stereo_profile_lines: tuple[str, ...] = field(default_factory=tuple)  # set only while stereo != off
    # user.cfg lines per stereo route ("split", "nvapi"), stereo sessions only
    stereo_engine_cfg_lines: dict = field(default_factory=dict)


def load_profile(game_id: str, directory: Path = PROFILE_DIR) -> GameProfile:
    data = json.loads((directory / f"{game_id}.json").read_text(encoding="utf-8"))
    return GameProfile(
        id=data["id"],
        title=data["title"],
        exe=data["exe"],
        default_dir=data["default_dir"],
        steam_app_id=data.get("steam_app_id"),
        proxy_name=data["proxy_name"],
        stereo_proxy_name=data.get("stereo_proxy_name"),
        session_section=data["session_section"],
        release_repo=data["release_repo"],
        release_tag_prefix=data.get("release_tag_prefix", "v"),
        engine_cfg_file=data.get("engine_cfg_file", "user.cfg"),
        engine_cfg_lines=tuple(data.get("engine_cfg_lines", [])),
        unsupported_exes=tuple(data.get("unsupported_exes", [])),
        profile_settings_file=data.get("profile_settings_file"),
        stereo_profile_lines=tuple(data.get("stereo_profile_lines", [])),
        stereo_engine_cfg_lines={k: tuple(v) for k, v in data.get("stereo_engine_cfg_lines", {}).items()},
    )
