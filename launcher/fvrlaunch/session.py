"""One VR session = publish the session section, put the proxy in the game folder,
apply the engine-settings block, start the XR host and the game, then undo all of it.

Every change to the game folder is journaled first, so a crash or power loss is undone
on the next launcher start. Files we did not create are never overwritten or deleted.
"""
from __future__ import annotations

import json
import mmap
import os
import shutil
import struct
import sys
import time
from dataclasses import dataclass
from pathlib import Path

from .games import GameProfile
from .manifest import sha256_file

CFG_BEGIN = "# >>> FrostbiteVR session settings (removed automatically after the VR session)"
CFG_END = "# <<< FrostbiteVR session settings"
SECTION_BYTES = 4096
SESSION_PROTOCOL = 1


class SessionError(RuntimeError):
    pass


# ---- executable checks ---------------------------------------------------------------
def pe_machine(path: Path) -> int | None:
    with path.open("rb") as fh:
        head = fh.read(4096)
    if head[:2] != b"MZ":
        return None
    pe = struct.unpack_from("<I", head, 0x3C)[0]
    if head[pe:pe + 4] != b"PE\0\0":
        return None
    return struct.unpack_from("<H", head, pe + 4)[0]


def preflight(profile: GameProfile, game_dir: Path) -> dict:
    exe = game_dir / profile.exe
    if not exe.exists():
        raise SessionError(f"{profile.exe} was not found in {game_dir}. Choose the {profile.title} folder.")
    machine = pe_machine(exe)
    if machine != 0x8664:
        raise SessionError(f"{profile.exe} is not a 64-bit executable; FrostbiteVR supports the 64-bit game only.")
    return {"exe": str(exe), "sha256": sha256_file(exe), "size": exe.stat().st_size}


# ---- user.cfg block ------------------------------------------------------------------
def apply_cfg_block(cfg: Path, lines: tuple[str, ...]) -> bool:
    """Returns True if the file did not exist before (so cleanup may delete it)."""
    created = not cfg.exists()
    existing = "" if created else _strip_block(cfg.read_text(encoding="utf-8", errors="replace"))
    sep = "" if not existing or existing.endswith("\n") else "\n"
    block = "\n".join([CFG_BEGIN, *lines, CFG_END]) + "\n"
    cfg.write_text(existing + sep + block, encoding="utf-8")
    return created


def remove_cfg_block(cfg: Path, created: bool) -> None:
    if not cfg.exists():
        return
    rest = _strip_block(cfg.read_text(encoding="utf-8", errors="replace"))
    if created and not rest.strip():
        cfg.unlink()
    else:
        cfg.write_text(rest, encoding="utf-8")


def _strip_block(text: str) -> str:
    out, inside = [], False
    for line in text.splitlines(keepends=True):
        s = line.strip()
        if s == CFG_BEGIN:
            inside = True
            continue
        if s == CFG_END:
            inside = False
            continue
        if not inside:
            out.append(line)
    return "".join(out)


# ---- game profile settings (PROFSAVE_profile) ----------------------------------------
# BF4 keeps its options as "Key Value" lines and rewrites the whole file on exit, so the
# session changes and restores single keys, never the file: options the player changes
# in-game during the session survive.
def _split_profile(text: str) -> list[str]:
    return text.splitlines(keepends=True)


def _profile_value(lines: list[str], key: str) -> tuple[int, str | None]:
    for i, line in enumerate(lines):
        k, _, v = line.rstrip("\r\n").partition(" ")
        if k == key:
            return i, v
    return -1, None


def _write_profile(path: Path, lines: list[str]) -> None:
    tmp = path.with_name(path.name + ".fvr-tmp")
    tmp.write_bytes("".join(lines).encode("utf-8"))
    os.replace(tmp, path)


def apply_profile_keys(path: Path, settings_lines: tuple[str, ...]) -> list[dict]:
    """Sets each "Key Value"; returns journal entries {key, original, ours} (original None =
    the key was absent). Missing file: nothing is changed (BF4 creates it on first run)."""
    if not path.exists():
        return []
    lines = _split_profile(path.read_bytes().decode("utf-8", errors="surrogateescape"))
    entries = []
    for setting in settings_lines:
        key, _, ours = setting.partition(" ")
        i, original = _profile_value(lines, key)
        entries.append({"key": key, "original": original, "ours": ours})
        if original == ours:
            continue
        nl = "\n"
        if i >= 0:
            nl = lines[i][len(lines[i].rstrip("\r\n")):] or "\n"
            lines[i] = f"{key} {ours}{nl}"
        else:
            if lines and not lines[-1].endswith("\n"):
                lines[-1] += nl
            lines.append(f"{key} {ours}{nl}")
    _write_profile(path, lines)
    return entries


def restore_profile_keys(path: Path, entries: list[dict]) -> list[str]:
    """Puts back each original value, but only where the key still holds our value."""
    notes: list[str] = []
    if not entries or not path.exists():
        return notes
    lines = _split_profile(path.read_bytes().decode("utf-8", errors="surrogateescape"))
    changed = False
    for e in entries:
        i, current = _profile_value(lines, e["key"])
        if current != e["ours"]:
            if e["original"] != e["ours"] and current is not None and current != e["original"]:
                notes.append(f"Kept {e['key']} {current}: it was changed during the session.")
            continue
        if e["original"] == e["ours"]:
            continue
        if e["original"] is None:
            del lines[i]
        else:
            nl = lines[i][len(lines[i].rstrip("\r\n")):]
            lines[i] = f"{e['key']} {e['original']}{nl}"
        changed = True
    if changed:
        _write_profile(path, lines)
    return notes


def documents_dir() -> Path:
    """The user's Documents folder, following OneDrive/folder redirection on Windows."""
    if sys.platform == "win32":
        import ctypes
        from ctypes import wintypes

        class GUID(ctypes.Structure):
            _fields_ = [("d1", wintypes.DWORD), ("d2", wintypes.WORD), ("d3", wintypes.WORD),
                        ("d4", ctypes.c_ubyte * 8)]
        # FOLDERID_Documents {FDD39AD0-238F-46AF-ADB4-6C85480369C7}
        fid = GUID(0xFDD39AD0, 0x238F, 0x46AF, (ctypes.c_ubyte * 8)(0xAD, 0xB4, 0x6C, 0x85, 0x48, 0x03, 0x69, 0xC7))
        out = ctypes.c_wchar_p()
        if ctypes.windll.shell32.SHGetKnownFolderPath(ctypes.byref(fid), 0, None, ctypes.byref(out)) == 0:
            try:
                return Path(out.value)
            finally:
                ctypes.windll.ole32.CoTaskMemFree(out)
    return Path.home() / "Documents"


def resolve_profile_settings_file(template: str | None, documents: Path) -> Path | None:
    return Path(template.replace("%DOCUMENTS%", str(documents))) if template else None


# ---- proxy -----------------------------------------------------------------------------
def install_proxy(game_dir: Path, proxy_src: Path, name: str) -> Path:
    dest = game_dir / name
    if dest.exists():
        if sha256_file(dest) == sha256_file(proxy_src):
            return dest  # ours, left by an interrupted session
        raise SessionError(
            f"Another {name} is already in the game folder (ReShade or a different mod). "
            f"Rename it while playing in VR; FrostbiteVR will not overwrite it."
        )
    shutil.copy2(proxy_src, dest)
    return dest


def remove_proxy(dest: Path, expected_sha256: str) -> bool:
    if dest.exists() and sha256_file(dest) == expected_sha256:
        dest.unlink()
        return True
    return False


# ---- session section -------------------------------------------------------------------
def session_text(*, adapter: Path, log_dir: Path, exe_sha256: str, host_pipe: str, reload_mode: str,
                 developer: bool, launcher_pid: int | None = None, stereo: str = "off", stereo_route: str = "split",
                 stereo_separation: float = 100.0, spoof_gpu_vendor: bool = False,
                 research_dump_dir: Path | None = None) -> str:
    lines = [
        "# FrostbiteVR session (read by the dxgi proxy; absent = proxy does nothing)",
        f"protocol={SESSION_PROTOCOL}",
        f"adapter={adapter}",
        f"log_dir={log_dir}",
        f"exe_sha256={exe_sha256}",
        f"host_pipe={host_pipe}",
        f"reload_mode={reload_mode}",
        f"developer={1 if developer else 0}",
        f"stereo={stereo}",
        f"stereo_route={stereo_route}",
        f"stereo_separation={stereo_separation:g}",
        f"spoof_gpu_vendor={1 if spoof_gpu_vendor else 0}",
        f"launcher_pid={launcher_pid if launcher_pid is not None else os.getpid()}",
    ]
    if research_dump_dir is not None:  # developer sessions only (checked by the caller)
        lines.append(f"research_dump_dir={research_dump_dir}")
    text = "\n".join(lines) + "\n"
    if len(text.encode("utf-8")) >= SECTION_BYTES:
        raise SessionError("session text too long (install path?)")
    return text


class SessionSection:
    """Named shared memory. Windows only; elsewhere it just keeps the text (tests)."""

    def __init__(self, name: str, text: str):
        self.text = text
        self._map = None
        if sys.platform == "win32":
            self._map = mmap.mmap(-1, SECTION_BYTES, tagname=name)
            payload = text.encode("utf-8")
            self._map.seek(0)
            self._map.write(payload + b"\0" * (SECTION_BYTES - len(payload)))

    def close(self) -> None:
        if self._map is not None:
            self._map.close()
            self._map = None


# ---- journal ---------------------------------------------------------------------------
@dataclass
class Journal:
    path: Path

    def write(self, data: dict) -> None:
        tmp = self.path.with_suffix(".tmp")
        tmp.write_text(json.dumps(data, indent=2), encoding="utf-8")
        os.replace(tmp, self.path)

    def read(self) -> dict | None:
        if not self.path.exists():
            return None
        try:
            return json.loads(self.path.read_text(encoding="utf-8"))
        except ValueError:
            return None

    def clear(self) -> None:
        self.path.unlink(missing_ok=True)


def prepare_game_folder(profile: GameProfile, game_dir: Path, proxy_src: Path, apply_cfg: bool,
                        journal: Journal, extra_proxies: list[Path] | None = None,
                        profile_keys: tuple[Path, tuple[str, ...]] | None = None,
                        extra_cfg_lines: tuple[str, ...] = ()) -> dict:
    """proxy_src is the dxgi proxy; extra_proxies (e.g. the nvapi64.dll stereo shim) are
    installed under their own file names with the same protections. profile_keys is
    (PROFSAVE_profile path, "Key Value" lines) set for this session only. extra_cfg_lines
    (e.g. RenderDevice.StereoEnable for stereo) go in the user.cfg block even when the
    optional engine settings are off."""
    sources = [(proxy_src, profile.proxy_name)] + [(p, p.name) for p in (extra_proxies or [])]
    record = {"game_dir": str(game_dir),
              "proxies": [{"path": str(game_dir / name), "sha256": sha256_file(src)} for src, name in sources],
              "cfg": None, "cfg_created": False, "profile_file": None, "profile_keys": [],
              "started": time.time()}
    journal.write(record)  # before any change
    for src, name in sources:
        if (game_dir / name).exists() and sha256_file(game_dir / name) != sha256_file(src):
            raise SessionError(
                f"Another {name} is already in the game folder (ReShade or a different mod). "
                f"Rename it while playing in VR; FrostbiteVR will not overwrite it.")
    for src, name in sources:
        install_proxy(game_dir, src, name)
    cfg_lines = (profile.engine_cfg_lines if apply_cfg else ()) + tuple(extra_cfg_lines)
    if cfg_lines:
        cfg = game_dir / profile.engine_cfg_file
        record["cfg"] = str(cfg)
        record["cfg_created"] = not cfg.exists()
        journal.write(record)
        apply_cfg_block(cfg, cfg_lines)
    if profile_keys and profile_keys[1] and profile_keys[0].exists():
        path, wanted = profile_keys
        lines = _split_profile(path.read_bytes().decode("utf-8", errors="surrogateescape"))
        # Journal the originals before writing, so an interrupted write is still undone.
        record["profile_file"] = str(path)
        record["profile_keys"] = [{"key": k, "original": _profile_value(lines, k)[1], "ours": v}
                                  for k, _, v in (s.partition(" ") for s in wanted)]
        journal.write(record)
        apply_profile_keys(path, wanted)
    return record


def restore_game_folder(journal: Journal) -> list[str]:
    """Undo a (possibly interrupted) session. Returns human-readable notes."""
    record = journal.read()
    notes: list[str] = []
    if not record:
        return notes
    proxies = record.get("proxies") or [{"path": record["proxy"], "sha256": record["proxy_sha256"]}]  # 0.1.0 journals
    for entry in proxies:
        proxy = Path(entry["path"])
        if proxy.exists() and not remove_proxy(proxy, entry["sha256"]):
            notes.append(f"Left {proxy.name} in place: it is not FrostbiteVR's file.")
    if record.get("cfg"):
        remove_cfg_block(Path(record["cfg"]), bool(record.get("cfg_created")))
    if record.get("profile_file"):
        notes += restore_profile_keys(Path(record["profile_file"]), record.get("profile_keys") or [])
    journal.clear()
    return notes
