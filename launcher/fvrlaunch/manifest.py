"""Release manifest: one JSON per release, listing the package hash and every file's hash.

{
  "name": "FrostbiteVR-BF4", "version": "0.1.1", "channel": "prerelease",
  "min_launcher": "0.1.0", "notes": "...",
  "package": {"file": "FrostbiteVR-BF4-0.1.1.zip", "sha256": "...", "size": 123},
  "files": [{"path": "bin/FrostbiteVR.Bf4.dll", "sha256": "...", "size": 456}, ...]
}
"""
from __future__ import annotations

import hashlib
import json
from dataclasses import dataclass
from pathlib import Path, PurePosixPath

CHUNK = 1 << 20


class ManifestError(ValueError):
    pass


@dataclass(frozen=True)
class FileEntry:
    path: str
    sha256: str
    size: int


@dataclass(frozen=True)
class Manifest:
    name: str
    version: str
    channel: str
    min_launcher: str
    notes: str
    package_file: str
    package_sha256: str
    package_size: int
    files: tuple[FileEntry, ...]

    def file_map(self) -> dict[str, FileEntry]:
        return {f.path: f for f in self.files}


def sha256_file(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as fh:
        while chunk := fh.read(CHUNK):
            h.update(chunk)
    return h.hexdigest()


def safe_relpath(rel: str) -> str:
    """Reject absolute paths, drive letters and '..' (zip-slip)."""
    p = PurePosixPath(rel.replace("\\", "/"))
    if p.is_absolute() or ":" in rel or any(part in ("..", "") for part in p.parts):
        raise ManifestError(f"unsafe path in package: {rel!r}")
    return str(p)


def parse_version(v: str) -> tuple:
    core, _, pre = v.partition("-")
    nums = tuple(int(x) for x in core.split("."))
    # A pre-release sorts before its release: 0.2.0-hotfix1 < 0.2.0
    return nums + ((0, pre) if pre else (1, ""))


def load_manifest(text: str) -> Manifest:
    try:
        d = json.loads(text)
        pkg = d["package"]
        files = tuple(
            FileEntry(safe_relpath(f["path"]), str(f["sha256"]).lower(), int(f["size"])) for f in d["files"]
        )
        m = Manifest(
            name=d["name"], version=d["version"], channel=d.get("channel", "stable"),
            min_launcher=d.get("min_launcher", "0.0.0"), notes=d.get("notes", ""),
            package_file=pkg["file"], package_sha256=str(pkg["sha256"]).lower(), package_size=int(pkg["size"]),
            files=files,
        )
        parse_version(m.version)
    except (KeyError, TypeError, ValueError) as exc:
        raise ManifestError(f"invalid manifest: {exc}") from exc
    if len({f.path.lower() for f in files}) != len(files):
        raise ManifestError("duplicate paths in manifest")
    return m


def build_manifest(root: Path, name: str, version: str, channel: str, package: Path, notes: str = "",
                   min_launcher: str = "0.1.0") -> dict:
    files = []
    for p in sorted(root.rglob("*")):
        if p.is_file():
            rel = p.relative_to(root).as_posix()
            files.append({"path": rel, "sha256": sha256_file(p), "size": p.stat().st_size})
    return {
        "name": name, "version": version, "channel": channel, "min_launcher": min_launcher, "notes": notes,
        "package": {"file": package.name, "sha256": sha256_file(package), "size": package.stat().st_size},
        "files": files,
    }
