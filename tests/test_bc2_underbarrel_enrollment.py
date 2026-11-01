import copy
import hashlib
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

    def test_ammo_header_exact_enrolled_coverage(self):
        header = (self.folder / 'AmmoConfigs.generated.h').read_text()
        rows = [r for r in self.source['configurations'] if r['weapon_class'] == 'wcUgl']
        self.assertEqual(len(rows), 80)
        self.assertEqual([sum(r['asset'] == name for r in rows) for name in ('40mmgl', '40mmsg', '40mmsmk')], [32, 24, 24])
        for row in rows:
            self.assertEqual(header.count(' {' + json.dumps(row['asset']) + ',' + json.dumps(row['configuration']) + ','), 1)
        self.assertEqual(header.count('ReloadDescriptorAdmission::ReviewedNative'), 80)
        self.assertNotIn('MagazineNativeProfile', header)
        self.assertNotIn('MagazineNativeRegistration', header)

    def test_reproduce_ammo_header_from_pinned_catalogs(self):
        paths = [Path(p['path']) for p in self.source['catalog_sources']]
        if not all(p.is_file() for p in paths):
            self.skipTest('Retained offline typed catalogs are unavailable on this machine')
        header, rows = enrollment.ammo_header(self.source, [p.read_bytes() for p in paths])
        self.assertEqual(header, (self.folder / 'AmmoConfigs.generated.h').read_text())
        self.assertEqual(len(rows), 80)
        self.assertTrue(all(not r['runtime_enabled'] and not r['registry_change'] for r in rows))

    def ammo_fixture(self):
        # A self-contained typed-source fixture exercises joins without requiring
        # the external retained catalogs on every deterministic-test host.
        row = copy.deepcopy(next(r for r in self.source['configurations'] if r['weapon_class'] == 'wcUgl'))
        fields = {
            'FireLogic.FireLogicType': 'fltSingleFire', 'FireLogic.ReloadType': 'rtMagazine',
            'FireLogic.FireInputAction': 'EiaFire', 'FireLogic.ReloadInputAction': 'EiaReload',
            'FireLogic.ReloadLogic': 'rlWeaponSwitchCancelsUnfinishedReload',
            'Ammo.MagazineCapacity': 1, 'Ammo.NumberOfMagazines': 2,
            'FireLogic.ReloadDelay': 0., 'FireLogic.ReloadTime': 2.45, 'FireLogic.ReloadThreshold': .55,
            'FireLogic.PostReloadSequenceTime': 0., 'FireLogic.BoltAction.BoltActionDelay': .5,
            'FireLogic.BoltAction.BoltActionTime': 1.,
            'FireLogic.BoltAction.HoldBoltActionUntilFireRelease': True,
            'FireLogic.BoltAction.HoldBoltActionUntilZoomRelease': False,
        }
        typed = dict(native_name=row['asset'], weapon_class='wcUgl', resource=row['source']['resource'],
                     resource_sha256=row['source']['sha256'], instance_guid=row['source']['instance_guid'],
                     firing_resource='Objects/Fixture/Firing.dbx', firing_sha256='1' * 64,
                     firing_guid='11111111-1111-1111-1111-111111111111',
                     function_resource='Objects/Fixture/Firing.dbx', function_sha256='1' * 64,
                     function_guid='22222222-2222-2222-2222-222222222222',
                     missing_fields=[], fields={k: dict(value=v) for k, v in fields.items()})
        catalog = dict(schema='fvr.bc2.authored_weapon_configuration', schema_version=1,
                       archive='fixture.fbrb', index_sha256='2' * 64, parse_errors=[], resolved_weapons=[typed])
        source = dict(schema=enrollment.SCHEMA, configurations=[row], catalog_sources=[])
        return source, catalog

    def pin_ammo_catalog(self, source, catalog):
        raw = json.dumps(catalog, sort_keys=True).encode()
        source['catalog_sources'] = [dict(sha256=hashlib.sha256(raw).hexdigest(),
                                         archive=catalog['archive'], index_sha256=catalog['index_sha256'])]
        return raw

    def test_ammo_read_review_retains_authored_values_without_manual_admission(self):
        source, catalog = self.ammo_fixture()
        header, rows = enrollment.ammo_header(source, [self.pin_ammo_catalog(source, catalog)])
        self.assertEqual(len(rows), 1)
        row = rows[0]
        self.assertEqual(row['configuration']['admission'], 'Candidate')
        self.assertFalse(row['runtime_enabled'])
        self.assertFalse(row['native_runtime_admission'])
        self.assertFalse(row['registry_change'])
        self.assertEqual(row['configuration']['values']['baseCapacity'], 1)
        self.assertEqual(row['configuration']['values']['numberOfMagazines'], 2)
        self.assertEqual([w['offset'] for w in row['configuration']['timing']], [0x10, 0x14, 0x18, 0x20, 0x24, 0x2c])
        self.assertIn('ReloadDescriptorAdmission::ReviewedNative', header)

    def test_ammo_rejects_changed_catalog_bytes(self):
        source, catalog = self.ammo_fixture()
        raw = self.pin_ammo_catalog(source, catalog)
        with self.assertRaisesRegex(ValueError, 'Pinned catalog content changed'):
            enrollment.ammo_header(source, [raw + b' '])

    def test_ammo_rejects_inexact_or_ambiguous_source_joins(self):
        for mutation in ('name', 'path', 'hash', 'guid', 'class', 'source_type', 'missing', 'duplicate', 'fields', 'archive'):
            source, catalog = self.ammo_fixture()
            row = catalog['resolved_weapons'][0]
            if mutation == 'name': row['native_name'] = '40mmsg'
            if mutation == 'path': row['resource'] = 'Objects/Unknown.dbx'
            if mutation == 'hash': row['resource_sha256'] = '0' * 64
            if mutation == 'guid': row['instance_guid'] = '00000000-0000-0000-0000-000000000000'
            if mutation == 'class': row['weapon_class'] = 'wcAssault'
            if mutation == 'source_type': source['configurations'][0]['source']['type'] = 'OtherType'
            if mutation == 'missing': catalog['resolved_weapons'] = []
            if mutation == 'duplicate': catalog['resolved_weapons'].append(copy.deepcopy(row))
            if mutation == 'fields': row['missing_fields'] = ['Ammo.MagazineCapacity']
            raw = self.pin_ammo_catalog(source, catalog)
            if mutation == 'archive': source['catalog_sources'][0]['index_sha256'] = '0' * 64
            with self.subTest(mutation=mutation), self.assertRaises(ValueError):
                enrollment.ammo_header(source, [raw])

    def test_ammo_rejects_unreviewed_typed_values(self):
        _, fixture = self.ammo_fixture()
        for key, field in fixture['resolved_weapons'][0]['fields'].items():
            source, catalog = self.ammo_fixture()
            old = field['value']
            value = not old if type(old) is bool else old + 1 if type(old) in (float, int) else 'Unknown'
            catalog['resolved_weapons'][0]['fields'][key]['value'] = value
            raw = self.pin_ammo_catalog(source, catalog)
            with self.subTest(field=key), self.assertRaises(ValueError):
                enrollment.ammo_header(source, [raw])

    def test_ammo_rejects_conflicting_firing_source_chain(self):
        source, catalog = self.ammo_fixture()
        first = self.pin_ammo_catalog(source, catalog)
        changed = copy.deepcopy(catalog)
        changed['archive'] = 'other-fixture.fbrb'
        changed['resolved_weapons'][0]['firing_sha256'] = '3' * 64
        second = json.dumps(changed, sort_keys=True).encode()
        source['catalog_sources'].append(dict(sha256=hashlib.sha256(second).hexdigest(),
                                              archive=changed['archive'], index_sha256=changed['index_sha256']))
        with self.assertRaisesRegex(ValueError, 'Conflicting exact underbarrel source chain'):
            enrollment.ammo_header(source, [first, second])

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
