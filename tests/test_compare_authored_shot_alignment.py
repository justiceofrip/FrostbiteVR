"""Report provenance checks; these do not establish native firing or aim math."""
import copy
import hashlib
import json
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
import compare_authored_shot_alignment as alignment


class JsonSource:
    def __init__(self, value):
        self.value = value

    def read_text(self):
        return json.dumps(self.value)

    def read_bytes(self):
        return self.read_text().encode()

    def __str__(self):
        return 'synthetic-provenance-fixture'


class ProvenanceTests(unittest.TestCase):
    def setUp(self):
        root = Path(__file__).resolve().parents[1]
        self.axes = JsonSource(json.loads((root / 'profiles/opening426/reviewed-model-axes.json').read_text()))
        self.bindings = JsonSource(json.loads((root / 'profiles/opening426/nambu-grip-bindings.json').read_text()))
        self.firing = JsonSource({'fields': {'InitialDirection': {'raw_xyzw': [0, 0, 1, 0]}}})
        axis = self.axes.value['profiles'][0]
        axis['review']['firing_fields_sha256'] = hashlib.sha256(self.firing.read_bytes()).hexdigest()
        binding = self.bindings.value['profiles'][0]
        identity = [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1]
        evidence = dict(configuration_path_verified=True, authored_binding=True, authored_axes=True,
            asset='Nambu100', weapon=100, weapon_data=200, persistence=0,
            configuration_path=binding['weapon']['resource'][:-4], mesh=axis['configured_mesh_path'],
            binding_digest=axis['binding_digest'], axis_evidence=alignment.digest(axis),
            rig_fingerprint=int(axis['rig_fingerprint'].split(':')[1], 16),
            model_forward=axis['model_forward'], model_up=axis['model_up'],
            reference_pose=[0, 0, 0, 0, 0, 0, 1], effective_aim=[0, 0, 0, 0, 0, 0, 1], body=identity)
        self.row = dict(phase=2, weapon_name='Nambu100', weapon=100, weapon_data=200, persistence=0,
            matrix_stage='native_pre_spread_composition', observed_candidate_unexpired=True,
            token_valid=True, observed_qpc=100, observed_current_generation=12,
            observed_current_deadline_qpc=200, native_shot_matrix=identity, output_matrix=identity,
            ms=1, shot_token=7, client_path=False, native_muzzle_supported=False, origin_written=False,
            tracked_shot_candidate=dict(generation=10, deadline_qpc=150, space=3, owner_generation=4,
                units_per_metre=1, tracked_weapon=identity, tracked_flash=identity, aim_evidence=evidence))

    def compare(self, row=None):
        native = JsonSource({'gameplay': {'fire_origin_observation': {'records': [row or self.row]}}})
        return alignment.compare(native, self.axes, self.bindings, self.firing, 'Nambu100')

    def test_unexpired_older_render_is_not_same_shot_or_projectile_proof(self):
        result = self.compare()
        self.assertEqual(result['observations'], 1)
        self.assertEqual(result['observation_boundary'], 'native_pre_spread_composition')
        for name in ('native_verified', 'same_shot_input_join_verified', 'physical_controller_alignment_verified',
                     'spread_measured', 'final_projectile_velocity_measured', 'projectile_impact_tested'):
            self.assertFalse(result[name], name)
        row = result['spaces'][0]['first_recorded_composition'][0]
        self.assertEqual(row['render_generation_gap'], 2)
        self.assertEqual(row['candidate_remaining_qpc_ticks'], 50)
        self.assertFalse(row['candidate_age_known'])
        self.assertIsNone(row['native_consumed_input_generation'])
        self.assertNotIn('barrel_to_controller_degrees', row)
        self.assertNotIn('shot_to_barrel_degrees', row)

    def test_stale_wrong_stage_or_reversed_generation_rejects(self):
        for field, value in (('observed_candidate_unexpired', False), ('observed_current_generation', 9),
                             ('observed_qpc', 150), ('observed_current_deadline_qpc', 100),
                             ('matrix_stage', 'projectile_velocity')):
            with self.subTest(field=field):
                row = copy.deepcopy(self.row)
                row[field] = value
                result = self.compare(row)
                self.assertEqual(result['observations'], 0)
                self.assertEqual(len(result['rejected']), 1)

    def test_current_equipment_must_match_render_candidate(self):
        row = copy.deepcopy(self.row)
        row['persistence'] = 1
        self.assertEqual(self.compare(row)['observations'], 0)

    def test_non_neutral_initial_direction_does_not_get_applied_twice(self):
        self.firing.value['fields']['InitialDirection']['raw_xyzw'] = [1, 0, 0, 0]
        self.axes.value['profiles'][0]['review']['firing_fields_sha256'] = hashlib.sha256(self.firing.read_bytes()).hexdigest()
        with self.assertRaisesRegex(ValueError, 'neutral InitialDirection'):
            self.compare()


if __name__ == '__main__':
    unittest.main()
