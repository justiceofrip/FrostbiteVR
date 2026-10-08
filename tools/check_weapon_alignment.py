"""Validate saved authored grip and rendered aim evidence; no process access."""
import argparse, json, math
from pathlib import Path
import numpy as np

def read(folder):
    folder = Path(folder)
    d = json.loads((folder / "native-trace.json").read_text())
    done = json.loads((folder / "completion.json").read_text())
    assert d["hooks_disabled"] and done["bootstrap_exit"] == 0 and done["game_responding"]
    assert not done["game_exited"] and not done["new_crash_report"]
    rig = d["gameplay"]["rig_publication"]
    assert not any(rig[k] for k in ("source_changes", "packing_failures", "fallback_failures"))
    return d, rig["hand_evidence"]

def matrix(row, key):
    return np.array(row[key], dtype=float).reshape(4, 4)

def relation(row, authored=False):
    return matrix(row, "native_right_wrist" if authored else "right_wrist_matrix") @ np.linalg.inv(matrix(row, "native" if authored else "placed"))

def angle(a, b):
    return math.degrees(math.acos(float(np.clip(np.dot(a,b)/np.linalg.norm(a)/np.linalg.norm(b), -1, 1))))

def check_switch(folder):
    d, rows = read(folder)
    parts = []
    for row in rows:
        if not parts or parts[-1][-1]["weapon"] != row["weapon"]:
            parts.append([])
        parts[-1].append(row)
    assert len(parts) == 3 and parts[0][0]["weapon"] == parts[2][0]["weapon"]
    assert parts[0][0]["weapon"] != parts[1][0]["weapon"]
    maximum = change = 0.
    for index, part in enumerate(parts):
        settled = [r for r in part if not r["attachment_pending"]]
        # The fixture switches one second after startup; hook warm-up can leave
        # that first fragment entirely pending. Both actual equip transitions
        # must still settle, and all available settled samples are checked.
        assert any(r["attachment_pending"] for r in part)
        if index: assert settled
        for row in settled:
            error = np.linalg.norm(relation(row)[3,:3] - relation(row, True)[3,:3])
            maximum = max(maximum, float(error))
        change = max(change, float(np.linalg.norm(relation(part[0],True)[3,:3] - relation(part[-1],True)[3,:3])))
    assert maximum < .001 and change > .07
    assert d["gameplay"]["next_weapon_commands"] == d["gameplay"]["previous_weapon_commands"] == 1
    return dict(native_report=str(folder), weapon_sequence=[p[0]["weapon"] for p in parts], max_settled_grip_error_m=maximum, delayed_authored_attachment_change_m=change)

def check_support(folder):
    d, rows = read(folder)
    attached = [r for r in rows if r["support_attached"]]
    free = [r for r in rows if not r["support_attached"] and r["arms"][0]["tracked"]]
    assert len(attached) > 10 and len(free) > 10
    errors = []
    for row in attached:
        expected = matrix(row, "native_left_wrist") @ np.linalg.inv(matrix(row, "native")) @ matrix(row, "placed")
        errors.append(math.dist(expected[3,:3], row["arms"][0]["resolved"]))
    free_errors = []
    snap_distances = []
    for row in rows:
        if not row["arms"][0]["tracked"]: continue
        p = row["local_grips"][0]
        raw = np.array([p[0],p[1],-p[2],1.]) @ matrix(row, "eye_base")
        delta = math.dist(raw[:3], row["arms"][0]["resolved"])
        (snap_distances if row["support_attached"] else free_errors).append(delta)
    assert max(errors) < .001 and max(free_errors) < .001
    assert max(snap_distances) > .01
    shots = [r for r in d["gameplay"]["fire_origin_observation"]["records"] if r["phase"] == 2 and r.get("tracked_shot_candidate")]
    angles = [angle(-matrix(r["tracked_shot_candidate"],"tracked_weapon")[2,:3], -matrix(r["tracked_shot_candidate"],"mapped_shot")[2,:3]) for r in shots]
    if angles: assert angles[0] < .25 and max(angles) < 5
    return dict(native_report=str(folder), attached_samples=len(attached), free_samples=len(free), max_authored_support_error_m=max(errors), max_free_hand_error_m=max(free_errors), max_visible_snap_distance_m=max(snap_distances), firing_measured=bool(angles), first_shot_barrel_error_degrees=angles[0] if angles else None, burst_recoil_spread_max_degrees=max(angles) if angles else None)

def check_recovery(folder):
    _, rows = read(folder)
    settled = [r for r in rows if not r["attachment_pending"]]
    assert len({r["space"] for r in settled}) == 3
    first = relation(settled[0])
    drift = max(float(np.linalg.norm(relation(r)[3,:3] - first[3,:3])) for r in settled)
    angular = max(angle(-matrix(r,"placed")[2,:3], matrix(r,"eye_base")[2,:3]) for r in settled)
    assert drift < .001 and angular < .05
    return dict(native_report=str(folder), spaces=sorted({r["space"] for r in settled}), max_grip_drift_across_recenter_m=drift, max_barrel_direction_error_degrees=angular)

if __name__ == "__main__":
    p = argparse.ArgumentParser()
    p.add_argument("--switch"); p.add_argument("--support"); p.add_argument("--recovery"); p.add_argument("--output")
    a = p.parse_args()
    result = {"headset_tested":False, "passed":True}
    for name, fn in (("switch",check_switch),("support",check_support),("recovery",check_recovery)):
        if getattr(a,name): result[name] = fn(Path(getattr(a,name)))
    text = json.dumps(result, indent=2)
    if a.output: Path(a.output).write_text(text+"\n", encoding="utf8")
    print(text)
