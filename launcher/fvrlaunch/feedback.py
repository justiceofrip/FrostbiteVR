"""Manual feedback: build a sanitized report, save it as a small zip on the install
drive, and open a pre-filled GitHub issue for the player to review and submit.
Nothing is uploaded automatically and no credentials ship in the launcher."""
from __future__ import annotations

import getpass
import json
import os
import platform
import re
import time
import urllib.parse
import zipfile
from pathlib import Path

MAX_LOG_BYTES = 64 * 1024
MAX_URL_BODY = 5500  # stay well under browser/GitHub URL limits

_PATTERNS = [
    (re.compile(r"(?i)(gh[pousr]_[A-Za-z0-9]{20,}|github_pat_[A-Za-z0-9_]{20,})"), "<token>"),
    (re.compile(r"(?i)\b(token|secret|password|apikey|api_key)\s*[=:]\s*\S+"), r"\1=<redacted>"),
    (re.compile(r"[A-Za-z0-9._%+-]+@[A-Za-z0-9.-]+\.[A-Za-z]{2,}"), "<email>"),
    (re.compile(r"\b(?!127\.0\.0\.1\b)(?:\d{1,3}\.){3}\d{1,3}\b"), "<ip>"),
]


def _personal_terms() -> list[tuple[str, str]]:
    terms = []
    home = os.path.expanduser("~")
    if home and home != "~":
        terms.append((home, "%USERPROFILE%"))
    for value, label in ((os.environ.get("USERNAME") or _safe_user(), "<user>"),
                         (os.environ.get("COMPUTERNAME") or platform.node(), "<pc>")):
        if value and len(value) >= 3:
            terms.append((value, label))
    return terms


def _safe_user() -> str:
    try:
        return getpass.getuser()
    except Exception:  # noqa: BLE001 - best effort only
        return ""


def sanitize(text: str, extra: list[tuple[str, str]] | None = None) -> str:
    for needle, label in (extra or []) + _personal_terms():
        text = re.sub(re.escape(needle), label.replace("\\", "\\\\"), text, flags=re.IGNORECASE)
    for pattern, repl in _PATTERNS:
        text = pattern.sub(repl, text)
    return text


def _tail(path: Path, limit: int = MAX_LOG_BYTES) -> str:
    if not path.exists():
        return ""
    with path.open("rb") as fh:
        size = fh.seek(0, os.SEEK_END)
        fh.seek(max(0, size - limit))
        return fh.read().decode("utf-8", errors="replace")


SETTINGS_KEYS = ("game", "reload_mode", "apply_engine_cfg", "channel", "developer", "openxr_runtime")


def collect(summary: str, details: str, settings: dict, versions: dict, log_files: list[Path],
            extra_terms: list[tuple[str, str]] | None = None) -> dict:
    report = {
        "summary": summary.strip(),
        "details": details.strip(),
        "created": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
        "versions": versions,
        "system": {"os": platform.platform(), "python": platform.python_version()},
        "settings": {k: settings.get(k) for k in SETTINGS_KEYS},
        "logs": {p.name: _tail(p) for p in log_files},
    }
    return json.loads(sanitize(json.dumps(report), extra_terms))


def write_bundle(report: dict, out_dir: Path) -> Path:
    out_dir.mkdir(parents=True, exist_ok=True)
    path = out_dir / f"feedback-{time.strftime('%Y%m%d-%H%M%S')}.zip"
    with zipfile.ZipFile(path, "w", zipfile.ZIP_DEFLATED) as zf:
        zf.writestr("report.json", json.dumps({k: v for k, v in report.items() if k != "logs"}, indent=2))
        for name, text in report["logs"].items():
            zf.writestr(f"logs/{name}", text)
    return path


def issue_url(repo: str, report: dict, bundle: Path | None) -> str:
    log_excerpt = ""
    for name, text in report["logs"].items():
        lines = text.strip().splitlines()[-25:]
        if lines:
            log_excerpt += f"\n--- {name} (last {len(lines)} lines) ---\n" + "\n".join(lines)
    diagnostics = (
        f"versions: {json.dumps(report['versions'])}\nsystem: {report['system']['os']}\n"
        f"settings: {json.dumps(report['settings'])}\n{log_excerpt}"
    )[:MAX_URL_BODY]
    attach = f"\n\nAttach the saved bundle: {bundle.name}" if bundle else ""
    body = f"{report['details']}{attach}\n\n```\n{diagnostics}\n```"
    params = {
        "template": "vr-feedback.yml",
        "labels": "feedback,bf4",
        "title": f"[BF4 VR] {report['summary'] or 'Feedback'}"[:120],
        "summary": report["summary"],
        "details": report["details"] + attach,
        "diagnostics": diagnostics,
        "body": body,
    }
    return f"https://github.com/{repo}/issues/new?" + urllib.parse.urlencode(params)
