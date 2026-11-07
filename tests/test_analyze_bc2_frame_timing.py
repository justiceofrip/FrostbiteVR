"""Deterministic journal contract regressions; no game/runtime activity."""

import contextlib
import copy
import io
import json
from pathlib import Path
import sys
import tempfile
import unittest
import uuid

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import analyze_bc2_frame_timing as timing


@contextlib.contextmanager
def scratch_directory():
    # Python 3.13's Windows mode=0700 temp ACL is inaccessible to some sandbox
    # tokens. A unique directory with normal inherited ACLs works in both.
    directory = Path(tempfile.gettempdir()) / ("frame-timing-test-" + uuid.uuid4().hex)
    directory.mkdir(mode=0o777)
    try:
        yield directory
    finally:
        for child in directory.iterdir():
            child.unlink()
        directory.rmdir()


def ticket(sequence=1):
    return dict(resource_epoch=3, sequence=sequence, frame=21203 + sequence,
                tracking=6922 + sequence, space=2, predicted_ns=177129697956542 + sequence,
                session=[2, 40, 1, 64, 0, 0, 0, 0, 194, 44, 1, 64, 0, 0, 0, 0])


def native_row(sequence=1):
    t = ticket(sequence)
    return dict(channel=1, request=1111 + sequence, owner=5064015825085266496,
                frame=t["frame"], device=1, tracking=t["tracking"], space=t["space"],
                predicted_ns=t["predicted_ns"], ticket=t, width=1024, height=768,
                sharing_mode=3, end="consumed", ack_observed_qpc=0,
                qpc=dict(begin=20000, capture_left_begin=21000, capture_left_queued=21010,
                         capture_right_begin=22000, capture_right_queued=22010,
                         restored=22100, export_begin=22200, producer_fence_ready=23000,
                         staged=23100, published=23200, reaped=40000, released=41000))


def snapshot(rows=None):
    rows = [native_row()] if rows is None else rows
    return dict(schema=timing.NATIVE_SCHEMA, clock="qpc_ticks", frequency=10_000_000,
                available=True, count=len(rows), completed=len(rows) + 200,
                overwritten=200, dropped=0, unmatched=0, invalid_clock=0,
                rows=rows, active=None, retiring=None, first_non_consumed=None)


def copy_row(sequence=1, completed=True):
    return dict(copy_ticket=ticket(sequence), copy_sequence=sequence,
                copy_submitted=True, copy_pending=not completed, copy_completed=completed,
                copy_retired=False, copy_cancelled=False, copy_composed=False,
                copy_submitted_ns=2_500_000, copy_completed_ns=3_000_000 if completed else 0,
                copy_composed_ns=0)


def host_row(sequence=1, completed=True):
    return dict(frame=7300 + sequence, started_ns=1_000_000, ended_ns=5_000_000,
                prior_copy=None, copy_observation_overflow=False, **copy_row(sequence, completed))


def reports():
    return ({"native_frame_timing": snapshot(), "native_frame_timing_slots": []},
            {"frame_diagnostics": {"schema": 1, "clock": timing.HOST_CLOCK,
                                   "frames": 1000, "recorded": 500, "dropped": 499,
                                   "rows": [host_row()]}})


class TimingAnalysisTests(unittest.TestCase):
    def test_exact_join_has_known_durations_and_records_loss(self):
        n, h = reports()
        result = timing.analyze(n, h)
        self.assertEqual(result["counts"]["joined_tickets"], 1)
        row = result["rows"][0]
        self.assertEqual(row["identity"]["request"], 1112)
        self.assertEqual(row["phase_durations_ns"]["published_to_host_copy_submitted"], 180000)
        self.assertEqual(row["phase_durations_ns"]["host_copy_submitted_to_completed_observed"], 500000)
        self.assertEqual(result["native_journals"][0]["overwritten"], 200)
        self.assertEqual(result["host_journal"]["dropped"], 499)

    def test_all_full_ticket_components_are_required(self):
        for field in (*timing.TICKET_FIELDS, "session"):
            with self.subTest(field=field):
                n, h = reports()
                t = h["frame_diagnostics"]["rows"][0]["copy_ticket"]
                if field == "session":
                    t[field][0] += 1
                else:
                    t[field] += 1
                if field == "sequence":
                    h["frame_diagnostics"]["rows"][0]["copy_sequence"] = t[field]
                result = timing.analyze(n, h)
                self.assertEqual(result["counts"]["joined_tickets"], 0)
                self.assertEqual(result["rows"][0]["join_status"], "no_host_ticket")

    def test_native_ticket_frame_mismatch_is_quarantined(self):
        n, h = reports()
        n["native_frame_timing"]["rows"][0]["frame"] += 1
        result = timing.analyze(n, h)
        self.assertEqual(result["counts"]["native_invalid_rows_or_journals"], 1)
        self.assertEqual(result["counts"]["joined_tickets"], 0)

    def test_source_slot_identity_is_preserved(self):
        n, h = reports()
        n["native_frame_timing_slots"] = [{"slot": 1, "timing": n["native_frame_timing"]}]
        n["native_frame_timing"] = snapshot([])
        n["native_frame_timing"]["frequency"] = 0
        result = timing.analyze(n, h)
        self.assertEqual(result["counts"]["joined_tickets"], 1)
        self.assertEqual(result["rows"][0]["ref"]["slot"], 1)

    def test_ticket_collision_across_slots_refuses_join(self):
        n, h = reports()
        n["native_frame_timing_slots"] = [{"slot": 1, "timing": copy.deepcopy(n["native_frame_timing"])}]
        n["native_frame_timing_slots"][0]["timing"]["rows"][0]["request"] += 1
        result = timing.analyze(n, h)
        self.assertEqual(result["counts"]["joined_tickets"], 0)
        self.assertEqual(result["rows"][0]["join_status"], "ambiguous_native_ticket")
        self.assertEqual(result["rows"][0]["phase_durations_ns"], {})

    def test_duplicate_native_identity_is_not_counted_twice(self):
        n, h = reports()
        n["native_frame_timing"]["rows"] *= 2
        n["native_frame_timing"]["count"] = 2
        result = timing.analyze(n, h)
        self.assertEqual(result["counts"]["joined_tickets"], 0)
        self.assertEqual(result["rows"][0]["join_status"], "ambiguous_native_identity")

    def test_repeated_host_observations_count_one_pair(self):
        n, h = reports()
        h["frame_diagnostics"]["rows"] = [host_row(completed=False), host_row()]
        result = timing.analyze(n, h)
        self.assertEqual(result["counts"]["joined_tickets"], 1)
        self.assertEqual(len(result["rows"][0]["host_observations"]), 2)
        self.assertEqual(result["cohorts"][0]["phases"]["host_copy_submitted_to_completed_observed"]["samples"], 1)

    def test_prior_copy_does_not_join_successor_ticket(self):
        n, h = reports()
        h["frame_diagnostics"]["rows"] = [host_row(2, False)]
        h["frame_diagnostics"]["rows"][0]["prior_copy"] = copy_row(1)
        result = timing.analyze(n, h)
        self.assertEqual(result["counts"]["joined_tickets"], 1)
        self.assertEqual(result["counts"]["unmatched_host_tickets"], 1)
        self.assertEqual(result["rows"][0]["host_observations"][0]["kind"], "prior_copy")

    def test_conflicting_timestamp_is_not_min_max_merged(self):
        n, h = reports()
        h["frame_diagnostics"]["rows"].append(host_row())
        h["frame_diagnostics"]["rows"][1]["copy_completed_ns"] += 1
        result = timing.analyze(n, h)
        self.assertEqual(result["counts"]["joined_tickets"], 0)
        self.assertEqual(result["rows"][0]["join_status"], "conflicting_host_timestamps")

    def test_negative_cross_process_delta_never_enters_statistics(self):
        n, h = reports()
        h["frame_diagnostics"]["rows"][0]["copy_submitted_ns"] = 2_200_000
        result = timing.analyze(n, h)
        row = result["rows"][0]
        self.assertEqual(row["phase_status"]["published_to_host_copy_submitted"], "negative_clock_delta")
        self.assertNotIn("published_to_host_copy_submitted", row["phase_durations_ns"])

    def test_zero_stamps_are_missing_not_zero_durations(self):
        n, h = reports()
        h["frame_diagnostics"]["rows"][0]["copy_completed_ns"] = 0
        n["native_frame_timing"]["rows"][0]["qpc"]["producer_fence_ready"] = 0
        result = timing.analyze(n, h)
        self.assertEqual(result["rows"][0]["phase_status"]["host_copy_submitted_to_completed_observed"], "missing_boundary")
        self.assertIsNone(result["cohorts"][0]["phases"]["native_export_to_fence_ready_observed"]["median_ms"])

    def test_boundary_straddling_window_is_not_mixed(self):
        n, h = reports()
        result = timing.analyze(n, h, 2_000_000, 2_800_000)
        self.assertEqual(result["rows"][0]["phase_status"]["host_copy_submitted_to_completed_observed"], "outside_window")
        self.assertEqual(result["rows"][0]["phase_durations_ns"]["published_to_host_copy_submitted"], 180000)

    def test_nonoverlapping_reports_return_explicit_no_data(self):
        n, h = reports()
        h["frame_diagnostics"]["rows"][0]["started_ns"] = 10_000_000
        h["frame_diagnostics"]["rows"][0]["ended_ns"] = 11_000_000
        result = timing.analyze(n, h)
        self.assertEqual(result["status"], "no_joinable_rows_in_window")
        self.assertIsNone(result["window_qpc_ns"])
        self.assertEqual(len(result["excluded_native"]), 1)

    def test_explicit_window_outside_overlap_is_refused(self):
        n, h = reports()
        for a, b in ((1, 100), (2_000_000, None), (3_000_000, 2_000_000)):
            with self.subTest(a=a, b=b), self.assertRaises(timing.EvidenceError):
                timing.analyze(n, h, a, b)

    def test_qpc_conversion_preserves_integer_epoch_precision(self):
        ticks, frequency = 9_007_199_254_740_993, 10_000_000
        self.assertEqual(timing.qpc_ns(ticks, frequency), ticks * 100)
        self.assertEqual(timing.qpc_ns(101, 3), 33_666_666_666)

    def test_wrong_clock_or_frequency_fails_closed(self):
        for target in ("host_clock", "native_clock", "frequency"):
            n, h = reports()
            if target == "host_clock":
                h["frame_diagnostics"]["clock"] = "steady_clock_ns"
            else:
                n["native_frame_timing"]["clock" if target == "native_clock" else target] = "xr_time" if target == "native_clock" else 0
            with self.subTest(target=target), self.assertRaises(timing.EvidenceError):
                timing.analyze(n, h)

    def test_sentinels_do_not_expand_window_or_samples(self):
        n, h = reports()
        first = native_row(50)
        first["qpc"]["begin"] = 1
        n["native_frame_timing"]["first_non_consumed"] = first
        h["frame_diagnostics"]["first_input_success_gap"] = host_row(50)
        result = timing.analyze(n, h)
        self.assertEqual(result["window_qpc_ns"][0], 2_000_000)
        self.assertEqual(result["counts"]["selected_native_rows"], 1)
        self.assertEqual(len(result["excluded_sentinels"]["native"]), 1)

    def test_unticketed_cancelled_row_keeps_local_phases(self):
        n, h = reports()
        r = n["native_frame_timing"]["rows"][0]
        r["ticket"] = {"sequence": 0}
        r["end"] = "cancelled"
        result = timing.analyze(n, h)
        self.assertEqual(result["rows"][0]["join_status"], "unticketed_native_row")
        self.assertIn("native_begin_to_restored", result["rows"][0]["phase_durations_ns"])

    def test_different_epoch_cohorts_are_not_pooled(self):
        n, h = reports()
        r = native_row(2)
        r["ticket"]["resource_epoch"] = 4
        n["native_frame_timing"] = snapshot([native_row(), r])
        result = timing.analyze(n, h)
        self.assertEqual(len(result["cohorts"]), 2)
        self.assertNotIn("phases", result)

    def test_quantiles_count_samples_and_show_tail(self):
        result = timing.summary([1_000_000, 2_000_000, 3_000_000, 100_000_000])
        self.assertEqual(result, dict(samples=4, median_ms=2.5, p95_ms=100.0, p99_ms=100.0, max_ms=100.0))

    def test_different_slot_clock_frequencies_are_refused(self):
        n, h = reports()
        s = snapshot([native_row(2)])
        s["frequency"] *= 2
        n["native_frame_timing_slots"] = [{"slot": 1, "timing": s}]
        with self.assertRaises(timing.EvidenceError):
            timing.analyze(n, h)

    def test_unavailable_native_journal_is_not_empty_success(self):
        n, h = reports()
        n["native_frame_timing"]["available"] = False
        result = timing.analyze(n, h)
        self.assertEqual(result["counts"]["joined_tickets"], 0)
        self.assertEqual(result["issues"]["native"][0]["reason"], "journal_unavailable")

    def test_empty_journal_count_mismatch_is_refused(self):
        n, h = reports()
        n["native_frame_timing"]["rows"] = []
        with self.assertRaises(timing.EvidenceError):
            timing.analyze(n, h)

    def test_invalid_host_clock_and_sequence_are_explicit(self):
        for field, value in (("ended_ns", 10), ("copy_sequence", 99), ("copy_completed_ns", -1)):
            n, h = reports()
            h["frame_diagnostics"]["rows"][0][field] = value
            result = timing.analyze(n, h)
            self.assertEqual(result["counts"]["joined_tickets"], 0)
            self.assertEqual(len(result["issues"]["host"]), 1)

    def test_active_native_row_is_explicitly_incomplete(self):
        n, h = reports()
        active = native_row(2)
        active["end"] = "pending"
        active["qpc"] = {"begin": 22000}
        active["ticket"] = {"sequence": 0}
        n["native_frame_timing"]["active"] = active
        result = timing.analyze(n, h)
        self.assertEqual(result["counts"]["selected_native_rows"], 2)
        self.assertEqual(result["rows"][1]["ref"]["location"], "active")
        self.assertEqual(result["rows"][1]["phase_status"]["native_begin_to_published"], "missing_boundary")

    def test_cli_input_identity_size_and_overwrite_guards(self):
        with scratch_directory() as folder:
            p = Path(folder)
            n, h = reports()
            (p / "native.json").write_text(json.dumps(n), encoding="utf-8")
            (p / "host.json").write_text(json.dumps(h), encoding="utf-8")
            argv = ["--native", str(p / "native.json"), "--host", str(p / "host.json")]
            self.assertEqual(timing.main([*argv, "--output", str(p / "result.json")]), 0)
            result = json.loads((p / "result.json").read_text(encoding="utf-8"))
            self.assertEqual(len(result["inputs"]["native"]["sha256"]), 64)
            with contextlib.redirect_stderr(io.StringIO()):
                self.assertEqual(timing.main([*argv, "--output", str(p / "native.json")]), 2)
            with self.assertRaises(timing.EvidenceError):
                timing.load_report(p / "native.json", 1)

    def test_duplicate_json_keys_and_empty_report_are_refused(self):
        with scratch_directory() as folder:
            p = Path(folder) / "bad.json"
            p.write_text('{"frame_diagnostics":{},"frame_diagnostics":{}}', encoding="utf-8")
            with self.assertRaises(timing.EvidenceError):
                timing.load_report(p, 1000)
            p.write_text("", encoding="utf-8")
            with self.assertRaises(json.JSONDecodeError):
                timing.load_report(p, 1000)


if __name__ == "__main__":
    unittest.main()
