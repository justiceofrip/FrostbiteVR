#!/usr/bin/env python3
"""Join one BC2 native/host final-report pair without estimating missing phases.

Usage: python -B tools/analyze_bc2_frame_timing.py --native native-trace.json
       --host host.stdout.log --output timing.json
       python -B tools/analyze_bc2_frame_timing.py --hud-only
       --host host.stdout.log --output hud.json

Inputs must be completed reports from ONE saved session on the same machine/boot.
Only exact full PairTickets join. Native request and slot are recovered from the
unique matching native row, not from host frame number or request ordinal.
The default window is the intersection of the two retained journal ranges.
An explicit window uses --start-qpc-ns / --end-qpc-ns (both required).
No archive discovery, runtime activity, third-party packages or FPS calculation.
HUD-only mode audits the bounded first_hud_gap host snapshot separately; it
requires neither native timing nor a ticket join and supplies no native cause.
Use --hud-recent with explicit QPC bounds for retained later omission episodes.
"""

from __future__ import annotations

import argparse
from collections import Counter, defaultdict
import hashlib
import json
import math
from pathlib import Path
import statistics
import sys


NATIVE_SCHEMA = "fvr.native-frame-timing.v1"
HOST_CLOCK = "host_qpc_ns; predicted/retained age are xr_time"
TICKET_FIELDS = ("resource_epoch", "sequence", "frame", "tracking", "space", "predicted_ns")
NATIVE_FIELDS = ("channel", "request", "owner", "frame", "device", "tracking", "space", "predicted_ns")
PHASES = {
    "native_begin_to_left_capture": ("begin", "capture_left_begin"),
    "native_left_capture_call": ("capture_left_begin", "capture_left_queued"),
    "native_left_queued_to_right_capture": ("capture_left_queued", "capture_right_begin"),
    "native_right_capture_call": ("capture_right_begin", "capture_right_queued"),
    "native_right_queued_to_restored": ("capture_right_queued", "restored"),
    "native_begin_to_restored": ("begin", "restored"),
    "native_restored_to_export": ("restored", "export_begin"),
    "native_export_to_relay_ready_observed": ("export_begin", "relay_copies_ready"),
    "native_export_to_fence_ready_observed": ("export_begin", "producer_fence_ready"),
    "native_fence_ready_to_staged": ("producer_fence_ready", "staged"),
    "native_staged_to_published": ("staged", "published"),
    "native_begin_to_published": ("begin", "published"),
    "published_to_host_copy_submitted": ("published", "host_copy_submitted"),
    "host_copy_submitted_to_completed_observed": ("host_copy_submitted", "host_copy_completed"),
    "native_begin_to_host_copy_completed_observed": ("begin", "host_copy_completed"),
    "host_copy_completed_to_native_reaped": ("host_copy_completed", "reaped"),
    "native_published_to_reaped": ("published", "reaped"),
    "native_reaped_to_released": ("reaped", "released"),
    "native_ack_observed_to_released": ("ack_observed", "released"),
    "native_capture_release_call": ("capture_release_begin", "capture_released"),
    "host_copy_submitted_to_composed_observed": ("host_copy_submitted", "host_copy_composed"),
}
LIMITS = [
    "All durations are CPU-observed elapsed boundaries, not GPU execution time.",
    "Retained journals are bounded and host rows are change/slow/failure-selected; quantiles are not whole-session estimates.",
    "No native game FPS, WorldHook-derived FPS, compositor-derived game FPS, or optimization-causation claim is computed.",
    "Successful host request submission has no same-request timestamp in these journals; input publication is not request submission.",
    "Presented images lack a full ticket; retained generation/space/predicted time cannot establish same-frame presentation.",
    "Host copy_composed observes XR image release after queued rendering, not display or GPU completion.",
    "Host input flags describe the observing host frame, not necessarily the native request; no tracked-WORLD or on-foot classification is inferred.",
    "Native begin is a bridge boundary, not a measured native admission timestamp or a complete CPU rendering interval.",
    "First-failure/worst-frame snapshots are audited separately and excluded from the retained-window distribution.",
]
HUD_GATES = {
    "not_called": "not_called", "unavailable": "hud_unavailable", "failed": "hud_failed",
    "no_clock": "clock_unavailable", "inactive": "inactive", "tracking": "tracking",
    "placement": "placement", "no_sample": "first_select_no_sample", "upload": "upload",
    "final_no_sample": "final_select_no_sample", "image_owner": "image_owner", "submitted": "submitted",
}
HUD_SAMPLE_FIELDS = ("sequence", "observed_ns", "deadline_ns", "actor_generation",
                     "hud_scene_generation", "space_generation", "loaded", "reserve", "capacity", "tag")
HUD_LIMITS = [
    "Only first_hud_gap is analyzed: a prior successful HUD submission, first focused world omission, and following observations (at most 32 rows).",
    "This selected snapshot cannot explain or count all session gaps, establish a fix, or measure FPS or headset visibility.",
    "Decisions are recorded host gates, not inferred native causes. Native owner/scene timestamps are unavailable.",
    "Deadline comparisons use original host-wire QPC ns only, never predicted XR time; an open window is not a complete sample-validity check.",
    "Expired count evidence alone does not explain omission: a separate current-owner lease can retain the displayed digits.",
    "Receive snapshots can predate the observing frame; repeated rows do not establish repeated receives.",
    "Menu and actual owner/space transitions may intentionally suppress the HUD; missing evidence is not an owner transition.",
]


class EvidenceError(ValueError):
    """An input does not satisfy the supported saved-journal contract."""


def integer(value, minimum=0):
    return type(value) is int and value >= minimum


def ticket_key(ticket):
    if not isinstance(ticket, dict):
        raise EvidenceError("missing full ticket")
    for field in TICKET_FIELDS:
        if not integer(ticket.get(field), 1):
            raise EvidenceError(f"invalid ticket {field}")
    session = ticket.get("session")
    if (not isinstance(session, list) or len(session) != 16
            or not all(integer(v) and v <= 255 for v in session) or not any(session)):
        raise EvidenceError("invalid ticket session")
    return (tuple(session), *(ticket[field] for field in TICKET_FIELDS))


def qpc_ns(ticks, frequency):
    # Same integer conversion as BodyPropClockNs; never round a float epoch.
    return (ticks // frequency) * 1_000_000_000 + (ticks % frequency) * 1_000_000_000 // frequency


def summary(values):
    values = sorted(values)
    if not values:
        return {"samples": 0, "median_ms": None, "p95_ms": None, "p99_ms": None, "max_ms": None}
    return {"samples": len(values), "median_ms": statistics.median(values) / 1e6,
            "p95_ms": values[math.ceil(len(values) * .95) - 1] / 1e6,
            "p99_ms": values[math.ceil(len(values) * .99) - 1] / 1e6,
            "max_ms": values[-1] / 1e6}


def require_rows(document, label):
    rows = document.get("rows")
    if not isinstance(rows, list) or not all(isinstance(row, dict) for row in rows):
        raise EvidenceError(f"{label}: rows must be an array of objects")
    return rows


def read_native(report):
    snapshots = []
    if isinstance(report.get("native_frame_timing"), dict):
        snapshots.append(("primary", None, report["native_frame_timing"]))
    slots = report.get("native_frame_timing_slots", [])
    if not isinstance(slots, list):
        raise EvidenceError("native_frame_timing_slots must be an array")
    seen_slots = set()
    for entry in slots:
        if not isinstance(entry, dict) or not integer(entry.get("slot")) or not isinstance(entry.get("timing"), dict):
            raise EvidenceError("invalid native slot journal")
        if entry["slot"] in seen_slots:
            raise EvidenceError("duplicate native slot journal")
        seen_slots.add(entry["slot"])
        snapshots.append((f"slot_{entry['slot']}", entry["slot"], entry["timing"]))
    if not snapshots:
        raise EvidenceError("native report has no supported timing journal")
    rows, metadata, issues, sentinels = [], [], [], []
    frequencies = set()
    for journal, slot, snapshot in snapshots:
        if snapshot.get("schema") != NATIVE_SCHEMA or snapshot.get("clock") != "qpc_ticks":
            raise EvidenceError(f"{journal}: unsupported native schema or clock")
        raw_rows = require_rows(snapshot, journal)
        metadata.append({"journal": journal, "slot": slot, **{k: snapshot.get(k) for k in (
            "available", "frequency", "count", "completed", "overwritten", "dropped", "unmatched", "invalid_clock")}})
        for field in ("first_non_consumed",):
            if snapshot.get(field) is not None:
                sentinels.append({"journal": journal, "kind": field, "row": snapshot[field]})
        if snapshot.get("available") is not True:
            issues.append({"journal": journal, "reason": "journal_unavailable", "ignored_rows": len(raw_rows)})
            continue
        if snapshot.get("count") != len(raw_rows):
            raise EvidenceError(f"{journal}: count differs from retained row count")
        frequency = snapshot.get("frequency")
        if not raw_rows and not snapshot.get("active") and not snapshot.get("retiring"):
            continue
        if not integer(frequency, 1) or frequency > 1_000_000_000:
            raise EvidenceError(f"{journal}: invalid QPC frequency")
        frequencies.add(frequency)
        if len(frequencies) > 1:
            raise EvidenceError("native journals disagree on the same-boot QPC frequency")
        entries = [(f"rows[{i}]", r) for i, r in enumerate(raw_rows)]
        entries += [(k, snapshot[k]) for k in ("active", "retiring") if snapshot.get(k) is not None]
        for location, row in entries:
            ref = {"journal": journal, "slot": slot, "location": location}
            try:
                if not isinstance(row, dict) or not all(integer(row.get(k), 1) for k in NATIVE_FIELDS):
                    raise EvidenceError("missing native identity")
                stamps = row.get("qpc")
                if not isinstance(stamps, dict) or not all(integer(v) for v in stamps.values()):
                    raise EvidenceError("invalid native QPC timestamps")
                stamps = {k: qpc_ns(v, frequency) for k, v in stamps.items()}
                ack = row.get("ack_observed_qpc", 0)
                if not integer(ack):
                    raise EvidenceError("invalid ack timestamp")
                stamps["ack_observed"] = qpc_ns(ack, frequency)
                ticket = row.get("ticket")
                try:
                    key = ticket_key(ticket)
                except EvidenceError:
                    # Unticketed cancelled/active frames still have local timing.
                    if isinstance(ticket, dict) and ticket.get("sequence") == 0:
                        key = None
                    else:
                        raise
                if key is not None and any(ticket[k] != row[k] for k in ("frame", "tracking", "space", "predicted_ns")):
                    raise EvidenceError("native key/ticket identity mismatch")
                rows.append({"ref": ref, "identity": {k: row[k] for k in NATIVE_FIELDS},
                             "ticket": ticket if key else None, "key": key, "stamps": stamps,
                             "end": row.get("end"), "width": row.get("width"), "height": row.get("height"),
                             "sharing_mode": row.get("sharing_mode")})
            except EvidenceError as exc:
                issues.append({**ref, "reason": str(exc)})
    return rows, metadata, issues, sentinels


def read_host(report):
    diagnostic = report.get("frame_diagnostics")
    if not isinstance(diagnostic, dict) or diagnostic.get("schema") != 1 or diagnostic.get("clock") != HOST_CLOCK:
        raise EvidenceError("unsupported or missing host frame_diagnostics schema/clock")
    rows = require_rows(diagnostic, "host")
    copies, issues, ranges = defaultdict(list), [], []
    for i, row in enumerate(rows):
        ref = {"row": i, "host_frame": row.get("frame")}
        start, end = row.get("started_ns"), row.get("ended_ns")
        if not integer(start, 1) or not integer(end, 1) or end < start:
            issues.append({**ref, "reason": "invalid_host_frame_clock"})
            continue
        ranges.append((start, end))
        for kind, copy in (("current", row), ("prior_copy", row.get("prior_copy"))):
            if copy is None:
                continue
            if not isinstance(copy, dict):
                issues.append({**ref, "kind": kind, "reason": "invalid_copy_object"})
                continue
            observed = any(copy.get(k) is True for k in ("copy_submitted", "copy_pending", "copy_completed", "copy_retired", "copy_cancelled", "copy_composed"))
            if copy.get("copy_ticket") is None and not observed and not copy.get("copy_sequence"):
                continue
            try:
                key = ticket_key(copy.get("copy_ticket"))
                if copy.get("copy_sequence") != copy["copy_ticket"]["sequence"]:
                    raise EvidenceError("copy sequence/ticket mismatch")
                stamps = {f"host_copy_{stage}": copy.get(f"copy_{stage}_ns", 0)
                          for stage in ("submitted", "completed", "composed")}
                if not all(integer(v) for v in stamps.values()):
                    raise EvidenceError("invalid host copy timestamp")
                if any(v > end for v in stamps.values()):
                    raise EvidenceError("copy timestamp after observing host frame")
                copies[key].append({"ref": {**ref, "kind": kind}, "stamps": stamps,
                                    "ticket": copy["copy_ticket"], "cancelled": copy.get("copy_cancelled") is True,
                                    "overflow": row.get("copy_observation_overflow") is True})
            except EvidenceError as exc:
                issues.append({**ref, "kind": kind, "reason": str(exc)})
    merged = {}
    for key, observations in copies.items():
        stamps, conflicts = {}, []
        for stage in ("host_copy_submitted", "host_copy_completed", "host_copy_composed"):
            values = {o["stamps"][stage] for o in observations if o["stamps"][stage]}
            if len(values) > 1:
                conflicts.append(stage)
            stamps[stage] = next(iter(values)) if len(values) == 1 else 0
        merged[key] = {"ticket": observations[0]["ticket"], "stamps": stamps,
                       "observations": [o["ref"] for o in observations], "conflicting_stamps": conflicts,
                       "cancelled": any(o["cancelled"] for o in observations),
                       "overflow": any(o["overflow"] for o in observations)}
    sentinels = {k: diagnostic.get(k) for k in ("first_rejected_frame", "first_input_success_gap")}
    focused = diagnostic.get("focused_render")
    sentinels["worst_focused_render_frame"] = focused.get("worst_frame") if isinstance(focused, dict) else None
    metadata = {k: diagnostic.get(k) for k in ("frames", "recorded", "dropped", "capacity", "copy_observation_overflow_frames")}
    metadata["retained_rows"] = len(rows)
    metadata["selection"] = "changed, slow, or failed frames; not every host frame"
    return merged, ranges, metadata, issues, sentinels


def analyze(native_report, host_report, start_ns=None, end_ns=None):
    """Return JSON-safe analysis for one explicitly paired saved session."""
    native, native_meta, native_issues, native_sentinels = read_native(native_report)
    host, host_ranges, host_meta, host_issues, host_sentinels = read_host(host_report)
    native_times = [v for row in native for v in row["stamps"].values() if v > 0]
    native_range = [min(native_times), max(native_times)] if native_times else None
    host_range = [min(a for a, _ in host_ranges), max(b for _, b in host_ranges)] if host_ranges else None
    overlap = ([max(native_range[0], host_range[0]), min(native_range[1], host_range[1])]
               if native_range and host_range else None)
    if overlap and overlap[0] >= overlap[1]:
        overlap = None
    if (start_ns is None) != (end_ns is None):
        raise EvidenceError("both window endpoints are required")
    if start_ns is not None:
        if not integer(start_ns, 1) or not integer(end_ns, 1) or start_ns >= end_ns:
            raise EvidenceError("invalid explicit QPC-nanosecond window")
        if not overlap or start_ns < overlap[0] or end_ns > overlap[1]:
            raise EvidenceError("explicit window must lie inside retained native/host overlap")
        window = [start_ns, end_ns]
    else:
        window = overlap
    by_ticket = defaultdict(list)
    by_identity = defaultdict(list)
    for index, row in enumerate(native):
        by_identity[(row["ref"]["journal"], *row["identity"].values())].append(index)
        if row["key"] is not None:
            by_ticket[row["key"]].append(index)
    duplicate_identity = {i for indices in by_identity.values() if len(indices) > 1 for i in indices}
    ambiguous_ticket = {i for indices in by_ticket.values() if len(indices) > 1 for i in indices}
    phase_statuses = defaultdict(Counter)
    cohorts, results, excluded, joined_keys = {}, [], [], set()
    for i, row in enumerate(native):
        public = {k: row[k] for k in ("ref", "identity", "ticket", "end")}
        begin = row["stamps"].get("begin", 0)
        if window is None or not begin or not window[0] <= begin <= window[1]:
            excluded.append({**public, "reason": "no_overlap" if window is None else "begin_missing_or_outside_window"})
            continue
        match = host.get(row["key"])
        status = ("ambiguous_native_identity" if i in duplicate_identity else
                  "ambiguous_native_ticket" if i in ambiguous_ticket else
                  "unticketed_native_row" if row["key"] is None else
                  "no_host_ticket" if match is None else
                  "conflicting_host_timestamps" if match["conflicting_stamps"] else "joined_full_ticket")
        stamps = dict(row["stamps"])
        if status == "joined_full_ticket":
            stamps.update(match["stamps"])
            joined_keys.add(row["key"])
        public.update(join_status=status, host_observations=match["observations"] if match else [],
                      host_conflicting_stamps=match["conflicting_stamps"] if match else [],
                      host_cancelled=match["cancelled"] if match else None,
                      host_observation_overflow=match["overflow"] if match else None,
                      phase_durations_ns={}, phase_status={})
        # Keep reset/owner/source/resolution cohorts separate, even in one window.
        cohort = {"journal": row["ref"]["journal"], "slot": row["ref"]["slot"],
                  **{k: row["identity"][k] for k in ("channel", "owner", "device", "space")},
                  "session": row["ticket"]["session"] if row["ticket"] else None,
                  "resource_epoch": row["ticket"]["resource_epoch"] if row["ticket"] else None,
                  **{k: row[k] for k in ("width", "height", "sharing_mode", "end")}}
        cohort_key = json.dumps(cohort, sort_keys=True)
        if cohort_key not in cohorts:
            cohorts[cohort_key] = {"identity": cohort, "rows": 0, "values": defaultdict(list), "statuses": defaultdict(Counter)}
        cohort_data = cohorts[cohort_key]
        cohort_data["rows"] += 1
        for name, (left, right) in PHASES.items():
            a, b = stamps.get(left, 0), stamps.get(right, 0)
            phase_status = ("ambiguous_identity" if i in duplicate_identity or i in ambiguous_ticket else
                            "missing_boundary" if not a or not b else
                            "negative_clock_delta" if b < a else
                            "outside_window" if a < window[0] or b > window[1] else "measured")
            public["phase_status"][name] = phase_status
            phase_statuses[name][phase_status] += 1
            cohort_data["statuses"][name][phase_status] += 1
            if phase_status == "measured":
                public["phase_durations_ns"][name] = b - a
                cohort_data["values"][name].append(b - a)
        public["incomplete_phases"] = [k for k, v in public["phase_status"].items() if v != "measured"]
        results.append(public)
    unmatched_host = []
    for key, row in host.items():
        if key in joined_keys:
            continue
        reason = ("no_native_full_ticket" if key not in by_ticket else
                  "ambiguous_native_ticket" if len(by_ticket[key]) > 1 else
                  "conflicting_host_timestamps" if row["conflicting_stamps"] else "native_row_outside_window")
        unmatched_host.append({**row, "reason": reason})
    def phase_report(values, statuses):
        return {name: {**summary(values[name]), "status_counts": dict(statuses[name])} for name in PHASES}
    cohort_output = [{"identity": c["identity"], "rows": c["rows"],
                      "phases": phase_report(c["values"], c["statuses"])} for c in cohorts.values()]
    counts = Counter(row["join_status"] for row in results)
    return {
        "schema": "fvr.offline-frame-timing-analysis.v1",
        "evidence_level": "saved_journal_analysis; input provenance supplied by caller",
        "status": "joined_partial_pipeline" if joined_keys else "no_joinable_rows_in_window",
        "clock": "same-boot Windows QPC; native ticks converted with recorded frequency to host QPC ns",
        "quantiles": "median; nearest-rank p95/p99; retained samples only",
        "native_range_qpc_ns": native_range, "host_range_qpc_ns": host_range,
        "window_qpc_ns": window, "window_selection": "explicit" if start_ns is not None else "retained_overlap",
        "join_contract": "complete session/resource_epoch/sequence/frame/tracking/space/predicted_ns ticket; unique native request/channel/owner/device/journal-slot",
        "native_journals": native_meta, "host_journal": host_meta,
        "counts": {"retained_native_rows": len(native), "selected_native_rows": len(results),
                   "excluded_native_rows": len(excluded), "unique_host_tickets": len(host),
                   "joined_tickets": len(joined_keys), "unmatched_host_tickets": len(unmatched_host),
                   "rows_with_unmeasured_phases": sum(bool(r["incomplete_phases"]) for r in results),
                   "native_invalid_rows_or_journals": len(native_issues), "host_invalid_rows_or_observations": len(host_issues),
                   "join_status": dict(counts)},
        "cohorts": cohort_output,
        # Combined quantiles are deliberately omitted: reset/source cohorts are separate.
        "phase_status_counts": {k: dict(v) for k, v in phase_statuses.items()},
        "rows": results, "excluded_native": excluded, "unmatched_host": unmatched_host,
        "issues": {"native": native_issues, "host": host_issues},
        "excluded_sentinels": {"native": native_sentinels, "host": host_sentinels},
        "limits": LIMITS,
    }


def hud_sample_state(sample):
    if not isinstance(sample, dict) or not all(type(sample.get(k)) is int for k in HUD_SAMPLE_FIELDS):
        return "missing_or_invalid_sample_fields"
    return "nil_sample" if not any(sample[k] for k in HUD_SAMPLE_FIELDS) else "recorded_sample"


def hud_owner(sample):
    fields = ("actor_generation", "hud_scene_generation", "space_generation")
    if isinstance(sample, dict) and all(integer(sample.get(k), 1) for k in fields):
        return tuple(sample[k] for k in fields)
    return None


def hud_owner_relation(left, right):
    a, b = hud_owner(left), hud_owner(right)
    return "not_comparable" if a is None or b is None else "same" if a == b else "different"


def hud_clock_check(sample, at_ns):
    state = hud_sample_state(sample)
    if state != "recorded_sample":
        return {"state": state}
    observed, deadline = sample["observed_ns"], sample["deadline_ns"]
    state = ("clock_unavailable" if not integer(at_ns, 1) else
             "sample_clock_unavailable" if observed <= 0 or deadline <= 0 else
             "invalid_sample_interval" if deadline <= observed else
             "observation_in_future" if observed > at_ns else
             "deadline_elapsed" if deadline <= at_ns else "within_recorded_window")
    return {"state": state, "observed_ns": observed, "deadline_ns": deadline, "at_ns": at_ns}


def analyze_hud(host_report):
    """Audit host observations without replaying admission or inventing native clocks."""
    diagnostic = host_report.get("frame_diagnostics")
    if not isinstance(diagnostic, dict) or diagnostic.get("schema") != 1 or diagnostic.get("clock") != HOST_CLOCK:
        raise EvidenceError("unsupported or missing host frame_diagnostics schema/clock")
    gap = diagnostic.get("first_hud_gap")
    result = {"schema": "fvr.offline-hud-gap-analysis.v1",
              "evidence_level": "saved_host_journal_analysis; input provenance supplied by caller",
              "status": "no_hud_gap_journal", "clock": "host-wire QPC ns; no native owner/scene clocks",
              "retained_rows": 0, "decision_counts": {}, "rows": [], "limits": HUD_LIMITS}
    if gap is None:
        return result
    if not isinstance(gap, dict):
        raise EvidenceError("first_hud_gap must be an object")
    rows = require_rows(gap, "first_hud_gap")
    capacity = gap.get("capacity")
    if not integer(capacity, 1) or capacity > 32 or len(rows) > capacity:
        raise EvidenceError("first_hud_gap exceeds supported bounded capacity (32)")
    result.update(status="retained_hud_observations" if rows else "no_captured_hud_gap",
                  retained_rows=len(rows), capacity=capacity, frozen=gap.get("frozen"),
                  recorded_scope=gap.get("scope"))
    prior = rows[0].get("ammo_hud") if rows else None
    prior_image = (prior.get("image") if isinstance(prior, dict) and prior.get("decision") == "submitted"
                   and rows[0].get("end_succeeded") is True else None)
    decisions = Counter()
    for index, row in enumerate(rows):
        hud = row.get("ammo_hud")
        hud = hud if isinstance(hud, dict) else {}
        decision = hud.get("decision")
        decision = decision if isinstance(decision, str) else "missing_decision"
        decisions[decision] += 1
        checks = {}
        for boundary, clock, names in (
                ("receive", "receive_ns", ("left", "right", "count_before_receive", "owner_before_receive")),
                ("first_select", "select_ns", ("count", "owner", "image")),
                ("final_select", "final_ns", ("count", "owner", "image"))):
            checks[boundary] = {name: hud_clock_check(hud.get(name), hud.get(clock)) for name in names}
        left, right = hud.get("left"), hud.get("right")
        paired = hud_owner_relation(left, right)
        wire_vs_prior = (hud_owner_relation(left, prior_image)
                         if hud.get("source_matches") is True and paired == "same" else "not_comparable")
        relations = {"wire_pair_owner": paired, "wire_owner_vs_prior_visible": wire_vs_prior,
                     "count_vs_owner": hud_owner_relation(hud.get("count"), hud.get("owner")),
                     "image_vs_owner": hud_owner_relation(hud.get("image"), hud.get("owner"))}
        facts = []
        for name in ("left", "right"):
            state = hud_sample_state(hud.get(name))
            if state != "recorded_sample":
                facts.append(f"wire_{name}_{state}")
        if hud.get("source_matches") is False:
            facts.append("wire_source_did_not_match")
        if paired == "different":
            facts.append("wire_pair_owner_mismatch")
        for name in ("left", "right", "count", "owner", "image"):
            owner = hud_owner(hud.get(name))
            if owner and integer(row.get("space"), 1) and owner[2] != row["space"]:
                facts.append(f"{name}_space_differs_from_host_frame")
        if relations["count_vs_owner"] == "different":
            facts.append("retained_count_owner_mismatch")
        if wire_vs_prior == "different":
            facts.append("wire_owner_differs_from_prior_visible")
        receive = checks["receive"]
        if (all(receive[k]["state"] == "deadline_elapsed" for k in ("left", "right"))
                and receive["owner_before_receive"]["state"] == "within_recorded_window"):
            facts.append("wire_deadlines_elapsed_while_prior_owner_window_open")
        if (hud_sample_state(hud.get("count_before_receive")) == "recorded_sample"
                and hud_sample_state(hud.get("owner_before_receive")) == "recorded_sample"
                and hud.get("count_before_receive") == hud.get("count")
                and hud.get("owner_before_receive") == hud.get("owner")):
            facts.append("retained_count_and_owner_unchanged_since_receive")
        for boundary in ("first_select", "final_select"):
            if checks[boundary]["owner"]["state"] == "deadline_elapsed":
                facts.append(f"owner_deadline_elapsed_at_{boundary}")
        menu_layers = row.get("menu_layers")
        context = ("menu_may_intentionally_suppress_hud"
                   if row.get("menu") is True or integer(menu_layers, 1) else
                   "submission_failed" if row.get("end_succeeded") is False else
                   "no_world_layer" if row.get("world_layer") is False else
                   "render_inactive" if any(row.get(k) is False for k in ("focused", "present", "should_render")) else
                   "owner_transition_may_intentionally_suppress_hud" if wire_vs_prior == "different" else
                   "focused_world_observation" if all(row.get(k) is True for k in
                       ("focused", "present", "should_render", "world_layer", "end_succeeded")) else
                   "context_evidence_missing")
        start, end, received = row.get("started_ns"), row.get("ended_ns"), hud.get("receive_ns")
        receive_scope = ("clock_unavailable" if not all(integer(v, 1) for v in (start, end, received)) or end < start else
                         "predates_observing_frame" if received < start else
                         "after_observing_frame" if received > end else "within_observing_frame")
        result["rows"].append({"row": index, "frame": row.get("frame"), "decision": decision,
                               "gate": HUD_GATES.get(decision, "unknown_recorded_gate"),
                               "context": context, "receive_scope": receive_scope, "facts": facts,
                               "owner_relations": relations, "clock_checks": checks, "recorded": row})
    result["decision_counts"] = dict(decisions)
    return result


def analyze_recent_hud(host_report, start_ns, end_ns):
    """Select actual in-window witnesses; episode spans alone prove no interior gate."""
    if not integer(start_ns, 1) or not integer(end_ns, 1) or end_ns <= start_ns:
        raise EvidenceError("recent HUD mode requires increasing positive start/end QPC ns")
    diagnostic = host_report.get("frame_diagnostics")
    if not isinstance(diagnostic, dict) or diagnostic.get("schema") != 1 or diagnostic.get("clock") != HOST_CLOCK:
        raise EvidenceError("unsupported or missing host frame_diagnostics schema/clock")
    result = {"schema": "fvr.offline-recent-hud-gap-analysis.v1",
              "evidence_level": "saved_host_journal_analysis; input provenance supplied by caller",
              "status": "no_recent_hud_gap_journal", "window": {"start_ns": start_ns, "end_ns": end_ns},
              "in_window_omission_witnesses": 0, "episodes": [],
              "limits": ["At most 32 recent episodes retain prior submission, first omission and first recovery; intermediate frames have no full witness.",
                         "Only omission witnesses wholly inside the requested QPC window are selected; prior/recovery rows are context and may be outside.",
                         "Episode span overlap does not prove an omission or its gate throughout that span; this is not arbitrary-window completeness."] + HUD_LIMITS[1:]}
    journal = diagnostic.get("recent_hud_gaps")
    if journal is None:
        return result
    if not isinstance(journal, dict) or journal.get("schema") != 1:
        raise EvidenceError("unsupported recent_hud_gaps schema")
    capacity, episodes = journal.get("capacity"), journal.get("episodes")
    if not integer(capacity, 1) or capacity > 32 or not isinstance(episodes, list) or len(episodes) > capacity:
        raise EvidenceError("recent_hud_gaps exceeds supported bounded capacity (32 episodes)")
    observed_start, observed_end = journal.get("observed_start_ns"), journal.get("observed_end_ns")
    overwritten, through, seen = journal.get("overwritten"), journal.get("overwritten_through_ns"), journal.get("episodes_seen")
    if (journal.get("clock_valid") is not True or not integer(observed_start, 1)
            or not integer(observed_end, observed_start) or not integer(overwritten, 0)
            or not integer(seen, len(episodes)) or not integer(through, 0)
            or through > observed_end or (overwritten == 0) != (through == 0)):
        raise EvidenceError("invalid recent HUD observation/overwrite clock metadata")
    result.update(status="no_retained_in_window_omission_witness", capacity=capacity,
                  retained_episodes=len(episodes), episodes_seen=seen, overwritten=overwritten,
                  overwritten_through_ns=through,
                  window_outside_observed_range=start_ns < observed_start or end_ns > observed_end,
                  overwritten_overlap_possible=overwritten > 0 and start_ns <= through and end_ns >= observed_start)
    previous_end = 0
    for index, episode in enumerate(episodes):
        if not isinstance(episode, dict):
            raise EvidenceError("recent HUD episode must be an object")
        rows = require_rows(episode, "recent HUD episode")
        if len(rows) not in (2, 3) or episode.get("recovered") is not (len(rows) == 3):
            raise EvidenceError("recent HUD episode requires prior/omission and optional recovery")
        previous_frame, previous_ns = 0, 0
        for row in rows:
            frame, start, end = row.get("frame"), row.get("started_ns"), row.get("ended_ns")
            if (not integer(frame, previous_frame + 1) or not integer(start, max(observed_start, previous_ns))
                    or not integer(end, start) or end > observed_end):
                raise EvidenceError("invalid recent HUD row frame/QPC order")
            previous_frame, previous_ns = frame, end
        prior, omission = rows[:2]
        hud = omission.get("ammo_hud")
        if (not isinstance(prior.get("ammo_hud"), dict) or prior["ammo_hud"].get("decision") != "submitted"
                or prior.get("end_succeeded") is not True or not isinstance(hud, dict)
                or hud.get("decision") not in HUD_GATES or hud["decision"] == "submitted"
                or any(omission.get(k) is not True for k in ("focused", "present", "should_render", "world_layer", "end_succeeded"))
                or omission.get("menu") is not False):
            raise EvidenceError("invalid recent HUD prior/omission witness")
        last_ns, last_frame = episode.get("last_omitted_ns"), episode.get("last_omitted_frame")
        if (not integer(last_ns, omission["ended_ns"]) or last_ns > observed_end
                or not integer(last_frame, omission["frame"]) or not integer(episode.get("omitted_frames"), 1)
                or omission["started_ns"] < previous_end):
            raise EvidenceError("invalid recent HUD episode omission span")
        if len(rows) == 3:
            recovery = rows[2]
            if (recovery["started_ns"] < last_ns or recovery["frame"] <= last_frame
                    or not isinstance(recovery.get("ammo_hud"), dict)
                    or recovery["ammo_hud"].get("decision") != "submitted" or recovery.get("end_succeeded") is not True):
                raise EvidenceError("invalid recent HUD recovery witness")
        elif index != len(episodes) - 1:
            raise EvidenceError("only the newest recent HUD episode may be unrecovered")
        previous_end = rows[-1]["ended_ns"] if len(rows) == 3 else last_ns
        if last_ns < start_ns or omission["started_ns"] > end_ns:
            continue
        inside = start_ns <= omission["started_ns"] and omission["ended_ns"] <= end_ns
        # Reuse the exact original host-gate analysis with this episode's own prior.
        audit = analyze_hud({"frame_diagnostics": {"schema": 1, "clock": HOST_CLOCK,
                            "first_hud_gap": {"capacity": 3, "rows": rows}}})
        result["episodes"].append({"retained_index": index,
                                   "selection": "in_window_omission_witness" if inside else "overlap_without_in_window_omission_witness",
                                   "omitted_frames_in_episode": episode["omitted_frames"],
                                   "last_omitted_frame": last_frame, "last_omitted_ns": last_ns,
                                   "recovered": episode["recovered"], "rows": audit["rows"]})
        result["in_window_omission_witnesses"] += int(inside)
    if result["in_window_omission_witnesses"]:
        result["status"] = "retained_in_window_omission_witnesses"
    return result


def load_report(path, max_bytes):
    if path.stat().st_size > max_bytes:
        raise EvidenceError(f"{path}: exceeds input byte limit")
    with path.open("rb") as stream:
        raw = stream.read(max_bytes + 1)
    if len(raw) > max_bytes:
        raise EvidenceError(f"{path}: exceeds input byte limit")
    def unique_object(pairs):
        result = {}
        for key, value in pairs:
            if key in result:
                raise EvidenceError(f"{path}: duplicate JSON key {key}")
            result[key] = value
        return result
    def invalid_constant(value):
        raise EvidenceError(f"{path}: non-finite JSON value {value}")
    document = json.loads(raw.decode("utf-8-sig"), object_pairs_hook=unique_object, parse_constant=invalid_constant)
    if not isinstance(document, dict):
        raise EvidenceError(f"{path}: expected JSON object")
    return document, {"path": str(path.resolve()), "bytes": len(raw), "sha256": hashlib.sha256(raw).hexdigest()}


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument("--native", type=Path)
    mode.add_argument("--hud-only", action="store_true", help="audit only the bounded host first_hud_gap snapshot")
    mode.add_argument("--hud-recent", action="store_true", help="audit recent HUD episodes within explicit QPC bounds")
    parser.add_argument("--host", type=Path, required=True)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--start-qpc-ns", type=int)
    parser.add_argument("--end-qpc-ns", type=int)
    parser.add_argument("--max-input-mib", type=int, default=128)
    args = parser.parse_args(argv)
    try:
        if args.max_input_mib < 1:
            raise EvidenceError("max-input-mib must be positive")
        inputs = [args.host] + ([args.native] if args.native else [])
        if args.output and args.output.resolve() in (p.resolve() for p in inputs):
            raise EvidenceError("output must not overwrite an input report")
        if args.hud_recent:
            host, host_source = load_report(args.host, args.max_input_mib * 1024 * 1024)
            result = analyze_recent_hud(host, args.start_qpc_ns, args.end_qpc_ns)
            result["inputs"] = {"host": host_source}
        elif args.hud_only:
            if args.start_qpc_ns is not None or args.end_qpc_ns is not None:
                raise EvidenceError("HUD-only snapshot is separate from timing windows; omit window arguments")
            host, host_source = load_report(args.host, args.max_input_mib * 1024 * 1024)
            result = analyze_hud(host)
            result["inputs"] = {"host": host_source}
        else:
            native, native_source = load_report(args.native, args.max_input_mib * 1024 * 1024)
            host, host_source = load_report(args.host, args.max_input_mib * 1024 * 1024)
            result = analyze(native, host, args.start_qpc_ns, args.end_qpc_ns)
            result["inputs"] = {"native": native_source, "host": host_source}
        text = json.dumps(result, indent=2, allow_nan=False) + "\n"
        if args.output:
            args.output.write_text(text, encoding="utf-8")
        else:
            sys.stdout.write(text)
        return 0
    except (EvidenceError, OSError, UnicodeError, json.JSONDecodeError) as exc:
        print(f"frame timing analysis refused: {exc}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
