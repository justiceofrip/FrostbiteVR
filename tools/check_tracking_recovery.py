"""Check recorded BC2 tracking-recovery evidence without process access."""
import json, math, argparse
from pathlib import Path

def check(folder):
    folder = Path(folder)
    read = lambda p: json.loads(p.read_text(encoding="utf-8-sig"))
    links = read(folder / "reports.json")
    native = Path(links["native_report"])
    d, h = read(native / "native-trace.json"), read(folder / "host.json")
    done = read(native / "completion.json")
    def require(ok, message):
        if not ok:
            raise ValueError(message)
    require(d["hooks_disabled"] and d["state"] == "observed", "native cleanup failed")
    require(done["bootstrap_exit"] == 0 and not done["game_exited"] and not done["new_crash_report"] and done["game_responding"], "game stability failed")
    require(h["user_presence_supported"] and h["user_presence_events"] == 3, "presence removal/return not observed")
    require(h["recenters"] == 2 and h["automatic_recenters"] == 1, "automatic/manual recenter not both observed")
    require(h["submitted_pairs"] >= 240 and h["rejected_pairs"] == 0 and h["async_timeouts"] == 0, "stereo delivery failed")
    s, g = d["native_stream"], d["gameplay"]
    require(not any(s[k] for k in ["camera_restore_failures", "gpu_failure_stage", "gpu_failure_code"]), "native render error")
    # Old-generation pairs may be discarded when recenter cancels pending work.
    r = g["rig_publication"]
    require(r["calibrations"] == 3 and r["torso_stabilized_poses"] == r["tracked_poses"] and r["tracked_poses"] > 100, "incomplete torso/recenter coverage")
    require(not any(r[k] for k in ["source_changes", "packing_failures", "fallback_failures"]), "native palette error")
    rows = r["hand_evidence"]
    spaces = sorted({x["space"] for x in rows})
    require(len(spaces) == 3 and {x["owner_generation"] for x in rows} == {1}, "reference or actor identity incorrect")
    views = g["view_basis_evidence"]
    require(len(views) >= 100 and all(x["shared_eye"] for x in views), "camera did not use shared eye origin")
    target_error = resolved_error = local_error = 0.
    for row in rows:
        b = row["eye_base"]
        require(math.dist([b[12], b[14]], [row["actor_position"][0], row["actor_position"][2]]) < .0002, "eye base has horizontal body offset")
        for side, arm in enumerate(row["arms"]):
            require(arm["tracked"], "missing tracked arm")
            p = row["local_grips"][side]
            local_error = max(local_error, math.dist(p, [-.25 if side == 0 else .25, -.45, -.4]))
            expected = [b[12+i]+p[0]*b[i]+p[1]*b[4+i]-p[2]*b[8+i] for i in range(3)]
            target_error = max(target_error, math.dist(expected, arm["target"]))
            resolved_error = max(resolved_error, math.dist(arm["target"], arm["resolved"]))
            require(arm["error"] < .001, "wrist reach clamped")
    require(local_error < .00001 and target_error < .0002 and resolved_error < .0005, "hand alignment or reconnect continuity failed")
    fire = g["fire_origin_observation"]
    require(fire["client_origin_writes"] == 0 and fire["server_origin_writes"] == 0, "zero-fire recovery fixture fired")
    watch = read(folder / "exception-watch" / "completion.json")
    require(watch["exceptions"] == 0 and watch["detached"] and watch["game_running"], "exception observation failed")
    return dict(passed=True, headset_tested=False, native_report=str(native), host_report=str(folder),
                automatic_recenters=h["automatic_recenters"], total_recenters=h["recenters"],
                reference_spaces=spaces, actor_generation=1, hand_records=len(rows),
                maximum_local_grip_error_m=local_error, maximum_target_error_m=target_error,
                maximum_resolved_wrist_error_m=resolved_error, shared_eye_views=len(views),
                fresh_pairs=h["submitted_pairs"], cancelled_pairs=s["discarded"],
                native_sources_unchanged=True, exceptions=0)

if __name__ == "__main__":
    p = argparse.ArgumentParser()
    p.add_argument("folder")
    p.add_argument("--output")
    a = p.parse_args()
    result = json.dumps(check(a.folder), indent=2)
    if a.output:
        Path(a.output).write_text(result+"\n", encoding="utf8")
    print(result)
