"""Check sampled VS constants against each eye and mixed view/projection controls.

Requires --PassEvidence capture. Unknown shader layouts remain unknown; this
checks the first sampled constant block, not every draw or headset acceptance.
"""
import argparse
import json
from pathlib import Path
import numpy as np
from analyze_eye_projection import decode_geometry_camera


def camera_cases(trace):
    views, projections, cameras = [], [], []
    for eye in range(2):
        data = np.fromfile(trace / f"eye-{eye}-step-0-camera-0.bin", dtype="<f4")
        def matrix(offset):
            return data[offset//4:offset//4+16].reshape(4, 4).T.astype(np.float64)
        views.append(matrix(0x220))
        projections.append(matrix(0x2e0))
        cameras.append(matrix(0x3a0))
    return cameras + [projections[1] @ views[0], projections[0] @ views[1]]


def analyze(trace):
    rows = json.loads((trace / "pass-buffer-evidence.json").read_text())
    cases = camera_cases(trace)
    checked, unknown, unavailable = [], [], []
    for row in rows:
        slot = next((b for b in row["buffers"] if b["slot"] == 0), None)
        path = trace / f"pass-{row['eye']}-{row['pass']}-cb-0.bin"
        identity = {k: row[k] for k in ("eye", "pass", "sequence", "kind", "count", "vs", "ps")}
        if not row["complete"] or not slot or not slot["done"] or not path.exists():
            unavailable.append(identity)
            continue
        decoded = decode_geometry_camera(np.fromfile(path, dtype="<f4"), cases)
        item = dict(identity, **decoded)
        (unknown if decoded["matches_eye_camera"] is None else checked).append(item)
    wrong = [r for r in checked if r["matches_eye_camera"] != r["eye"]]
    overflow = max((r["overflow"] for r in rows), default=0)
    status = "fail" if wrong else "inconclusive" if not checked or overflow or unavailable else "pass"
    return dict(status=status, scope="recognized sampled VS constant layouts; unknown layouts excluded",
                headset_verified=False, total_samples=len(rows), recognized=len(checked),
                unknown=len(unknown), unavailable=len(unavailable), overflow=overflow,
                wrong_eye_samples=wrong, checked=checked, unknown_samples=unknown)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("trace", type=Path)
    args = parser.parse_args()
    result = analyze(args.trace)
    (args.trace / "pass-projection-check.json").write_text(json.dumps(result, indent=2)+"\n")
    print(json.dumps({k:v for k,v in result.items() if k not in ("checked", "unknown_samples")}, indent=2))
    raise SystemExit({"pass":0, "fail":2, "inconclusive":3}[result["status"]])
