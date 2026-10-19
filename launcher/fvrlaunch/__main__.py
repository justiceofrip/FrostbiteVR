"""python -m fvrlaunch [ui|update|rollback|cleanup|status|install-local] [--root DIR]

install-local installs a locally built package (tools/release/make_release.py output)
without GitHub:  python -m fvrlaunch install-local dist/X.manifest.json dist/X.zip --root G:\\FrostbiteVR"""
from __future__ import annotations

import argparse
import shutil
import sys
from pathlib import Path

from .games import load_profile
from .session import Journal, restore_game_folder
from .settings import Settings
from .storage import redirect_temp
from .updater import Installer, list_releases


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(prog="fvrlaunch")
    ap.add_argument("command", nargs="?", default="ui",
                    choices=["ui", "update", "rollback", "cleanup", "status", "install-local"])
    ap.add_argument("files", nargs="*", type=Path, help="install-local: MANIFEST ZIP")
    ap.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[2])
    args = ap.parse_args(argv)
    root: Path = args.root
    redirect_temp(root)  # this process and its children only
    installer = Installer(root)
    for note in restore_game_folder(Journal(root / "session-journal.json")):
        print(note)

    if args.command == "ui":
        from .ui import LauncherWindow  # tkinter only when needed
        LauncherWindow(root).run()
    elif args.command == "update":
        settings = Settings.load(root / "settings.json")
        profile = load_profile(settings["game"])
        pick = installer.pick_update(list_releases(profile.release_repo, installer.fetch, installer.tmp), settings["channel"])
        print(installer.install_from_urls(pick.manifest_url, pick.package_url) if pick else "up to date")
    elif args.command == "install-local":
        if len(args.files) != 2:
            ap.error("install-local needs MANIFEST ZIP")
        local = Installer(root, fetch=lambda src, dest: shutil.copyfile(src, dest))
        print(local.install_from_urls(str(args.files[0]), str(args.files[1])))
    elif args.command == "rollback":
        print(installer.rollback())
    elif args.command == "status":
        print(installer.state())
    return 0


if __name__ == "__main__":
    sys.exit(main())
