"""Prepare BC2's native next-launch window mode; never launch or modify a process.

Default is a read-only plan. --apply requires BC2 to be closed, creates an exact
backup, then changes only [WindowSettings] Fullscreen to false. Runtime process
checks bound the operation but cannot lock out a concurrent external game launch.
"""
from __future__ import annotations
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import tempfile
from datetime import datetime, timezone


class PreparationError(RuntimeError):
    pass


def planned_bytes(raw: bytes) -> tuple[bytes, str, bool]:
    """Return replacement bytes, codec and whether the value changes."""
    bom = b""
    for mark, codec in ((b"\xef\xbb\xbf", "utf-8"), (b"\xff\xfe", "utf-16-le"), (b"\xfe\xff", "utf-16-be")):
        if raw.startswith(mark):
            bom, encoding = mark, codec
            break
    else:
        encoding = "utf-8"
    body = raw[len(bom):]
    try:
        text = body.decode(encoding)
    except UnicodeDecodeError:
        if bom:
            raise PreparationError("Invalid BOM-marked settings encoding")
        encoding = "cp1252"
        try:
            text = body.decode(encoding)
        except UnicodeDecodeError as exc:
            raise PreparationError("Unsupported settings encoding") from exc
    if text.encode(encoding) != body or "\0" in text:
        raise PreparationError("Settings encoding is not losslessly supported")
    lines = text.splitlines(keepends=True)
    in_window = False
    sections = 0
    found = []
    for i, line in enumerate(lines):
        content = line.rstrip("\r\n")
        section = re.fullmatch(r"\s*\[([^\]]+)\]\s*(?:[;#].*)?", content)
        if section:
            in_window = section[1].strip().casefold() == "windowsettings"
            sections += int(in_window)
            continue
        if not in_window or not re.match(r"^\s*Fullscreen\s*=", content, re.I):
            continue
        match = re.fullmatch(r"([ \t]*Fullscreen[ \t]*=[ \t]*)(true|false)([ \t]*(?:[;#].*)?)(\r\n|\r|\n)?", line, re.I)
        if not match:
            raise PreparationError("Fullscreen must be one unambiguous true/false value")
        found.append((i, match))
    if sections != 1 or len(found) != 1:
        raise PreparationError("Expected exactly one WindowSettings section and one Fullscreen key")
    i, match = found[0]
    if match[2].casefold() == "false":
        return raw, encoding, False
    lines[i] = match[1] + "false" + match[3] + (match[4] or "")
    return bom + "".join(lines).encode(encoding), encoding, True


def validate_path(path: Path, expected: Path) -> Path:
    expected = expected.absolute()
    if os.path.normcase(str(path.absolute())) != os.path.normcase(str(expected)):
        raise PreparationError("Refusing a settings path other than the exact approved BC2 settings.ini")
    if path.is_symlink() or not path.is_file() or path.resolve() != expected:
        raise PreparationError("Settings must be the original regular file, with no redirected path")
    return expected


def running_bc2() -> list[dict]:
    if os.name != "nt":
        raise PreparationError("Live process validation requires Windows")
    result = subprocess.run(
        ["powershell.exe", "-NoProfile", "-NonInteractive", "-Command",
         "$ErrorActionPreference='Stop'; @(Get-CimInstance Win32_Process -Filter \"Name='BFBC2Game.exe'\") | "
         "Select-Object ProcessId,Name | ConvertTo-Json -Compress"],
        capture_output=True, text=True, timeout=15, creationflags=subprocess.CREATE_NO_WINDOW)
    if result.returncode or result.stderr.strip():
        raise PreparationError("Could not prove BC2 is closed")
    try:
        data = json.loads(result.stdout) if result.stdout.strip() else []
    except json.JSONDecodeError as exc:
        raise PreparationError("Invalid process validation response") from exc
    records = data if isinstance(data, list) else [data]
    if any(not isinstance(r, dict) or r.get("Name", "").casefold() != "bfbc2game.exe"
           or not isinstance(r.get("ProcessId"), int) or r["ProcessId"] <= 0 for r in records):
        raise PreparationError("Unexpected process validation response")
    return records


def prepare(path: Path, expected: Path, process_reader, apply: bool = False) -> dict:
    def closed():
        active = process_reader()
        if active:
            raise PreparationError("BC2 is running; close it normally before preparing the next launch")
    closed()
    path = validate_path(path, expected)
    original = path.read_bytes()
    replacement, encoding, changed = planned_bytes(original)
    report = {
        "schema": "fvr.bc2.windowed_launch_preparation", "schema_version": 1,
        "utc": datetime.now(timezone.utc).isoformat(), "settings": str(path),
        "original_sha256": hashlib.sha256(original).hexdigest(),
        "prepared_sha256": hashlib.sha256(replacement).hexdigest(),
        "encoding": encoding, "change_needed": changed, "applied": False,
        "backup": None, "game_launched": False,
        "limitation": "Next-launch preparation; desktop visibility still requires native validation."
    }
    if not changed or not apply:
        return report
    closed()
    if validate_path(path, expected).read_bytes() != original:
        raise PreparationError("Settings changed during preparation")
    stamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S.%fZ")
    backup = path.with_name(path.name + ".bc2vr-windowed-" + stamp + ".bak")
    with backup.open("xb") as stream:
        stream.write(original)
        stream.flush()
        os.fsync(stream.fileno())
    report["backup"] = str(backup)
    temporary = None
    try:
        with tempfile.NamedTemporaryFile(mode="wb", dir=path.parent, prefix=path.name + ".bc2vr-", suffix=".tmp", delete=False) as stream:
            temporary = Path(stream.name)
            stream.write(replacement)
            stream.flush()
            os.fsync(stream.fileno())
        closed()
        if validate_path(path, expected).read_bytes() != original:
            raise PreparationError("Settings changed before atomic replacement; backup retained")
        os.replace(temporary, path)
        temporary = None
        if path.read_bytes() != replacement:
            raise PreparationError("Settings verification failed after replacement; original backup retained")
        report["applied"] = True
        return report
    finally:
        if temporary is not None:
            temporary.unlink(missing_ok=True)


def main() -> int:
    expected = Path.home() / "Documents" / "BFBC2" / "settings.ini"
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--settings", type=Path, default=expected,
                        help="Must be the exact current user's Documents/BFBC2/settings.ini")
    parser.add_argument("--apply", action="store_true", help="Apply after BC2 is closed; otherwise print a read-only plan")
    args = parser.parse_args()
    try:
        print(json.dumps(prepare(args.settings, expected, running_bc2, args.apply), indent=2))
        return 0
    except (PreparationError, OSError, subprocess.SubprocessError) as exc:
        print(json.dumps({"error": str(exc), "applied": None, "game_launched": False}))
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
