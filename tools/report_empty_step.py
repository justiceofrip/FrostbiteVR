"""Read BC2's schema-1 empty-Step journal; never change game state.

Only paired before/after samples from a single invocation can establish an
observed reload entry. Stage and hand correlations are evidence, not causes.
"""
from __future__ import annotations

import argparse
from collections import Counter
import hashlib
import json
import math
from pathlib import Path


STAGES = (
    "boundary", "owner_or_context", "owner_evidence", "firing_read", "policy",
    "eligibility", "profile_or_timing", "policy_lock", "adjacent_recheck",
    "applied", "patch_failed",
)
BOUNDARY_REASONS = (
    "none", "step_delta_nonfinite", "step_delta_nonpositive", "step_delta_too_large",
    "context_decode_failed", "step_delta_exceeds_context", "owner_evidence_failed",
    "before_read_failed", "context_delta_too_large", "context_multiplier_not_one",
    "unsupported_input", "unsupported_context_flags",
)
LIMITS = [
    "No automatic-reload cause or native feature admission is inferred.",
    "The rolling journal can omit earlier transitions; no entry found is not proof of suppression.",
    "Profile/config matching, server identity, wrapper offsets, caller/stack checks and cancellation epochs are not fully serialized.",
    "now_ns is sampled after the original call; expiry at that time does not prove expiry when eligibility was checked.",
    "support_holding describes the fresh producer state, not independently verified physical hand contact.",
    "A loaded<=1 reload entry can be automatic or explicit; input_flags must be considered.",
    "Older captures do not identify the native family; profile 0 alone never identifies a gun.",
    "Last-known expired-owner attribution supplies no current native state or authority.",
    "Earlier producers logged only registered manual-cycle targets; missing idle entries do not prove protection.",
]
LOSS_COUNTERS = ("unowned_skipped", "last_known_attributed", "salient_retained",
                 "salient_dropped", "salient_boundary_dropped", "salient_transition_dropped")


class ReportError(ValueError):
    pass


def integer(value):
    return type(value) is int


def finite(value):
    return type(value) in (int, float) and math.isfinite(value)


def find_journals(document, path="$", depth=0):
    if depth > 40:
        raise ReportError("Report nesting exceeds 40 levels")
    if isinstance(document, dict):
        for key, value in document.items():
            child = f"{path}.{key}"
            if key == "empty_step_diagnostic":
                yield child, value
            elif isinstance(value, (dict, list)):
                yield from find_journals(value, child, depth + 1)
    elif isinstance(document, list):
        for index, value in enumerate(document):
            if isinstance(value, (dict, list)):
                yield from find_journals(value, f"{path}[{index}]", depth + 1)


def check_state(value):
    if value is None:
        return True
    return (isinstance(value, dict)
            and all(integer(value.get(k)) for k in ("firing", "current", "next", "loaded", "reserve", "flags_a8"))
            and 0 <= value["firing"] <= 0xFFFFFFFF
            and 0 <= value["current"] <= 15 and 0 <= value["next"] <= 15
            and 0 <= value["flags_a8"] <= 255
            and finite(value.get("timer")))


def check_row(row):
    if not isinstance(row, dict):
        return "row is not an object"
    ints = ("stage", "now_ns", "input_sequence", "owner_revision", "observed_ns",
            "deadline_ns", "branch", "profile", "phase")
    if not all(integer(row.get(k)) for k in ints):
        return "missing or non-integer row metadata"
    if not (0 <= row["stage"] < len(STAGES) and 0 <= row["branch"] <= 3):
        return "unknown stage or branch"
    if not (row["now_ns"] > 0 and row["observed_ns"] >= 0 and row["deadline_ns"] >= 0):
        return "invalid diagnostic clock"
    if not all(type(row.get(k)) is bool for k in ("requested", "applied", "restored", "owner_retained")):
        return "missing or non-boolean outcome"
    if "family" in row and not (integer(row["family"]) and 0 <= row["family"] <= 0xFFFFFFFF):
        return "invalid native family metadata"
    if "family_known" in row and (type(row["family_known"]) is not bool
            or (row["family_known"] and "family" not in row)):
        return "invalid native family availability"
    for key in ("boundary_reason_known", "raw_context_known"):
        if key in row and type(row[key]) is not bool:
            return "invalid boundary metadata availability"
    for key in ("boundary_reason", "step_delta_bits", "context_delta_bits", "context_multiplier_bits", "context_input_flags"):
        if key in row and not (integer(row[key]) and 0 <= row[key] <= 0xFFFFFFFF):
            return "invalid raw boundary word"
    if row.get("boundary_reason_known") and not (integer(row.get("boundary_reason"))
            and 0 <= row["boundary_reason"] < len(BOUNDARY_REASONS)):
        return "unknown boundary reason"
    if row.get("raw_context_known") and (not all(k in row for k in
            ("step_delta_bits", "context_delta_bits", "context_multiplier_bits", "context_input_flags"))
            or not isinstance(row.get("context_flag_bytes"), list) or len(row["context_flag_bytes"]) != 5
            or not all(integer(x) and 0 <= x <= 255 for x in row["context_flag_bytes"])):
        return "invalid raw boundary context"
    if "lease_invalid_last_known" in row and type(row["lease_invalid_last_known"]) is not bool:
        return "invalid last-known attribution metadata"
    if row.get("lease_invalid_last_known") and (row.get("before") is not None or row.get("after") is not None
            or row.get("context") is not None or row["owner_retained"] or row["applied"] or row["requested"]
            or row.get("raw_context_known") or row.get("boundary_reason_known")):
        return "last-known attribution cannot provide current state or patch evidence"
    owner = row.get("owner")
    if not isinstance(owner, list) or len(owner) != 7 or not all(integer(x) and x >= 0 for x in owner):
        return "invalid complete owner identity"
    if "before" not in row or "after" not in row or not check_state(row["before"]) or not check_state(row["after"]):
        return "invalid before/after state"
    context = row.get("context")
    if context is not None:
        if not (isinstance(context, dict) and finite(context.get("delta"))
                and finite(context.get("reload_multiplier")) and integer(context.get("input_flags"))
                and isinstance(context.get("flags_24_28"), list) and len(context["flags_24_28"]) == 5
                and all(type(x) is int and x in (0, 1) for x in context["flags_24_28"])):
            return "invalid decoded context"
    interaction = row.get("interaction")
    if interaction is not None and not (
        isinstance(interaction, dict)
        and all(integer(interaction.get(k)) for k in ("input", "observed_ns", "deadline_ns", "magazine_phase"))
        and all(type(interaction.get(k)) is bool for k in ("support_holding", "owns_left_hand", "blocks_weapon_actions"))
    ):
        return "invalid interaction sample"
    return None


def interaction_label(row):
    sample = row.get("interaction")
    if not sample:
        return "unknown"
    valid = (all(row["owner"])
             and 0 < sample["input"] <= row["input_sequence"]
             and 0 < sample["observed_ns"] <= row["now_ns"] < sample["deadline_ns"]
             and sample["deadline_ns"] - sample["observed_ns"] <= 150_000_000)
    if not valid:
        return "unknown_stale_or_invalid"
    return "support_holding" if sample["support_holding"] else "support_not_holding"


def boundary_details(row):
    known = row.get("boundary_reason_known", False)
    return {"known": known, "reason": BOUNDARY_REASONS[row["boundary_reason"]] if known else "unknown",
            "step_delta_bits": row.get("step_delta_bits"),
            "raw_context": {k: row[k] for k in ("context_delta_bits", "context_multiplier_bits",
                            "context_input_flags", "context_flag_bytes")} if row.get("raw_context_known") else None}


def delta_limit(policy):
    # Select from the frozen build receipt, never infer a newer policy because
    # an old trace would otherwise fail. Match the C++ binary32 boundary.
    import struct
    if policy not in ("legacy50", "bounded100"):
        raise ReportError("unknown simulation-delta policy")
    return struct.unpack('<f', struct.pack('<f', .05 if policy == "legacy50" else .1))[0]


def observed_predicate_mismatches(row, delta_policy="legacy50"):
    """Describe visible predicate values without recreating an incomplete gate."""
    facts = []
    before, context = row["before"], row.get("context")
    if before:
        if (before["current"], before["next"]) != (2, 2):
            facts.append("before_not_idle_2_2")
        if before["flags_a8"] & 24:
            facts.append("flags_a8_has_bit8_or_bit16")
        if before["timer"] < 0:
            facts.append("negative_phase_timer")
    if context:
        if not 0 < context["delta"] <= delta_limit(delta_policy):
            facts.append("delta_outside_gate_range")
        if context["reload_multiplier"] != 1:
            facts.append("reload_multiplier_not_one")
        if context["input_flags"] & ~5:
            facts.append("input_flags_outside_fire_reload")
        for offset, expected in ((0, 1), (2, 0), (4, 0)):
            if context["flags_24_28"][offset] != expected:
                facts.append(f"context_0x{0x24 + offset:02x}_not_{expected}")
    if row["observed_ns"] > row["now_ns"]:
        facts.append("owner_observation_in_future")
    if row["deadline_ns"] and row["now_ns"] >= row["deadline_ns"]:
        facts.append("owner_deadline_expired_by_post_call_observation")
    return facts


def analyze_journal(journal, path, delta_policy="legacy50"):
    if not isinstance(journal, dict) or type(journal.get("schema")) is not int or journal["schema"] != 1:
        raise ReportError(f"{path}: unsupported journal schema")
    if journal.get("diagnostic_only") is not True or type(journal.get("drained")) is not bool:
        raise ReportError(f"{path}: missing diagnostic/drain status")
    if journal.get("stages") != list(STAGES):
        raise ReportError(f"{path}: stage mapping differs from schema 1")
    for key in ("capacity", "observed", "overwritten", "lock_drops", "publisher_lock_drops"):
        if not integer(journal.get(key)) or journal[key] < 0:
            raise ReportError(f"{path}: invalid {key}")
    for key in LOSS_COUNTERS:
        if key in journal and (not integer(journal[key]) or journal[key] < 0):
            raise ReportError(f"{path}: invalid {key}")
    # The producer added three protected Arming receipts to its former
    # 128-entry layout. Both are schema 1; no other capacity is understood.
    if journal["capacity"] not in (128, 131) or not isinstance(journal.get("rows"), list) or len(journal["rows"]) > journal["capacity"]:
        raise ReportError(f"{path}: invalid rolling journal size")
    counts = journal.get("counts")
    if (not isinstance(counts, list) or any(not integer(x) or x < 0 for x in counts)
            or (journal["drained"] and (len(counts) != len(STAGES) or sum(counts) != journal["observed"]))):
        raise ReportError(f"{path}: invalid lifetime counts")
    result = {
        "path": path, "status": "analyzed" if journal["drained"] else "not_drained",
        "observed": journal["observed"], "overwritten": journal["overwritten"],
        "lock_drops": journal["lock_drops"], "publisher_lock_drops": journal["publisher_lock_drops"],
        "lifetime_stage_counts": dict(zip(STAGES, counts)),
        "retained_rows": len(journal["rows"]), "invalid_rows": [], "reload_entries": [],
        "paired_but_unconfirmed_entries": [], "restore_anomalies": [], "groups": [],
        "retention": {key: journal.get(key) for key in LOSS_COUNTERS},
        "last_known_rows": 0,
    }
    if not journal["drained"]:
        return result
    groups = {}
    for index, row in enumerate(journal["rows"]):
        error = check_row(row)
        if error:
            result["invalid_rows"].append({"row": index, "reason": error})
            continue
        support = interaction_label(row)
        family = row.get("family")
        family_known = row.get("family_known", False)
        family_label = {0: "tube", 1: "detachable_magazine"}.get(family, "unknown") if family_known else "unknown"
        last_known = row.get("lease_invalid_last_known", False)
        result["last_known_rows"] += int(last_known)
        key = (tuple(row["owner"]), row["owner_revision"], family_known, family, row["profile"], row["branch"], support, last_known)
        group = groups.setdefault(key, {"owner": row["owner"], "owner_revision": row["owner_revision"],
            "family_known": family_known, "family": family, "family_label": family_label, "profile": row["profile"], "branch": row["branch"], "interaction": support,
            "lease_invalid_last_known": last_known,
            "rows": 0, "stages": Counter(), "boundary_reasons": Counter(), "paired_reload_entries": 0})
        group["rows"] += 1
        group["stages"][STAGES[row["stage"]]] += 1
        boundary = boundary_details(row)
        group["boundary_reasons"][boundary["reason"]] += 1
        if row["applied"] and not row["restored"]:
            result["restore_anomalies"].append({"row": index, "now_ns": row["now_ns"], "owner": row["owner"]})
        before, after = row["before"], row["after"]
        # A later unrelated sample is never substituted for a missing after read.
        if not (before and after and before["loaded"] in (0, 1)
                and before["current"] != 10 and before["next"] != 10
                and (after["current"] == 10 or after["next"] == 10)):
            continue
        context = row.get("context")
        entry = {"row": index, "now_ns": row["now_ns"], "owner": row["owner"],
            "owner_revision": row["owner_revision"], "branch": row["branch"], "profile": row["profile"],
            "family_known": family_known, "family": family, "family_label": family_label,
            "stage": STAGES[row["stage"]], "interaction": support, "before": before, "after": after,
            "context": context, "boundary": boundary, "requested": row["requested"], "applied": row["applied"],
            "restored": row["restored"], "visible_predicate_mismatches": observed_predicate_mismatches(row, delta_policy),
            "explicit_reload_input": bool(context["input_flags"] & 4) if context else None}
        retained = (row["owner_retained"] and all(row["owner"]) and row["branch"] < 3
                    and row["owner_revision"] > 0 and row["input_sequence"] > 0
                    and 0 < row["observed_ns"] <= row["now_ns"]
                    and before["firing"] == after["firing"] and before["firing"] >= 0x10000)
        if retained:
            result["reload_entries"].append(entry)
            group["paired_reload_entries"] += 1
        else:
            entry["reason"] = "owner retention or exact firing identity not established"
            result["paired_but_unconfirmed_entries"].append(entry)
    result["groups"] = list(groups.values())
    # Protected rows precede rolling rows in the producer; don't infer chronology
    # from storage position, especially when correlating a late family switch.
    result["reload_entries"].sort(key=lambda entry: (entry["now_ns"], entry["row"]))
    result["paired_but_unconfirmed_entries"].sort(key=lambda entry: (entry["now_ns"], entry["row"]))
    if result["invalid_rows"]:
        result["status"] = "partial_invalid_rows"
    return result


def analyze(document, delta_policy="legacy50"):
    delta_limit(delta_policy)
    journals = list(find_journals(document))
    return {"schema": 1, "status": "analyzed" if journals else "missing_diagnostic",
            "delta_policy": delta_policy,
            "journals": [analyze_journal(value, path, delta_policy) for path, value in journals], "limits": LIMITS}


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("trace", type=Path)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--delta-policy", choices=("legacy50", "bounded100"), default="legacy50",
                        help="Match the frozen producer build: 212 uses bounded100; older journals default to legacy50")
    args = parser.parse_args(argv)
    if args.output and (args.output.resolve() == args.trace.resolve()
                        or (args.output.exists() and args.trace.exists() and args.output.samefile(args.trace))):
        parser.error("output must not overwrite the input trace")
    try:
        raw = args.trace.read_bytes()
        document = json.loads(raw.decode("utf-8-sig"), parse_constant=lambda x: (_ for _ in ()).throw(ReportError(f"nonfinite JSON value: {x}")))
        report = analyze(document, args.delta_policy)
        report["source"] = {"path": str(args.trace.resolve()), "sha256": hashlib.sha256(raw).hexdigest()}
        encoded = json.dumps(report, indent=2, allow_nan=False) + "\n"
        if args.output:
            args.output.write_text(encoded, encoding="utf-8")
            print(json.dumps({"status": report["status"], "output": str(args.output.resolve()),
                              "journals": len(report["journals"])}))
        else:
            print(encoded, end="")
        return 0
    except (OSError, ValueError) as error:
        parser.exit(2, f"empty-step report: {error}\n")


if __name__ == "__main__":
    raise SystemExit(main())
