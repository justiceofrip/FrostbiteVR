"""Starts the XR host and the game, waits for the game to exit, then restores the
game folder. BF4 on Steam starts through the EA app, so the launcher asks Steam to start
it and then waits for bf4.exe to appear instead of spawning it directly."""
from __future__ import annotations

import csv
import io
import os
import subprocess
import sys
import time
from dataclasses import dataclass
from pathlib import Path
from typing import Callable

from .games import GameProfile
from .session import (Journal, SessionError, SessionSection, documents_dir, preflight, prepare_game_folder,
                      resolve_profile_settings_file, restore_game_folder, session_text)
from .settings import Settings

Report = Callable[[str], None]
NO_SWAPCHAIN_HINT = ("No DirectX 11 swapchain was seen. In Battlefield 4: Options > Video > Graphics API, "
                     "choose DirectX 11 (Mantle is not supported), then start the VR session again.")


def find_pids(exe: str) -> list[int]:
    if sys.platform != "win32":
        return []
    out = subprocess.run(["tasklist", "/FI", f"IMAGENAME eq {exe}", "/FO", "CSV", "/NH"],
                         capture_output=True, text=True, creationflags=0x08000000).stdout
    pids = []
    for row in csv.reader(io.StringIO(out)):
        if len(row) >= 2 and row[0].lower() == exe.lower():
            pids.append(int(row[1]))
    return pids


def start_game(profile: GameProfile, game_dir: Path, report: Report, args: tuple[str, ...] = ()) -> None:
    if args:  # developer launch arguments: start the exe directly (Steam URLs with args need a click)
        subprocess.Popen([str(game_dir / profile.exe), *args], cwd=str(game_dir))
        report(f"Starting {profile.exe} directly with {len(args)} launch arguments (developer).")
        return
    if profile.steam_app_id and "steamapps" in str(game_dir).lower():
        os.startfile(f"steam://rungameid/{profile.steam_app_id}")  # type: ignore[attr-defined]
        report("Starting Battlefield 4 through Steam (the EA app may open first).")
    else:
        report("Start Battlefield 4 from the EA app now. FrostbiteVR is waiting for it.")


def start_xr_host(version_dir: Path, profile: GameProfile, pid: int, log_dir: Path, report: Report,
                  loader: str = ""):
    """Starts the OpenXR host once the game's PID is known. It shows the game screen as a
    theater panel with the laser pointer (menu lane) and exits with the game. The adapter
    connects when the host writes its channel token to logs/xr-channel.txt. Three frame
    channels (xr-channel-2.txt, -3.txt) let the game take the next head poses while it
    renders the current eye pair, which raises the head-tracked view's update rate."""
    host = version_dir / "bin" / "FvrXrHost.exe"
    if not host.exists():
        report("XR host is not in this package; the game will run, but nothing is shown in the headset.")
        return None
    out = (log_dir / "xr-host.json").open("wb")
    err = (log_dir / "xr-host.log").open("wb")
    try:
        return subprocess.Popen([str(host), "--ipc", "--controllers", "--theater-input", "--game-exe", profile.exe,
                                 "--until-game-exit", str(pid), "--channel-file", str(log_dir / "xr-channel.txt"),
                                 "--frame-channels", "3",
                                 *(["--loader", loader] if loader else [])],
                                cwd=str(host.parent), stdout=out, stderr=err, creationflags=0x08000000)
    finally:
        out.close()
        err.close()


@dataclass
class SessionEnv:
    """Everything run_session needs from the OS. Tests substitute an emulated game."""
    find_pids: Callable[[str], list[int]] = find_pids
    start_game: Callable[[GameProfile, Path, Report], None] = start_game
    start_xr_host: Callable[..., object] = start_xr_host
    sleep: Callable[[float], None] = time.sleep
    monotonic: Callable[[], float] = time.monotonic
    documents_dir: Callable[[], Path] = documents_dir


def run_session(profile: GameProfile, settings: Settings, install_root: Path, version_dir: Path,
                report: Report, wait_timeout: float = 180.0, env: SessionEnv | None = None) -> None:
    env = env or SessionEnv()
    game_dir = Path(settings["game_dir"] or profile.default_dir)
    journal = Journal(install_root / "session-journal.json")
    for note in restore_game_folder(journal):  # leftovers from an interrupted session
        report(note)

    info = preflight(profile, game_dir)
    if env.find_pids(profile.exe):
        raise SessionError("Battlefield 4 is already running. Close it, then start the VR session.")
    log_dir = install_root / "logs"
    log_dir.mkdir(parents=True, exist_ok=True)
    pipe = f"FrostbiteVR_{profile.id}_{os.getpid()}"
    stereo = settings["stereo"]
    route = settings["stereo_route"]
    if route == "nvapi" and not profile.stereo_proxy_name:
        route = "split"  # no shim for this game: the engine route is the only one
    text = session_text(adapter=version_dir / "bin" / "FrostbiteVR.Bf4.dll", log_dir=log_dir,
                        exe_sha256=info["sha256"], host_pipe=pipe, reload_mode=settings["reload_mode"],
                        developer=bool(settings["developer"]), stereo=stereo, stereo_route=route,
                        stereo_separation=float(settings["stereo_separation"]),
                        spoof_gpu_vendor=bool(settings["pretend_nvidia"]) and route == "nvapi",
                        research_dump_dir=(install_root / "research"
                                           if settings["developer"] and settings["research_dump_image"] else None))
    probe_file = log_dir / "stereo-probe.txt"
    probe_file.unlink(missing_ok=True)  # only this session's verdict counts
    section = SessionSection(profile.session_section, text)
    host = None
    try:
        # The nvapi64.dll shim is only for the 3D Vision route; split-screen needs nothing.
        extra = [version_dir / "bin" / "proxy" / profile.stereo_proxy_name] if stereo != "off" and route == "nvapi" else []
        # Stereo sessions also turn on BF4's own Stereoscopy option, key by key, restored after.
        profile_file = resolve_profile_settings_file(profile.profile_settings_file, env.documents_dir())
        keys = (profile_file, profile.stereo_profile_lines) if stereo != "off" and profile_file else None
        prepare_game_folder(profile, game_dir, version_dir / "bin" / "proxy" / profile.proxy_name,
                            bool(settings["apply_engine_cfg"]), journal, extra_proxies=extra, profile_keys=keys,
                            extra_cfg_lines=(profile.stereo_engine_cfg_lines.get(route, ()) if stereo != "off" else ()) +
                            (tuple(settings["developer_cfg_lines"]) if settings["developer"] else ()))
        for name in ("xr-channel.txt", "xr-channel-2.txt", "xr-channel-3.txt"):
            (log_dir / name).unlink(missing_ok=True)  # the host requires new token files
        launch_args = tuple(settings["launch_args"]) if settings["developer"] else ()
        if launch_args:
            env.start_game(profile, game_dir, report, launch_args)
        else:
            env.start_game(profile, game_dir, report)
        pid = _wait_for(env, lambda: env.find_pids(profile.exe), wait_timeout)
        if not pid:
            raise SessionError("Battlefield 4 did not start within 3 minutes.")
        loader = settings["xr_loader"] if settings["developer"] else ""
        host = (env.start_xr_host(version_dir, profile, pid, log_dir, report, loader) if loader
                else env.start_xr_host(version_dir, profile, pid, log_dir, report))
        report("Battlefield 4 is running. Put on the headset.")
        _check_swapchain(env, profile, log_dir / "bf4-adapter.log", report)
        while env.find_pids(profile.exe):
            env.sleep(2.0)
        report("Battlefield 4 closed.")
        if stereo != "off":
            report(stereo_summary(probe_file))
    finally:
        section.close()
        if host and host.poll() is None:
            host.terminate()
        for note in restore_game_folder(journal):
            report(note)
        report("Game folder restored.")


def stereo_summary(probe_file: Path) -> str:
    """One line for the launcher window from the adapter's stereo-probe.txt."""
    if not probe_file.exists():
        return "Stereo probe: no report was written (the adapter did not start, or BF4 closed at once)."
    lines = probe_file.read_text(encoding="utf-8", errors="replace").splitlines()
    verdict = next((l.split(":", 1)[1].strip() for l in lines if l.startswith("verdict:")), "unknown")
    nxt = next((l.split(":", 1)[1].strip() for l in lines if l.startswith("next:")), "")
    return f"Stereo probe: {verdict}. {nxt}".strip()


def _wait_for(env: SessionEnv, probe, timeout: float):
    end = env.monotonic() + timeout
    while env.monotonic() < end:
        hits = probe()
        if hits:
            return hits[0]
        env.sleep(1.0)
    return None


def _check_swapchain(env: SessionEnv, profile: GameProfile, log: Path, report: Report, timeout: float = 90.0) -> None:
    """Up to 90 s for the main menu; returns early once seen or if the game exits."""
    end = env.monotonic() + timeout
    while env.monotonic() < end:
        if log.exists() and "adopted swapchain" in log.read_text(encoding="utf-8", errors="replace")[-65536:]:
            return
        if not env.find_pids(profile.exe):
            return
        env.sleep(1.0)
    report(NO_SWAPCHAIN_HINT)
