"""Storage policy: everything (downloads, staging, temp, logs) stays on the install
drive. Only the launcher process and its children get TEMP/TMP redirected; Windows-wide
settings are never touched."""
from __future__ import annotations

import os
import shutil
from pathlib import Path

MIN_FREE_BYTES = 512 * 1024 * 1024
SLACK_BYTES = 128 * 1024 * 1024


class StorageError(RuntimeError):
    pass


def ensure_space(directory: Path, needed_bytes: int) -> None:
    directory.mkdir(parents=True, exist_ok=True)
    free = shutil.disk_usage(directory).free
    required = max(MIN_FREE_BYTES, needed_bytes + SLACK_BYTES)
    if free < required:
        raise StorageError(
            f"Not enough space on {directory.anchor or directory}: "
            f"{free // 2**20} MiB free, {required // 2**20} MiB needed."
        )


def redirect_temp(install_root: Path) -> Path:
    tmp = install_root / "tmp"
    tmp.mkdir(parents=True, exist_ok=True)
    for var in ("TEMP", "TMP", "TMPDIR"):
        os.environ[var] = str(tmp)
    return tmp


def tree_size(path: Path) -> int:
    total = 0
    for p in path.rglob("*"):
        if p.is_file():
            total += p.stat().st_size
    return total
