"""Bounded offline verification of one completed BC2 empty-fire monitor run."""
import argparse
import hashlib
import json
from pathlib import Path
import report_empty_step as step

LIMITS = [
    "Verification covers this selected owner in this monitor run only; not all guns or a headset test.",
    "Retained rolling/salient journals are bounded: no retained reload entry is not exhaustive absence proof.",
    "Serialized rows do not independently establish native ABI/config admission; runtime guards remain authoritative.",
]


def audit(document):
    reasons = []
    def require(ok, reason):
        if not ok:
            reasons.append(reason)
    gameplay = document.get("gameplay", {})
    fixture = gameplay.get("empty_fire_probe", {})
    flow = gameplay.get("reload_flow", {})
    require(flow.get("drained") is True, "reload_flow_not_drained")
    require(document.get("hooks_disabled") is True, "hooks_not_disabled")
    require(fixture.get("phase") == 3 and fixture.get("failure") == 0, "fixture_not_completed")
    start = fixture.get("starting_loaded")
    require(type(start) is int and start > 0 and fixture.get("last_loaded") == 0
            and fixture.get("consumed") == start, "full_depletion_not_verified")
    dwell = fixture.get("verified_zero_dwell_ns")
    require(type(dwell) is int and dwell >= 3_000_000_000, "zero_dwell_not_verified")
    require(fixture.get("auto_reload_count_increase") is False, "count_increase")
    require(fixture.get("native_state_writes") is False and fixture.get("reload_input") is False,
            "fixture_writes_or_reload_input")
    owner = fixture.get("selected_owner")
    require(isinstance(owner, list) and len(owner) == 7
            and all(type(x) is int and x > 0 for x in owner), "selected_owner_missing")
    control = flow.get("empty_magazine_control", {})
    arrays = [control.get(k) for k in ("requested", "applied", "restored")]
    require(all(isinstance(a, list) and len(a) == 3 and all(type(x) is int and x > 0 for x in a)
                for a in arrays) and arrays[0] == arrays[1] == arrays[2], "branch_patch_restore_counts")
    require(all(type(control.get(k)) is int and control[k] == 0
                for k in ("patch_failures", "restore_failures")), "control_failures")
    receipt_rejections = control.get("receipt_failures")
    require(type(receipt_rejections) is int and receipt_rejections >= 0, "invalid_receipt_rejections")
    branches = set()
    families = set()
    try:
        report = step.analyze(document)
        require(bool(report["journals"]), "journal_missing")
        for journal in report["journals"]:
            require(journal["status"] == "analyzed", "journal_invalid_or_undrained")
            require(not journal["restore_anomalies"], "retained_restore_anomaly")
            require(not any(e["explicit_reload_input"] is not True for e in journal["reload_entries"]),
                    "retained_automatic_or_unknown_reload_entry")
        for _, journal in step.find_journals(document):
            for row in journal.get("rows", []):
                if step.check_row(row):
                    continue
                before, after, context = row["before"], row["after"], row.get("context")
                if row["owner"] == owner and row["owner_retained"] and before and after:
                    require(after["loaded"] <= before["loaded"], "retained_count_increase")
                if not (row["owner"] == owner and row["owner_retained"] and row["owner_revision"] > 0
                        and row["input_sequence"] > 0 and row["branch"] < 3
                        and row["requested"] and row["applied"] and row["restored"]
                        and row.get("family_known") is True and row.get("family") in (0, 1)
                        and before and after and context and not context["input_flags"] & 4
                        and before["firing"] == after["firing"] and before["firing"] >= 0x10000
                        and (before["current"], before["next"], after["current"], after["next"]) == (2, 2, 2, 2)
                        and before["loaded"] == after["loaded"] == 0
                        and before["reserve"] == after["reserve"] > 0
                        and 0 < row["observed_ns"] <= row["now_ns"] < row["deadline_ns"]
                        and not step.observed_predicate_mismatches(row)):
                    continue
                branches.add(row["branch"])
                families.add(row["family"])
        require(branches == {0, 1, 2}, "owned_zero_pair_missing_branch")
        require(len(families) == 1, "known_single_family_not_verified")
    except (ValueError, TypeError, KeyError) as error:
        reasons.append("journal_decode: " + str(error))
    return {"schema": 1, "status": "bounded_monitor_verified" if not reasons else "inconclusive",
            "reasons": reasons, "verified_branches": sorted(branches), "families": sorted(families),
            "receipt_rejections": receipt_rejections,
            "receipt_rejection_note": "Informational: ordinary shot count/state changes reject receipt observations; final owned zero pairs are required separately.",
            "fixture": fixture, "limits": LIMITS}


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("trace", type=Path)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args(argv)
    if args.output and (args.output.resolve() == args.trace.resolve()
                        or (args.output.exists() and args.output.samefile(args.trace))):
        parser.error("output must not overwrite input")
    try:
        raw = args.trace.read_bytes()
        document = json.loads(raw.decode("utf-8-sig"), parse_constant=lambda x: (_ for _ in ()).throw(ValueError(x)))
        result = audit(document)
        result["source"] = {"path": str(args.trace.resolve()), "sha256": hashlib.sha256(raw).hexdigest()}
        encoded = json.dumps(result, indent=2, allow_nan=False) + "\n"
        if args.output:
            args.output.write_text(encoded, encoding="utf-8")
        else:
            print(encoded, end="")
        return 0 if result["status"] == "bounded_monitor_verified" else 1
    except (OSError, ValueError, TypeError) as error:
        parser.exit(2, "empty-fire audit: " + str(error) + "\n")


if __name__ == "__main__":
    raise SystemExit(main())
