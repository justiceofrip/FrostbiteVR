"""Synthetic regressions for named native weapon mechanism observations."""
import copy
import json
from pathlib import Path
import sys
import tempfile
import unittest

import numpy as np
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import weapon_mechanism_pipeline as mechanisms
import weapon_profile_pipeline as profiles


def transform(position=(0., 0., 0.), angle=0.):
    c, s = np.cos(angle), np.sin(angle)
    value = np.array([[c, s, 0, 0], [-s, c, 0, 0], [0, 0, 1, 0], [*position, 1.]])
    return value


def make_row(index, episode=1, asset="XM8_sp_s", joint_angle=0., hidden=False, units=1.):
    world = transform((100 + index * .1, 20, -40), index * .03)
    local = transform((.02, .04, -.5), joint_angle)
    wrist = transform((-.02, -.07, -.21))
    left = transform((.05, -.05, -.6))
    # Scale translations, not basis vectors, to simulate another native unit convention.
    for value in (world, local, wrist, left):
        value[3, :3] *= units
    bone = local @ world
    if hidden:
        bone = np.eye(4)
        bone[:3, :3] *= 1e-4
        bone[3, :3] = world[3, :3]
    return {
        "generation": index + 1, "capture_sequence": index + 1, "capture_episode": episode,
        "owner_generation": 1, "actor": 10, "weapon": 100 if asset == "XM8_sp_s" else 200,
        "space": 2, "units_per_meter": units, "skeleton": "fnv1a64:test-shared-rig",
        "asset_name": asset, "profile_id": "bc2:" + asset, "attachment_pending": False,
        "captured_ms": 1000 + 100 * index,
        "bone_roles": {"weapon_root": "jntWpn_1", "left_wrist": "LeftHand", "right_wrist": "RightHand", "muzzle": None},
        "native": world.reshape(-1).tolist(),
        "native_right_wrist": (wrist @ world).reshape(-1).tolist(),
        "native_left_wrist": (left @ world).reshape(-1).tolist(),
        "weapon_bones_complete": True, "weapon_bones_dropped": 0,
        "native_weapon_bones": [
            {"name": "jntWpn_1", "parent_name": "outside", "hidden": False, "native": world.reshape(-1).tolist()},
            {"name": "jntWpn_7", "parent_name": "jntWpn_1", "hidden": hidden, "native": bone.reshape(-1).tolist()}
        ]}


def trip_rows(angle=np.pi / 2):
    return ([make_row(i) for i in range(10)] +
            [make_row(i, 2, "40mmGL", angle) for i in range(10, 20)] +
            [make_row(i, 3) for i in range(20, 30)])


class MechanismTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name)

    def tearDown(self):
        self.assertEqual(self.root.resolve().parent, Path(tempfile.gettempdir()).resolve())
        self.assertTrue(self.root.name.startswith("tmp"))
        self.tmp.cleanup()

    def capture(self, rows):
        path = self.root / "native-trace.json"
        path.write_text(json.dumps({
            "hooks_disabled": True, "gameplay": {"rig_publication": {
                "weapon_profile_samples": rows, "source_changes": 0, "packing_failures": 0,
                "fallback_failures": 0, "native_animation_written": False,
                "weapon_profile_capture": {"invalid_samples": 0, "capacity_dropped": 0}}}}))
        (self.root / "completion.json").write_text(json.dumps({
            "bootstrap_exit": 0, "game_exited": False, "game_responding": True, "new_crash_report": False}))
        return path

    def batch(self, rows):
        return mechanisms.build_batch([self.capture(rows)])

    def test_world_motion_cancels_and_roundtrip_keeps_generic_name(self):
        result = self.batch(trip_rows())
        self.assertFalse(mechanisms.strict_incomplete(result))
        trip = result["captures"][0]["round_trips"][0]
        self.assertEqual(len(trip["repeatable_changes"]), 1)
        change = trip["repeatable_changes"][0]
        self.assertEqual(change["name"], "jntWpn_7")
        self.assertIsNone(change["semantic_role"])
        self.assertAlmostEqual(change["outbound"]["angle_change_degrees"], 90, places=4)
        self.assertLess(change["outbound"]["position_change_m"], 1e-10)
        self.assertFalse(result["runtime_enabled"])
        self.assertFalse(trip["mechanism_verified"])

    def test_no_change_is_valid_observation_without_inventing_interaction(self):
        result = self.batch(trip_rows(0.))
        self.assertFalse(mechanisms.strict_incomplete(result))
        self.assertFalse(result["captures"][0]["round_trips"][0]["repeatable_changes"])

    def test_pending_equip_bones_are_excluded(self):
        rows = trip_rows()
        for sample in rows[:2]:
            sample["attachment_pending"] = True
            sample["native_weapon_bones"][1]["native"] = [0] * 16
        result = self.batch(rows)
        self.assertFalse(mechanisms.strict_incomplete(result))
        self.assertEqual(result["captures"][0]["episodes"][0]["counts"]["pending"], 2)

    def test_short_initial_dwell_does_not_prove_return_trip(self):
        result = self.batch(trip_rows()[5:])
        self.assertTrue(mechanisms.strict_incomplete(result))
        self.assertIn("jntWpn_7", result["captures"][0]["round_trips"][0]["unresolved_bones"])

    def test_different_actors_cannot_form_return_trip(self):
        rows = trip_rows()
        for sample in rows[10:20]:
            sample["actor"] = 11
        result = self.batch(rows)
        trip = result["captures"][0]["round_trips"][0]
        self.assertEqual(trip["status"], "not_comparable")
        self.assertIn("actor", trip["reason"])

    def test_same_generic_names_different_rig_are_not_merged(self):
        rows = trip_rows()
        for sample in rows[10:20]:
            sample["skeleton"] = "different-bind-and-topology"
        result = self.batch(rows)
        self.assertEqual(result["captures"][0]["round_trips"][0]["status"], "not_comparable")

    def test_bone_name_correspondence_requires_same_parent_topology(self):
        rows = trip_rows()
        for sample in rows[10:20]:
            extra = copy.deepcopy(sample["native_weapon_bones"][0])
            extra["name"] = "middle"
            extra["parent_name"] = "jntWpn_1"
            sample["native_weapon_bones"].append(extra)
            sample["native_weapon_bones"][1]["parent_name"] = "middle"
        result = self.batch(rows)
        trip = result["captures"][0]["round_trips"][0]
        self.assertEqual(trip["status"], "not_comparable")
        self.assertIn("topology", trip["reason"])

    def test_unit_scale_preserves_measured_rotation_and_offset(self):
        row = make_row(1, joint_angle=np.pi / 2, units=100.)
        _, bones = mechanisms.snapshot(row, profiles.Policy())
        np.testing.assert_allclose(bones["jntWpn_7"]["transform"][3, :3], [.02, .04, -.5], atol=1e-10)

    def test_missing_scale_or_incomplete_capture_is_rejected(self):
        for field in ("units_per_meter", "weapon_bones_complete"):
            sample = make_row(1)
            sample.pop(field)
            with self.assertRaises(ValueError):
                mechanisms.snapshot(sample, profiles.Policy())
        sample = make_row(1)
        sample["weapon_bones_dropped"] = 1
        with self.assertRaisesRegex(ValueError, "incomplete"):
            mechanisms.snapshot(sample, profiles.Policy())

    def test_invalid_child_transforms_are_rejected_not_normalized(self):
        for error in ("nan", "degenerate", "scaled", "reflection", "projective"):
            with self.subTest(error=error):
                sample = make_row(1)
                value = np.eye(4)
                if error == "nan":
                    value[0, 0] = float("nan")
                elif error == "degenerate":
                    value[0, :3] = 0
                elif error == "scaled":
                    value[0, 0] = 1.2
                elif error == "reflection":
                    value[0, 0] = -1
                else:
                    value[1, 3] = .2
                sample["native_weapon_bones"][1]["native"] = value.reshape(-1).tolist()
                with self.assertRaises(ValueError):
                    mechanisms.snapshot(sample, profiles.Policy())

    def test_small_native_roundoff_uses_existing_rigidity_tolerance(self):
        sample = make_row(1)
        value = np.array(sample["native_weapon_bones"][1]["native"]).reshape(4, 4)
        value[:3, :3] *= 1.0001
        sample["native_weapon_bones"][1]["native"] = value.reshape(-1).tolist()
        mechanisms.snapshot(sample, profiles.Policy())

    def test_duplicate_names_and_cyclic_topology_rejected(self):
        sample = make_row(1)
        sample["native_weapon_bones"].append(copy.deepcopy(sample["native_weapon_bones"][1]))
        with self.assertRaisesRegex(ValueError, "duplicate"):
            mechanisms.snapshot(sample, profiles.Policy())
        sample = make_row(1)
        sample["native_weapon_bones"][1]["parent_name"] = "jntWpn_7"
        with self.assertRaisesRegex(ValueError, "cycle"):
            mechanisms.snapshot(sample, profiles.Policy())

    def test_root_must_be_inclusive_and_match_dedicated_native_matrix(self):
        sample = make_row(1)
        sample["native_weapon_bones"] = sample["native_weapon_bones"][1:]
        with self.assertRaisesRegex(ValueError, "root is absent"):
            mechanisms.snapshot(sample, profiles.Policy())
        sample = make_row(1)
        sample["native_weapon_bones"][0]["native"][12] += .1
        with self.assertRaisesRegex(ValueError, "disagrees"):
            mechanisms.snapshot(sample, profiles.Policy())

    def test_hidden_leaf_is_not_inverted_or_called_a_sight(self):
        rows = trip_rows()
        for i in range(10, 20):
            rows[i] = make_row(i, 2, "40mmGL", hidden=True)
        result = self.batch(rows)
        self.assertFalse(mechanisms.strict_incomplete(result))
        change = result["captures"][0]["round_trips"][0]["repeatable_changes"][0]
        self.assertEqual(change["outbound"]["status"], "visibility_change")
        self.assertIsNone(change["semantic_role"])
        middle = result["captures"][0]["episodes"][1]["bones"]["jntWpn_7"]
        self.assertNotIn("matrix", middle["visible_pose"])

    def test_hidden_branch_or_rigid_bone_mislabeled_hidden_rejected(self):
        sample = make_row(1)
        sample["native_weapon_bones"][1]["hidden"] = True
        with self.assertRaisesRegex(ValueError, "collapsed"):
            mechanisms.snapshot(sample, profiles.Policy())
        sample = make_row(1, hidden=True)
        child = copy.deepcopy(sample["native_weapon_bones"][0])
        child.update(name="child", parent_name="jntWpn_7")
        sample["native_weapon_bones"].append(child)
        with self.assertRaisesRegex(ValueError, "leaf"):
            mechanisms.snapshot(sample, profiles.Policy())

    def test_hidden_instantaneous_samples_do_not_establish_visibility_state(self):
        rows = trip_rows()
        for i in range(10, 20):
            rows[i] = make_row(i, 2, "40mmGL", hidden=True)
            rows[i]["captured_ms"] = 2000 + i
        result = self.batch(rows)
        self.assertTrue(mechanisms.strict_incomplete(result))
        state = result["captures"][0]["episodes"][1]["bones"]["jntWpn_7"]
        self.assertEqual(state["visibility"], "insufficient_hidden_samples")

    def test_duplicate_source_samples_do_not_create_a_stable_bone_pose(self):
        rows = [make_row(0) for _ in range(12)]
        result = self.batch(rows)
        first = result["captures"][0]["episodes"][0]
        self.assertEqual(first["counts"]["rejected_sequence"], 11)
        self.assertNotIn("matrix", first["bones"]["jntWpn_7"]["visible_pose"])
        self.assertTrue(mechanisms.strict_incomplete(result))

    def test_root_parent_inside_subtree_is_rejected(self):
        sample = make_row(1)
        sample["native_weapon_bones"][0]["parent_name"] = "jntWpn_7"
        with self.assertRaisesRegex(ValueError, "outside"):
            mechanisms.snapshot(sample, profiles.Policy())

    def test_pivot_line_fit_uses_row_vector_stationary_equation(self):
        closed = transform((.04, -.12, -.2), .3)
        motion = transform(angle=np.pi / 2)
        pivot = np.array([.1, .2, .3])
        motion[3, :3] = pivot - pivot @ motion[:3, :3]
        opened = closed @ motion
        c = {"status": "candidate", "matrix": closed.reshape(-1).tolist()}
        o = {"status": "candidate", "matrix": opened.reshape(-1).tolist()}
        fit = mechanisms.hinge_candidate(c, o, profiles.Policy())
        self.assertEqual(fit["status"], "pivot_line_candidate")
        np.testing.assert_allclose(fit["axis_direction"], [0, 0, 1], atol=1e-10)
        np.testing.assert_allclose(fit["point_on_axis_m"], [.1, .2, 0], atol=1e-10)
        self.assertLess(fit["stationary_fit_residual_m"], 1e-10)
        self.assertIsNone(fit["semantic_role"])

    def test_half_turn_pivot_axis_remains_well_defined(self):
        c = {"status": "candidate", "matrix": transform().reshape(-1).tolist()}
        o = {"status": "candidate", "matrix": transform(angle=np.pi).reshape(-1).tolist()}
        fit = mechanisms.hinge_candidate(c, o, profiles.Policy())
        self.assertEqual(fit["status"], "pivot_line_candidate")
        np.testing.assert_allclose(fit["axis_direction"], [0, 0, 1], atol=1e-10)

    def test_translation_and_tiny_rotation_do_not_invent_pivot(self):
        c = {"status": "candidate", "matrix": transform().reshape(-1).tolist()}
        for angle in (0., .01):
            o = {"status": "candidate", "matrix": transform((.1, 0, 0), angle).reshape(-1).tolist()}
            fit = mechanisms.hinge_candidate(c, o, profiles.Policy())
            self.assertEqual(fit["status"], "insufficient_rotation")
            self.assertNotIn("point_on_axis_m", fit)

    def test_screw_displacement_and_nonreturning_motion_reject_hinge(self):
        c = {"status": "candidate", "matrix": transform().reshape(-1).tolist()}
        o = {"status": "candidate", "matrix": transform((0, 0, .1), np.pi / 2).reshape(-1).tolist()}
        fit = mechanisms.hinge_candidate(c, o, profiles.Policy())
        self.assertEqual(fit["status"], "non_hinge_rigid_change")
        self.assertAlmostEqual(fit["stationary_fit_residual_m"], .1)
        self.assertNotIn("point_on_axis_m", fit)
        self.assertEqual(mechanisms.hinge_candidate(c, o, profiles.Policy(), repeatable=False)["status"], "nonrepeatable")

    def test_nonreturning_pose_is_reported_and_fails_strict(self):
        rows = trip_rows()
        for i in range(20, 30):
            rows[i] = make_row(i, 3, joint_angle=.3)
        result = self.batch(rows)
        self.assertTrue(mechanisms.strict_incomplete(result))
        self.assertIn("jntWpn_7", result["captures"][0]["round_trips"][0]["nonreturning_bones"])

    def test_one_bad_child_preserves_partial_evidence_but_strict_rejects(self):
        rows = trip_rows()
        rows[0]["native_weapon_bones"][1]["native"] = [0] * 16
        result = self.batch(rows)
        self.assertTrue(mechanisms.strict_incomplete(result))
        first = result["captures"][0]["episodes"][0]
        self.assertEqual(first["counts"]["rejected_snapshots"], 1)
        self.assertEqual(first["bones"]["jntWpn_7"]["visible_pose"]["status"], "candidate")


if __name__ == "__main__":
    unittest.main()
