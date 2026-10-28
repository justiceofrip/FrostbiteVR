"""Offline bench selection/provenance regressions; no native process access."""
import copy
import contextlib
import io
import json
from pathlib import Path
import unittest
from unittest.mock import patch

import bc2_campaign_bench as bench
import bc2_test_range_catalog as catalog

ROOT = Path(__file__).resolve().parents[1]
SP = 'game:levels/sp_common/level-00.fbrb'
MAP = 'game:levels/sp_02/level-00.fbrb'
RESOURCE = 'objects/weapons/handheld/test/test.dbx'
GUID = '11111111-1111-1111-1111-111111111111'


def fixture():
    fields = {key: {'value': 1} for key in (
        'Ammo.MagazineCapacity', 'FireLogic.FireLogicType', 'FireLogic.ReloadType',
        'FireLogic.BoltAction.BoltActionDelay', 'FireLogic.BoltAction.BoltActionTime',
        'FireLogic.BoltAction.HoldBoltActionUntilFireRelease',
        'FireLogic.BoltAction.HoldBoltActionUntilZoomRelease')}
    report = {'schema': 'fvr.bc2.authored_weapon_configuration', 'schema_version': 1,
              'archive': 'Dist/win32/levels/sp_common/level-00.fbrb', 'index_sha256': 'a'*64,
              'resolved_weapons': [{'resource': RESOURCE, 'instance_guid': GUID,
                                   'resource_sha256': 'b'*64, 'native_name': 'test',
                                   'weapon_class': 'wcSmg', 'fields': fields}]}
    source = catalog.plan([report], {MAP: [SP]}, {},
                          {SP: {'status': 'indexed', 'index_sha256': 'a'*64, 'entries': {RESOURCE: 65536}}}, maps=['sp_02'])
    config = {'schema': bench.CONFIG, 'limits': {'loadout_items': 2, 'body_cache_bytes': 16777216,
                                               'body_cache_records': 64},
              'mechanisms': {'magazine': ['remove', 'return', 'fire']}, 'limitations': []}
    enrollment = {'schema': 'fvr.bc2.exact-resource-visibility-body-enrollment.v1',
                  'family': bench.FAMILY, 'profiles': [{
                      'identity': {'weapon': {'resource': RESOURCE, 'guid': GUID, 'sha256': 'b'*64},
                                   'assetPath': RESOURCE[:-4]},
                      'native_id': '1234567890123456', 'descriptor_digest': 'c'*64,
                      'configured_meshes': ['mesh'], 'body_part': 'part'}]}
    p = enrollment['profiles'][0]
    p.update(magazine_part='magazine', rig_fingerprint='a7f219a1426216ab',
             configuration={'assetPath': RESOURCE[:-4]})
    p['key'] = 'bc2:magazine-descriptor:' + bench.digest(p['identity'])
    p['descriptor_digest'] = bench.digest(p['configuration'])
    return source, config, enrollment, report


def native_proof(source, runtime='d'*64):
    row = source['definitions'][0]
    return {'schema': 'fvr.bc2.campaign_bench.acceptance.v1', 'independent_native_review': True,
            'accepted': True, 'accepted_features': ['magazine', 'support'],
            'runtime': {'sha256': runtime}, 'configuration': {
                k: row[k] for k in ('resource', 'instance_guid', 'resource_sha256')}}


class BenchTests(unittest.TestCase):
    def test_default_is_metadata_only_and_never_enables_runtime(self):
        source, config, enrollment, _ = fixture()
        p = bench.plan(source, [enrollment], config, 'sp_02')
        self.assertTrue(p['planning_only'])
        self.assertFalse(p['process_opened'] or p['native_calls'] or p['runtime_admission_granted'])
        self.assertEqual(p['rows'][0]['coverage'], 'configured_metadata')
        self.assertFalse(p['rows'][0]['native_tested'] or p['rows'][0]['runtime_enabled'])
        self.assertEqual(p['loadout'], [])

    def test_name_and_native_class_do_not_grant_magazine_support(self):
        source, config, _, _ = fixture()
        p = bench.plan(source, [], config, 'sp_02', [RESOURCE])
        self.assertEqual(p['rows'][0]['coverage'], 'unsupported_by_supplied_enrollments')
        self.assertIsNone(p['rows'][0]['mechanism'])

    def test_exact_resource_guid_and_hash_are_all_required(self):
        source, config, enrollment, _ = fixture()
        for field, value in [('resource', RESOURCE.replace('test.dbx', 'other.dbx')),
                             ('guid', '22222222-2222-2222-2222-222222222222'), ('sha256', 'c'*64)]:
            changed = copy.deepcopy(enrollment); changed['profiles'][0]['identity']['weapon'][field] = value
            p = changed['profiles'][0]
            if field == 'resource':
                p['identity']['assetPath'] = value[:-4]
                p['configuration']['assetPath'] = value[:-4]
                p['descriptor_digest'] = bench.digest(p['configuration'])
            p['key'] = 'bc2:magazine-descriptor:' + bench.digest(p['identity'])
            self.assertEqual(bench.plan(source, [changed], config, 'sp_02')['rows'][0]['coverage'],
                             'unsupported_by_supplied_enrollments')

    def test_conflicting_enrollments_and_forged_catalog_identity_reject(self):
        source, config, enrollment, _ = fixture()
        changed = copy.deepcopy(enrollment); changed['profiles'][0]['body_part'] = 'different'
        with self.assertRaisesRegex(ValueError, 'Conflicting'):
            bench.plan(source, [enrollment, changed], config, 'sp_02')
        source['definitions'][0]['resource_sha256'] = 'c'*64
        with self.assertRaisesRegex(ValueError, 'identity digest'):
            bench.plan(source, [], config, 'sp_02')

    def test_overlay_ambiguity_is_retained_not_promoted(self):
        source, config, enrollment, _ = fixture()
        m = source['maps'][0]
        m['ambiguous_definition_candidates'] = [{'definition': key, 'matched_origins': [SP],
            'unresolved_resource_owners': [], 'deletion_entries': ['game:levels/sp_02/level-00.fbrb']}
            for key in m['mounted_definition_candidates']]
        m['mounted_definition_candidates'] = []
        p = bench.plan(source, [enrollment], config, 'sp_02', [RESOURCE])
        self.assertEqual(p['rows'][0]['availability'], 'ambiguous_overlay')
        self.assertEqual(p['rows'][0]['availability_evidence']['deletion_entries'], ['game:levels/sp_02/level-00.fbrb'])
        self.assertFalse(p['runtime_admission_granted'])

    def test_availability_requires_complete_disjoint_partition(self):
        source, config, _, _ = fixture()
        source['maps'][0]['uncovered_definitions'] = source['maps'][0]['mounted_definition_candidates']
        with self.assertRaisesRegex(ValueError, 'overlapping'):
            bench.plan(source, [], config, 'sp_02')
        source['maps'][0]['uncovered_definitions'] = []; source['maps'][0]['mounted_definition_candidates'] = []
        with self.assertRaisesRegex(ValueError, 'partition'):
            bench.plan(source, [], config, 'sp_02')

    def test_selector_reuses_all_exact_typed_fields_without_native_admission(self):
        source, config, enrollment, report = fixture()
        p = bench.plan(source, [enrollment], config, 'sp_02', [RESOURCE], bench.typed_rows([report]))
        selector = p['selector_document']['selectors'][0]
        self.assertEqual(selector['configuration']['fields'], report['resolved_weapons'][0]['fields'])
        self.assertEqual(selector['configuration']['resource_sha256'], 'b'*64)
        self.assertEqual(selector['path'], RESOURCE[:-4])
        self.assertFalse(p['runtime_admission_granted'])

    def test_missing_typed_fields_remain_explicit(self):
        source, config, enrollment, report = fixture()
        del report['resolved_weapons'][0]['fields']['Ammo.MagazineCapacity']
        p = bench.plan(source, [enrollment], config, 'sp_02', [RESOURCE], bench.typed_rows([report]))
        self.assertEqual(p['selector_document']['selectors'], [])
        self.assertEqual(p['selector_gaps'][0]['missing_fields'], ['Ammo.MagazineCapacity'])

    def test_unknown_or_duplicate_selection_and_map_reject(self):
        source, config, _, _ = fixture()
        for selections in ([RESOURCE, RESOURCE], ['not-a-resource']):
            with self.assertRaises(ValueError):
                bench.plan(source, [], config, 'sp_02', selections)
        with self.assertRaises(ValueError):
            bench.plan(source, [], config, 'sp_99')

    def test_mechanism_metadata_does_not_grant_native_acceptance(self):
        source, config, _, _ = fixture()
        config['known_configurations'] = [{**native_proof(source)['configuration'], 'mechanism': 'magazine'}]
        p = bench.plan(source, [], config, 'sp_02')
        self.assertEqual(p['rows'][0]['mechanism'], 'magazine')
        self.assertFalse(p['rows'][0]['native_tested'])

    def test_visibility_body_enrollment_alone_is_not_a_magazine_route(self):
        source, config, enrollment, report = fixture()
        del enrollment['profiles'][0]['magazine_part']
        p = bench.plan(source, [enrollment], config, 'sp_02', [RESOURCE], bench.typed_rows([report]))
        row = p['rows'][0]
        self.assertEqual(row['coverage'], 'configured_metadata')
        self.assertIsNone(row['mechanism'])
        self.assertFalse(row['manual_magazine_metadata_eligible'])
        self.assertIn('reload', row['metadata_gaps'])
        self.assertFalse(p['selector_document']['selectors'][0]['measured_family_geometry'])

    def test_magazine_descriptor_must_match_its_identity_and_configuration(self):
        source, config, enrollment, _ = fixture()
        for field, value in [('key', 'bc2:magazine-descriptor:' + '0'*64),
                             ('descriptor_digest', '0'*64)]:
            changed = copy.deepcopy(enrollment); changed['profiles'][0][field] = value
            with self.assertRaisesRegex(ValueError, 'Magazine descriptor'):
                bench.plan(source, [changed], config, 'sp_02')
        changed = copy.deepcopy(enrollment)
        changed['profiles'][0]['configuration']['assetPath'] = 'other/configuration'
        changed['profiles'][0]['descriptor_digest'] = bench.digest(changed['profiles'][0]['configuration'])
        with self.assertRaisesRegex(ValueError, 'Magazine descriptor'):
            bench.plan(source, [changed], config, 'sp_02')

    def test_unrecognized_reload_family_does_not_inherit_magazine_recipe(self):
        source, config, enrollment, _ = fixture()
        enrollment['family'] = 'future-belt-fed'
        row = bench.plan(source, [enrollment], config, 'sp_02')['rows'][0]
        self.assertIsNone(row['mechanism'])
        self.assertFalse(row['manual_magazine_metadata_eligible'])

    def test_only_exact_independently_reviewed_same_runtime_evidence_is_current(self):
        source, config, enrollment, _ = fixture()
        proof = native_proof(source)
        p = bench.plan(source, [enrollment], config, 'sp_02', evidence=[proof], runtime_sha='d'*64)
        self.assertTrue(p['rows'][0]['native_tested'])
        self.assertEqual(p['rows'][0]['accepted_features'], ['magazine', 'support'])
        self.assertFalse(p['rows'][0]['runtime_enabled'])
        p = bench.plan(source, [enrollment], config, 'sp_02', evidence=[proof], runtime_sha='e'*64)
        self.assertFalse(p['rows'][0]['native_tested'])
        self.assertIsNotNone(p['rows'][0]['historical_native_evidence'])

    def test_provision_completion_cannot_masquerade_as_native_reload_acceptance(self):
        source, config, _, _ = fixture()
        for proof in ({'schema': 'fvr.bc2.campaign_bench.completion.v1', 'complete': True},
                      {**native_proof(source), 'independent_native_review': False},
                      {**native_proof(source), 'accepted_features': ['all_weapons']}):
            with self.assertRaises(ValueError):
                bench.plan(source, [], config, 'sp_02', evidence=[proof])

    def test_asset_budget_cannot_be_increased_by_plan_config(self):
        source, config, _, _ = fixture(); config['limits']['body_cache_bytes'] *= 10
        config['limits']['body_cache_records'] *= 10
        p = bench.plan(source, [], config, 'sp_02')
        self.assertEqual(p['asset_budget']['maximum_bytes'], 16777216)
        self.assertEqual(p['asset_budget']['maximum_records'], 64)

    def test_conflicting_typed_fields_reject(self):
        _, _, _, report = fixture(); changed = copy.deepcopy(report)
        changed['resolved_weapons'][0]['fields']['Ammo.MagazineCapacity']['value'] = 9
        with self.assertRaisesRegex(ValueError, 'Conflicting'):
            bench.typed_rows([report, changed])

    def test_repository_default_inputs_are_supported_and_keep_ww2_separate(self):
        config, _ = bench.read(ROOT / 'config/campaign-bench.json')
        documents = [bench.read(ROOT / p)[0] for p in config['enrollment_documents']]
        rows = bench.enrollment_rows(documents)
        self.assertEqual(len(rows), 16)
        self.assertTrue(any('beach/nambu100/' in key[0] for key in rows))
        self.assertFalse(any('jap_type5/' in key[0] or '/thompson/' in key[0] for key in rows))
        self.assertEqual(len(config['known_configurations']), 5)
        for row in config['known_configurations']:
            self.assertTrue((ROOT / row['source']).is_file())
            self.assertTrue((ROOT / row['historical_evidence']).is_file())

    def test_pin_change_and_existing_output_reject(self):
        file = ROOT / 'config/campaign-bench.json'
        _, pin = bench.read(file); pin['sha256'] = '0'*64
        with self.assertRaisesRegex(ValueError, 'changed'):
            bench.check_pin(pin)
        with self.assertRaises(ValueError):
            bench.write_new(file, {})

    def test_non_g_output_rejects_before_input_reads(self):
        with patch.object(bench, 'read', side_effect=AssertionError('No input reads')), contextlib.redirect_stderr(io.StringIO()):
            with self.assertRaises(SystemExit):
                bench.main(['--catalog', 'missing', '--map', 'sp_02', '--output', 'C:/never-write.json'])

    def test_bad_provision_receipt_rejects_before_binary_access(self):
        with patch.object(bench, 'read', return_value=({'schema': 'unknown'}, {})), \
             patch.object(bench, 'check_pin', side_effect=AssertionError('No binary access')):
            with self.assertRaisesRegex(ValueError, 'ABI'):
                bench.provision_binding('unreviewed.json', 'guessed.h')


class CorpusReadinessTests(unittest.TestCase):
    """Use the shipped exact data; no per-gun policy simulation or game calls."""
    @classmethod
    def setUpClass(cls):
        cls.config, _ = bench.read(ROOT / 'config/campaign-bench.json')
        cls.catalog, _ = bench.read(ROOT / 'reports/test-range-catalog-20261020.json')
        cls.enrollments = [bench.read(ROOT / p)[0] for p in cls.config['enrollment_documents']]
        cls.bodies = [bench.read(ROOT / p)[0] for p in cls.config['body_asset_documents']]
        cls.supports = [bench.read(ROOT / p)[0] for p in cls.config['support_binding_documents']]

    def make_plan(self, **kwargs):
        return bench.plan(self.catalog, self.enrollments, self.config, 'sp_01',
                          body_documents=kwargs.pop('body_documents', self.bodies),
                          support_documents=kwargs.pop('support_documents', self.supports), **kwargs)

    @staticmethod
    def row(plan, resource):
        matches = [r for r in plan['rows'] if r['resource'] == resource.lower()]
        if len(matches) != 1:
            raise AssertionError('Expected one exact corpus row: ' + resource)
        return matches[0]

    def test_actual_corpus_keeps_reload_body_and_support_as_independent_features(self):
        p = self.make_plan()
        self.assertEqual(len(p['rows']), 288)
        exact_rows = {(r['resource'], r['instance_guid'], r['resource_sha256']): r for r in p['rows']}
        enrolled = bench.enrollment_rows(self.enrollments)
        self.assertEqual(len(enrolled), 16)
        for key in enrolled:
            self.assertTrue(exact_rows[key]['manual_magazine_metadata_eligible'], key)
            self.assertEqual(exact_rows[key]['feature_readiness']['reload']['status'], 'configured_metadata')
        for known in self.config['known_configurations']:
            key = bench.exact(known['resource'], known['instance_guid'], known['resource_sha256'])
            self.assertEqual(exact_rows[key]['mechanism'], known['mechanism'])
        # Every one of the eighteen exact body records has display coverage;
        # their presence never assigns a mechanism to the remaining catalog.
        bodies, _ = bench.feature_rows(self.bodies)
        self.assertEqual(len(bodies), 18)
        for key in bodies:
            self.assertEqual(exact_rows[key]['feature_readiness']['body_display']['status'], 'configured_metadata', key)
        type5 = self.row(p, 'objects/weapons/handheld/beach/jap_type5/type5.dbx')
        self.assertEqual(type5['feature_readiness']['holster']['status'], 'configured_metadata')
        self.assertIsNone(type5['mechanism'])
        self.assertFalse(type5['manual_magazine_metadata_eligible'] or type5['combined_metadata_complete'])
        self.assertEqual(type5['metadata_gaps'], ['reload', 'support'])
        self.assertTrue(any(r['feature_readiness']['support']['status'] == 'configured_metadata'
                            and r['mechanism'] is None for r in p['rows']))
        self.assertFalse(any(r['native_tested'] or r['runtime_enabled'] or r['accepted_features'] for r in p['rows']))
        self.assertFalse(p['runtime_admission_granted'] or p['asset_budget']['cache_verified'])

    def test_missing_support_is_a_shared_readiness_gap_without_revoking_reload_metadata(self):
        resource = 'objects/weapons/handheld/beach/nambu100/nambu100.dbx'
        good = self.row(self.make_plan(), resource)
        self.assertTrue(good['combined_metadata_complete'])
        no_opening = [d for d in self.supports if not any(p['native_asset_name'] == 'Nambu100' for p in d['profiles'])]
        changed = self.row(self.make_plan(support_documents=no_opening), resource)
        self.assertEqual(changed['metadata_gaps'], ['support'])
        self.assertTrue(changed['manual_magazine_metadata_eligible'])
        self.assertFalse(changed['combined_metadata_complete'])
        self.assertEqual(good['feature_readiness']['reload'], changed['feature_readiness']['reload'])

    def test_support_never_borrows_same_name_mesh_from_another_configuration(self):
        resource = 'objects/weapons/handheld/beach/nambu100/nambu100.dbx'
        for field, value in [('resource', resource.replace('nambu100.dbx', 'other.dbx')),
                             ('instance_guid', GUID), ('sha256', '0'*64)]:
            changed = copy.deepcopy(self.supports)
            support = next(p for d in changed for p in d['profiles'] if p['native_asset_name'] == 'Nambu100')
            support['source_weapons'][0][field] = value
            support['binding_digest'] = bench.digest({k: v for k, v in support.items() if k != 'binding_digest'})
            row = self.row(self.make_plan(support_documents=changed), resource)
            self.assertEqual(row['feature_readiness']['support']['status'], 'missing_from_supplied_metadata')
            self.assertTrue(row['manual_magazine_metadata_eligible'])

    def test_exact_support_still_requires_consistent_mesh_rig_and_unique_binding(self):
        resource = 'objects/weapons/handheld/beach/nambu100/nambu100.dbx'
        for field, value in [('configured_mesh_path', 'other/mesh'), ('rig_fingerprint', 'fnv1a64:0000000000000001')]:
            changed = copy.deepcopy(self.supports)
            support = next(p for d in changed for p in d['profiles'] if p['native_asset_name'] == 'Nambu100')
            support[field] = value
            support['binding_digest'] = bench.digest({k: v for k, v in support.items() if k != 'binding_digest'})
            row = self.row(self.make_plan(support_documents=changed), resource)
            self.assertEqual(row['feature_readiness']['support']['status'], 'incompatible_metadata')
            self.assertFalse(row['combined_metadata_complete'])
        changed = copy.deepcopy(self.supports)
        doc = next(d for d in changed if any(p['native_asset_name'] == 'Nambu100' for p in d['profiles']))
        duplicate = copy.deepcopy(doc['profiles'][0]); duplicate['left_hand_in_weapon'][12] += 0.1
        duplicate['binding_digest'] = bench.digest({k: v for k, v in duplicate.items() if k != 'binding_digest'})
        doc['profiles'].append(duplicate)
        row = self.row(self.make_plan(support_documents=changed), resource)
        self.assertEqual(row['feature_readiness']['support']['status'], 'ambiguous_metadata')

    def test_support_pose_requires_consistent_binding_digest_and_valid_authored_matrix(self):
        changed = copy.deepcopy(self.supports)
        support = next(p for d in changed for p in d['profiles'] if p['native_asset_name'] == 'Nambu100')
        support['left_hand_in_weapon'][12] += 0.1
        with self.assertRaisesRegex(ValueError, 'Authored support reference metadata'):
            self.make_plan(support_documents=changed)
        support['left_hand_in_weapon'][0] = 99.0
        support['binding_digest'] = bench.digest({k: v for k, v in support.items() if k != 'binding_digest'})
        with self.assertRaisesRegex(ValueError, 'Nonrigid'):
            self.make_plan(support_documents=changed)

    def test_body_identity_does_not_borrow_by_native_name_or_make_ammo_resources(self):
        resource = 'objects/weapons/handheld/beach/jap_type5/type5.dbx'
        changed = copy.deepcopy(self.bodies)
        body = next(p for d in changed for p in d['profiles'] if p['asset'] == 'Garand')
        body['authored_configuration']['sha256'] = '0'*64
        row = self.row(self.make_plan(body_documents=changed), resource)
        self.assertEqual(row['feature_readiness']['body_display']['status'], 'missing_from_supplied_metadata')
        self.assertFalse(row['manual_magazine_metadata_eligible'])
        selected = self.make_plan(selections=[resource])
        self.assertEqual(selected['asset_budget']['selected_body_parts'], ['body_holstered_42f20447f5130950'])
        self.assertFalse(selected['asset_budget']['cache_verified'])

    def test_body_holster_anchor_requires_finite_rigid_affine_metadata(self):
        resource = 'objects/weapons/handheld/beach/jap_type5/type5.dbx'
        original = next(p for d in self.bodies for p in d['profiles'] if p['asset'] == 'Garand')
        malformed = [0.0]*16; malformed[15] = 1.0
        scaled = original['holster_from_weapon'].copy(); scaled[0:3] = [2*x for x in scaled[0:3]]
        nonfinite = original['holster_from_weapon'].copy(); nonfinite[12] = float('nan')
        nonaffine = original['holster_from_weapon'].copy(); nonaffine[3] = 1.0
        for anchor in (malformed, scaled, nonfinite, nonaffine, '0'*16, [0.0]*15):
            with self.subTest(anchor=repr(anchor)):
                changed = copy.deepcopy(self.bodies)
                body = next(p for d in changed for p in d['profiles'] if p['asset'] == 'Garand')
                body['holster_from_weapon'] = anchor
                with self.assertRaises(ValueError):
                    self.make_plan(body_documents=changed)
        for anchor in (None, []):
            changed = copy.deepcopy(self.bodies)
            body = next(p for d in changed for p in d['profiles'] if p['asset'] == 'Garand')
            body['holster_from_weapon'] = anchor
            row = self.row(self.make_plan(body_documents=changed), resource)
            self.assertEqual(row['feature_readiness']['body_display']['status'], 'configured_metadata')
            self.assertEqual(row['feature_readiness']['holster']['status'], 'missing_from_supplied_metadata')
            self.assertFalse(row['manual_magazine_metadata_eligible'])

    def test_cli_reads_and_pins_all_default_feature_documents_without_process_or_writes(self):
        with patch.object(bench, 'write_new') as output, contextlib.redirect_stdout(io.StringIO()):
            bench.main(['--catalog', str(ROOT / 'reports/test-range-catalog-20261020.json'),
                        '--map', 'sp_01', '--output', 'G:/Unity/VRMMO/bench-readiness-test-no-write.json'])
        self.assertEqual(output.call_count, 1)
        p = output.call_args.args[1]
        pins = {Path(i['path']).resolve() for i in p['inputs']}
        for relative in self.config['body_asset_documents'] + self.config['support_binding_documents']:
            self.assertIn((ROOT / relative).resolve(), pins)
        self.assertFalse(self.row(p, 'objects/weapons/handheld/beach/jap_type5/type5.dbx')['manual_magazine_metadata_eligible'])
        self.assertFalse(p['process_opened'] or p['native_calls'])


if __name__ == '__main__':
    unittest.main()
