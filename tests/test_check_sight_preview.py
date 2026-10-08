"""Synthetic checker regressions; no native capture, assets or filesystem fixture."""
import copy
import importlib.util
import json
import math
from pathlib import Path
import unittest
from unittest.mock import patch

_spec = importlib.util.spec_from_file_location(
    "sight_preview_under_test", Path(__file__).resolve().parents[1] / "tools/check_sight_preview.py")
checker = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(checker)


def transform(angle=0., position=(0., 0., 0.)):
    c, s = math.cos(angle), math.sin(angle)
    return [1., 0., 0., 0., 0., c, s, 0., 0., -s, c, 0., *position, 1.]


def fixture():
    rows, gestures, modes, samples = [], [], [], []
    for token, start_weapon, target, action, start in ((1, 10, 20, 33, 100), (2, 20, 10, 36, 200)):
        base = 0. if token == 1 else math.pi / 2
        gestures.append(dict(id=token, start_generation=start, end_generation=start + 8, weapon=start_weapon,
                             request=token + 70, committed=True, start_mode=token - 1))
        modes.append(dict(gesture=token + 70, **{"from": start_weapon}, target=target,
                          action=action, actor=1000, weak=2000, owner=7, space=9,
                          ms=100 * token, ack_ms=100 * token + 1, cancelled=False))
        for n, angle in enumerate((0., .3, .6, 1.)):
            angle *= 1 if token == 1 else -1
            total = base + angle
            grasp = [0., .09 * math.sin(total), -.09 * math.cos(total)]
            # Native mode has changed; the preview remains relative to its grab.
            native = base if n < 2 else (1.3 if token == 1 else .2)
            weapon = start_weapon if n < 2 else target
            rows.append(dict(token=token, generation=start + n + 1, input_generation=start + n + 1,
                             owner=7, space=9, weapon=weapon, physical_item=99, grab_generation=start,
                             phase=1 if n < 2 else 3, angle=angle, gesture_angle=angle,
                             native_observed=False, raw_observed=False, reach_clamped=False,
                             native_rear=transform(native), native_front=transform(-native, (0., 0., -.2)),
                             rear=transform(total), front=transform(-total, (0., 0., -.2)),
                             wrist=transform(position=grasp), resolved_wrist=transform(position=grasp),
                             raw_wrist=transform(position=(grasp[0] + .01, *grasp[1:])),
                             palm_wrist=[0., 0., 0.], grasp=grasp))
            if n in (0, 2):
                asset = "XM8_sp_s" if weapon == 10 else "40mmgl"
                samples.append(dict(weapon=weapon, owner_generation=7, space=9, actor=1000,
                                    generation=start + n + 1, asset_name=asset,
                                    skeleton="fnv1a64:a7f219a1426216ab",
                                    asset_label_source="selected_soldier_weapon_data",
                                    native_pose_source="actor_first_person_rig_pre_vr",
                                    capture_provenance=dict(persistence=99, weapon_data=weapon + 100,
                                                            rig=dict(soldier=1000, weak=2000)),
                                    weapon_bones_complete=True, weapon_bones_dropped=0,
                                    bone_roles=dict(weapon_root="jntWpn_1"),
                                    native_weapon_bones=[dict(name=name, parent_name="jntWpn_1", hidden=False,
                                                             native=transform(), inverse_bind=transform())
                                                         for name in ("jntWpn_1", "jntWpn_10", "jntWpn_11")]))
    configs = []
    for weapon, asset, path, slot in (
            (10, "XM8_sp_s", "Objects/Weapons/Handheld/US_rgl_XM8/SP_rgl_XM8_Scoped", 1),
            (20, "40mmgl", "Objects/Weapons/Handheld/US_rgl_XM8/SP_rgl_XM320_Scoped", 3)):
        configs.append(dict(owner=dict(weapon=weapon, actor_generation=7, space=9, soldier=1000, weak=2000),
                            configuration_path_verified=True, configuration_path=path, weapon_name=asset,
                            selected_slot=slot, weapon_data=weapon + 100, inventory=5000))
    rig = dict(native_animation_written=False, source_changes=0, packing_failures=0, fallback_failures=0,
               weapon_profile_samples=samples, sight_preview=dict(poses=8, rejected=0, records=rows))
    return dict(hooks_disabled=True, state="observed",
                native_stream=dict(camera_restore_failures=0, gpu_failure_stage=0, gpu_failure_code=0),
                gameplay=dict(rig_publication=rig, sight_flip=dict(gestures=gestures),
                              weapon_mode_binding_verified=True, weapon_mode_records=modes,
                              selected_meshes_observer=dict(configuration_identity_samples=configs)))


class SightPreviewCheckerTests(unittest.TestCase):
    def check(self, doc):
        text = json.dumps(doc)
        completion = json.dumps(dict(game_responding=True, game_exited=False, new_crash_report=False))
        def read_text(path, *args, **kwargs):
            if path.name == "native-trace.json": return text
            if path.name == "completion.json": return completion
            raise AssertionError(path)
        with patch.object(Path, "read_text", read_text), patch.object(Path, "read_bytes", return_value=text.encode()):
            return checker.check(Path("synthetic-fixture"))

    def rows(self, doc):
        return doc["gameplay"]["rig_publication"]["sight_preview"]["records"]

    def rejects(self, doc, reason):
        result = self.check(doc)
        self.assertFalse(result["passed"])
        self.assertIn(reason, result["failures"])

    def test_frozen_basis_and_exact_recognized_handover(self):
        result = self.check(fixture())
        self.assertTrue(result["passed"], result["failures"])
        self.assertLess(result["max_angle_error_rad"], 1e-8)

    def test_delayed_zero_angle_sample_within_episode(self):
        doc = fixture()
        for row in self.rows(doc):
            row["generation"] += 2
            row["input_generation"] += 2
        for sample in doc["gameplay"]["rig_publication"]["weapon_profile_samples"]:
            sample["generation"] += 2
        result = self.check(doc)
        self.assertTrue(result["passed"], result["failures"])

    def test_initial_sample_outside_episode(self):
        for kind in ("before_start", "after_end"):
            with self.subTest(kind=kind):
                doc = fixture(); first = self.rows(doc)[0]
                first["generation"] = 99 if kind == "before_start" else 109
                first["input_generation"] = first["generation"]
                self.rejects(doc, "Missing initial zero-angle captured grasp basis")

    def test_tampered_angle(self):
        doc = fixture(); self.rows(doc)[2]["angle"] += .05
        self.rejects(doc, "Rear/front preview rotation does not match opposite hinge angles")

    def test_arbitrary_identity_changes(self):
        for field, value, reason in (
                ("weapon", 123, "Unproven native weapon handover inside fixed grasp"),
                ("physical_item", 123, "Physical gun changed inside a fixed grasp"),
                ("owner", 123, "Owner or space changed inside a fixed grasp"),
                ("space", 123, "Owner or space changed inside a fixed grasp")):
            with self.subTest(field=field):
                doc = fixture(); self.rows(doc)[2][field] = value; self.rejects(doc, reason)

    def test_missing_ambiguous_or_unacknowledged_mode(self):
        for kind in ("missing", "ambiguous", "unacknowledged", "wrong_action"):
            with self.subTest(kind=kind):
                doc = fixture(); modes = doc["gameplay"]["weapon_mode_records"]
                if kind == "missing": modes.clear()
                elif kind == "ambiguous": modes.append(copy.deepcopy(modes[0]))
                elif kind == "unacknowledged": modes[0]["ack_ms"] = 0
                else: modes[0]["action"] = 36
                self.rejects(doc, "Unproven native weapon handover inside fixed grasp")

    def test_initial_baseline_proof_is_required(self):
        for kind in ("truncated", "nonzero", "native_already_observed"):
            with self.subTest(kind=kind):
                doc = fixture(); rows = self.rows(doc)
                if kind == "truncated": rows.pop(0)
                elif kind == "nonzero": rows[0]["angle"] = .01
                else: rows[0]["native_observed"] = True
                self.rejects(doc, "Missing initial zero-angle captured grasp basis")

    def test_missing_rig_or_unsupported_configuration(self):
        for kind in ("missing_raw", "wrong_configuration"):
            with self.subTest(kind=kind):
                doc = fixture()
                if kind == "missing_raw": doc["gameplay"]["rig_publication"]["weapon_profile_samples"].clear()
                else: doc["gameplay"]["selected_meshes_observer"]["configuration_identity_samples"][1]["configuration_path"] = "unsupported"
                self.rejects(doc, "Unproven native weapon handover inside fixed grasp")

    def test_reach_clamp_still_fails(self):
        doc = fixture(); self.rows(doc)[2]["reach_clamped"] = True
        self.rejects(doc, "Fixture sight hand was reach-clamped")

    def test_resolved_palm_tolerance_still_fails(self):
        doc = fixture(); self.rows(doc)[2]["resolved_wrist"][12] += .002
        self.rejects(doc, "Rendered palm misses frame by over1mm")


if __name__ == "__main__":
    unittest.main()
