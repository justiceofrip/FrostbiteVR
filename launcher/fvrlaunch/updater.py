"""Updates from GitHub Releases with verify-then-swap and one-step rollback.

Layout under the install root (all on the install drive):
  versions/<version>/...   immutable installed versions (at most current + previous)
  state.json               {"current": "...", "previous": "..."}  (atomic replace)
  tmp/                     downloads + staging, emptied after each operation

Disk thrift: files identical to the current version are hard-linked instead of written,
so a hotfix that touches one DLL costs one DLL of space. Only two versions are kept.
"""
from __future__ import annotations

import json
import os
import shutil
import urllib.request
import zipfile
from dataclasses import dataclass
from pathlib import Path
from typing import Callable

from . import __version__
from .manifest import Manifest, ManifestError, load_manifest, parse_version, safe_relpath, sha256_file
from .storage import ensure_space

Fetch = Callable[[str, Path], None]  # download url -> file


class UpdateError(RuntimeError):
    pass


@dataclass
class ReleaseInfo:
    version: str
    prerelease: bool
    manifest_url: str
    package_url: str


def http_fetch(url: str, dest: Path) -> None:
    req = urllib.request.Request(url, headers={"User-Agent": f"FrostbiteVR-Launcher/{__version__}",
                                               "Accept": "application/octet-stream"})
    with urllib.request.urlopen(req, timeout=60) as resp, dest.open("wb") as out:
        shutil.copyfileobj(resp, out, 1 << 20)


def list_releases(repo: str, fetch: Fetch, tmp: Path) -> list[ReleaseInfo]:
    dest = tmp / "releases.json"
    fetch(f"https://api.github.com/repos/{repo}/releases?per_page=20", dest)
    out = []
    for rel in json.loads(dest.read_text(encoding="utf-8")):
        if rel.get("draft"):
            continue
        assets = {a["name"]: a["browser_download_url"] for a in rel.get("assets", [])}
        man = next((n for n in assets if n.endswith(".manifest.json")), None)
        pkg = next((n for n in assets if n.endswith(".zip")), None)
        if man and pkg:
            out.append(ReleaseInfo(rel["tag_name"].lstrip("v"), bool(rel.get("prerelease")), assets[man], assets[pkg]))
    dest.unlink(missing_ok=True)
    return out


class Installer:
    def __init__(self, root: Path, fetch: Fetch = http_fetch):
        self.root = root
        self.fetch = fetch
        self.versions = root / "versions"
        self.tmp = root / "tmp"
        self.state_path = root / "state.json"

    # ---- state -------------------------------------------------------------------
    def state(self) -> dict:
        if self.state_path.exists():
            return json.loads(self.state_path.read_text(encoding="utf-8"))
        return {"current": None, "previous": None}

    def _write_state(self, state: dict) -> None:
        tmp = self.state_path.with_suffix(".tmp")
        tmp.write_text(json.dumps(state, indent=2), encoding="utf-8")
        os.replace(tmp, self.state_path)

    def current_dir(self) -> Path | None:
        cur = self.state().get("current")
        return self.versions / cur if cur else None

    # ---- update --------------------------------------------------------------------
    def pick_update(self, releases: list[ReleaseInfo], channel: str) -> ReleaseInfo | None:
        current = self.state().get("current")
        allowed = [r for r in releases if channel == "prerelease" or not r.prerelease]
        newer = [r for r in allowed if current is None or parse_version(r.version) > parse_version(current)]
        return max(newer, key=lambda r: parse_version(r.version), default=None)

    def install_from_urls(self, manifest_url: str, package_url: str) -> str:
        self.tmp.mkdir(parents=True, exist_ok=True)
        man_path = self.tmp / "incoming.manifest.json"
        self.fetch(manifest_url, man_path)
        manifest = load_manifest(man_path.read_text(encoding="utf-8"))
        if parse_version(manifest.min_launcher) > parse_version(__version__):
            raise UpdateError(f"This update needs launcher {manifest.min_launcher}; run the new Setup first.")
        ensure_space(self.root, manifest.package_size * 2 + sum(f.size for f in manifest.files))
        pkg_path = self.tmp / manifest.package_file
        try:
            self.fetch(package_url, pkg_path)
            return self.install_package(manifest, pkg_path)
        finally:
            pkg_path.unlink(missing_ok=True)
            man_path.unlink(missing_ok=True)

    def install_package(self, manifest: Manifest, package: Path) -> str:
        if sha256_file(package) != manifest.package_sha256:
            raise UpdateError("Downloaded package does not match its manifest hash; nothing was changed.")
        target = self.versions / manifest.version
        if target.exists():
            raise UpdateError(f"Version {manifest.version} is already installed.")
        staging = self.tmp / f"staging-{manifest.version}"
        shutil.rmtree(staging, ignore_errors=True)
        staging.mkdir(parents=True)
        current = self.current_dir()
        current_hashes = self._hash_index(current) if current else {}
        expected = manifest.file_map()
        try:
            with zipfile.ZipFile(package) as zf:
                names = [n for n in zf.namelist() if not n.endswith("/")]
                for n in names:
                    safe_relpath(n)
                if {safe_relpath(n) for n in names} != set(expected):
                    raise UpdateError("Package contents do not match the manifest file list.")
                for rel, entry in expected.items():
                    dest = staging / rel
                    dest.parent.mkdir(parents=True, exist_ok=True)
                    reuse = current_hashes.get(rel)
                    if reuse and reuse[1] == entry.sha256:
                        os.link(reuse[0], dest)  # same bytes: share them
                        continue
                    with zf.open(rel) as src, dest.open("wb") as out:
                        shutil.copyfileobj(src, out, 1 << 20)
                    if sha256_file(dest) != entry.sha256:
                        raise UpdateError(f"{rel} failed verification; nothing was changed.")
            self.versions.mkdir(parents=True, exist_ok=True)
            os.replace(staging, target)
        except (zipfile.BadZipFile, ManifestError) as exc:
            raise UpdateError(f"Package rejected: {exc}") from exc
        finally:
            shutil.rmtree(staging, ignore_errors=True)
        state = self.state()
        self._write_state({"current": manifest.version, "previous": state.get("current")})
        (target / ".manifest.json").write_text(json.dumps({"version": manifest.version}), encoding="utf-8")
        self.prune()
        return manifest.version

    def _hash_index(self, root: Path) -> dict[str, tuple[Path, str]]:
        index = {}
        for p in root.rglob("*"):
            if p.is_file() and p.name != ".manifest.json":
                index[p.relative_to(root).as_posix()] = (p, sha256_file(p))
        return index

    # ---- rollback / prune --------------------------------------------------------------
    def rollback(self) -> str:
        state = self.state()
        prev = state.get("previous")
        if not prev or not (self.versions / prev).exists():
            raise UpdateError("No previous version to roll back to.")
        self._write_state({"current": prev, "previous": state.get("current")})
        return prev

    def prune(self) -> None:
        state = self.state()
        keep = {state.get("current"), state.get("previous")}
        if self.versions.exists():
            for d in self.versions.iterdir():
                if d.is_dir() and d.name not in keep:
                    shutil.rmtree(d, ignore_errors=True)
