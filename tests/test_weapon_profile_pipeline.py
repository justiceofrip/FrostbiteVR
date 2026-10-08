"""Regressions for evidence extraction; synthetic matrices, no game assets/process."""
import copy
import hashlib
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import weapon_profile_pipeline as pipeline
import numpy as np


def transform(position=(0., 0., 0.)):
    result = np.eye(4)
    result[3, :3] = position
    return result


def row(index, asset="XM8_sp_s", owner=1, pointer=100, right=(-.02, -.07, -.21), pending=False):
    weapon = transform((100 + index * .1, 20, -40))
    local = transform(right)
    left = transform((.05, -.05, -.65))
    return {"generation": index + 1, "captured_ms": 1000 + index * 100, "owner_generation": owner, "weapon": pointer,
            "space": 2, "units_per_meter": 1, "skeleton": "stable-topology-a", "asset_name": asset,
            "profile_id": "bc2:" + asset, "attachment_pending": pending,
            "bone_roles": {"weapon_root": "jntWpn_1", "left_wrist": "LeftHand", "right_wrist": "RightHand"},
            "native": weapon.reshape(-1).tolist(),
            "native_right_wrist": (local @ weapon).reshape(-1).tolist(),
            "native_left_wrist": (left @ weapon).reshape(-1).tolist(),
            "placed": weapon.reshape(-1).tolist(),
            "right_wrist_matrix": (local @ weapon).reshape(-1).tolist(),
            "arms": [{"tracked": True}, {"tracked": True}]}


def document(rows, new=True):
    field = "weapon_profile_samples" if new else "hand_evidence"
    return {"hooks_disabled": True, "gameplay": {"rig_publication": {
        field: rows, "source_changes": 0, "packing_failures": 0, "fallback_failures": 0,
        "native_animation_written": False}}}


class PipelineTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name)

    def tearDown(self):
        # Only this test-created directory may be recursively cleaned up.
        self.assertEqual(self.root.resolve().parent, Path(tempfile.gettempdir()).resolve())
        self.assertTrue(self.root.name.startswith("tmp"))
        self.tmp.cleanup()

    def capture(self, rows, name="capture", new=True, extra=None):
        folder = self.root / name
        folder.mkdir()
        path = folder / "native-trace.json"
        value = document(rows, new)
        if extra:
            value["gameplay"]["rig_publication"].update(extra)
        path.write_text(json.dumps(value), encoding="utf8")
        (folder / "completion.json").write_text(json.dumps({
            "bootstrap_exit": 0, "game_exited": False, "game_responding": True, "new_crash_report": False}))
        return path

    def measurement(self, result, hand="right"):
        return result["profiles"][0]["rig_variants"][0][hand + "_wrist_in_weapon"]

    def test_world_motion_cancels_without_averaging_or_acceptance(self):
        path = self.capture([row(i) for i in range(16)])
        batch = pipeline.build_batch([path])
        measured = self.measurement(batch)
        self.assertEqual(measured["status"], "candidate")
        np.testing.assert_allclose(np.array(measured["matrix"]).reshape(4, 4)[3, :3], [-.02, -.07, -.21])
        self.assertFalse(batch["profiles"][0]["runtime_enabled"])
        self.assertTrue(all(x["verification"] == "unverified" for x in batch["profiles"][0]["features"].values()))
        self.assertIsNone(batch["profiles"][0]["model_forward"])

    def test_delayed_equip_pose_does_not_cache_previous_gun(self):
        rows = [row(i, right=(-.03, -.146, -.267), pending=True) for i in range(8)]
        rows += [row(i) for i in range(8, 24)]
        batch = pipeline.build_batch([self.capture(rows)])
        self.assertEqual(batch["captures"][0]["segments"][0]["counts"]["pending_or_unknown"], 8)
        np.testing.assert_allclose(np.array(self.measurement(batch)["matrix"]).reshape(4, 4)[3, :3], [-.02, -.07, -.21])

    def test_actor_and_item_boundaries_never_share_stability_samples(self):
        rows = [row(i, owner=1) for i in range(4)]
        rows += [row(i, owner=2) for i in range(4, 8)]
        rows += [row(i, owner=2, pointer=200, asset="SPAS12_sp") for i in range(8, 12)]
        batch = pipeline.build_batch([self.capture(rows)])
        self.assertEqual(len(batch["profiles"]), 2)
        self.assertEqual(len(batch["captures"][0]["segments"]), 3)
        for profile in batch["profiles"]:
            self.assertEqual(profile["rig_variants"][0]["right_wrist_in_weapon"]["status"], "insufficient_or_unstable_evidence")

    def test_actor_pointer_separates_equal_generation_numbers(self):
        rows = [row(i) for i in range(4)] + [row(i) for i in range(4)]
        for i, sample in enumerate(rows):
            sample["actor"] = 10 if i < 4 else 20
        batch = pipeline.build_batch([self.capture(rows)])
        self.assertEqual(len(batch["captures"][0]["segments"]), 2)
        self.assertNotIn("matrix", self.measurement(batch))

    def test_same_bone_roles_do_not_merge_different_topologies(self):
        rows = [row(i) for i in range(8)] + [row(i) for i in range(8, 16)]
        for sample in rows[8:]:
            sample["skeleton"] = "stable-topology-b"
        batch = pipeline.build_batch([self.capture(rows)])
        self.assertEqual(len(batch["profiles"][0]["rig_variants"]), 2)

    def test_units_are_required_for_new_raw_capture_and_normalized(self):
        rows = [row(i) for i in range(8)]
        for sample in rows:
            sample["units_per_meter"] = 100
            for key in ("native", "native_left_wrist", "native_right_wrist", "placed", "right_wrist_matrix"):
                value = np.array(sample[key]).reshape(4, 4)
                value[3, :3] *= 100
                sample[key] = value.reshape(-1).tolist()
        batch = pipeline.build_batch([self.capture(rows, "scaled")])
        np.testing.assert_allclose(np.array(self.measurement(batch)["matrix"]).reshape(4, 4)[3, :3], [-.02, -.07, -.21])
        rows[0].pop("units_per_meter")
        batch = pipeline.build_batch([self.capture(rows, "missing")])
        self.assertIn("units_per_meter", batch["captures"][0]["issues"][0]["reason"])
        self.assertNotIn("matrix", self.measurement(batch))

    def test_raw_capture_survives_unrelated_publication_fallback_warning(self):
        path = self.capture([row(i) for i in range(8)], extra={"fallback_failures": 30})
        batch = pipeline.build_batch([path])
        self.assertEqual(batch["captures"][0]["checks"]["status"], "passed")
        self.assertEqual(batch["captures"][0]["checks"]["publication_warnings"]["fallback_failures"], 30)
        self.assertEqual(self.measurement(batch)["status"], "candidate")
        self.assertEqual(batch["profiles"][0]["features"]["support_grip"]["verification"], "unverified")

    def test_episode_and_capture_sequence_handle_generation_rollback(self):
        rows = [row(i) for i in range(4)] + [row(i) for i in range(4)]
        for i, sample in enumerate(rows):
            sample["capture_sequence"] = i + 1
            sample["capture_episode"] = 1 if i < 4 else 2
        batch = pipeline.build_batch([self.capture(rows)])
        self.assertEqual(len(batch["captures"][0]["segments"]), 2)
        self.assertNotIn("matrix", self.measurement(batch))

    def test_many_frames_in_one_instant_do_not_prove_stability(self):
        rows = [row(i) for i in range(12)]
        for i, sample in enumerate(rows):
            sample["captured_ms"] = 1000 + i
        batch = pipeline.build_batch([self.capture(rows)])
        self.assertNotIn("matrix", self.measurement(batch))
        measured = batch["captures"][0]["segments"][0]["right_wrist_in_weapon"]
        self.assertEqual(measured["inlier_span_ms"], 11)

    def test_distinct_actor_stable_offsets_are_not_averaged(self):
        rows = [row(i, owner=1) for i in range(10)]
        rows += [row(i, owner=2, right=(.3, -.07, -.21)) for i in range(10, 20)]
        measured = self.measurement(pipeline.build_batch([self.capture(rows)]))
        self.assertEqual(measured["status"], "conflicting_stable_poses")
        self.assertNotIn("matrix", measured)

    def test_multiple_stable_animation_poses_are_ambiguous(self):
        rows = [row(i) for i in range(16)] + [row(i, right=(.3, -.07, -.21)) for i in range(16, 24)]
        batch = pipeline.build_batch([self.capture(rows)])
        self.assertEqual(batch["captures"][0]["segments"][0]["right_wrist_in_weapon"]["status"], "ambiguous_or_unstable")
        self.assertNotIn("matrix", self.measurement(batch))

    def test_stable_capture_does_not_hide_another_ambiguous_pose(self):
        first = self.capture([row(i) for i in range(12)], "stable")
        rows = [row(i) for i in range(12)] + [row(i, right=(.3, -.07, -.21)) for i in range(12, 24)]
        second = self.capture(rows, "ambiguous")
        result = self.measurement(pipeline.build_batch([first, second]))
        self.assertEqual(result["status"], "unresolved_pose_variation")
        self.assertNotIn("matrix", result)

    def test_isolated_outlier_is_reported_and_not_baked_in(self):
        rows = [row(i) for i in range(18)]
        rows[8] = row(8, right=(.3, -.07, -.21))
        batch = pipeline.build_batch([self.capture(rows)])
        measured = batch["captures"][0]["segments"][0]["right_wrist_in_weapon"]
        self.assertEqual(measured["status"], "candidate")
        self.assertEqual(measured["outlier_rows"], [8])
        self.assertGreater(measured["max_position_deviation_m"], .3)

    def test_invalid_matrices_are_rejected(self):
        for change in ("nonfinite", "degenerate", "reflection", "projective"):
            with self.subTest(change=change):
                value = np.eye(4)
                if change == "nonfinite":
                    value[0, 0] = float("nan")
                elif change == "degenerate":
                    value[0, :3] = 0
                elif change == "reflection":
                    value[0, 0] = -1
                else:
                    value[2, 3] = .5
                with self.assertRaises(ValueError):
                    pipeline.matrix(value)
        rows = [row(i) for i in range(8)]
        rows[0]["native"] = [0] * 16
        batch = pipeline.build_batch([self.capture(rows)])
        self.assertEqual(batch["captures"][0]["segments"][0]["counts"]["invalid_transform"], 1)
        self.assertNotIn("matrix", self.measurement(batch))

    def test_strict_rejects_bad_matrix_without_erasing_valid_candidate(self):
        rows = [row(i) for i in range(24)]
        rows[12]["native_right_wrist"] = [0] * 16
        batch = pipeline.build_batch([self.capture(rows)])
        self.assertEqual(self.measurement(batch)["status"], "candidate")
        self.assertTrue(pipeline.strict_incomplete(batch))
        self.assertEqual(batch["captures"][0]["segments"][0]["counts"]["invalid_transform"], 1)

    def test_strict_rejects_out_of_order_same_episode_but_keeps_partial_audit(self):
        rows = [row(i) for i in range(21)]
        rows[-1]["capture_sequence"] = 0
        batch = pipeline.build_batch([self.capture(rows)])
        self.assertEqual(self.measurement(batch)["status"], "candidate")
        self.assertTrue(pipeline.strict_incomplete(batch))
        self.assertEqual(batch["captures"][0]["segments"][0]["counts"]["out_of_order_generation"], 1)

    def test_strict_rejects_duplicate_even_with_sufficient_remaining_samples(self):
        rows = [row(i) for i in range(20)] + [row(0)]
        batch = pipeline.build_batch([self.capture(rows)])
        self.assertEqual(self.measurement(batch)["status"], "candidate")
        self.assertTrue(pipeline.strict_incomplete(batch))
        self.assertEqual(batch["captures"][0]["segments"][0]["counts"]["duplicate_generation"], 1)

    def test_duplicate_frame_cannot_create_sufficient_evidence(self):
        rows = [row(0) for _ in range(20)]
        batch = pipeline.build_batch([self.capture(rows)])
        self.assertEqual(batch["captures"][0]["segments"][0]["counts"]["duplicate_generation"], 19)
        self.assertNotIn("matrix", self.measurement(batch))

    def test_new_collector_replaces_debug_ring_not_double_counts(self):
        rows = [row(i) for i in range(8)]
        batch = pipeline.build_batch([self.capture(rows, extra={"hand_evidence": rows})])
        self.assertEqual(batch["captures"][0]["sample_count"], 8)
        self.assertEqual(batch["captures"][0]["evidence_field"], "weapon_profile_samples")

    def test_pointer_is_capture_local_and_requires_mapping(self):
        rows = [row(i) for i in range(8)]
        for sample in rows:
            sample.pop("asset_name")
            sample.pop("profile_id")
        a = self.capture(rows, "a", False)
        b = self.capture(rows, "b", False)
        self.assertEqual(pipeline.build_batch([a, b])["unmapped_segments"], 2)
        mapping = {str(a.resolve()): [{"weapon": 100, "asset_id": "XM8_sp_s"}],
                   str(b.resolve()): [{"weapon": 100, "asset_id": "SPAS12_sp"}]}
        self.assertEqual(len(pipeline.build_batch([a, b], mapping)["profiles"]), 2)

    def test_ambiguous_legacy_pointer_map_is_rejected(self):
        rows = [row(i) for i in range(8)]
        path = self.capture(rows, new=False)
        bindings = [{"weapon": 100, "asset_id": "XM8_sp_s"}, {"weapon": 100, "asset_id": "SPAS12_sp"}]
        batch = pipeline.build_batch([path], {str(path.resolve()): bindings})
        self.assertFalse(batch["profiles"])
        self.assertEqual(len(batch["captures"][0]["issues"]), 8)

    def test_mapping_is_bound_to_exact_capture_hash(self):
        capture = self.capture([row(i) for i in range(8)])
        path = self.root / "mapping.json"
        path.write_text(json.dumps({"schema": "fvr.weapon_capture_map", "schema_version": 1,
                                   "captures": [{"trace": str(capture), "sha256": "wrong", "items": []}]}))
        with self.assertRaisesRegex(ValueError, "SHA-256"):
            pipeline.read_mapping(path)

    def test_source_write_failure_blocks_measurement_promotion(self):
        path = self.capture([row(i) for i in range(8)], extra={"source_changes": 1})
        batch = pipeline.build_batch([path])
        self.assertEqual(batch["captures"][0]["checks"]["status"], "failed")
        self.assertNotIn("matrix", self.measurement(batch))

    @staticmethod
    def selected_rig_provenance(sample):
        sample['actor'] = 10
        sample['capture_provenance'] = {'equipment_generation': 5, 'weapon_data': 101, 'persistence': 102,
            'rig': {'soldier': 10, 'weak': 11, 'animation': 12, 'skeleton': 13, 'pose': 14,
                    'world_header': 15, 'world_matrices': 16, 'skin_matrices': 17,
                    'evaluated_matrices': 18, 'count': 128, 'native_ik': False}}
        sample['pose_asset_binding_verified'] = sample['submitted_mesh_binding_verified'] = False
        return sample

    def test_legacy_selected_label_stays_unbound_even_if_pose_stable(self):
        batch = pipeline.build_batch([self.capture([row(i) for i in range(8)])])
        m = self.measurement(batch)
        self.assertEqual(m['status'], 'candidate')
        self.assertFalse(m['usable_as_verified_grasp'])
        self.assertFalse(batch['profiles'][0]['pose_asset_binding_verified'])
        attribution = batch['captures'][0]['segments'][0]['pose_asset_attribution']
        self.assertEqual(attribution['status'], 'unbound_selected_asset_label')
        self.assertIsNone(attribution['selected_equipment_and_rig'])
        self.assertIn('no proven submitted mesh/animation backlink', pipeline.render_audit(batch))

    def test_new_provenance_records_exact_rig_without_claiming_mesh(self):
        rows = [self.selected_rig_provenance(row(i)) for i in range(8)]
        batch = pipeline.build_batch([self.capture(rows)])
        segment = batch['captures'][0]['segments'][0]
        self.assertEqual(segment['pose_asset_attribution']['selected_equipment_and_rig'], rows[0]['capture_provenance'])
        self.assertFalse(segment['left_wrist_in_weapon']['usable_as_verified_grasp'])
        self.assertFalse(self.measurement(batch)['pose_asset_binding_verified'])

    def test_same_pointer_asset_and_rig_hash_split_changed_equipment_tuple(self):
        for field in ('equipment_generation', 'weapon_data', 'persistence'):
            with self.subTest(field=field):
                rows = [self.selected_rig_provenance(row(i)) for i in range(16)]
                for sample in rows[8:]: sample['capture_provenance'][field] += 1
                batch = pipeline.build_batch([self.capture(rows, field)])
                self.assertEqual(len(batch['captures'][0]['segments']), 2)
                self.assertFalse(self.measurement(batch)['usable_as_verified_grasp'])

    def test_same_pointer_split_actual_rig_and_buffer_replacement(self):
        for field in ('animation', 'skeleton', 'pose', 'world_header', 'world_matrices', 'skin_matrices', 'evaluated_matrices'):
            with self.subTest(field=field):
                rows = [self.selected_rig_provenance(row(i)) for i in range(16)]
                for sample in rows[8:]: sample['capture_provenance']['rig'][field] += 1
                batch = pipeline.build_batch([self.capture(rows, field)])
                self.assertEqual(len(batch['captures'][0]['segments']), 2)

    def test_new_and_legacy_provenance_do_not_merge(self):
        rows = [self.selected_rig_provenance(row(i)) for i in range(8)] + [row(i) for i in range(8, 16)]
        for sample in rows: sample['actor'] = 10
        batch = pipeline.build_batch([self.capture(rows)])
        self.assertEqual(len(batch['captures'][0]['segments']), 2)

    def test_true_mesh_proof_flag_cannot_promote_unbound_capture(self):
        for flag in ('pose_asset_binding_verified', 'submitted_mesh_binding_verified'):
            sample = self.selected_rig_provenance(row(1));sample[flag] = True
            with self.assertRaisesRegex(ValueError, 'unsupported asserted'):
                pipeline.pose_asset_provenance(sample)

    def test_capture_provenance_rejects_wrong_actor_missing_or_bad_ids(self):
        valid = self.selected_rig_provenance(row(1))
        bad = copy.deepcopy(valid);bad['capture_provenance']['rig']['soldier'] += 1
        with self.assertRaisesRegex(ValueError, 'actor/rig'): pipeline.pose_asset_provenance(bad)
        for field in ('equipment_generation', 'weapon_data'):
            for value in (None, 0, -1, True, 2**64):
                bad = copy.deepcopy(valid);bad['capture_provenance'][field] = value
                with self.assertRaises(ValueError): pipeline.pose_asset_provenance(bad)
        bad = copy.deepcopy(valid);del bad['capture_provenance']['rig']['evaluated_matrices']
        with self.assertRaises(ValueError): pipeline.pose_asset_provenance(bad)

    def test_explicit_scoped_review_preserves_evidence_without_enabling_runtime(self):
        evidence = self.root / "acceptance.json"
        evidence.write_text('{"accepted":"SPAS and XM8 grips; no reconnect claim"}')
        ref = {"path": str(evidence), "sha256": pipeline.digest(evidence), "scope": "XM8 support grip only"}
        review = self.root / "reviews.json"
        review.write_text(json.dumps({"schema": "fvr.weapon_profile_reviews", "schema_version": 1,
            "reviews": [{"stable_id": "bc2:XM8_sp_s", "applies_to_checkpoint": "known-build",
                         "features": {"support_grip": {"verification": "headset_accepted",
                                                       "native_evidence": ref, "headset_evidence": ref}}}]}))
        reviews = pipeline.read_reviews(review)
        batch = pipeline.build_batch([self.capture([row(i) for i in range(8)])], reviews=reviews)
        profile = batch["profiles"][0]
        self.assertEqual(profile["features"]["support_grip"]["verification"], "headset_accepted")
        self.assertEqual(profile["features"]["translated_muzzle"]["verification"], "unverified")
        self.assertFalse(profile["runtime_enabled"])
        evidence.write_text("{}")
        with self.assertRaisesRegex(ValueError, "SHA-256"):
            pipeline.read_reviews(review)


if __name__ == "__main__":
    unittest.main()
