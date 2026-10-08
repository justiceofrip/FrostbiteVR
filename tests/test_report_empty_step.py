"""Synthetic producer-schema captures test reporting, not native correctness."""
import copy
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

TOOL = Path(__file__).resolve().parents[1] / "tools" / "report_empty_step.py"
SPEC = importlib.util.spec_from_file_location("report_empty_step", TOOL)
MOD = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MOD)


def row():
    return {
        "stage": 5, "now_ns": 1_020_000_000, "input_sequence": 41,
        "owner_revision": 8, "observed_ns": 1_000_000_000,
        "deadline_ns": 1_150_000_000, "branch": 0, "profile": 1, "phase": 1,
        "owner": [0x10100, 0x10200, 0x10300, 0x10400, 3, 4, 5],
        "requested": False, "applied": False, "restored": False, "owner_retained": True,
        "before": {"firing": 0x20100, "current": 2, "next": 2, "loaded": 0,
                   "reserve": 60, "timer": 0.0, "flags_a8": 0},
        "after": {"firing": 0x20100, "current": 2, "next": 10, "loaded": 0,
                  "reserve": 60, "timer": 0.0, "flags_a8": 0},
        "context": {"delta": 0.011, "reload_multiplier": 1.0, "input_flags": 0,
                    "flags_24_28": [1, 0, 1, 0, 0]},
        "interaction": {"input": 40, "observed_ns": 1_010_000_000, "deadline_ns": 1_110_000_000,
                        "support_holding": True, "owns_left_hand": False,
                        "blocks_weapon_actions": False, "magazine_phase": 0},
    }


def capture(*rows):
    counts = [0] * len(MOD.STAGES)
    for sample in rows:
        counts[sample["stage"]] += 1
    return {"gameplay": {"reload_flow": {"empty_step_diagnostic": {
        "schema": 1, "capacity": 128, "diagnostic_only": True, "drained": True,
        "lock_drops": 0, "publisher_lock_drops": 0,
        "stages": list(MOD.STAGES), "counts": counts, "observed": len(rows),
        "overwritten": 0, "rows": list(rows),
    }}}}


def journal(document):
    return document["gameplay"]["reload_flow"]["empty_step_diagnostic"]


class ReportTests(unittest.TestCase):
    def boundary_row(self):
        sample = row()
        sample.update(boundary_reason_known=True, boundary_reason=3,
                      raw_context_known=True, step_delta_bits=0x3D75C28F,
                      context_delta_bits=0x3D75C28F, context_multiplier_bits=0x3F800000,
                      context_input_flags=0, context_flag_bytes=[1, 0, 0, 0, 0])
        return sample

    def test_boundary_reason_preserved_with_paired_entry(self):
        result = self.result(self.boundary_row())
        self.assertEqual(result['reload_entries'][0]['boundary']['reason'], 'step_delta_too_large')
        self.assertEqual(result['groups'][0]['boundary_reasons'], {'step_delta_too_large': 1})

    def test_raw_nonfinite_word_does_not_emit_nonfinite_json(self):
        sample = self.boundary_row()
        sample.update(boundary_reason=1, step_delta_bits=0x7FC00000, context=None)
        result = self.result(sample)
        encoded = json.dumps(result, allow_nan=False)
        self.assertIn('step_delta_nonfinite', encoded)
        self.assertEqual(result['reload_entries'][0]['boundary']['step_delta_bits'], 0x7FC00000)

    def test_malformed_boundary_metadata_rejected(self):
        for change in ({'boundary_reason': 12}, {'boundary_reason_known': 1},
                       {'raw_context_known': 1}, {'step_delta_bits': -1},
                       {'context_flag_bytes': [1, 0, 0, 0, 256]}, {'context_input_flags': True}):
            sample = self.boundary_row();sample.update(change)
            with self.subTest(change=change):
                self.assertTrue(self.result(sample)['invalid_rows'])

    def test_raw_boundary_requires_complete_words(self):
        sample = self.boundary_row();del sample['context_delta_bits']
        self.assertTrue(self.result(sample)['invalid_rows'])

    def test_historical_owner_cannot_supply_current_boundary_reason(self):
        sample = self.boundary_row()
        sample.update(lease_invalid_last_known=True, before=None, after=None, context=None,
                      owner_retained=False, requested=False, applied=False, restored=False)
        self.assertTrue(self.result(sample)['invalid_rows'])

    def test_older_capture_boundary_is_unknown(self):
        self.assertEqual(self.result(row())['reload_entries'][0]['boundary']['reason'], 'unknown')

    def result(self, *rows):
        return MOD.analyze(capture(*rows))["journals"][0]

    def test_old_default_profile_is_not_a_gun_identity(self):
        sample = row()
        sample["profile"] = 0
        entry = self.result(sample)["reload_entries"][0]
        self.assertIsNone(entry["family"])
        self.assertEqual(entry["family_label"], "unknown")

    def test_family_separates_same_owner_default_profile(self):
        tube, magazine = row(), row()
        tube["family"], magazine["family"] = 0, 1
        tube["family_known"] = magazine["family_known"] = True
        result = self.result(tube, magazine)
        self.assertEqual(len(result["groups"]), 2)
        self.assertEqual([x["family_label"] for x in result["reload_entries"]], ["tube", "detachable_magazine"])

    def test_expired_owner_annotation_has_no_current_evidence(self):
        sample = row()
        sample.update(lease_invalid_last_known=True, before=None, after=None, context=None, owner_retained=False)
        result = self.result(sample)
        self.assertEqual(result["last_known_rows"], 1)
        self.assertEqual(result["reload_entries"], [])
        sample["after"] = row()["after"]
        self.assertEqual(self.result(sample)["status"], "partial_invalid_rows")

    def test_protected_bank_loss_is_explicit_and_optional(self):
        data = capture(row())
        journal(data).update(salient_retained=25, salient_dropped=32,
                             salient_boundary_dropped=32, salient_transition_dropped=0)
        loss = MOD.analyze(data)["journals"][0]["retention"]
        self.assertEqual(loss["salient_boundary_dropped"], 32)
        self.assertEqual(loss["salient_transition_dropped"], 0)
        self.assertIsNone(loss["unowned_skipped"])
        journal(data)["salient_transition_dropped"] = True
        with self.assertRaises(MOD.ReportError):
            MOD.analyze(data)

    def test_protected_then_rolling_entries_use_native_time(self):
        late, early = row(), row()
        late["now_ns"] += 20_000_000
        result = self.result(late, early)
        self.assertEqual([x["row"] for x in result["reload_entries"]], [1, 0])

    def test_paired_entry_preserves_context_without_claiming_cause(self):
        report = self.result(row())
        entry = report["reload_entries"][0]
        self.assertEqual(entry["stage"], "eligibility")
        self.assertEqual(entry["interaction"], "support_holding")
        self.assertFalse(entry["explicit_reload_input"])
        self.assertEqual(entry["visible_predicate_mismatches"], ["context_0x26_not_0"])
        self.assertNotIn("cause", entry)

    def test_missing_old_diagnostic_is_not_a_pass(self):
        report = MOD.analyze({"gameplay": {"reload_flow": {"empty_magazine_control": {"applied": [99, 99, 99]}}}})
        self.assertEqual(report["status"], "missing_diagnostic")

    def test_does_not_join_separate_before_and_after_rows(self):
        first, second = row(), row()
        first["after"] = None
        second["before"] = None
        report = self.result(first, second)
        self.assertEqual(report["reload_entries"], [])
        self.assertEqual(report["paired_but_unconfirmed_entries"], [])

    def test_owner_change_and_firing_change_are_unconfirmed(self):
        for change in ("owner", "firing"):
            with self.subTest(change=change):
                sample = row()
                if change == "owner":
                    sample["owner_retained"] = False
                else:
                    sample["after"]["firing"] += 4
                report = self.result(sample)
                self.assertEqual(report["reload_entries"], [])
                self.assertEqual(len(report["paired_but_unconfirmed_entries"]), 1)

    def test_weapon_pointer_reuse_does_not_merge_generations(self):
        first, second = row(), row()
        second["owner"][5] += 1
        report = self.result(first, second)
        self.assertEqual(len(report["groups"]), 2)

    def test_separate_firing_branches_and_owner_revisions(self):
        samples = [row() for _ in range(3)]
        samples[1]["branch"] = 1
        samples[2]["owner_revision"] += 1
        self.assertEqual(len(self.result(*samples)["groups"]), 3)

    def test_stale_future_and_overlong_hand_evidence_is_unknown(self):
        for field, value in (("deadline_ns", 1_020_000_000), ("observed_ns", 1_030_000_000),
                             ("deadline_ns", 1_500_000_000), ("input", 42)):
            with self.subTest(field=field, value=value):
                sample = row()
                sample["interaction"][field] = value
                self.assertEqual(self.result(sample)["reload_entries"][0]["interaction"], "unknown_stale_or_invalid")

    def test_explicit_reload_is_not_mislabelled_automatic(self):
        sample = row()
        sample["context"]["input_flags"] = 4
        self.assertTrue(self.result(sample)["reload_entries"][0]["explicit_reload_input"])

    def test_already_reloading_is_not_a_new_entry(self):
        sample = row()
        sample["before"]["next"] = 10
        self.assertEqual(self.result(sample)["reload_entries"], [])

    def test_post_call_expiry_is_descriptive_only(self):
        sample = row()
        sample["deadline_ns"] = sample["now_ns"]
        entry = self.result(sample)["reload_entries"][0]
        self.assertIn("owner_deadline_expired_by_post_call_observation", entry["visible_predicate_mismatches"])

    def test_restore_failure_survives_without_reload_entry(self):
        sample = row()
        sample["applied"] = True
        sample["after"]["next"] = 2
        report = self.result(sample)
        self.assertEqual(len(report["restore_anomalies"]), 1)
        self.assertEqual(report["reload_entries"], [])

    def test_ring_loss_and_lock_drops_are_kept(self):
        data = capture(row())
        journal(data).update(overwritten=90, lock_drops=5, publisher_lock_drops=6)
        report = MOD.analyze(data)["journals"][0]
        self.assertEqual((report["overwritten"], report["lock_drops"], report["publisher_lock_drops"]), (90, 5, 6))

    def test_not_drained_rows_are_not_analyzed(self):
        data = capture(row())
        journal(data)["drained"] = False
        report = MOD.analyze(data)["journals"][0]
        self.assertEqual(report["status"], "not_drained")
        self.assertEqual(report["reload_entries"], [])

    def test_invalid_rows_cannot_supply_positive_evidence(self):
        for field, value in (("stage", True), ("owner", [1]), ("now_ns", -1), ("before", {"loaded": 0})):
            sample = row()
            data = capture(sample)
            sample[field] = value
            report = MOD.analyze(data)["journals"][0]
            self.assertEqual(report["status"], "partial_invalid_rows")
            self.assertEqual(report["reload_entries"], [])

    def test_unknown_schema_stage_map_and_inconsistent_counts_fail(self):
        for key, value in (("schema", 2), ("schema", True), ("stages", ["invented"]), ("observed", 4)):
            data = capture(row())
            journal(data)[key] = value
            with self.assertRaises(MOD.ReportError):
                MOD.analyze(data)

    def test_invalid_native_ranges_cannot_supply_entry_evidence(self):
        for field, value in (("current", -1), ("next", 16), ("flags_a8", 256), ("firing", 0x100000000)):
            sample = row()
            sample["before"][field] = value
            report = self.result(sample)
            self.assertEqual(report["status"], "partial_invalid_rows")
            self.assertEqual(report["reload_entries"], [])

    def test_two_journals_are_analyzed_without_cross_session_merge(self):
        data = [capture(row()), capture(row())]
        self.assertEqual(len(MOD.analyze(data)["journals"]), 2)

    def test_input_is_unchanged(self):
        data = capture(row())
        before = copy.deepcopy(data)
        MOD.analyze(data)
        self.assertEqual(data, before)

    def test_cli_hash_and_no_input_overwrite(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "trace.json"
            output = Path(directory) / "report.json"
            raw = json.dumps(capture(row())).encode()
            path.write_bytes(raw)
            run = subprocess.run([sys.executable, "-B", str(TOOL), str(path), "--output", str(output)], capture_output=True, text=True)
            self.assertEqual(run.returncode, 0, run.stderr)
            report = json.loads(output.read_text())
            self.assertEqual(report["source"]["sha256"], MOD.hashlib.sha256(raw).hexdigest())
            run = subprocess.run([sys.executable, "-B", str(TOOL), str(path), "--output", str(path)], capture_output=True, text=True)
            self.assertEqual(run.returncode, 2)
            self.assertEqual(path.read_bytes(), raw)

    def test_cli_rejects_hardlink_to_source(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "trace.json"
            alias = Path(directory) / "alias.json"
            raw = json.dumps(capture(row())).encode()
            path.write_bytes(raw)
            try:
                os.link(path, alias)
            except OSError as error:
                self.skipTest(f"hard links unavailable: {error}")
            run = subprocess.run([sys.executable, "-B", str(TOOL), str(path), "--output", str(alias)], capture_output=True, text=True)
            self.assertEqual(run.returncode, 2)
            self.assertEqual(path.read_bytes(), raw)


if __name__ == "__main__":
    unittest.main()
