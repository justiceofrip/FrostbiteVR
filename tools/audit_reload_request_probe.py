"""Strict bounded external-request fixture audit; no process access."""
import argparse
import json
from pathlib import Path


def audit(trace):
    errors = []
    def require(ok, message):
        if not ok:
            errors.append(message)
    gameplay = trace.get("gameplay", {})
    probe = gameplay.get("reload_request_probe", {})
    flow = gameplay.get("reload_flow", {})
    require(trace.get("hooks_disabled") is True and flow.get("drained") is True, "hooks not drained")
    require(probe.get("enabled") is True and probe.get("scripted_reservation") is True and probe.get("physical_bridge_integrated") is False, "diagnostic boundary missing")
    for field in ("started", "submitted", "acknowledged", "reheld_350ms", "stopped"):
        require(probe.get(field) is True, f"missing {field}")
    require(probe.get("reason") == 0 and probe.get("dropped") == 0, "fixture failed or journal overflowed")
    rows = probe.get("journal", [])
    picked = {}
    for event in (1, 2, 3, 4, 5, 7):
        matches = [r for r in rows if r.get("event") == event]
        require(len(matches) == 1, f"event {event} must occur exactly once")
        if matches:
            picked[event] = matches[0]
    require(not any(r.get("event") == 6 for r in rows), "failed event present")
    if len(picked) != 6:
        return {"passed": False, "errors": errors}
    start, held, submit, ack, cancel, reheld = (picked[n] for n in (1, 2, 3, 4, 5, 7))
    times = [r.get("now_ns", 0) for r in (start, held, submit, ack, reheld, cancel)]
    require(times == sorted(times) and min(times) > 0, "event chronology invalid")
    cutoff = flow.get("start_ns", 0) + 20_000_000_000
    require(submit.get("now_ns", 0) > cutoff, "request did not cross recorder expiry")
    require(reheld.get("now_ns", 0) - ack.get("now_ns", 0) >= 350_000_000, "rehold shorter than350ms")
    require(cancel.get("now_ns", 0) >= reheld.get("now_ns", 0), "cancel preceded rehold")
    cycle = start.get("cycle", 0)
    request = submit.get("request", 0)
    require(cycle > 0 and request > 0 and request == submit.get("sequence"), "request not tied to fresh receiver intent")
    for row in (start, held, submit, ack, reheld):
        require(row.get("cycle") == cycle, "cycle changed")
        require(0 < row.get("source_ns", 0) <= row.get("now_ns", 0) < row.get("deadline_ns", 0), "stale original controller timestamp")
        require(row.get("deadline_ns", 0) - row.get("source_ns", 0) <= 150_000_000, "control lifetime widened")
    for row in (submit, ack, reheld):
        require(row.get("request") == request, "request identity changed")
    baseline = held.get("lease", {})
    load, reserve = baseline.get("loaded", -1), baseline.get("reserve", -1)
    require(baseline.get("held") is True and load >= 0 and reserve >= 2, "initial native held lease missing")
    for row in (submit, reheld):
        require(row.get("lease", {}).get("held") is True, "required all-three held lease missing")
    long_hold = [r for r in rows if cutoff < r.get("now_ns", 0) < submit.get("now_ns", 0) and r.get("lease", {}).get("held") is True]
    require(bool(long_hold), "no genuine held lease beyond20s before release")
    n = reheld.get("native", {})
    branches = n.get("branches", [])
    require(len(branches) == 3, "reheld native branch snapshot missing")
    for b in branches:
        require((b.get("loaded"), b.get("reserve")) == (load + 1, reserve - 1), "request did not conserve exactly one shell on all copies")
        require(b.get("transfers") == 1 and b.get("last_transfer", 0) > 0, "missing exact owned transfer")
    if len(branches) == 3:
        require(ack.get("server_invocation", 0) == branches[2].get("last_transfer", -1), "ack does not name actual server invocation")
    natural = [r.get("native", {}) for r in rows if r.get("now_ns", 0) > cancel.get("now_ns", 0) and r.get("native")]
    require(bool(natural), "no post-cancel native snapshots")
    if natural:
        final = natural[-1].get("branches", [])
        require(len(final) == 3 and all((b.get("loaded"), b.get("reserve")) == (load + 2, reserve - 2) for b in final), "ordinary final reload did not conserve remaining shell")
    hold = flow.get("diagnostic_hold", {})
    require(hold.get("patch_failures") == 0 and hold.get("restore_failures") == 0, "native context patch/restore failed")
    require(len(hold.get("applied", [])) == 3 and all(v > 0 for v in hold["applied"]) and hold.get("applied") == hold.get("restored"), "not all exact patches restored")
    require(flow.get("request_cycle", {}).get("undelivered_acknowledgements") == 0, "ack delivery raced cancellation")
    recovery = probe.get("retirement", {})
    if recovery.get("enabled") is True:
        require(recovery.get("verified") is True and recovery.get("reason") == 0, "retirement proof incomplete")
        receipt, source = recovery.get("receipt", {}), recovery.get("reserve", {})
        identity = recovery.get("expected_identity", {})
        require(len(identity.get("owner", [])) == 7 and len(identity.get("server", [])) == 3 and
                len(identity.get("firing", [])) == 3 and all(identity.get("firing", [])), "retirement identity missing")
        require(receipt.get("identity") == identity and source.get("identity") == identity, "retirement/reserve owner changed")
        if len(branches) == 3:
            require(identity.get("owner") == n.get("owner") and identity.get("server") == n.get("server") and
                    identity.get("firing") == [b.get("firing") for b in branches], "retirement does not name held native copies")
        require(receipt.get("verified") is True and receipt.get("cycle") == cycle and receipt.get("event", 0) > 0,
                "retirement receipt cycle/event invalid")
        stopped = recovery.get("stopped_ns", 0)
        observed, checked, deadline = (receipt.get(k, 0) for k in ("observed_ns", "checked_ns", "deadline_ns"))
        require(0 < stopped <= cancel.get("now_ns", 0) <= observed <= checked < deadline and
                deadline-observed <= 200_000_000, "retirement receipt stale or before cancellation")
        source_time, source_checked, source_deadline = (source.get(k, 0) for k in ("observed_ns", "checked_ns", "deadline_ns"))
        require(source.get("verified") is True and source.get("sequence", 0) > 0 and
                checked <= source_time <= source_checked < min(source_deadline, deadline) and
                source_deadline-source_time <= 200_000_000, "reserve was not freshly read after retirement")
        post_load, post_count, capacity = (source.get(k, -1) for k in ("loaded", "count", "capacity"))
        require(0 <= post_load <= capacity and post_count >= 0 and capacity > 0 and
                post_load+post_count == load+reserve, "post-retirement ammo counts not conserved")
    return {"passed": not errors, "errors": errors, "cycle": cycle, "request": request, "server_invocation": ack.get("server_invocation"), "scripted_reservation": True, "physical_bridge_integrated": False}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("trace", type=Path)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    result = audit(json.loads(args.trace.read_text(encoding="utf-8-sig")))
    text = json.dumps(result, indent=2) + "\n"
    if args.output:
        args.output.write_text(text, encoding="utf-8")
    print(text, end="")
    return 0 if result["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
