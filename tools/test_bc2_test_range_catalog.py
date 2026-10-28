"""Conservative exact-definition and literal mount-graph planner regressions."""
import contextlib
import io
from pathlib import Path
import unittest
from unittest.mock import patch

import bc2_test_range_catalog as catalog


SP = 'game:levels/sp_common/level-00.fbrb'
MP = 'game:levels/mp_common/level-00.fbrb'
PATCH = 'package:levels/sp_02/level-00.fbrb'
MAP = 'game:levels/sp_02/level-00.fbrb'
RESOURCE = 'objects/weapons/handheld/pistol/pistol.dbx'
GUID = '11111111-1111-1111-1111-111111111111'


def report(owner=SP, content='a', index='b', name='MP443', native_class='wcSmg'):
    return {'schema': 'fvr.bc2.authored_weapon_configuration', 'schema_version': 1,
            'archive': catalog.bundle_archive(owner), 'index_sha256': index * 64,
            'parse_errors': [], 'unresolved_weapons': [],
            'resolved_weapons': [{'native_name': name, 'weapon_class': native_class,
                                 'resource': RESOURCE, 'instance_guid': GUID,
                                 'resource_sha256': content * 64,
                                 'fields': {'FireLogic.ReloadType': {'value': 'rtMagazine'}}}]}


def indexed(owner=SP, index='b', entries=None):
    return {owner: {'status': 'indexed', 'index_sha256': index * 64,
                    'entries': {RESOURCE: 65536} if entries is None else entries}}


def plan(documents=None, targets=None, indexes=None, **selection):
    return catalog.plan(documents or [report()], {MAP: targets if targets is not None else [SP]}, {},
                        indexes if indexes is not None else indexed(), **selection)


class CatalogTests(unittest.TestCase):
    def test_literal_mount_graph_has_no_implicit_recursive_or_async_mount(self):
        sections = catalog.parse_package(
            'Signature 0123\nAuthorative\nBundleSource ' + MAP + '\nBundleTarget ' + SP +
            '\nBundleSource ' + SP + '\nBundleTarget ' + MP +
            '\nBundleSource game:async/weapon/pistol-00.fbrb\nBundleTarget ' + MP + '\n')
        result = catalog.plan([report(MP)], sections, {}, indexed(MP), maps=['sp_02'])
        self.assertEqual(result['maps'][0]['targets'], [SP])
        self.assertEqual(len(result['maps'][0]['uncovered_definitions']), 1)
        self.assertEqual(result['maps'][0]['needed_origin_candidates'], [MP])

    def test_exact_mount_candidate_retains_native_class_without_pistol_inference(self):
        result = plan()
        row = result['definitions'][0]
        self.assertEqual(row['native_class'], 'wcSmg')
        self.assertNotIn('physical_family', row)
        self.assertFalse(row['live_native_verified'])
        self.assertEqual(result['maps'][0]['mounted_definition_candidates'], [row['id']])
        self.assertFalse(result['runtime_admission_granted'])

    def test_content_variants_remain_distinct_and_overlay_is_ambiguous(self):
        indexes = {**indexed(), **indexed(PATCH, 'd')}
        result = plan([report(), report(PATCH, 'c', 'd')], [SP, PATCH], indexes)
        self.assertEqual(len(result['definitions']), 2)
        self.assertEqual(len({r['id'] for r in result['definitions']}), 2)
        self.assertEqual(result['maps'][0]['mounted_definition_candidates'], [])
        self.assertEqual(len(result['maps'][0]['ambiguous_definition_candidates']), 2)

    def test_identical_configuration_in_multiple_origins_merges_safely(self):
        result = plan([report(), report(MP)], [SP, MP], {**indexed(), **indexed(MP)})
        self.assertEqual(len(result['definitions']), 1)
        self.assertEqual(len(result['definitions'][0]['origins']), 2)
        self.assertEqual(len(result['maps'][0]['mounted_definition_candidates']), 1)

    def test_unknown_overlay_tombstone_and_unreadable_target_never_admit(self):
        cases = [
            indexed(PATCH, 'e'),
            indexed(PATCH, 'e', {RESOURCE: 0}),
            {PATCH: {'status': 'missing'}},
        ]
        for additional in cases:
            with self.subTest(additional=additional):
                result = plan(targets=[SP, PATCH], indexes={**indexed(), **additional})
                self.assertEqual(result['maps'][0]['mounted_definition_candidates'], [])
                self.assertEqual(len(result['maps'][0]['ambiguous_definition_candidates']), 1)

    def test_stale_index_missing_entry_and_deleted_origin_remain_uncovered(self):
        for indexes in (indexed(index='f'), indexed(entries={}), indexed(entries={RESOURCE: 0})):
            result = plan(indexes=indexes)
            self.assertFalse(result['definitions'][0]['installed_definition_evidence'][0]['index_and_entry_match'])
            self.assertEqual(len(result['maps'][0]['uncovered_definitions']), 1)

    def test_unresolved_campaign_rows_survive_and_common_wrapper_supported(self):
        source = report()
        source['unresolved_weapons'] = [{'resource': 'missing.dbx', 'reason': 'Missing reference'}]
        campaign = {'schema': 'fvr.bc2.campaign_weapon_configurations', 'schema_version': 1,
                    'archives': [source], 'errors': [{'archive': 'bad.fbrb', 'reason': 'Unreadable'}]}
        result = plan([campaign, {'schema': 'fvr.bc2.common_weapon_metadata.v1', 'configuration': report(MP)}])
        self.assertEqual(len(result['unresolved_input_rows']), 2)
        self.assertEqual(len(result['definitions']), 1)

    def test_selection_and_missing_dispatch_fields_are_explicit(self):
        result = plan(native_classes=['wcSmg'], assets=['MP443'], directories=[RESOURCE.rsplit('/', 1)[0]])
        family = next(iter(result['dispatch_families']))
        self.assertIn('FireLogic.FireLogicType', result['dispatch_families'][family]['missing_fields'])
        self.assertEqual(len(plan(families=[family])['definitions']), 1)
        for selection in ({'assets': ['M1911']}, {'families': ['absent']}, {'maps': ['sp_999']}):
            with self.assertRaises(ValueError):
                plan(**selection)

    def test_conflicting_exact_typed_labels_reject(self):
        with self.assertRaisesRegex(ValueError, 'conflicting'):
            plan([report(), report(name='Other')])
        with self.assertRaisesRegex(ValueError, 'conflicting configuration content'):
            plan([report(), report(content='c')])

    def test_bundle_manifest_is_listing_not_availability(self):
        records = catalog.parse_bundle_manifest('levels/sp_02/level\t' + GUID + '\t1\n')
        result = catalog.plan([report()], {}, records, indexed())
        self.assertTrue(result['maps'][0]['listed_in_bundle_manifest'])
        self.assertFalse(result['maps'][0]['mount_section_present'])
        self.assertEqual(result['maps'][0]['mounted_definition_candidates'], [])

    def test_bundle_manifest_single_logical_root_marker_is_not_filesystem_access(self):
        records = catalog.parse_bundle_manifest('/async/ondemand_sounds\t' + GUID + '\t1\n')
        self.assertIn('async/ondemand_sounds', records)
        for name in ('//async/ondemand_sounds', '/../escape', 'C:/escape'):
            with self.assertRaises(ValueError):
                catalog.parse_bundle_manifest(name + '\t' + GUID + '\t1\n')
        with self.assertRaises(ValueError):
            catalog.parse_bundle_manifest('/same\t' + GUID + '\t1\nsame\t' + GUID + '\t1\n')

    def test_manifest_ambiguity_and_path_escape_reject(self):
        for text in ('BundleTarget ' + SP, 'BundleSource ' + MAP + '\nBundleTarget ' + SP +
                     '\nBundleTarget ' + SP, 'BundleSource game:../escape.fbrb\nBundleTarget ' + SP):
            with self.assertRaises(ValueError):
                catalog.parse_package(text)
        with self.assertRaises(ValueError):
            catalog.strict_json(b'{"schema":"a","schema":"b"}')

    def test_cli_rejects_existing_output_and_game_output_before_index_reads(self):
        game = Path(__file__).resolve().parent
        inputs = [Path(__file__).resolve(), Path(catalog.__file__).resolve(),
                  game / 'inspect_bc2_mesh_asset.py']
        args = ['--game', str(game), '--report', str(inputs[0]),
                '--package-mft', str(inputs[1]), '--bundle-manifest', str(inputs[2]), '--output']
        with (patch.object(catalog, 'read_indexes', side_effect=AssertionError('Unexpected index access')),
              patch.object(Path, 'open', side_effect=AssertionError('No files should be opened')),
              contextlib.redirect_stderr(io.StringIO())):
            for output in (inputs[0], game.parent / 'AGENTS.md', game / 'new.json'):
                with self.assertRaises(SystemExit):
                    catalog.main(args + [str(output)])

    def test_index_reader_never_reads_payload_and_preserves_deleted_entries(self):
        class Entry:
            name = RESOURCE
            flags = 0
        class Archive:
            index_sha256 = 'b' * 64
            entries = [Entry()]
            def read_selected(self, names):
                raise AssertionError('Payload access forbidden')
        game = Path(__file__).resolve().parent
        with (patch.object(catalog, 'ArchivePool', return_value=lambda _: Archive()),
              patch.object(Path, 'is_file', return_value=True)):
            result = catalog.read_indexes(game, {SP}, {SP: {'b' * 64}})
        self.assertEqual(result[SP]['entries'][RESOURCE], 0)


if __name__ == '__main__':
    unittest.main()
