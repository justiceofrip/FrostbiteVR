"""Container-layout variation must never relax required asset or geometry pins."""
import copy
import hashlib
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch, Mock

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
import bc2_asset_resource_identity as identity
import bc2_body_ammo_assets as ammo
import bc2_configured_body_assets as configured
import bc2_magazine_assembly_cache as assembly
from bc2_magazine_contact_batch import ArchivePool
from inspect_bc2_mesh_asset import Archive
from test_audio_resource_identity import archive_bytes


class ResourceIdentityTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.rows = json.loads((ROOT / 'config/body-ammo-assets.json').read_text())['profiles']

    def test_complete_current_catalog_has_consistent_exact_resource_pins(self):
        pins = identity.catalog_resource_pins(self.rows, ammo.SKELETON_ARCHIVE, ammo.SKELETON_RESOURCE, ammo.SKELETON_SHA)
        self.assertEqual(len(self.rows), 252)
        self.assertEqual(len(pins), 409)
        self.assertEqual(pins[(ammo.SKELETON_ARCHIVE.casefold(), ammo.SKELETON_RESOURCE)], ammo.SKELETON_SHA)
        broken = copy.deepcopy(self.rows[0])
        broken['mesh_sha256'] = '0' * 64
        with self.assertRaisesRegex(ValueError, 'Conflicting'):
            identity.catalog_resource_pins([self.rows[0], broken], ammo.SKELETON_ARCHIVE, ammo.SKELETON_RESOURCE, ammo.SKELETON_SHA)

    def test_real_archive_relayout_accepts_only_exact_selected_content(self):
        with tempfile.TemporaryDirectory() as folder:
            game = Path(folder)
            path = game / 'fixture.fbrb'
            required = b'reviewed geometry bytes'
            path.write_bytes(archive_bytes([('mesh.res', required)]))
            original_index = Archive(path).index_sha256
            path.write_bytes(archive_bytes([('unrelated.res', b'new localization'), ('mesh.res', required)]))
            with self.assertRaisesRegex(ValueError, 'index differs'):
                ArchivePool(game, path.name, original_index)(path)
            pool = identity.VerifiedResources(game, ArchivePool(game, path.name, original_index, verify_common_index=False), {(path.name, 'mesh.res'): hashlib.sha256(required).hexdigest()})
            self.assertEqual(pool(path).read_selected(['mesh.res']), {'mesh.res': required})
            with self.assertRaisesRegex(ValueError, 'not pinned'):
                pool(path).read_selected(['unrelated.res'])
            path.write_bytes(archive_bytes([('mesh.res', b'changed required geometry')]))
            new_pool = identity.VerifiedResources(game, ArchivePool(game, path.name, original_index, verify_common_index=False), {(path.name, 'mesh.res'): hashlib.sha256(required).hexdigest()})
            with self.assertRaisesRegex(ValueError, 'content differs'):
                new_pool(path).read_selected(['mesh.res'])

    def test_only_known_container_provenance_may_differ(self):
        def change_indexes(row):
            for key in ('archive_index_sha256', 'configuration_archive_index_sha256', 'closed_archive_index_sha256'):
                if key in row:
                    row[key] = 'f' * 64
            for source in row.get('sources', []):
                source['archive_index_sha256'] = 'f' * 64
            for container in ('assembly_source', 'mod_policy_source'):
                for label in ('mesh', 'lod', 'skeleton', 'static', 'reload'):
                    if label in row.get(container, {}):
                        row[container][label]['archive_index_sha256'] = 'f' * 64
            for alias in row.get('configured_aliases', []):
                change_indexes(alias['profile'])

        for original in self.rows:
            changed = copy.deepcopy(original)
            change_indexes(changed)
            self.assertFalse(identity.profiles_match([changed], [original]))
            self.assertTrue(identity.profiles_match([changed], [original], True))
            for field in ('asset', 'mesh', 'part', 'rig_fingerprint', 'inverse_bind', 'sections'):
                mutation = copy.deepcopy(changed)
                mutation[field] = 'changed'
                self.assertFalse(identity.profiles_match([mutation], [original], True), field)
            mutation = copy.deepcopy(changed)
            mutation['new_unreviewed_field'] = True
            self.assertFalse(identity.profiles_match([mutation], [original], True))
        rigid = copy.deepcopy(next(r for r in self.rows if r.get('rigid_assembly')))
        changed = copy.deepcopy(rigid)
        changed['assembly_source']['profile']['reload_clip']['archive_index_sha256'] = 'f' * 64
        self.assertFalse(identity.profiles_match([changed], [rigid], True))

    def test_new_policy_and_alias_resource_hashes_remain_exact(self):
        policy = next(r for r in self.rows if r.get('mod_authored_magazine'))
        for label in ('mesh', 'lod', 'skeleton', 'static', 'reload'):
            changed = copy.deepcopy(policy)
            changed['mod_policy_source'][label]['sha256'] = '0' * 64
            self.assertFalse(identity.profiles_match([changed], [policy], True), label)
        original = next(r for r in self.rows if r.get('configured_aliases'))
        for mutation in ('payload', 'configuration', 'clip', 'nested'):
            changed = copy.deepcopy(original)
            alias = changed['configured_aliases'][0]
            if mutation == 'payload':
                alias['geometry_payload_sha256'] = '0' * 64
            elif mutation == 'configuration':
                alias['profile']['authored_configuration']['sha256'] = '0' * 64
            elif mutation == 'clip':
                alias['profile']['closed_clip_sha256'] = '0' * 64
            else:
                alias['profile']['configured_aliases'] = [{'profile': {}}]
                with self.assertRaisesRegex(ValueError, 'Nested configuration alias'):
                    identity.profiles_match([changed], [original], True)
                continue
            self.assertFalse(identity.profiles_match([changed], [original], True), mutation)

    def test_actual_catalog_driver_rejects_skeleton_before_parsing_or_writing(self):
        with tempfile.TemporaryDirectory() as folder:
            game = Path(folder)
            path = game / ammo.SKELETON_ARCHIVE
            path.parent.mkdir(parents=True)
            path.write_bytes(archive_bytes([(ammo.SKELETON_RESOURCE, b'wrong skeleton')]))
            with patch.object(configured, 'Skeleton') as parser, self.assertRaisesRegex(ValueError, 'Catalog resource content differs'):
                configured.derive_catalog(game, self.rows, resource_content=True)
            parser.assert_not_called()

    def test_actual_composite_keeps_clip_hash_before_parser_with_changed_index(self):
        with tempfile.TemporaryDirectory() as folder:
            game = Path(folder)
            path = game / 'clip.fbrb'
            raw = b'reviewed static clip'
            path.write_bytes(archive_bytes([('clip.res', raw)]))
            cfg = {'resource': 'weapon.dbx', 'sha256': 'a' * 64}
            binding = dict(weapon=cfg, animation_source=dict(archive=path.name, archive_index_sha256='0' * 64), animation_chain=dict(animation_resource='clip.res'), animation_sha256=hashlib.sha256(raw).hexdigest())
            spec = {'authored_configuration': cfg, 'meshes': []}
            for content_mode, expected in ((False, 'invariant'), (True, 'parser reached')):
                with patch.object(configured, 'Clip', side_effect=RuntimeError('parser reached')) as parser, self.assertRaisesRegex((ValueError, RuntimeError), expected):
                    configured.composite(game, Archive, spec, binding, {}, None, {}, {}, resource_content=content_mode)
                self.assertEqual(parser.call_count, int(content_mode))
            path.write_bytes(archive_bytes([('clip.res', b'changed clip')]))
            with patch.object(configured, 'Clip') as parser, self.assertRaises(ValueError):
                configured.composite(game, Archive, spec, binding, {}, None, {}, {}, resource_content=True)
            parser.assert_not_called()

    def test_actual_assembly_loader_checks_every_required_resource_with_changed_indexes(self):
        original = next(r for r in self.rows if r.get('rigid_assembly'))
        for changed_label in ('mesh', 'lod', 'skeleton', 'static', 'reload'):
            reviewed = copy.deepcopy(original)
            resources = {}
            for label in ('mesh', 'lod', 'skeleton', 'static', 'reload'):
                ref = reviewed['assembly_source'][label]
                raw = ('synthetic ' + label).encode()
                ref['sha256'] = hashlib.sha256(raw).hexdigest()
                resources[ref['resource']] = raw
            resources[reviewed['assembly_source'][changed_label]['resource']] += b'corruption'
            archive = Mock(index_sha256='f' * 64)
            archive.read_selected.side_effect = lambda names: {n: resources[n] for n in names}
            with patch('bc2_weapon_animation_pipeline.Skeleton'), patch('bc2_weapon_animation_pipeline.Clip'), patch.object(assembly, 'derive_cache') as derive, self.assertRaisesRegex(ValueError, 'resource changed'):
                assembly.derive_reviewed_cache(Path('.'), lambda _: archive, reviewed, resource_content=True)
            derive.assert_not_called()

    def test_resource_mode_cannot_derive_enrollment_metadata(self):
        for args in (['--resource-content'], ['--resource-content', '--derive-catalog', 'forbidden.json'], ['--resource-content', '--header', 'forbidden.h']):
            with patch.object(ammo, 'derive_all') as derive, self.assertRaisesRegex(ValueError, 'requires a reviewed catalog'):
                ammo.main(['--game', '.', '--output', 'forbidden.cache'] + args)
            derive.assert_not_called()


if __name__ == '__main__':
    unittest.main()
