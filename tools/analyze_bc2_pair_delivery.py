#!/usr/bin/env python3
"""Read-only, stdlib join of retained native and host full PairTicket timings.

Prints JSON to stdout; never launches processes or modifies source evidence.
"""

import argparse
from collections import Counter, defaultdict
import json
import math
from pathlib import Path
import statistics


TICKET_FIELDS = ("resource_epoch", "sequence", "frame", "tracking", "space", "predicted_ns")


def ticket_key(ticket):
    """Reject partial identities instead of silently joining sequence alone."""
    if not isinstance(ticket, dict):
        return None
    session = ticket.get("session")
    if not isinstance(session, list) or len(session) != 16 or not any(session):
        return None
    if any(type(value) is not int or not 0 <= value <= 255 for value in session):
        return None
    values = tuple(ticket.get(field) for field in TICKET_FIELDS)
    if any(type(value) is not int for value in values) or values[1] <= 0:
        return None
    return (tuple(session), *values)


def qpc_ns(ticks, frequency):
    if type(ticks) is not int or type(frequency) is not int or ticks <= 0 or frequency <= 0:
        return None
    quotient, remainder = divmod(ticks, frequency)
    return quotient * 1_000_000_000 + remainder * 1_000_000_000 // frequency


def positive(value):
    return type(value) is int and value > 0


def distribution(values):
    """Signed elapsed milliseconds; nearest-rank p95, no clamping."""
    if not values:
        return {"n": 0}
    ordered = sorted(values)
    return {"n": len(values), "min": round(ordered[0] / 1e6, 6),
            "median": round(statistics.median(ordered) / 1e6, 6),
            "p95": round(ordered[math.ceil(.95 * len(ordered)) - 1] / 1e6, 6),
            "max": round(ordered[-1] / 1e6, 6),
            "mean": round(statistics.mean(ordered) / 1e6, 6),
            "negative": sum(value < 0 for value in values)}


def unique(values):
    return next(iter(values)) if len(values) == 1 else None


def read_json(path):
    text = path.read_text(encoding="utf-8-sig")
    value, end = json.JSONDecoder().raw_decode(text.lstrip())
    if text.lstrip()[end:].strip():
        raise ValueError("Unexpected trailing content in " + str(path))
    return value


def timing_blocks(value, path="$", native=False):
    if isinstance(value, dict):
        if (native and value.get("schema") == "fvr.native-frame-timing.v1") or (
            not native and "rows" in value and "host_qpc_ns" in value.get("clock", "")
        ):
            yield path, value
            return
        for key, child in value.items():
            yield from timing_blocks(child, path + "." + key, native)
    elif isinstance(value, list):
        for index, child in enumerate(value):
            yield from timing_blocks(child, path + "[" + str(index) + "]", native)


def analyze(args, native, host, receipt):
    session = read_json(args.session / "session.json")
    if Path(receipt["session_folder"]).resolve() != args.session.resolve():
        raise ValueError("Canonical receipt is for a different session")
    receipt_host = next(process["identity"] for process in receipt["processes"] if process["name"] == "host")
    if receipt_host != session["host"] or native["pid"] != session["game"]["pid"]:
        raise ValueError("Session process identities do not match canonical receipt/native report")
    native_blocks = list(timing_blocks(native, native=True))
    host_blocks = list(timing_blocks(host))
    if not native_blocks or len(host_blocks) != 1:
        raise ValueError("Expected native timing blocks and one host diagnostic journal")
    native_records = defaultdict(list)
    ncounts = Counter()
    journal = []
    for path, block in native_blocks:
        frequency = block["frequency"]
        if block.get("clock") != "qpc_ticks" or not block.get("available") or not positive(frequency):
            raise ValueError("Native QPC clock unavailable: " + path)
        journal.append({"path": path, "frequency": frequency,
                        **{key: block[key] for key in ("completed", "overwritten", "dropped", "unmatched", "invalid_clock")}})
        for source, rows in (("rows", block["rows"]), ("active", [block.get("active")]),
                             ("retiring", [block.get("retiring")])):
            for row in rows:
                if row is None:
                    continue
                ncounts[source] += 1
                key = ticket_key(row.get("ticket"))
                if key is None:
                    ncounts["invalid_ticket"] += 1
                    continue
                native_records[key].append((row, frequency))
    hpath, hblock = host_blocks[0]
    host_records = defaultdict(list)
    hcounts = Counter()
    phases = Counter(menu=0, world=0, other=0)
    for row in hblock["rows"]:
        phase = "menu" if row.get("menu") else "world" if row.get("world_layer") else "other"
        phases[phase] += 1
        for source, observation in (("primary", row), ("prior_copy", row.get("prior_copy"))):
            if observation is None:
                continue
            hcounts[source + "_observations"] += 1
            ticket = observation.get("copy_ticket")
            if ticket is None:
                hcounts["without_ticket"] += 1
                continue
            key = ticket_key(ticket)
            if key is None:
                hcounts["invalid_ticket"] += 1
                continue
            hcounts[source + "_ticket_observations"] += 1
            host_records[key].append((observation, phase, row["frame"], source))
    native_keys, host_keys = set(native_records), set(host_records)
    matched = native_keys & host_keys
    timings = defaultdict(list)
    matched_phases = Counter()
    anomalies = Counter()
    points = []
    examples = []
    for key in sorted(matched):
        nrows, hrows = native_records[key], host_records[key]
        native_values = {stage: {stamp for row, frequency in nrows
                        if (stamp := qpc_ns(row["qpc"].get(stage), frequency)) is not None}
                         for stage in ("begin", "export_begin", "published")}
        host_values = {stage: {row[stage] for row, _, _, _ in hrows if positive(row.get(stage))}
                       for stage in ("copy_submitted_ns", "copy_completed_ns")}
        values = {**native_values, **host_values}
        for stage, stamps in values.items():
            if len(stamps) > 1:
                anomalies[stage + "_conflicting_tickets"] += 1
            elif not stamps:
                anomalies[stage + "_missing_tickets"] += 1
        stamps = {stage: unique(values) for stage, values in values.items()}
        published, submitted = stamps["published"], stamps["copy_submitted_ns"]
        if published is None or submitted is None:
            continue
        phase_set = {phase for _, phase, _, _ in hrows}
        phase = next(iter(phase_set)) if len(phase_set) == 1 else "mixed"
        matched_phases[phase] += 1
        points.append((published, submitted))
        for label, start, end in (
            ("published_to_copy_submit", published, submitted),
            ("native_begin_to_published", stamps["begin"], published),
            ("native_export_begin_to_published", stamps["export_begin"], published),
            ("copy_submit_to_completion_observation", submitted, stamps["copy_completed_ns"]),
        ):
            if start is not None and end is not None:
                delta = end - start
                timings[label].append(delta)
                if delta < 0 and len(examples) < 3:
                    examples.append({"metric": label, "delta_ns": delta,
                                     "ticket": {"session": list(key[0]), **dict(zip(TICKET_FIELDS, key[1:]))}})
    for label in ("published_to_copy_submit", "native_begin_to_published",
                  "native_export_begin_to_published", "copy_submit_to_completion_observation"):
        timings.setdefault(label, [])
    native_unmatched_published = sum(any(qpc_ns(row["qpc"].get("published"), frequency) is not None
                                     for row, frequency in native_records[key]) for key in native_keys - host_keys)
    overlap_start = min((point[0] for point in points), default=None)
    unmatched_before_overlap = sum(
        bool(stamps := [stamp for row, frequency in native_records[key]
                        if (stamp := qpc_ns(row["qpc"].get("published"), frequency)) is not None])
        and overlap_start is not None and max(stamps) < overlap_start
        for key in native_keys - host_keys
    )
    native_pub_times = [stamp for rows in native_records.values() for row, frequency in rows
                        if (stamp := qpc_ns(row["qpc"].get("published"), frequency)) is not None]
    host_submit_times = [row["copy_submitted_ns"] for rows in host_records.values()
                         for row, _, _, _ in rows if positive(row.get("copy_submitted_ns"))]
    eligible = len(points)
    result = {
        "schema": "fvr.feedback66.pair-timing-join.v1",
        "evidence": "actual-game xr-sim retained overlap; offline analysis only",
        "session": args.session.as_posix(),
        "canonical_receipt": args.receipt.as_posix(),
        "runtime": {"version": session["version"], "name": host["runtime"], "system": host["system"],
                    "host_path": session["host"]["path"], "canonical_identity_verified": True,
                    "normal_game_close": receipt["normal_game_close"], "native_exit_code": session["native_exit_code"]},
        "method": {
            "key": ["session[16]", *TICKET_FIELDS],
            "conversion": "(q//frequency)*1000000000+(q%frequency)*1000000000//frequency; positive ticks only",
            "dedup": "Full ticket; distinct nonzero timestamps must agree. Conflicts excluded per boundary, signed deltas retained.",
            "scope": "Native rows/active/retiring; host ring rows primary and prior_copy. Saved first/worst outliers excluded.",
            "distribution": "Unique matched tickets; milliseconds; nearest-rank p95. No cross-session comparison."
        },
        "native": {"journals": journal, "rows": ncounts["rows"], "active": ncounts["active"],
                   "retiring": ncounts["retiring"], "invalid_tickets": ncounts["invalid_ticket"],
                   "unique_tickets": len(native_keys), "duplicate_ticket_rows": sum(map(len, native_records.values())) - len(native_keys),
                   "unmatched_tickets": len(native_keys - host_keys), "unmatched_with_publication": native_unmatched_published,
                   "unmatched_published_before_overlap": unmatched_before_overlap,
                   "published_ns_range": [min(native_pub_times), max(native_pub_times)] if native_pub_times else None},
        "host": {"path": hpath, "retained_rows": len(hblock["rows"]), "recorded": hblock["recorded"],
                 "overwritten_rows": hblock["dropped"], "whole_session_frames": hblock["frames"],
                 "whole_session_menu_only_frames": hblock["menu_only_frames"],
                 "retained_phases": dict(phases), "primary_ticket_observations": hcounts["primary_ticket_observations"],
                 "prior_copy_observations": hcounts["prior_copy_observations"],
                 "prior_copy_ticket_observations": hcounts["prior_copy_ticket_observations"],
                 "without_ticket": hcounts["without_ticket"], "invalid_tickets": hcounts["invalid_ticket"],
                 "unique_tickets": len(host_keys), "repeated_ticket_observations": sum(map(len, host_records.values())) - len(host_keys),
                 "unmatched_tickets": len(host_keys - native_keys),
                 "frame_range": [min(row["frame"] for row in hblock["rows"]), max(row["frame"] for row in hblock["rows"])],
                 "submitted_ns_range": [min(host_submit_times), max(host_submit_times)] if host_submit_times else None,
                 "copy_observation_overflow_frames": hblock["copy_observation_overflow_frames"]},
        "join": {"matched_tickets": len(matched), "timing_eligible": eligible,
                 "timing_excluded": len(matched) - eligible, "phases": dict(matched_phases),
                 "publication_span_ms": round((max(p[0] for p in points) - min(p[0] for p in points)) / 1e6, 6) if points else None,
                 "timestamp_issues": dict(anomalies), "negative_examples": examples},
        "signed_elapsed_ms": {label: distribution(values) for label, values in timings.items()},
        "limits": "Publication is producer observation after successful Publish, outside IPC mutex; a host may observe before it. Copy completion is polling observation. Retained/event-selected overlap only; no menu distribution without retained menu pairs, whole-session delay split, FPS or GPU timing claim.",
        "next": "Measured post-publication delivery delay supports narrow host request_started_qpc_ns and host_pair_observed_qpc_ns retention per successful ticket in RemoteFrameProvider before selecting an optimization. These separate request/queued-source wait from host observation/copy scheduling. Existing native publication timestamps suffice; no new native instrumentation indicated. Previous 34.014 ms is feedback64 aggregate, not this sample's denominator."
    }
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("session", type=Path)
    parser.add_argument("--receipt", type=Path, required=True)
    parser.add_argument("--inspect", action="store_true")
    args = parser.parse_args()
    native = read_json(args.session / "native-trace.json")
    host = read_json(args.session / "host.stdout.log")
    receipt = read_json(args.receipt)
    if args.inspect:
        result = {"receipt": receipt, "native_top_keys": list(native), "host_top_keys": list(host)}
        for name, value, is_native in (("native", native, True), ("host", host, False)):
            result[name] = [{"path": path, "metadata": {k: v for k, v in block.items()
                if not isinstance(v, (dict, list))}, "keys": list(block),
                "first_row": block.get("rows", [])[:1]} for path, block in timing_blocks(value, native=is_native)]
    else:
        result = analyze(args, native, host, receipt)
    rendered = json.dumps(result, indent=2 if args.inspect else None, separators=None if args.inspect else (",", ":"))
    if not args.inspect and len((rendered + "\n").encode("utf-8")) > 5000:
        raise ValueError("Bounded report exceeds 5000 bytes")
    print(rendered)


if __name__ == "__main__":
    main()
