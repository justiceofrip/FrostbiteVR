"""Offline regressions for the reviewed three-model fixed-container data.

No installed game, native execution, mock success receipt, or headset claim.
"""
import copy
import hashlib
import math
from pathlib import Path
import struct
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.dont_write_bytecode = True
sys.path.insert(0, str(ROOT / 'tools'))
import bc2_fixed_container_geometry as fixed


class FixedContainerGeometry(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.document = fixed.load(ROOT / 'profiles/weapons46-special/geometry.json')
        cls.models = {m['label']: m for m in cls.document['models']}
        cls.policy = fixed.load(ROOT / 'handoff/tasks/WEAPONS/20261030-special46/authoring-policy.json')
        cls.sources = {label: fixed.load(ROOT / rule['sample_path']) for label, rule in cls.policy['models'].items()}

    def test_exact_native_capacity_bulk_transfer_and_one_ledger_policy(self):
        self.assertEqual(set(self.models), {'MP412', 'M1Garand', 'Type5'})
        for name, capacity in [('MP412', 6), ('M1Garand', 8), ('Type5', 8)]:
            profile = self.models[name]['interaction']
            self.assertEqual((profile['capacity'], profile['roundsPerInsertion']), (capacity, capacity))
            for native in self.sources[name]['native']:
                self.assertEqual(native['fields']['Ammo.MagazineCapacity']['value'], capacity)
                self.assertEqual(native['fields']['FireLogic.ReloadType']['value'], 'rtMagazine')
            self.assertIn('No ejection/reset/second ammunition ledger', self.models[name]['visibility']['live_rounds'])

    def test_native_alias_does_not_merge_different_garand_meshes_or_configurations(self):
        m1, type5 = self.models['M1Garand'], self.models['Type5']
        self.assertEqual({s['asset'] for m in (m1, type5) for s in m['selectors']}, {'Garand'})
        self.assertNotEqual(m1['selectors'][0]['mesh'], type5['selectors'][0]['mesh'])
        self.assertNotEqual(m1['selectors'][0]['meshSha256'], type5['selectors'][0]['meshSha256'])
        self.assertNotEqual(m1['interaction']['id'], type5['interaction']['id'])
        selectors = [s for m in self.models.values() for s in m['selectors']]
        keys = [(s['configurationPath'], s['mesh'], s['rigFingerprint']) for s in selectors]
        self.assertEqual(len(keys), 4)
        self.assertEqual(len(set(keys)), 4)

    def test_rigid_frames_remain_proper_after_runtime_float32_conversion(self):
        count = 0
        def walk(value):
            nonlocal count
            if isinstance(value, list):
                if len(value) == 16 and all(isinstance(x, (int, float)) for x in value):
                    fixed.rigid(list(struct.unpack('<16f', struct.pack('<16f', *value))))
                    count += 1
                else:
                    for item in value:
                        walk(item)
            elif isinstance(value, dict):
                for item in value.values():
                    walk(item)
        walk(self.document)
        self.assertGreater(count, 300)

    def test_all_operations_meet_real_recognizer_motion_constraints(self):
        for model in self.models.values():
            for value in model['interaction']['geometry'].values():
                fixed.validate_motion(value['motion'])
        self.assertEqual(set(self.models['MP412']['interaction']['geometry']), {'Open', 'EjectSpent', 'Insert', 'Close'})
        for label in ('M1Garand', 'Type5'):
            self.assertEqual(set(self.models[label]['interaction']['geometry']), {'Insert'})

    def test_mp412_barrel_axis_and_complete_source_geometry_reproduce_endpoints(self):
        model, source = self.models['MP412'], self.sources['MP412']
        for step, sample in [('Open', 24), ('EjectSpent', 43), ('Close', 0)]:
            motion = model['interaction']['geometry'][step]['motion']
            end = fixed.mag.multiply(fixed.along(motion['motionFromContact'], 'Hinge', motion['travel']), motion['weaponFromMotion'])
            expected = fixed.mag.multiply(model['grasps'][step]['part_from_wrist'], fixed.at(source, sample, 'jntWpn_4'))
            distance, angle = fixed.mag.grasp_separation(end, expected)
            self.assertLess(distance, 1e-6)
            self.assertLess(angle, 1e-6)
        self.assertTrue(model['interaction']['geometry']['EjectSpent']['motion']['returnToStart'])
        full = sum(model['interaction']['geometry'][step]['motion']['travel'] for step in ('Open', 'EjectSpent'))
        self.assertAlmostEqual(math.degrees(full), 86.004424, places=4)
        coupled = {p['bone'] for p in model['parts'] if p['coupledOpening']}
        self.assertEqual(coupled, {'jntWpn_4', 'jntWpn_12', 'jntWpn_13'})
        self.assertEqual(len(model['parts']), len(source['parts']))

    def test_speedloader_has_metal_body_and_exact_carry_segment_offset(self):
        model, source = self.models['MP412'], self.sources['MP412']
        self.assertEqual(model['supply_parts'], ['jntWpn_13', 'jntWpnwpnJnt_14'])
        hardware = next(p for p in model['parts'] if p['bone'] == 'jntWpnwpnJnt_14')
        expected = fixed.mag.multiply(fixed.at(source, 63, hardware['bone']), fixed.mag.inverse_rigid(fixed.at(source, 63, 'jntWpn_13')))
        self.assertEqual(hardware['supplyFromPart'], expected)
        self.assertGreater(hardware['visible_triangles'], 300)
        self.assertLess(model['grasps']['Insert']['contact_surface_distance_m'], .04)

    def test_two_part_garand_clip_and_explicit_right_to_left_source_policy(self):
        for label in ('M1Garand', 'Type5'):
            model = self.models[label]
            self.assertEqual(model['supply_parts'], ['jntWpn_12', 'jntWpn_13'])
            actual = fixed.contact(self.sources[label], 36, 'jntWpn_12', 'RightHand')
            mirrored = fixed.mirrored_left(actual)
            self.assertEqual(model['grasps']['Insert']['part_from_wrist'], mirrored['part_from_wrist'])
            self.assertEqual(model['grasps']['Insert']['wrist_from_fingers'], mirrored['wrist_from_fingers'])
            self.assertLess(actual['knuckle_bounds_distance_m'], .03)

    def test_decoder_failures_stay_rejected_and_outside_chosen_grip(self):
        for label in ('M1Garand', 'Type5'):
            gaps = self.models[label]['source']['preserved_decoder_gaps']
            self.assertEqual(len(gaps), 8)
            self.assertEqual({g['bone'] for g in gaps}, {'jntWpn_12', 'jntWpn_13'})
            with self.assertRaisesRegex(ValueError, 'absent; do not substitute'):
                fixed.at(self.sources[label], 23, 'jntWpn_12')
            for index in (33, 36, 69):
                fixed.at(self.sources[label], index, 'jntWpn_12')

    def test_cache_catalog_covers_every_supply_part_with_exact_hashes(self):
        catalog = fixed.load(ROOT / 'profiles/weapons46-special/body-ammo-assets.json')['profiles']
        self.assertEqual(len(catalog), 6)
        keys = {(r['asset'], r['mesh'], r['part']) for r in catalog}
        self.assertEqual(len(keys), 6)
        for model in self.models.values():
            for part in model['supply_parts']:
                key = (model['selectors'][0]['asset'], model['selectors'][0]['mesh'], part)
                self.assertIn(key, keys)
            for row in model['body_cache_entries']:
                self.assertTrue(row['sections'])
                self.assertEqual(len(row['mesh_sha256']), 64)
                self.assertEqual(len(row['lod_sha256']), 64)
                self.assertTrue(all(s['part_triangles'] > 0 and len(s['vertex_skin_hash']) == 16 for s in row['sections']))

    def test_rejects_nonrigid_frame_and_overlapping_or_teleport_placement(self):
        frame = fixed.mag.identity(); frame[0] = -1
        with self.assertRaises(ValueError):
            fixed.rigid(frame)
        motion = copy.deepcopy(self.models['M1Garand']['interaction']['geometry']['Insert']['motion'])
        motion['placement']['weaponFromDestination'] = motion['placement']['weaponFromSource'][:]
        with self.assertRaisesRegex(ValueError, 'overlap'):
            fixed.validate_motion(motion)
        motion['placement']['weaponFromDestination'][12] += .04
        with self.assertRaisesRegex(ValueError, 'teleport'):
            fixed.validate_motion(motion)

    def test_single_round_mp412_policy_cannot_reuse_bulk_transfer(self):
        policy = copy.deepcopy(self.policy['models']['MP412'])
        policy['rounds_per_insertion'] = 1
        with self.assertRaisesRegex(ValueError, 'single-round insertion'):
            fixed.model_profile(self.sources['MP412'], policy)

    def test_source_pins_and_generated_outputs_reproduce_exactly(self):
        for rule in self.policy['models'].values():
            self.assertEqual(hashlib.sha256((ROOT / rule['sample_path']).read_bytes()).hexdigest(), rule['sample_sha256'])
        fixed.derive(ROOT / 'handoff/tasks/WEAPONS/20261030-special46/authoring-policy.json',
                     ROOT / 'profiles/weapons46-special', verify=True)


if __name__ == '__main__':
    unittest.main()
