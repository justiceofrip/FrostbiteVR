import copy
import hashlib
import json
from pathlib import Path
import struct
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT/'tools'))
import bc2_firearm_cache_completion as completion
import bc2_firearm_body_aliases as aliases
import bc2_body_ammo_assets as ammo
import bc2_body_equipment_assets as equipment


def fixture(resource='Weapon/A.dbx'):
    cfg = dict(resource=resource, sha256='a'*64, instance_guid='00000000-0000-0000-0000-000000000001', mesh_references=[])
    row = dict(asset='A', mesh='Weapon/Mesh', part='body_'+resource[-5], rig_fingerprint=7,
        sections=[dict(section='body', count=3, stride=20, palette_size=1, part_palette_index=0,
                       position='Float3', vertex_skin_hash='0'*16, position_hash='0'*16, part_triangles=1)],
        inverse_bind=[1 if n%5 == 0 else 0 for n in range(16)], configured_meshes=['Weapon/Mesh'],
        closed_clip_sha256='b'*64, holster_from_weapon=[1 if n%5 == 0 else 0 for n in range(16)],
        grip=[0,0,0], muzzle=[0,0,1], closed_pose='static_authored_pose',
        sources=[dict(archive='A.fbrb', archive_index_sha256='c'*64, mesh='Weapon/Mesh', mesh_sha256='d'*64, lod_sha256='e'*64)],
        display_only=True, configuration_path=resource[:-4], authored_configuration=cfg,
        configuration_archive='A.fbrb', configuration_archive_index_sha256='c'*64,
        closed_archive='A.fbrb', closed_archive_index_sha256='c'*64, closed_clip='Closed.res')
    record = b''.join(ammo.text(row[k]) for k in ('asset','mesh','part'))+struct.pack('<QI',7,1)
    record += ammo.text('body')+struct.pack('<5I',60,6,2,0,0)+bytes(60)+struct.pack('<3H',0,1,2)
    return row, record


class CompletionTests(unittest.TestCase):
    def test_existing_record_bytes_are_preserved(self):
        row, record = fixture()
        payload = ammo.MAGIC+struct.pack('<I',1)+record
        self.assertEqual(completion.record_slices(payload,[row]),[record])
        for bad in (payload[:-1],payload+b'x',payload[:8]+struct.pack('<I',2)+payload[12:]):
            with self.assertRaises(ValueError):
                completion.record_slices(bad,[row])

    def test_foreign_cache_key_is_rejected(self):
        row, record = fixture(); row['asset']='Other'
        with self.assertRaises(ValueError):
            completion.record_slices(ammo.MAGIC+struct.pack('<I',1)+record,[row])

    def test_alias_keeps_exact_selector_and_shared_record_key(self):
        row, record = fixture(); alias, alias_record = fixture('Weapon/B.dbx')
        aliases.add_alias(row,record,alias,alias_record)
        expanded=aliases.expand_equipment_rows([row])
        self.assertEqual([p['configuration_path'] for p in expanded],['Weapon/A','Weapon/B'])
        self.assertEqual([p['part'] for p in expanded],[row['part']]*2)
        generated=equipment.equipment_header([row])
        self.assertIn('BodyEquipmentProfile,2>',generated)
        self.assertIn('"Weapon/B"',generated)
        self.assertEqual(row['configured_aliases'][0]['geometry_payload_sha256'],
                         hashlib.sha256(aliases.geometry_payload(record)).hexdigest())

    def test_alias_rejects_same_name_foreign_mesh_and_changed_bytes(self):
        for change in ('mesh','clip','geometry','config'):
            row,record=fixture(); alias,alias_record=fixture('Weapon/B.dbx')
            if change=='mesh':alias['sources'][0]['mesh_sha256']='f'*64
            if change=='clip':alias['closed_clip_sha256']='f'*64
            if change=='geometry':alias_record=alias_record[:-1]+b'\xff'
            if change=='config':alias['authored_configuration']['resource']='Weapon/A.dbx'
            with self.subTest(change=change),self.assertRaises(ValueError):
                aliases.add_alias(row,record,alias,alias_record)

    def test_alias_pins_include_own_configuration(self):
        row,record=fixture(); alias,alias_record=fixture('Weapon/B.dbx')
        aliases.add_alias(row,record,alias,alias_record)
        self.assertIn(('A.fbrb','Weapon/B.dbx','a'*64),list(aliases.alias_resource_pins(row)))

    def test_display_policy_is_explicit_and_configuration_bound(self):
        row,_=fixture(); spec=completion.reviewed_spec(row); binding=completion.reviewed_binding(row)
        review=completion.make_display_policy(spec,binding,'Composite required closed controls not static: varying')
        completion.validate_display_policy(review,row['authored_configuration'],row['closed_clip'],row['closed_clip_sha256'])
        for field in ('strict_static_passed','native_verified','headset_tested'):
            changed=copy.deepcopy(review);changed[field]=True
            changed['policy_digest']=completion.digest({k:v for k,v in changed.items() if k!='policy_digest'})
            with self.assertRaises(ValueError):
                completion.validate_display_policy(changed,row['authored_configuration'],row['closed_clip'],row['closed_clip_sha256'])
        with self.assertRaises(ValueError):
            completion.make_display_policy(spec,binding,'Missing clip')

    def test_manifest_covers_exact_roster_and_bound_without_assets(self):
        plan=json.loads((ROOT/'handoff/tasks/WEAPONS/20261030-cache48/missing-manifest.json').read_text())
        self.assertEqual(plan['summary']['roster_rows'],181)
        self.assertEqual(len(plan['covered_bodies'])+len(plan['missing_bodies']),181)
        self.assertEqual(plan['summary']['planned_cache_rows'],252)
        self.assertGreater(plan['summary']['naive_cache_rows'],completion.MAX_ROWS)
        self.assertTrue(all(j['carried_parts']==[j['part']] for j in plan['missing_shells']))
        ns=next(j for j in plan['missing_shells'] if j['asset']=='NS2000')
        self.assertEqual(ns['part'],'jntWpn_13')
        self.assertEqual(ns['hidden_other_rounds'],['jntWpnwpnJnt_14'])
        self.assertFalse(any(j['part']=='jntWpnwpnJnt_14' for j in plan['missing_shells']))


if __name__=='__main__':
    unittest.main()
