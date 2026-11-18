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


# Synthetic saved wire fixture, deliberately using a count deadline older than
# its independent owner lease. No native owner/scene clocks are present.
HUD_SAVED_REPORT = r'''
{"frame_diagnostics":{"schema":1,"clock":"host_qpc_ns; predicted/retained age are xr_time",
 "first_hud_gap":{"capacity":32,"frozen":false,"scope":"prior submission and first omission",
 "rows":[
  {"frame":600,"space":2,"started_ns":1000000000,"ended_ns":1010000000,
   "predicted_ns":9000000000000000,"world_layer":true,"end_succeeded":true,
   "should_render":true,"focused":true,"present":true,"menu":false,"menu_layers":0,
   "context":"submitted","wire_owner_differs_from_prior_visible":false,
   "ammo_hud":{"decision":"submitted","receive_serial":70,"receive_ns":1001000000,
    "select_ns":1002000000,"final_ns":1003000000,"source_matches":true,
    "receive_accepted":true,"pending":false,"has_image":true,"source_space":2,
    "left":{"sequence":41,"observed_ns":990000000,"deadline_ns":1020000000,"actor_generation":7,"hud_scene_generation":9,"space_generation":2,"loaded":8,"reserve":24,"capacity":8,"tag":0},
    "right":{"sequence":41,"observed_ns":990000000,"deadline_ns":1020000000,"actor_generation":7,"hud_scene_generation":9,"space_generation":2,"loaded":8,"reserve":24,"capacity":8,"tag":0},
    "count_before_receive":{"sequence":41,"observed_ns":990000000,"deadline_ns":1020000000,"actor_generation":7,"hud_scene_generation":9,"space_generation":2,"loaded":8,"reserve":24,"capacity":8,"tag":0},
    "owner_before_receive":{"sequence":0,"observed_ns":1000000000,"deadline_ns":1200000000,"actor_generation":7,"hud_scene_generation":9,"space_generation":2,"loaded":0,"reserve":0,"capacity":0,"tag":1},
    "count":{"sequence":41,"observed_ns":990000000,"deadline_ns":1020000000,"actor_generation":7,"hud_scene_generation":9,"space_generation":2,"loaded":8,"reserve":24,"capacity":8,"tag":0},
    "owner":{"sequence":0,"observed_ns":1000000000,"deadline_ns":1200000000,"actor_generation":7,"hud_scene_generation":9,"space_generation":2,"loaded":0,"reserve":0,"capacity":0,"tag":1},
    "image":{"sequence":41,"observed_ns":990000000,"deadline_ns":1020000000,"actor_generation":7,"hud_scene_generation":9,"space_generation":2,"loaded":8,"reserve":24,"capacity":8,"tag":0}}},
  {"frame":601,"space":2,"started_ns":1110000000,"ended_ns":1220000000,
   "predicted_ns":9000000000000001,"world_layer":true,"end_succeeded":true,
   "should_render":true,"focused":true,"present":true,"menu":false,"menu_layers":0,
   "context":"hud_omitted","wire_owner_differs_from_prior_visible":false,
   "ammo_hud":{"decision":"no_sample","receive_serial":71,"receive_ns":1115000000,
    "select_ns":1210000000,"final_ns":0,"source_matches":true,
    "receive_accepted":false,"pending":false,"has_image":true,"source_space":2,
    "left":{"sequence":42,"observed_ns":1010000000,"deadline_ns":1100000000,"actor_generation":7,"hud_scene_generation":9,"space_generation":2,"loaded":7,"reserve":24,"capacity":8,"tag":0},
    "right":{"sequence":42,"observed_ns":1010000000,"deadline_ns":1100000000,"actor_generation":7,"hud_scene_generation":9,"space_generation":2,"loaded":7,"reserve":24,"capacity":8,"tag":0},
    "count_before_receive":{"sequence":41,"observed_ns":990000000,"deadline_ns":1020000000,"actor_generation":7,"hud_scene_generation":9,"space_generation":2,"loaded":8,"reserve":24,"capacity":8,"tag":0},
    "owner_before_receive":{"sequence":0,"observed_ns":1000000000,"deadline_ns":1200000000,"actor_generation":7,"hud_scene_generation":9,"space_generation":2,"loaded":0,"reserve":0,"capacity":0,"tag":1},
    "count":{"sequence":41,"observed_ns":990000000,"deadline_ns":1020000000,"actor_generation":7,"hud_scene_generation":9,"space_generation":2,"loaded":8,"reserve":24,"capacity":8,"tag":0},
    "owner":{"sequence":0,"observed_ns":1000000000,"deadline_ns":1200000000,"actor_generation":7,"hud_scene_generation":9,"space_generation":2,"loaded":0,"reserve":0,"capacity":0,"tag":1},
    "image":{"sequence":41,"observed_ns":990000000,"deadline_ns":1020000000,"actor_generation":7,"hud_scene_generation":9,"space_generation":2,"loaded":8,"reserve":24,"capacity":8,"tag":0}}}
 ]}}}
'''


class HudAnalysisTests(unittest.TestCase):
    def setUp(self):
        self.report = json.loads(HUD_SAVED_REPORT)
        self.rows = self.report["frame_diagnostics"]["first_hud_gap"]["rows"]
        self.hud = self.rows[1]["ammo_hud"]

    def saved_analysis(self):
        # Exercise the real CLI/load/hash path against an isolated saved report.
        with scratch_directory() as folder:
            host, output = Path(folder) / "host.json", Path(folder) / "hud.json"
            host.write_text(json.dumps(self.report), encoding="utf-8")
            self.assertEqual(timing.main(["--hud-only", "--host", str(host), "--output", str(output)]), 0)
            return json.loads(output.read_text(encoding="utf-8"))

    def test_stale_wire_count_does_not_expire_independent_owner(self):
        self.hud.update(decision="submitted", select_ns=1120000000, final_ns=1125000000)
        result = self.saved_analysis()
        row = result["rows"][1]
        self.assertEqual(row["gate"], "submitted")
        self.assertIn("wire_deadlines_elapsed_while_prior_owner_window_open", row["facts"])
        self.assertIn("retained_count_and_owner_unchanged_since_receive", row["facts"])
        self.assertNotIn("owner_deadline_elapsed_at_first_select", row["facts"])
        self.assertEqual(row["clock_checks"]["first_select"]["count"]["state"], "deadline_elapsed")
        self.assertEqual(row["clock_checks"]["first_select"]["owner"]["state"], "within_recorded_window")
        self.assertEqual(row["recorded"], self.rows[1])
        self.assertEqual(set(result["inputs"]), {"host"})
        self.assertEqual(len(result["inputs"]["host"]["sha256"]), 64)
        self.assertNotIn("cohorts", result)

    def test_owner_deadline_expiry_is_at_recorded_first_select(self):
        row = self.saved_analysis()["rows"][1]
        self.assertEqual(row["gate"], "first_select_no_sample")
        self.assertIn("owner_deadline_elapsed_at_first_select", row["facts"])
        self.assertIn("wire_deadlines_elapsed_while_prior_owner_window_open", row["facts"])
        self.assertEqual(row["clock_checks"]["first_select"]["owner"],
                         {"state": "deadline_elapsed", "observed_ns": 1000000000,
                          "deadline_ns": 1200000000, "at_ns": 1210000000})
        self.assertEqual(row["clock_checks"]["final_select"]["owner"]["state"], "clock_unavailable")

    def test_owner_expiring_exactly_at_final_select_is_distinct(self):
        self.hud.update(decision="final_no_sample", select_ns=1199999999, final_ns=1200000000)
        row = self.saved_analysis()["rows"][1]
        self.assertEqual(row["gate"], "final_select_no_sample")
        self.assertNotIn("owner_deadline_elapsed_at_first_select", row["facts"])
        self.assertIn("owner_deadline_elapsed_at_final_select", row["facts"])

    def test_nil_packet_and_missing_eye_never_become_owner_change(self):
        nil = dict.fromkeys(self.hud["left"], 0)
        for right in (nil, None):
            with self.subTest(right=right):
                self.hud.update(left=nil, right=right, source_matches=False, count=nil, owner=nil)
                row = self.saved_analysis()["rows"][1]
                self.assertIn("wire_left_nil_sample", row["facts"])
                self.assertIn("wire_source_did_not_match", row["facts"])
                self.assertIn("wire_right_nil_sample" if right else "wire_right_missing_or_invalid_sample_fields", row["facts"])
                self.assertEqual(row["owner_relations"]["wire_owner_vs_prior_visible"], "not_comparable")
                self.assertNotIn("owner_transition", row["context"])
                self.assertNotIn("owner_deadline_elapsed_at_first_select", row["facts"])

    def test_actual_paired_owner_change_is_separate_from_missing_evidence(self):
        for name in ("left", "right"):
            self.hud[name]["hud_scene_generation"] = 10
        self.rows[1]["wire_owner_differs_from_prior_visible"] = True
        row = self.saved_analysis()["rows"][1]
        self.assertEqual(row["owner_relations"]["wire_owner_vs_prior_visible"], "different")
        self.assertEqual(row["context"], "owner_transition_may_intentionally_suppress_hud")
        self.assertIn("wire_owner_differs_from_prior_visible", row["facts"])
        self.hud["source_matches"] = False
        row = self.saved_analysis()["rows"][1]
        self.assertEqual(row["owner_relations"]["wire_owner_vs_prior_visible"], "not_comparable")

    def test_pair_and_space_mismatches_preserve_exact_fields(self):
        self.hud["right"]["actor_generation"] = 8
        self.hud["left"]["space_generation"] = 3
        self.hud["owner"]["hud_scene_generation"] = 11
        row = self.saved_analysis()["rows"][1]
        self.assertIn("wire_pair_owner_mismatch", row["facts"])
        self.assertIn("left_space_differs_from_host_frame", row["facts"])
        self.assertIn("retained_count_owner_mismatch", row["facts"])
        self.assertEqual(row["owner_relations"]["wire_owner_vs_prior_visible"], "not_comparable")

    def test_menu_context_does_not_turn_into_unexpected_gameplay_omission(self):
        for menu, layers in ((True, 0), (False, 1)):
            with self.subTest(menu=menu, layers=layers):
                self.rows[1].update(menu=menu, menu_layers=layers)
                self.hud.update(decision="inactive", select_ns=0, final_ns=0)
                row = self.saved_analysis()["rows"][1]
                self.assertEqual(row["context"], "menu_may_intentionally_suppress_hud")
                self.assertEqual(row["gate"], "inactive")
                self.assertNotIn("owner_deadline_elapsed_at_first_select", row["facts"])

    def test_tracking_and_placement_stop_before_select_without_claiming_expiry(self):
        for decision in ("tracking", "placement"):
            with self.subTest(decision=decision):
                self.hud.update(decision=decision, select_ns=0, final_ns=0)
                if decision == "tracking":
                    self.rows[1]["tracking_valid"] = False
                row = self.saved_analysis()["rows"][1]
                self.assertEqual(row["gate"], decision)
                self.assertEqual(row["clock_checks"]["first_select"]["owner"]["state"], "clock_unavailable")
                self.assertNotIn("owner_deadline_elapsed_at_first_select", row["facts"])

    def test_pending_upload_and_image_owner_gate_keep_distinct_evidence(self):
        self.hud.update(decision="upload", select_ns=1120000000, final_ns=0, pending=True, has_image=False)
        row = self.saved_analysis()["rows"][1]
        self.assertEqual(row["gate"], "upload")
        self.assertTrue(row["recorded"]["ammo_hud"]["pending"])
        self.assertFalse(row["recorded"]["ammo_hud"]["has_image"])
        self.assertEqual(row["clock_checks"]["first_select"]["owner"]["state"], "within_recorded_window")
        self.hud.update(decision="image_owner", final_ns=1125000000, pending=False, has_image=True)
        self.hud["image"]["actor_generation"] = 6
        row = self.saved_analysis()["rows"][1]
        self.assertEqual(row["gate"], "image_owner")
        self.assertEqual(row["owner_relations"]["image_vs_owner"], "different")
        self.assertNotIn("owner_deadline_elapsed_at_final_select", row["facts"])

    def test_unavailable_failed_and_absent_clock_have_no_select_expiry_evidence(self):
        for decision in ("unavailable", "failed", "no_clock"):
            with self.subTest(decision=decision):
                self.hud.update(decision=decision, select_ns=0, final_ns=0)
                row = self.saved_analysis()["rows"][1]
                self.assertEqual(row["decision"], decision)
                self.assertEqual(row["clock_checks"]["first_select"]["owner"]["state"], "clock_unavailable")

    def test_not_called_and_missing_evidence_are_explicit(self):
        self.rows[1]["ammo_hud"] = {"decision": "not_called", "receive_ns": 0, "select_ns": 0, "final_ns": 0}
        row = self.saved_analysis()["rows"][1]
        self.assertEqual(row["gate"], "not_called")
        self.assertEqual(row["owner_relations"]["wire_owner_vs_prior_visible"], "not_comparable")
        self.assertEqual(row["receive_scope"], "clock_unavailable")
        self.assertEqual(row["clock_checks"]["receive"]["left"]["state"], "missing_or_invalid_sample_fields")
        self.rows[1].pop("ammo_hud")
        self.assertEqual(self.saved_analysis()["rows"][1]["gate"], "unknown_recorded_gate")

    def test_absent_and_empty_journal_do_not_establish_zero_session_gaps(self):
        gap = self.report["frame_diagnostics"].pop("first_hud_gap")
        self.assertEqual(self.saved_analysis()["status"], "no_hud_gap_journal")
        gap["rows"] = []
        self.report["frame_diagnostics"]["first_hud_gap"] = gap
        result = self.saved_analysis()
        self.assertEqual(result["status"], "no_captured_hud_gap")
        self.assertEqual(result["decision_counts"], {})
        self.assertNotIn("session_gap_count", result)

    def test_repeat_receive_is_not_a_new_frame_packet(self):
        self.hud["receive_ns"] = 1001000000
        self.hud["receive_serial"] = 70
        row = self.saved_analysis()["rows"][1]
        self.assertEqual(row["receive_scope"], "predates_observing_frame")
        self.assertEqual(row["clock_checks"]["receive"]["left"]["state"], "observation_in_future")

    def test_hud_mode_preserves_input_and_window_guards(self):
        with scratch_directory() as folder:
            path = Path(folder) / "host.json"
            path.write_text(HUD_SAVED_REPORT, encoding="utf-8")
            argv = ["--hud-only", "--host", str(path)]
            with contextlib.redirect_stderr(io.StringIO()):
                for extra in (["--output", str(path)], ["--start-qpc-ns", "1"], ["--max-input-mib", "0"]):
                    self.assertEqual(timing.main([*argv, *extra]), 2)
                for extra in ([], ["--hud-only", "--native", str(path)]):
                    with self.assertRaises(SystemExit) as raised:
                        timing.main(["--host", str(path), *extra])
                    self.assertEqual(raised.exception.code, 2)
            self.assertEqual(path.read_text(encoding="utf-8"), HUD_SAVED_REPORT)

    def test_capacity_and_clock_are_bounded_without_relaxing_native_contract(self):
        for capacity in (0, 1, 33):
            self.report["frame_diagnostics"]["first_hud_gap"]["capacity"] = capacity
            with self.subTest(capacity=capacity), self.assertRaises(timing.EvidenceError):
                timing.analyze_hud(self.report)
        self.report["frame_diagnostics"]["clock"] = "xr_time"
        with self.assertRaises(timing.EvidenceError):
            timing.analyze_hud(self.report)
        with self.assertRaises(timing.EvidenceError):
            timing.analyze({}, json.loads(HUD_SAVED_REPORT))

    def test_default_timing_output_ignores_separately_scoped_hud_journal(self):
        native, host = reports()
        expected = timing.analyze(native, host)
        host["frame_diagnostics"]["first_hud_gap"] = self.report["frame_diagnostics"]["first_hud_gap"]
        self.assertEqual(timing.analyze(native, host), expected)


class RecentHudAnalysisTests(unittest.TestCase):
    def setUp(self):
        self.report = json.loads(HUD_SAVED_REPORT)
        prior, omission = copy.deepcopy(self.report["frame_diagnostics"]["first_hud_gap"]["rows"])
        recovery = copy.deepcopy(prior)
        recovery.update(frame=602, started_ns=1230000000, ended_ns=1240000000)
        self.episode = {"rows": [prior, omission, recovery], "omitted_frames": 1,
                        "last_omitted_frame": 601, "last_omitted_ns": 1220000000, "recovered": True}
        self.journal = {"schema": 1, "capacity": 32, "clock_valid": True,
                        "observed_start_ns": 900000000, "observed_end_ns": 3000000000,
                        "episodes_seen": 1, "overwritten": 0, "overwritten_through_ns": 0,
                        "episodes": [self.episode]}
        self.report["frame_diagnostics"]["recent_hud_gaps"] = self.journal

    def analyze(self, start=1110000000, end=1220000000):
        return timing.analyze_recent_hud(self.report, start, end)

    def test_exact_qpc_bounds_select_omission_with_outside_context(self):
        result = self.analyze()
        self.assertEqual(result["in_window_omission_witnesses"], 1)
        self.assertEqual(result["episodes"][0]["selection"], "in_window_omission_witness")
        self.assertEqual([r["frame"] for r in result["episodes"][0]["rows"]], [600, 601, 602])
        self.assertEqual(result["episodes"][0]["rows"][1]["gate"], "first_select_no_sample")
        # XR predicted time is unrelated to the requested QPC bounds.
        self.episode["rows"][1]["predicted_ns"] = 1
        self.assertEqual(self.analyze()["in_window_omission_witnesses"], 1)
        for start, end in ((1110000001, 1220000000), (1110000000, 1219999999)):
            result = self.analyze(start, end)
            self.assertEqual(result["in_window_omission_witnesses"], 0)
            self.assertEqual(result["episodes"][0]["selection"], "overlap_without_in_window_omission_witness")

    def test_pre_window_first_record_is_not_selected_in_recent_mode(self):
        original = timing.analyze_hud(self.report)
        self.journal["episodes"] = []
        self.journal["episodes_seen"] = 0
        self.assertEqual(self.analyze()["status"], "no_retained_in_window_omission_witness")
        self.assertEqual(self.analyze()["episodes"], [])
        self.assertEqual(timing.analyze_hud(self.report), original)

    def test_prolonged_gap_overlap_is_metadata_not_interior_witness(self):
        self.episode["rows"].pop()
        self.episode.update(recovered=False, omitted_frames=400, last_omitted_frame=1000, last_omitted_ns=2500000000)
        result = self.analyze(2000000000, 2600000000)
        self.assertEqual(result["in_window_omission_witnesses"], 0)
        self.assertEqual(result["episodes"][0]["omitted_frames_in_episode"], 400)
        self.assertEqual(len(result["episodes"][0]["rows"]), 2)
        self.assertNotIn("window_omission_count", result)

    def test_overwritten_overlap_and_outside_range_are_explicit(self):
        self.journal.update(overwritten=5, episodes_seen=6, overwritten_through_ns=1050000000)
        result = self.analyze(950000000, 1060000000)
        self.assertTrue(result["overwritten_overlap_possible"])
        self.assertEqual(result["in_window_omission_witnesses"], 0)
        self.assertFalse(self.analyze()["overwritten_overlap_possible"])
        self.assertTrue(self.analyze(800000000, 3100000000)["window_outside_observed_range"])

    def test_missing_journal_is_not_fallback_to_first_record(self):
        self.report["frame_diagnostics"].pop("recent_hud_gaps")
        self.assertEqual(self.analyze()["status"], "no_recent_hud_gap_journal")
        self.assertEqual(self.analyze()["in_window_omission_witnesses"], 0)

    def test_malformed_capacity_and_clocks_are_refused(self):
        for field, value in (("capacity", 33), ("clock_valid", False), ("observed_end_ns", 1),
                             ("overwritten", 1), ("episodes", [self.episode] * 33)):
            with self.subTest(field=field):
                journal = copy.deepcopy(self.journal)
                self.journal[field] = value
                with self.assertRaises(timing.EvidenceError):
                    self.analyze()
                self.journal.clear()
                self.journal.update(journal)
        for start, end in ((None, None), (1, None), (2, 1), (0, 1)):
            with self.assertRaises(timing.EvidenceError):
                self.analyze(start, end)

    def test_explicit_recent_cli_preserves_default_mode_and_input(self):
        with scratch_directory() as folder:
            path = Path(folder) / "host.json"
            original = json.dumps(self.report)
            path.write_text(original, encoding="utf-8")
            argv = ["--hud-recent", "--host", str(path)]
            with contextlib.redirect_stderr(io.StringIO()):
                self.assertEqual(timing.main(argv), 2)
                self.assertEqual(timing.main([*argv, "--start-qpc-ns", "1"]), 2)
                self.assertEqual(timing.main([*argv, "--output", str(path)]), 2)
            output = Path(folder) / "recent.json"
            self.assertEqual(timing.main([*argv, "--start-qpc-ns", "1110000000", "--end-qpc-ns", "1220000000",
                                          "--output", str(output)]), 0)
            self.assertEqual(json.loads(output.read_text(encoding="utf-8"))["in_window_omission_witnesses"], 1)
            self.assertEqual(path.read_text(encoding="utf-8"), original)


if __name__ == "__main__":
    unittest.main()
