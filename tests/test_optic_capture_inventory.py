"""Offline schema and rejection tests; no native process or graphics access."""
import contextlib
import copy
import hashlib
import importlib.util
import io
import json
from pathlib import Path
import tempfile
import unittest

MODULE = Path(__file__).resolve().parents[1] / "tools" / "optic_capture_inventory.py"
spec = importlib.util.spec_from_file_location("optic_capture_inventory", MODULE)
optic = importlib.util.module_from_spec(spec)
spec.loader.exec_module(optic)
IDENTITY = [1., 0., 0., 0., 0., 1., 0., 0., 0., 0., 1., 0., 0., 0., 0., 1.]


class OpticInventoryTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.dump("manifest.json", {"game_sha256": "A" * 64, "probe_sha256": "b" * 64,
                                    "pass_evidence": True, "mode": "tracked_native_ipc_stream"})
        self.dump("native-trace.json", {"state": "observed", "hooks_disabled": True,
                  "world_color_captures": [{"captured": True, "frame": 91,
                    "target": {"width": 1920, "height": 1080, "format": 28, "samples": 1}}],
                  "gameplay": {"rig_publication": {"weapon_profile_samples": [
                      {"asset_name": "XM8_sp_s", "capture_sequence": 1, "weapon": 123,
                       "attachment_pending": False, "capture_episode": 1}]}}})
        self.dump("projection-bindings.json", [{"context": 1, "address": 2, "callsite": 3,
                  "thread": 4, "frame": 91, "gather": 0, "projection": IDENTITY}])
        self.dump("camera-context-evidence.json", {"patched_scopes": 2, "patch_failures": 0,
                  "restore_failures": 0, "contexts": [{"eye": 0, "role": 0, "callsite": 3,
                  "snapshots": [{"valid": 1, "values": IDENTITY + IDENTITY + [0.] * 4}]}]})
        self.dump("tracked-camera-evidence.json", {"native_frame": 91, "captured_mask": 63,
                  "steps": ["applied", "before_draw", "after_draw"]})
        self.set_passes([self.pass_row()])

    def dump(self, name, data):
        (self.root / name).write_text(json.dumps(data), encoding="utf-8")

    def load(self, name):
        return json.loads((self.root / name).read_text())

    def pass_row(self, eye=0, number=0, **changes):
        row = {"eye": eye, "pass": number, "sequence": 71 + number,
               "kind": 1, "count": 1536, "vs": 100, "ps": 200,
               "target": 300, "depth": 400, "complete": True, "overflow": 0,
               "buffers": [{"slot": i, "source": 600 + i if i in (0, 2) else 0,
                            "bytes": 64 if i in (0, 2) else 0,
                            "done": i in (0, 2)} for i in range(4)]}
        row.update(changes)
        return row

    def set_passes(self, values):
        self.dump("pass-buffer-evidence.json", values)
        for row in values:
            for b in row["buffers"]:
                if b["done"] and b["bytes"]:
                    (self.root / f"pass-{row['eye']}-{row['pass']}-cb-{b['slot']}.bin").write_bytes(bytes(b["bytes"]))

    def audit(self):
        return optic.inventory(self.root)

    def test_legacy_counts_are_samples_not_draws_or_optic_identity(self):
        result = self.audit()
        self.assertEqual(result["status"], "valid_inventory")
        p = result["pass_inventory"]
        self.assertEqual(p["rows"], 1)
        self.assertEqual(p["constant_slots"][0]["stage_slot"], "VS:0")
        self.assertEqual(p["constant_slots"][2]["stage_slot"], "PS:0")
        self.assertEqual(result["constant_blob_provenance"]["verified_files"], 2)
        self.assertIsNone(p["native_draw_total"])
        self.assertIsNone(p["native_repeated_draw_total"])
        self.assertEqual(p["per_eye"][0]["max_sample_sequence"], 71)
        self.assertEqual(result["classification"], "unknown")
        self.assertIsNone(result["comparison_key"]["asset_name"])
        self.assertEqual(result["native_evidence"]["observed_assets"][0]["asset_name"], "XM8_sp_s")
        self.assertFalse(result["phase_comparison"]["ready"])
        self.assertEqual(result["comparison_key"]["game_sha256"], "a" * 64)

    def test_world_target_size_is_not_a_recorded_pass_viewport(self):
        result = self.audit()
        self.assertEqual(result["pass_inventory"]["recorded_viewports"], [])
        self.assertEqual(result["pass_inventory"]["rows_without_recorded_viewport"], 1)
        self.assertEqual(result["native_evidence"]["world_color_targets"][0]["width"], 1920)
        self.assertIn("per_sample_viewport_dimensions", result["phase_comparison"]["missing_fields"])

    def test_explicit_dimensions_are_reported_without_assuming_filter(self):
        self.set_passes([self.pass_row(width=512, height=512)])
        result = self.audit()
        self.assertEqual(result["pass_inventory"]["recorded_viewports"],
                         [{"width": 512, "height": 512, "sample_rows": 1}])
        self.assertFalse(result["phase_comparison"]["ready"])

    def test_repeated_and_cross_eye_tuples_do_not_become_native_draw_totals(self):
        self.set_passes([self.pass_row(), self.pass_row(number=1), self.pass_row(eye=1)])
        result = self.audit()
        p = result["pass_inventory"]
        self.assertEqual(p["cross_eye_shared_combinations"], 1)
        self.assertEqual(p["per_eye"][0]["repeated_sample_rows"], 1)
        self.assertEqual(p["per_eye"][1]["repeated_sample_rows"], 0)
        self.assertIsNone(p["native_draw_total"])
        self.assertEqual(result["status"], "partial")
        self.assertFalse(result["phase_comparison"]["cross_capture_pointer_matching_allowed"])

    def test_manifest_disabled_and_empty_is_explicit_not_absent_optic(self):
        manifest = self.load("manifest.json"); manifest["pass_evidence"] = False
        self.dump("manifest.json", manifest); self.set_passes([])
        result = self.audit()
        self.assertEqual(result["pass_inventory"]["capture_state"], "disabled")
        self.assertEqual(result["classification"], "unknown")
        self.assertFalse(result["phase_comparison"]["ready"])
        self.assertEqual(result["validation_errors"], [])

    def test_empty_enabled_vs_enablement_unknown(self):
        self.set_passes([])
        self.assertEqual(self.audit()["pass_inventory"]["capture_state"], "enabled_empty")
        manifest = self.load("manifest.json"); del manifest["pass_evidence"]
        self.dump("manifest.json", manifest)
        self.assertEqual(self.audit()["pass_inventory"]["capture_state"], "empty_enablement_unknown")

    def test_disabled_nonempty_is_contradiction(self):
        manifest = self.load("manifest.json"); manifest["pass_evidence"] = False
        self.dump("manifest.json", manifest)
        self.assertEqual(self.audit()["status"], "invalid")

    def test_pending_missing_truncated_buffers_remain_incomplete(self):
        row = self.pass_row(complete=False); row["buffers"][0]["done"] = False
        self.set_passes([row])
        result = self.audit()
        self.assertEqual(result["status"], "partial")
        self.assertEqual(result["pass_inventory"]["constant_slots"][0]["pending_or_unavailable"], 1)
        self.set_passes([self.pass_row()])
        (self.root / "pass-0-0-cb-0.bin").write_bytes(b"short")
        (self.root / "pass-0-0-cb-2.bin").unlink()
        result = self.audit()
        self.assertEqual(result["constant_blob_provenance"]["verified_files"], 0)
        self.assertEqual(result["status"], "partial")

    def test_overflow_is_max_per_eye_not_sum_of_repeated_counter(self):
        self.set_passes([self.pass_row(overflow=3), self.pass_row(number=1, vs=101, overflow=3)])
        result = self.audit()
        self.assertEqual(result["pass_inventory"]["per_eye"][0]["overflow_reported"], 3)
        self.assertEqual(result["status"], "partial")

    def test_bad_json_truncation_duplicate_keys_and_nonfinite_rejected(self):
        path = self.root / "pass-buffer-evidence.json"
        for payload in ('[{"eye":0', '{"rows":[],"rows":[]}', '[NaN]', 'null', '42'):
            with self.subTest(payload=payload):
                path.write_text(payload)
                self.assertEqual(self.audit()["status"], "invalid")

    def test_bad_pass_schema_and_duplicate_identity_rejected(self):
        mutations = [dict(eye=True), dict(eye=2), dict(count=-1), dict(complete=1),
                     dict(width=128), dict(buffers=[]), dict(vs="100")]
        for changes in mutations:
            with self.subTest(changes=changes):
                self.dump("pass-buffer-evidence.json", [self.pass_row(**changes)])
                self.assertEqual(self.audit()["status"], "invalid")
        self.set_passes([self.pass_row(), self.pass_row()])
        self.assertEqual(self.audit()["status"], "invalid")

    def test_projection_context_and_build_validation(self):
        projection = self.load("projection-bindings.json")
        projection[0]["projection"] = [1.] * 15
        self.dump("projection-bindings.json", projection)
        self.assertEqual(self.audit()["status"], "invalid")
        projection[0]["projection"] = IDENTITY
        self.dump("projection-bindings.json", projection)
        context = self.load("camera-context-evidence.json")
        context["contexts"][0]["snapshots"][0]["values"] = [0.] * 35
        self.dump("camera-context-evidence.json", context)
        self.assertEqual(self.audit()["status"], "invalid")
        context["contexts"] = []; self.dump("camera-context-evidence.json", context)
        manifest = self.load("manifest.json"); manifest["game_sha256"] = "almost"
        self.dump("manifest.json", manifest)
        result = self.audit()
        self.assertIsNone(result["comparison_key"]["game_sha256"])
        self.assertEqual(result["status"], "invalid")

    def test_reflection_is_lead_only_with_source_hash(self):
        reflection = self.root / "reflection.json"
        self.dump("reflection.json", [{"name": "SoldierWeaponData", "fields": [
            {"name": "ZoomRenderFov", "offset": "0x30", "type": "Float32"},
            {"name": "Hud", "offset": "0xa0", "type": "HudData"}]}])
        result = optic.inventory(self.root, [reflection])
        self.assertEqual(len(result["reflection_leads"]), 2)
        self.assertEqual(result["classification"], "unknown")
        source = next(r for r in result["sources"] if r["path"] == str(reflection))
        self.assertEqual(source["sha256"], hashlib.sha256(reflection.read_bytes()).hexdigest())

    def test_missing_optional_evidence_never_fabricates_readiness(self):
        for name in ("manifest.json", "native-trace.json", "projection-bindings.json",
                     "camera-context-evidence.json", "tracked-camera-evidence.json"):
            (self.root / name).unlink()
        result = self.audit()
        self.assertEqual(result["status"], "partial")
        self.assertFalse(result["phase_comparison"]["ready"])
        self.assertIsNone(result["comparison_key"]["game_sha256"])
        self.assertEqual(len(result["missing_files"]), 5)

    def test_cli_is_read_only_by_default_and_refuses_overwrite(self):
        before = {p.name: p.read_bytes() for p in self.root.iterdir()}
        with contextlib.redirect_stdout(io.StringIO()) as stream:
            result = optic.main(["--trace", str(self.root)])
        self.assertEqual(result, 0)
        output = json.loads(stream.getvalue())
        self.assertFalse(output["phase_comparison_ready"])
        self.assertEqual(before, {p.name: p.read_bytes() for p in self.root.iterdir()})
        output_path = self.root / "result.json"
        self.assertEqual(optic.main(["--trace", str(self.root), "--output", str(output_path)]), 0)
        with contextlib.redirect_stderr(io.StringIO()), self.assertRaises(SystemExit) as error:
            optic.main(["--trace", str(self.root), "--output", str(self.root / "manifest.json")])
        self.assertEqual(error.exception.code, 2)
        self.assertEqual(before["manifest.json"], (self.root / "manifest.json").read_bytes())

    def test_cli_malformed_capture_returns_failure_with_structured_report(self):
        self.dump("pass-buffer-evidence.json", {"wrong": []})
        with contextlib.redirect_stdout(io.StringIO()) as stream:
            code = optic.main(["--trace", str(self.root)])
        self.assertEqual(code, 2)
        self.assertEqual(json.loads(stream.getvalue())["captures"][0]["status"], "invalid")


if __name__ == "__main__":
    unittest.main()
