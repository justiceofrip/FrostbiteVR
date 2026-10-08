"""Saved raw rig evidence only; no process access, asset writes, or visibility edits."""
import argparse
import bisect
import collections
import hashlib
import json
import math
from pathlib import Path

FIELDS = ("current", "next", "loaded", "reserve")

def rigid(matrix):
    if not isinstance(matrix, list) or len(matrix) != 16 or not all(isinstance(v, (float, int)) and math.isfinite(v) for v in matrix):
        return False, None
    error = max(abs(matrix[r*4+c] - (1 if r == 3 else 0)) for r in range(4) for c in (3,))
    gram = max(abs(sum(matrix[i*4+k]*matrix[j*4+k] for k in range(3))-(i == j)) for i in range(3) for j in range(i, 3))
    a,b,c,_,d,e,f,_,g,h,i,*_ = matrix
    det = a*(e*i-f*h)-b*(d*i-f*g)+c*(d*h-e*g)
    return error <= 1e-5 and gram <= 1e-4 and det > 0, gram

def owner_matches(boundary, sample):
    return boundary.get("soldier") == sample.get("actor") and boundary.get("weapon") == sample.get("weapon") and \
        boundary.get("actor_generation") == sample.get("owner_generation") and boundary.get("space") == sample.get("space")

def audit(path):
    raw = path.read_bytes()
    data = json.loads(raw.decode("utf-8-sig"))
    game = data.get("gameplay", {})
    publication = game.get("rig_publication", {})
    samples = [s for s in publication.get("weapon_profile_samples", []) if s.get("asset_name") == "SPAS12_sp"]
    events = game.get("reload_flow", {}).get("records", [])
    updates = [e for e in events if e.get("kind") == 0 and e.get("finished") and e.get("identity_retained") and e.get("before") and e.get("after")]
    branch_rows = []
    for branch in range(3):
        rows = [e for e in updates if e["before"].get("branch") == branch]
        before = sorted(rows, key=lambda e:e["end_tick_ms"])
        after = sorted(rows, key=lambda e:e["begin_tick_ms"])
        branch_rows.append((before, [e["end_tick_ms"] for e in before], after, [e["begin_tick_ms"] for e in after]))
    hold_events = [e for e in updates if e.get("hold_applied") and e.get("hold_restored")]
    transfers = [e for e in events if e.get("kind") == 2 and e.get("finished") and e.get("identity_retained") and e.get("before") and e.get("after") and
        e["after"].get("loaded", 0) == e["before"].get("loaded", 0)+1 and e["after"].get("reserve", 0) == e["before"].get("reserve", 0)-1]
    last_transfer = max((e["end_tick_ms"] for e in transfers), default=None)
    counts = collections.Counter()
    rows = []
    for sample in samples:
        shells = [b for b in sample.get("native_weapon_bones", []) if b.get("name") == "jntWpn_7"]
        row = {"capture_sequence":sample.get("capture_sequence"), "captured_ms":sample.get("captured_ms"), "input_generation":sample.get("generation")}
        if not sample.get("weapon_bones_complete") or len(shells) != 1 or type(shells[0].get("hidden")) is not bool:
            row.update(visibility="unavailable", phase="unknown")
            rows.append(row);counts["unavailable"] += 1;continue
        bone = shells[0]
        proper, error = rigid(bone.get("native"))
        visibility = "native_hidden" if bone["hidden"] else "rigid_nonhidden" if proper else "nonrigid_nonhidden"
        row.update(visibility=visibility, basis_gram_error=error)
        states, held = [], []
        tick = sample["captured_ms"]
        gaps = []
        for before, ends, after, begins in branch_rows:
            at = bisect.bisect_right(ends, tick)-1
            bt = bisect.bisect_left(begins, tick)
            if at < 0 or bt >= len(after):
                break
            left,right = before[at],after[bt]
            if tick-left["end_tick_ms"] > 40 or right["begin_tick_ms"]-tick > 40:
                break
            l,r = left["after"],right["before"]
            if not owner_matches(l, sample) or not owner_matches(r, sample) or any(l.get(k) != r.get(k) for k in FIELDS):
                break
            states.append(tuple(l[k] for k in FIELDS))
            held.append(bool(left.get("hold_applied") and left.get("hold_restored") and right.get("hold_applied") and right.get("hold_restored")))
            gaps.append([tick-left["end_tick_ms"],right["begin_tick_ms"]-tick])
        phase = "unknown"
        if len(states) == 3 and len(set(states)) == 1:
            current,next_state,loaded,reserve = states[0]
            row.update(native_state=current, native_next=next_state, loaded=loaded, reserve=reserve, bracket_gaps_ms=gaps)
            if current == 11 and next_state == 12 and all(held):
                phase = "held_reload_bracket"
            elif current in (1,2) and next_state in (1,2):
                phase = "idle_after_last_transfer" if last_transfer is not None and tick > last_transfer else "idle_before_transfer"
            elif current in (10,11,12):
                phase = "ordinary_reload_bracket"
            else:
                phase = "other_native_phase"
        row["phase"] = phase
        counts[visibility] += 1
        counts[phase+"/"+visibility] += 1
        rows.append(row)
    return {"trace":str(path), "trace_sha256":hashlib.sha256(raw).hexdigest(),
        "native_calls":False, "native_visibility_modified":False,
        "interpretation":"hidden flag and rigid basis are exact raw pre-IK rig observations; phase uses consistent surrounding native reads, not an atomic rig/firing snapshot; this is not pixel visibility proof",
        "phase_bracket_max_gap_ms":40, "spas_samples":len(samples), "counts":dict(sorted(counts.items())),
        "hidden_leaf_poses":publication.get("hidden_leaf_poses"), "tracked_poses":publication.get("tracked_poses"),
        "capture_range_ms":[min((s["captured_ms"] for s in samples), default=None),max((s["captured_ms"] for s in samples), default=None)],
        "held_range_ms":[min((e["begin_tick_ms"] for e in hold_events), default=None),max((e["end_tick_ms"] for e in hold_events), default=None)],
        "verified_transfer_times_ms":[e["end_tick_ms"] for e in transfers],
        "raw_contact_consumer":publication.get("reload_presentation"), "samples":rows}

def main():
    parser=argparse.ArgumentParser()
    parser.add_argument("--trace", type=Path, action="append", required=True)
    parser.add_argument("--out", type=Path, required=True)
    args=parser.parse_args()
    if len(args.trace)>8:
        parser.error("bounded to eight explicit traces")
    report={"schema":"fvr.bc2.shell_native_visibility.v1", "reports":[audit(p) for p in args.trace]}
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(report, indent=2)+"\n", encoding="utf-8")
    for result in report["reports"]:
        print(Path(result["trace"]).parent.name, result["counts"])

if __name__ == "__main__":
    main()
