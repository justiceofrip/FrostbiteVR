import copy
import json
from pathlib import Path
import struct
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT/'tools'))
import bc2_feed_supply_cache as feed
import bc2_body_ammo_assets as ammo


def fixture(asset='Pouch'):
    # A mixed section includes one pouch triangle and one weapon-root triangle.
    # The production parser must retain the former only.
    vertices = b''.join(struct.pack('<3f8B', float(i), 0., 0., owner, 0, 0, 0, 255, 0, 0, 0)
                        for i, owner in enumerate((1, 1, 1, 0, 0, 0)))
    positions = b''.join(vertices[i*20:i*20+12] for i in range(6))
    section = dict(section='Mixed', count=6, stride=20, palette_size=2, part_palette_index=1,
                   position='Float3', vertex_skin_hash=f'{ammo.fnv(vertices):016x}',
                   position_hash=f'{ammo.fnv(positions):016x}', part_triangles=1)
    row = dict(asset=asset, mesh='Mesh', part='pouch', rig_fingerprint=7,
               inverse_bind=[1 if n%5 == 0 else 0 for n in range(16)], sections=[section])
    record = b''.join(ammo.text(row[k]) for k in ('asset', 'mesh', 'part'))+struct.pack('<QI', 7, 1)
    record += ammo.text('Mixed')+struct.pack('<5I', len(vertices), 12, 2, 0, 0)+vertices+struct.pack('<6H', *range(6))
    return row, record


class FeedSupplyTests(unittest.TestCase):
    def inputs(self):
        review = feed.read(ROOT/feed.REVIEW)
        samples = feed.read(ROOT/feed.SAMPLES)
        registrations = feed.registry_geometry((ROOT/feed.GENERATED).read_text(), (ROOT/feed.BUILTIN).read_text())
        return review, samples, registrations

    def test_all24_runtime_configurations_join12_exact_pouch_keys(self):
        jobs, joins = feed.jobs_from_review(*self.inputs())
        self.assertEqual(len(joins), 24)
        self.assertEqual(len(jobs), 12)
        self.assertEqual(len({feed.cache_key(j) for j in jobs}), 12)
        self.assertEqual(len({j['mesh_sha256'] for j in jobs}), 5)
        self.assertEqual({j['part'] for j in jobs}, {'jntWpn_6'})
        self.assertEqual({j['asset'] for j in jobs},
                         {'M60', 'M60_sp', 'M60_sp_s', 'PKM', 'M249', 'MG3', 'MG3k', 'MG3_sp',
                          'MG3_sp_k', 'QJU88', 'QJU88_sp', 'QJU88_sp_k'})
        self.assertEqual({tuple(j['cache_key']) for j in joins}, {feed.cache_key(j) for j in jobs})

    def test_foreign_runtime_keys_or_content_fail_closed(self):
        review, samples, registrations = self.inputs()
        for field, value in (('asset', 'M60'), ('meshPath', 'Other'), ('pouchBone.bone', 'jntWpn_1'),
                             ('rigFingerprint', 7), ('meshSha256', '0'*64), ('lodSha256', '0'*64)):
            changed = copy.deepcopy(registrations); changed[0][field] = value
            with self.subTest(field=field), self.assertRaisesRegex(ValueError, 'Runtime/review'):
                feed.jobs_from_review(review, samples, changed)

    def test_duplicate_configuration_cannot_hide_missing_variant(self):
        review, samples, registrations = self.inputs()
        review['configurations'][-1] = copy.deepcopy(review['configurations'][0])
        with self.assertRaisesRegex(ValueError, 'Duplicate/invalid'):
            feed.jobs_from_review(review, samples, registrations)

    def test_sample_mesh_pin_and_role_join_cannot_drift(self):
        review, samples, registrations = self.inputs()
        group = review['configurations'][0]['content_group']
        for field in ('mesh_sha256', 'lod_sha256'):
            changed = copy.deepcopy(samples); changed['groups'][group]['source'][field] = '0'*64
            with self.subTest(field=field), self.assertRaisesRegex(ValueError, 'Sample mesh'):
                feed.jobs_from_review(review, changed, registrations)

    def test_weighted_pouch_triangle_isolated_from_weapon_triangle(self):
        row, record = fixture()
        payload = ammo.MAGIC+struct.pack('<I', 1)+record
        feed.validate_supply(row, payload)
        changed = copy.deepcopy(row); changed['sections'][0]['part_triangles'] = 2
        with self.assertRaisesRegex(ValueError, 'triangle ownership'):
            feed.validate_supply(changed, payload)
        changed = copy.deepcopy(row); changed['sections'][0]['vertex_skin_hash'] = '0'*16
        with self.assertRaisesRegex(ValueError, 'fingerprint'):
            feed.validate_supply(changed, payload)

    def test_missing_foreign_and_truncated_records_rejected(self):
        row, record = fixture()
        payload = ammo.MAGIC+struct.pack('<I', 1)+record
        for bad in (payload[:-1], payload+b'\x00', payload[:8]+struct.pack('<I', 2)+payload[12:]):
            with self.assertRaises(ValueError):
                feed.validate_supply(row, bad)
        changed = copy.deepcopy(row); changed['part'] = 'cover'
        with self.assertRaises(ValueError):
            feed.validate_supply(changed, payload)

    def test_nonrigid_and_cross_part_triangles_rejected(self):
        row, record = fixture()
        payload = ammo.MAGIC+struct.pack('<I', 1)+record
        vertices_at = len(payload)-12-120
        changed = bytearray(payload)
        changed[vertices_at+16] = 128
        changed[vertices_at+17] = 127
        with self.assertRaisesRegex(ValueError, 'not rigid'):
            feed.validate_supply(row, bytes(changed))
        changed = bytearray(payload)
        changed[vertices_at+12] = 0
        with self.assertRaisesRegex(ValueError, 'cross-part'):
            feed.validate_supply(row, bytes(changed))

    def test_append_preserves_all_baseline_record_bytes(self):
        original, record = fixture('Original')
        baseline = ammo.MAGIC+struct.pack('<I', 1)+record
        addition, appended = fixture('New')
        rows, payload = feed.append_cache([original], baseline, [addition], [appended])
        self.assertEqual(len(rows), 2)
        self.assertEqual(payload[12:len(baseline)], baseline[12:])
        self.assertEqual(payload[len(baseline):], appended)
        with self.assertRaisesRegex(ValueError, 'Duplicate'):
            feed.append_cache([original], baseline, [original], [record])

    def test_appended_metadata_reproduces_headers_and_retains_baseline(self):
        public = ROOT/'profiles/weapons54-feed-cache'
        if not (public/'extraction-receipt.json').exists():
            self.skipTest('Installed extraction has not been enrolled')
        rows = feed.read(ROOT/'config/body-ammo-assets.json')['profiles']
        additions = feed.read(public/'additions-body-ammo-assets.json')['profiles']
        receipt = feed.read(public/'extraction-receipt.json')
        self.assertEqual(len(rows), 264)
        self.assertEqual(len(additions), 12)
        self.assertEqual(rows[-12:], additions)
        self.assertTrue(receipt['baseline_records_byte_exact'])
        self.assertEqual(receipt['preserved_keys'], 252)
        self.assertEqual(ammo.catalog_header(rows), (ROOT/'src/games/bc2/Bc2BodyAmmoAssetProfiles.h').read_text())
        self.assertEqual(feed.belt_header(rows[:2]+additions), (ROOT/'src/games/bc2/Bc2BeltPropProfiles.h').read_text())
        self.assertTrue(all(0 < sum(s['part_triangles'] for s in r['sections']) < 2000 for r in additions))
        self.assertTrue(all(len(r['inverse_bind']) == 16 and r['part'] == 'jntWpn_6' for r in additions))
        self.assertFalse(any(r.get('display_only') or r.get('mod_authored_magazine') for r in additions))
        plan = feed.read(public/'plan.json')
        self.assertEqual({tuple(j['cache_key']) for j in plan['configuration_joins']},
                         {feed.cache_key(r) for r in additions})


if __name__ == '__main__':
    unittest.main()
