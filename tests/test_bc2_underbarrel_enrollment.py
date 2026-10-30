import copy
import json
from pathlib import Path
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
import bc2_underbarrel_enrollment as enrollment


class ExactEnrollment(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.folder = ROOT / 'profiles/weapons54-underbarrel'
        cls.source = json.loads((cls.folder / 'enrollment.json').read_text())
        cls.measurements = [json.loads(p.read_text()) for p in cls.folder.glob('sight-*.json') if p.name != 'sight-coverage.json']
        cls.variants = json.loads((cls.folder / 'variant-coordinates.json').read_text())

    def test_reproduce_complete_source_headers(self):
        header, pairs = enrollment.family_header(self.source)
        self.assertEqual(header, (self.folder / 'WeaponModeFamilies.generated.h').read_text())
        self.assertEqual(len(pairs), 127)
        sight, coverage = enrollment.sight_header(self.source, self.measurements, self.variants)
        self.assertEqual(sight, (self.folder / 'SightAdapters.generated.h').read_text())
        self.assertEqual(len(coverage), len(pairs))
        self.assertNotIn('measurement_or_assembly_review_required', {r['status'] for r in coverage})
        self.assertTrue(any(p['primary_asset'] == 'AN94_sp_s' for p in pairs))
        self.assertEqual({p['secondary_asset'] for p in pairs}, {'40mmgl', '40mmsg', '40mmsmk'})

    def pair_fixture(self):
        pair = next(p for p in self.source['pairs'] if p['primary_asset'] == 'AN94_sp_s' and p['secondary_asset'] == '40mmgl')
        rows = [copy.deepcopy(r) for r in self.source['configurations'] if r['configuration'] in (pair['primary'], pair['secondary'])]
        return dict(schema=enrollment.SCHEMA, configurations=rows)

    def test_shared_name_does_not_invent_a_pair(self):
        for mutation in ('deploy', 'mesh', 'skeleton', 'missing_role', 'no_persistence', 'extra_state'):
            source = self.pair_fixture()
            primary = next(r for r in source['configurations'] if r['weapon_class'] != 'wcUgl')
            secondary = next(r for r in source['configurations'] if r['weapon_class'] == 'wcUgl')
            if mutation == 'deploy': secondary['alt_deploy_id'] += 1
            if mutation == 'mesh': secondary['states'][0]['meshes'] = ['foreign/mesh/guid']
            if mutation == 'skeleton': secondary['states'][0]['roles']['handsikpose']['skeleton_reference'] = 'foreign/rig/guid'
            if mutation == 'missing_role': secondary['states'][0]['roles'].pop('1p_altdeploy')
            if mutation == 'no_persistence': primary['persistent_id'] = ''
            if mutation == 'extra_state': primary['states'].append(copy.deepcopy(primary['states'][0]))
            with self.subTest(mutation=mutation): self.assertEqual(enrollment.pair_configurations(source), [])

    def test_duplicate_configuration_rejected(self):
        source = self.pair_fixture(); source['configurations'].append(copy.deepcopy(source['configurations'][0]))
        with self.assertRaises(ValueError): enrollment.pair_configurations(source)

    def test_same_asset_metadata_requires_variant_clip_bytes(self):
        variants = copy.deepcopy(self.variants)
        changed = next(r for r in variants['rows'] if '/SP_rif_AN94_Scoped' in r['configuration'])
        changed['clip_hashes'][1] = '0' * 64
        variants['digest'] = enrollment.digest({k: v for k, v in variants.items() if k != 'digest'})
        _, coverage = enrollment.sight_header(self.source, self.measurements, variants)
        self.assertTrue(all(r['status'] == 'measurement_or_assembly_review_required' for r in coverage if r['primary'] == changed['configuration']))
        self.assertTrue(any(r['status'] != 'measurement_or_assembly_review_required' for r in coverage if r['primary'] != changed['configuration']))

    def test_changed_measurement_refused(self):
        measurements = copy.deepcopy(self.measurements); measurements[0]['mesh_sha256'] = '0' * 64
        with self.assertRaises(ValueError): enrollment.sight_header(self.source, measurements, self.variants)

    def test_an94_uses_measured_own_geometry(self):
        result = next(r for r in self.measurements if 'AN94' in r['primary'])
        self.assertEqual(len(result['candidates']), 1)
        sight = result['candidates'][0]
        self.assertEqual(sight['bone'], 'jntWpn_7')
        self.assertGreater(sight['mechanism']['settled_travel_rad'], 1.6)
        self.assertLess(sight['mechanism']['pivot_residual_m'], .002)
        self.assertGreater(sight['settled_pose_observation'][0]['samples'], 9)
        self.assertFalse(result['native_accepted'])


if __name__ == '__main__':
    unittest.main()
