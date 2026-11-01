"""Portable exact-family boundaries; no game/native/headset acceptance."""
import copy
import hashlib
from pathlib import Path
import struct
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.dont_write_bytecode = True
sys.path.insert(0, str(ROOT / 'tools'))
import bc2_underbarrel_reload_geometry as underbarrel

OUTPUT = ROOT / 'profiles/weapons71-underbarrel-reload'
FAMILY = OUTPUT / 'xm8'
FAMILY_COUNTS = {'xm8': 11, 'an94': 11, 'm16': 18, 'aek': 11, 'hk416': 9, 'f2000': 11, 'aug': 9}
FAMILIES = [OUTPUT / name for name in FAMILY_COUNTS]


class UnderbarrelReloadGeometry(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.source = underbarrel.fixed.load(FAMILY / 'source.json')
        cls.measured = underbarrel.fixed.load(FAMILY / 'measurements.json')
        cls.model = underbarrel.model_profile(cls.source, cls.measured)

    @staticmethod
    def repin(source):
        variants = {v['configuration']: v for v in source['variants']}
        source['tuple_digests'] = {r['configuration']: underbarrel.digest(
            [r, variants[r['configuration']], underbarrel.coordinate_tuple(source['chain'])])
            for r in source['configurations']}

    def assert_matrix_close(self, actual, expected, tolerance=1e-6):
        self.assertLess(max(abs(a - b) for a, b in zip(actual, expected)), tolerance)

    def test_exact_eleven_selectors_and_three_native_names(self):
        selectors = self.model['selectors']
        self.assertEqual(len(selectors), 11)
        self.assertEqual({s['configurationPath'] for s in selectors}, set(self.source['configuration_paths']))
        self.assertEqual({s['asset'] for s in selectors}, {'40mmgl', '40mmsg', '40mmsmk'})
        self.assertEqual(len({(s['configurationPath'], s['mesh'], s['rigFingerprint']) for s in selectors}), 11)
        header = (OUTPUT / 'FixedContainer.generated.h').read_text(encoding='utf-8')
        self.assertIn('UnderbarrelFixedContainerEquipment', header)
        self.assertIn('FindUnderbarrelFixedContainerEquipment', header)
        self.assertIn('p.configurationPath==config&&p.mesh==mesh&&p.rigFingerprint==rig', header)
        self.assertNotIn('struct FixedContainerEquipmentProfile', header)
        self.assertNotIn('struct FixedContainerPartGeometry', header)

    def test_missing_or_duplicate_configuration_rejected(self):
        for key in ('configurations', 'variants', 'configuration_paths'):
            for duplicate in (False, True):
                source = copy.deepcopy(self.source)
                source[key] = source[key] + [source[key][0]] if duplicate else source[key][:-1]
                with self.subTest(key=key, duplicate=duplicate), self.assertRaises(ValueError):
                    underbarrel.model_profile(source, self.measured)

    def test_source_hash_change_rejected_even_for_nonrepresentative(self):
        source = copy.deepcopy(self.source)
        source['configurations'][0]['source']['sha256'] = '0' * 64
        with self.assertRaisesRegex(ValueError, 'source tuple pin'):
            underbarrel.model_profile(source, self.measured)

    def test_crossed_mesh_tree_skeleton_or_state_rejected_after_repin(self):
        for key in ('mesh', 'tree', 'skeleton', 'state'):
            source = copy.deepcopy(self.source)
            state = source['configurations'][0]['states'][0]
            if key == 'mesh':
                state['meshes'][0] = 'objects/weapons/handheld/wrong/wrong/00000000-0000-0000-0000-000000000000'
            elif key == 'tree':
                state['tree']['sha256'] = '0' * 64
            elif key == 'skeleton':
                state['roles']['handsikpose']['skeleton_reference'] += '/wrong'
            else:
                state['index'] = 1
            self.repin(source)
            with self.subTest(key=key), self.assertRaises(ValueError):
                underbarrel.model_profile(source, self.measured)

    def test_crossed_resource_bytes_rejected_after_repin(self):
        for key in ('mesh_sha256', 'lod_sha256', 'clip_hashes'):
            source = copy.deepcopy(self.source)
            source['variants'][0][key] = ['0' * 64, '1' * 64] if key == 'clip_hashes' else '0' * 64
            self.repin(source)
            with self.subTest(key=key), self.assertRaisesRegex(ValueError, 'byte-coordinate tuple'):
                underbarrel.model_profile(source, self.measured)

    def test_measured_tuple_must_match_all_source_coordinates(self):
        for key in ('rig', 'skeleton_sha256', 'mesh_sha256', 'clip_sha256'):
            measured = copy.deepcopy(self.measured)
            measured['coordinates'][key] = 'wrong'
            with self.subTest(key=key), self.assertRaisesRegex(ValueError, 'coordinate tuple'):
                underbarrel.model_profile(self.source, measured)

    def test_incomplete_hand_or_nonrigid_matrix_rejected(self):
        for bad in ('missing_finger', 'reflection', 'nonfinite'):
            measured = copy.deepcopy(self.measured)
            if bad == 'missing_finger':
                del measured['supply']['grasp']['fingers_to_part']['LeftHandThumb3']
            elif bad == 'reflection':
                measured['supply']['part_in_root']['jntWpn_13'][0] = -1
            else:
                measured['mechanism']['closed'][12] = float('nan')
            with self.subTest(bad=bad), self.assertRaises(ValueError):
                underbarrel.model_profile(self.source, measured)

    def test_measured_part_indices_cannot_cross_rigs_or_parts(self):
        measured = copy.deepcopy(self.measured)
        measured['parts']['jntWpn_13']['bone_index'] = 38
        with self.assertRaisesRegex(ValueError, 'part tuple'):
            underbarrel.model_profile(self.source, measured)

    def test_single_round_sequence_and_no_live_ammunition_deletion(self):
        interaction = self.model['interaction']
        self.assertEqual(interaction['kind'], 'SingleRoundBreech')
        self.assertEqual((interaction['capacity'], interaction['roundsPerInsertion']), (1, 1))
        self.assertEqual(set(interaction['geometry']), {'Open', 'Insert', 'Close'})
        for change in ({'capacity': 2}, {'rounds_per_insertion': 2}, {'delete_live_ammunition': True},
                       {'spent_case_visual_removal_only': False}):
            source = copy.deepcopy(self.source)
            source['policy'].update(change)
            with self.subTest(change=change), self.assertRaises(ValueError):
                underbarrel.model_profile(source, self.measured)

    def test_grasps_preserve_one_exact_snapshot_each(self):
        for step, section, sample in [('Open', 'mechanism', 7), ('Close', 'mechanism', 7), ('Insert', 'supply', 30)]:
            grasp = self.model['grasps'][step]
            source = self.measured[section]['grasp']
            self.assertEqual(grasp['sample'], sample)
            self.assertEqual(grasp['part_from_wrist'], source['hand_to_part'])
            for name, matrix in grasp['wrist_from_fingers'].items():
                self.assert_matrix_close(underbarrel.mag.multiply(matrix, grasp['part_from_wrist']),
                                         source['fingers_to_part'][name])
        self.assertEqual(self.model['interaction']['geometry']['Open']['contact']['radiusMeters'], .035)
        self.assertIn('wrist tolerance', self.source['policy']['opening'])

    def test_hinge_endpoints_reproduce_measured_part_transforms(self):
        for step, end in [('Open', 'open'), ('Close', 'closed')]:
            motion = self.model['interaction']['geometry'][step]['motion']
            actual = underbarrel.mag.multiply(underbarrel.fixed.along(motion['motionFromContact'], 'Hinge',
                                               motion['travel']), motion['weaponFromMotion'])
            expected = underbarrel.mag.multiply(self.model['grasps'][step]['part_from_wrist'],
                                                self.measured['mechanism'][end])
            self.assert_matrix_close(actual, expected)
            underbarrel.fixed.validate_motion(motion)

    def test_frozen_terminal_assembly_tracks_open_and_close(self):
        parts = {p['bone']: p for p in self.model['parts']}
        self.assertEqual(self.measured['supply']['assembly_sample'], 42)
        self.assertEqual(set(self.model['supply_parts']), {'jntWpn_13', 'jntWpnwpnJnt_14'})
        root = parts['jntWpn_13']; child = parts['jntWpnwpnJnt_14']
        for key in ('weaponFromClosed', 'weaponFromOpen'):
            self.assert_matrix_close(underbarrel.mag.multiply(child['supplyFromPart'], root[key]), child[key])
        self.assertTrue(all(p['coupledOpening'] for p in parts.values()))
        self.assertEqual(root['weaponFromStepEnd'][2], self.measured['supply']['destination'])

    def test_free_placement_does_not_claim_unimplemented_rail(self):
        insertion = self.model['interaction']['geometry']['Insert']['motion']
        self.assertEqual(insertion['kind'], 'FreePlacement')
        self.assertFalse(self.source['policy']['rail_enforced'])
        self.assertEqual(self.measured['bore']['inward_part'], [0, 0, -1])
        underbarrel.fixed.validate_motion(insertion)
        for change in ({'insertion_kind': 'Linear'}, {'rail_enforced': True}):
            source = copy.deepcopy(self.source); source['policy'].update(change)
            with self.assertRaisesRegex(ValueError, 'Shared rail API'):
                underbarrel.model_profile(source, self.measured)

    def test_seat_cannot_move_off_measured_breech(self):
        measured = copy.deepcopy(self.measured)
        measured['supply']['destination'][12] += .001
        with self.assertRaisesRegex(ValueError, 'Seated round'):
            underbarrel.model_profile(self.source, measured)

    def test_float32_frames_remain_proper(self):
        count = 0
        def visit(value):
            nonlocal count
            if isinstance(value, list):
                if len(value) == 16 and all(isinstance(v, (int, float)) for v in value):
                    underbarrel.fixed.rigid(list(struct.unpack('<16f', struct.pack('<16f', *value))))
                    count += 1
                else:
                    for item in value:
                        visit(item)
            elif isinstance(value, dict):
                for item in value.values():
                    visit(item)
        visit(self.model)
        self.assertGreater(count, 80)

    def test_generic_contract_accepts_another_family_id_without_aliases(self):
        source, measured = copy.deepcopy(self.source), copy.deepcopy(self.measured)
        source['family'] = measured['family'] = 'independent_measured_family_fixture'
        model = underbarrel.model_profile(source, measured)
        self.assertEqual(model['selectors'], self.model['selectors'])
        self.assertNotEqual(model['interaction']['id'], self.model['interaction']['id'])

    def test_explicit_slider_fixture_uses_existing_linear_mechanism_type(self):
        source, measured = copy.deepcopy(self.source), copy.deepcopy(self.measured)
        source['policy']['opening_kind'] = measured['mechanism']['motion_kind'] = 'Linear'
        measured['mechanism']['open'] = measured['mechanism']['closed'][:]
        measured['mechanism']['open'][14] += .1
        measured['supply']['destination'] = underbarrel.mag.multiply(
            measured['supply']['seat_in_mechanism'], measured['mechanism']['open'])
        model = underbarrel.model_profile(source, measured)
        for step, end in [('Open', 'open'), ('Close', 'closed')]:
            motion = model['interaction']['geometry'][step]['motion']
            self.assertEqual(motion['kind'], 'Linear')
            actual = underbarrel.mag.multiply(underbarrel.fixed.along(
                motion['motionFromContact'], 'Linear', motion['travel']), motion['weaponFromMotion'])
            expected = underbarrel.mag.multiply(model['grasps'][step]['part_from_wrist'], measured['mechanism'][end])
            self.assert_matrix_close(actual, expected)
        self.assertEqual(model['interaction']['geometry']['Insert']['motion']['kind'], 'FreePlacement')

    def test_artifacts_reproduce_without_reports_or_installed_assets(self):
        result = underbarrel.generate(FAMILIES, OUTPUT, verify=True)
        self.assertEqual(result['families'], 7)
        self.assertEqual(result['configurations'], 80)
        self.assertFalse(result['runtime_registered'])
        for family in FAMILIES:
            for receipt in underbarrel.fixed.load(family / 'source.json')['provenance']:
                path = ROOT / receipt['path']
                if path.is_file():
                    self.assertEqual(hashlib.sha256(path.read_bytes()).hexdigest(), receipt['sha256'])

    @staticmethod
    def family(name):
        source = underbarrel.fixed.load(OUTPUT / name / 'source.json')
        measured = underbarrel.fixed.load(OUTPUT / name / 'measurements.json')
        return source, measured, underbarrel.model_profile(source, measured)

    def test_seven_exact_families_keep_typed_ammunition_names(self):
        keys = []
        for name, count in FAMILY_COUNTS.items():
            source, measured, model = self.family(name)
            rows = {r['configuration']: r for r in source['configurations']}
            self.assertEqual(len(model['selectors']), count)
            self.assertEqual({s['asset'] for s in model['selectors']}, {'40mmgl', '40mmsg', '40mmsmk'})
            for selector in model['selectors']:
                row = rows[selector['configurationPath']]
                self.assertEqual(selector['asset'], row['asset'])
                self.assertEqual(selector['configurationGuid'], row['source']['instance_guid'])
                self.assertEqual(selector['configurationSha256'], row['source']['sha256'])
                keys.append((selector['configurationPath'], selector['mesh'], selector['rigFingerprint']))
            self.assertEqual(measured['coordinates'], underbarrel.coordinate_tuple(source['chain']))
        self.assertEqual(len(keys), len(set(keys)))
        enrollment = underbarrel.fixed.load(ROOT / 'profiles/weapons54-underbarrel/enrollment.json')
        self.assertEqual({key[0] for key in keys}, {row['configuration'] for row in enrollment['configurations']
                         if row['asset'] in {'40mmgl', '40mmsg', '40mmsmk'}})

    def test_an94_fixed_muzzle_has_only_insertion_and_no_fake_opening(self):
        source, measured, model = self.family('an94')
        self.assertEqual(model['interaction']['kind'], 'SingleRoundMuzzle')
        self.assertEqual(set(model['interaction']['geometry']), {'Insert'})
        self.assertEqual(set(model['grasps']), {'Insert'})
        self.assertEqual({p['bone'] for p in model['parts']}, {'jntWpnwpnJnt_16', 'jntWpn_17'})
        self.assertTrue(all(p['supplyPart'] and not p['coupledOpening'] for p in model['parts']))
        self.assertEqual(measured['mechanism']['closed'], measured['mechanism']['open'])
        self.assertEqual(measured['bore']['inward_part'], [0, 0, 1])
        self.assertEqual(model['grasps']['Insert']['sample'], 30)
        self.assertEqual(measured['supply']['seat_sample'], 'HandsIKPose')
        self.assertEqual(model['interaction']['geometry']['Insert']['motion']['placement']['weaponFromDestination'],
                         measured['supply']['destination'])
        self.assertFalse(source['policy']['delete_live_ammunition'])

    def test_an94_rejects_fabricated_motion_or_missing_exact_archive(self):
        for change in ('moving_tube', 'invented_grasp', 'breech_kind', 'missing_variant'):
            source, measured, unused = self.family('an94')
            if change == 'moving_tube':
                measured['mechanism']['open'][14] += .1
            elif change == 'invented_grasp':
                measured['mechanism']['grasp'] = measured['supply']['grasp']
            elif change == 'breech_kind':
                source['policy']['kind'] = 'SingleRoundBreech'
            else:
                source['variants'].pop()
            with self.subTest(change=change), self.assertRaises(ValueError):
                underbarrel.model_profile(source, measured)

    def test_m16_measured_slider_reverses_with_same_captured_grip(self):
        source, measured, model = self.family('m16')
        self.assertEqual(model['interaction']['kind'], 'SingleRoundBreech')
        self.assertEqual(set(model['interaction']['geometry']), {'Open', 'Insert', 'Close'})
        self.assertEqual(model['grasps']['Open'], model['grasps']['Close'])
        self.assertEqual(model['grasps']['Open']['sample'], 5)
        for step, end in (('Open', 'open'), ('Close', 'closed')):
            motion = model['interaction']['geometry'][step]['motion']
            self.assertEqual(motion['kind'], 'Linear')
            self.assertAlmostEqual(motion['travel'], .09511144, places=8)
            actual = underbarrel.mag.multiply(underbarrel.fixed.along(
                motion['motionFromContact'], 'Linear', motion['travel']), motion['weaponFromMotion'])
            expected = underbarrel.mag.multiply(model['grasps'][step]['part_from_wrist'], measured['mechanism'][end])
            self.assert_matrix_close(actual, expected)
        self.assertEqual(model['grasps']['Insert']['sample'], 25)
        self.assertEqual(measured['supply']['seat_sample'], 45)
        self.assertFalse(measured['bore']['clearance_limit']['mesh_clearance_proved'])
        self.assertFalse(source['policy']['rail_enforced'])

    def test_new_families_preserve_rigid_16_17_supply_and_same_frame_handshape(self):
        for name in ('an94', 'm16'):
            source, measured, model = self.family(name)
            parts = {p['bone']: p for p in model['parts']}
            root, child = parts['jntWpnwpnJnt_16'], parts['jntWpn_17']
            self.assertEqual((root['bone_index'], child['bone_index']), (39, 99))
            for key in ('weaponFromClosed', 'weaponFromOpen'):
                self.assert_matrix_close(underbarrel.mag.multiply(child['supplyFromPart'], root[key]), child[key])
            for key in ('weaponFromStepStart', 'weaponFromStepEnd'):
                for start, end in zip(root[key], child[key]):
                    self.assert_matrix_close(underbarrel.mag.multiply(child['supplyFromPart'], start), end)
            grasp = model['grasps']['Insert']
            for bone, matrix in grasp['wrist_from_fingers'].items():
                self.assert_matrix_close(underbarrel.mag.multiply(matrix, grasp['part_from_wrist']),
                                         measured['supply']['grasp']['fingers_to_part'][bone])
            geometry = model['interaction']['geometry']['Insert']
            self.assertEqual(geometry['contact']['radiusMeters'], .035)
            self.assertEqual(geometry['motion']['kind'], 'FreePlacement')

    def test_new_families_reject_crossed_coordinates_and_partial_handshapes(self):
        for name in ('an94', 'm16'):
            for change in ('wrong_mesh', 'missing_finger', 'wrong_seat', 'missing_child'):
                source, measured, unused = self.family(name)
                if change == 'wrong_mesh':
                    source['variants'][0]['mesh_sha256'] = '0' * 64
                    self.repin(source)
                elif change == 'missing_finger':
                    del measured['supply']['grasp']['fingers_to_part']['LeftHandThumb3']
                elif change == 'wrong_seat':
                    measured['supply']['destination'][12] += .001
                else:
                    del measured['supply']['part_in_root']['jntWpn_17']
                with self.subTest(name=name, change=change), self.assertRaises(ValueError):
                    underbarrel.model_profile(source, measured)

    def test_original_forty_model_rows_and_transforms_are_preserved(self):
        models = {model['label']: model for unused, measured, model in
                  (self.family(name) for name in ('xm8', 'an94', 'm16'))}
        self.assertEqual(underbarrel.digest(models),
                         'dae297e0cf97ecb99c8effeb48a51ee9d6d38090b4031cee25fef09c3e4daa32')

    def test_aek_fixed_muzzle_uses_distinct_12_13_geometry(self):
        source, measured, model = self.family('aek')
        self.assertEqual(model['interaction']['kind'], 'SingleRoundMuzzle')
        self.assertEqual(set(model['interaction']['geometry']), {'Insert'})
        self.assertEqual(set(model['grasps']), {'Insert'})
        self.assertEqual(measured['mechanism']['bone'], 'jntWpn_1')
        self.assertEqual(measured['mechanism']['closed'], measured['mechanism']['open'])
        self.assertEqual([(p['bone'], p['bone_index']) for p in model['parts']],
                         [('jntWpn_12', 38), ('jntWpn_13', 100)])
        self.assertEqual([p['visible_triangles'] for p in measured['parts'].values()], [140, 200])
        self.assertTrue(all(p['supplyPart'] and not p['coupledOpening'] for p in model['parts']))
        self.assertEqual(measured['bore']['inward_part'], [0, 0, 1])
        self.assertEqual(model['grasps']['Insert']['sample'], 30)
        self.assertEqual(measured['supply']['seat_sample'], 'HandsIKPose')
        self.assertEqual(measured['supply']['part_in_root']['jntWpn_13'][14], -.04886011)
        self.assertEqual(measured['supply']['destination'][14], -.69611317)

    def test_report73_sliders_keep_distinct_bones_samples_and_endpoint_travel(self):
        for name, bone, index, supply_sample, seat_sample, travel in (
                ('hk416', 'jntWpn_9', 34, 30, 57, .10615205),
                ('f2000', 'jntWpn_5', 42, 24, 'HandsIKPose', .11232941),
                ('aug', 'jntWpn_12', 38, 27, 47, .10690527)):
            source, measured, model = self.family(name)
            with self.subTest(name=name):
                self.assertEqual(measured['mechanism']['bone'], bone)
                self.assertEqual(source['skeleton_bones'][bone]['index'], index)
                self.assertEqual(model['interaction']['kind'], 'SingleRoundBreech')
                self.assertEqual(set(model['interaction']['geometry']), {'Open', 'Insert', 'Close'})
                self.assertEqual(model['grasps']['Open'], model['grasps']['Close'])
                self.assertEqual(model['grasps']['Open']['sample'], 5)
                self.assertEqual(model['grasps']['Insert']['sample'], supply_sample)
                self.assertEqual(measured['supply']['seat_sample'], seat_sample)
                self.assertEqual(measured['mechanism']['closed_sample'], 0)
                for step, end in (('Open', 'open'), ('Close', 'closed')):
                    motion = model['interaction']['geometry'][step]['motion']
                    self.assertEqual(motion['kind'], 'Linear')
                    self.assertAlmostEqual(motion['travel'], travel, places=8)
                    actual = underbarrel.mag.multiply(underbarrel.fixed.along(
                        motion['motionFromContact'], 'Linear', motion['travel']), motion['weaponFromMotion'])
                    expected = underbarrel.mag.multiply(model['grasps'][step]['part_from_wrist'],
                                                        measured['mechanism'][end])
                    self.assert_matrix_close(actual, expected)

    def test_report73_supply_bone_joins_and_same_frame_handshapes(self):
        for name in underbarrel.REPORT73_FAMILIES:
            source, measured, model = self.family(name)
            parts = {part['bone']: part for part in model['parts']}
            root_name, child_name = measured['supply']['parts']
            root, child = parts[root_name], parts[child_name]
            self.assertEqual((root['bone_index'], child['bone_index']),
                             (100, 114) if name == 'aug' else (38, 100))
            for key in ('weaponFromStepStart', 'weaponFromStepEnd'):
                for root_frame, child_frame in zip(root[key], child[key]):
                    self.assert_matrix_close(underbarrel.mag.multiply(child['supplyFromPart'], root_frame), child_frame)
            for step, grasp in model['grasps'].items():
                section = 'supply' if step == 'Insert' else 'mechanism'
                self.assertEqual(len(grasp['wrist_from_fingers']), 15)
                for bone, matrix in grasp['wrist_from_fingers'].items():
                    self.assert_matrix_close(underbarrel.mag.multiply(matrix, grasp['part_from_wrist']),
                                             measured[section]['grasp']['fingers_to_part'][bone])
                self.assertEqual(model['interaction']['geometry'][step]['contact']['radiusMeters'], .035)
            self.assertEqual(model['interaction']['geometry']['Insert']['motion']['kind'], 'FreePlacement')
            self.assertFalse(source['policy']['rail_enforced'])
            self.assertFalse(source['policy']['delete_live_ammunition'])
            self.assertTrue(source['policy']['spent_case_visual_removal_only'])

    def test_aug_additional_render_part_preserves_independent_endpoints(self):
        source, measured, model = self.family('aug')
        extra = measured['mechanism']['render_parts']['jntWpn_9']
        part = next(p for p in model['parts'] if p['bone'] == 'jntWpn_9')
        self.assertEqual(part['bone_index'], 34)
        self.assertFalse(part['supplyPart'])
        self.assertTrue(part['coupledOpening'])
        self.assertFalse(extra['semantic_role_proved'])
        self.assertEqual(set(model['grasps']), {'Open', 'Insert', 'Close'})
        self.assertEqual(part['weaponFromClosed'], extra['closed'])
        self.assertEqual(part['weaponFromOpen'], extra['open'])
        self.assertEqual(part['weaponFromStepStart'], [extra['closed'], extra['open'], extra['open'], extra['open']])
        self.assertEqual(part['weaponFromStepEnd'], [extra['open'], extra['open'], extra['open'], extra['closed']])
        self.assertAlmostEqual(extra['closed'][14] - extra['open'][14], .10615192, places=8)
        barrel_delta = underbarrel.mag.multiply(underbarrel.mag.inverse_rigid(measured['mechanism']['closed']),
                                               measured['mechanism']['open'])
        copied = underbarrel.mag.multiply(extra['closed'], barrel_delta)
        self.assertGreater(underbarrel.mag.norm(underbarrel.mag.sub(copied[12:15], extra['open'][12:15])), .0007)

    def test_report73_measurements_are_pinned_to_their_exact_source(self):
        coordinates = []
        for name in underbarrel.REPORT73_FAMILIES:
            source, measured, unused = self.family(name)
            self.assertEqual(source['measurements_sha256'], underbarrel.digest(measured))
            coordinates.append(tuple(source['chain'][key] for key in ('clip_sha256', 'mesh_sha256', 'lod_sha256')))
            measured['supply']['source'][12] += .001
            with self.subTest(name=name), self.assertRaisesRegex(ValueError, 'geometry pin'):
                underbarrel.model_profile(source, measured)
        self.assertEqual(len(set(coordinates)), 4)

    def test_report73_rejects_crossed_bones_even_after_measurement_repin(self):
        for name in underbarrel.REPORT73_FAMILIES:
            for change in ('bone_index', 'parent', 'missing_finger', 'seat', 'ammunition_part'):
                source, measured, unused = self.family(name)
                root = measured['supply']['root']
                if change == 'bone_index':
                    measured['parts'][root]['bone_index'] = 34
                elif change == 'parent':
                    measured['parts'][root]['parent'] = 'LeftHand'
                elif change == 'missing_finger':
                    del measured['supply']['grasp']['fingers_to_part']['LeftHandThumb3']
                elif change == 'seat':
                    measured['supply']['destination'][12] += .001
                else:
                    measured['supply']['parts'].append(measured['mechanism']['bone'])
                source['measurements_sha256'] = underbarrel.digest(measured)
                with self.subTest(name=name, change=change), self.assertRaises(ValueError):
                    underbarrel.model_profile(source, measured)

    def test_report73_nonrepresentative_configuration_tuples_remain_exact(self):
        for name in underbarrel.REPORT73_FAMILIES:
            for change in ('tree', 'mesh', 'skeleton', 'archive_bytes', 'missing_variant'):
                source, measured, unused = self.family(name)
                row = source['configurations'][-1]
                state = row['states'][0]
                if change == 'tree':
                    state['tree']['sha256'] = '0' * 64
                elif change == 'mesh':
                    state['meshes'][0] = 'objects/weapons/handheld/wrong/wrong/00000000-0000-0000-0000-000000000000'
                elif change == 'skeleton':
                    state['roles']['handsikpose']['skeleton_reference'] += '/wrong'
                elif change == 'archive_bytes':
                    source['variants'][-1]['mesh_sha256'] = '0' * 64
                else:
                    source['variants'].pop()
                if change != 'missing_variant':
                    self.repin(source)
                with self.subTest(name=name, change=change), self.assertRaises(ValueError):
                    underbarrel.model_profile(source, measured)

    def test_aug_extra_part_requires_measured_render_only_slider(self):
        for change in ('missing_part', 'semantic_role', 'rotation', 'unbounded_travel'):
            source, measured, unused = self.family('aug')
            extra = measured['mechanism']['render_parts']['jntWpn_9']
            if change == 'missing_part':
                del measured['parts']['jntWpn_9']
            elif change == 'semantic_role':
                extra['semantic_role_proved'] = True
            elif change == 'rotation':
                extra['open'][0] = -1
            else:
                extra['open'][14] -= 2
            source['measurements_sha256'] = underbarrel.digest(measured)
            with self.subTest(change=change), self.assertRaises(ValueError):
                underbarrel.model_profile(source, measured)


if __name__ == '__main__':
    unittest.main()
