import copy
import json
import math
from pathlib import Path
import struct
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
import bc2_firearm_muzzle_enrollment as muzzle
from bc2_weapon_config_pipeline import Node, Value


def binding():
    b = dict(weapon_bone='jntWpn_1',state_count=1,state_index=0,
             coordinate_convention=muzzle.CONVENTION,
             animation_sha256='1'*64,skeleton_sha256='2'*64,rig_fingerprint='fnv1a64:1234567890abcdef',
             rig_topology=[dict(name='root',parent_index=-1),dict(name='jntWpn_1',parent_index=0),
                           dict(name='jntWpn_Flash',parent_index=1)],
             static_parts_and_fingers={'jntWpn_Flash':[1,0,0,0,0,1,0,0,0,0,1,0,.01,.1,-.9,1]})
    b['binding_digest'] = muzzle.digest(b)
    return b


def resign(b):
    b['binding_digest'] = muzzle.digest({k:v for k,v in b.items() if k!='binding_digest'})


class MuzzleEnrollmentTests(unittest.TestCase):
    def test_all_exact_roster_and_compiled_support_sources(self):
        folder = ROOT / 'profiles/weapons54-muzzle'
        document = json.loads((folder/'enrollment.json').read_text())
        extraction = json.loads((folder/'source-metadata.json').read_text())
        roster = json.loads((ROOT/'handoff/tasks/WEAPONS/20261030-roster43/roster.json').read_text())
        supports = muzzle.support_headers(ROOT/'profiles/weapons43/CombinedSupport.h',ROOT)
        self.assertEqual(document['gaps'],[])
        self.assertEqual(document['summary'],dict(configurations=181,enrolled_configurations=181,asset_names=77))
        self.assertEqual({(r['asset'],r['configuration']) for r in roster['rows']},
                         {(p['asset'],p['configuration_path']) for p in document['profiles']})
        self.assertEqual((folder/'Muzzle.generated.h').read_text(),muzzle.header(document))
        for p in document['profiles']:
            self.assertEqual(p['profile_digest'],muzzle.digest({k:v for k,v in p.items() if k!='profile_digest'}))
            self.assertEqual(p['origin']['origin_digest'],muzzle.digest({k:v for k,v in p['origin'].items() if k!='origin_digest'}))
            self.assertEqual(p['firing'],extraction['firing'][p['configuration_path']])
            self.assertTrue(muzzle.ordinary(p['firing']['fields']))
            self.assertEqual(p['origin']['direction_claim'],'none')
            self.assertEqual(p['origin']['native_orientation'],'preserve')
            self.assertFalse(p['native_verified'])
            matches=[s for s in supports if (s['asset'],s['mesh'],s['binding_digest'],s['clip_sha256'],s['skeleton_sha256'],s['rig_fingerprint']) ==
                (p['asset'],p['mesh_path'],p['support_binding_digest'],p['clip_sha256'],p['skeleton_sha256'],p['rig_fingerprint'])]
            self.assertEqual(len(matches),1)
        self.assertLess(extraction['selected_resource_bytes'],16*1024*1024)
        self.assertFalse(extraction['raw_assets_exported'])

    def test_origin_requires_exact_bone_subtree_and_source_digest(self):
        b=binding();reference=muzzle.origin_reference(b)
        self.assertEqual(reference['flash_in_weapon'][12:15],[.01,.1,-.9])
        b['static_parts_and_fingers']['jntWpn_Flash'][12]+=.05
        with self.assertRaisesRegex(ValueError,'digest'):muzzle.origin_reference(b)
        b=binding();b['rig_topology'][2]['parent_index']=0;resign(b)
        with self.assertRaisesRegex(ValueError,'subtree'):muzzle.origin_reference(b)
        b=binding();b['rig_topology'][2]['parent_index']=2;resign(b)
        with self.assertRaisesRegex(ValueError,'subtree'):muzzle.origin_reference(b)
        b=binding();b['coordinate_convention']='native unconverted';resign(b)
        with self.assertRaisesRegex(ValueError,'frame'):muzzle.origin_reference(b)

    def test_dynamic_reference_is_explicit_and_never_claims_static_direction(self):
        b=binding();transform=b['static_parts_and_fingers'].pop('jntWpn_Flash');resign(b)
        with self.assertRaisesRegex(ValueError,'decoded'):muzzle.origin_reference(b)
        decoded=dict(animation_sha256=b['animation_sha256'],skeleton_sha256=b['skeleton_sha256'],
                     rig_fingerprint=b['rig_fingerprint'],reference_time_seconds=0.,flash_in_weapon=transform)
        result=muzzle.origin_reference(b,decoded)
        self.assertEqual(result['status'],'exact_authored_initial_reference_live_relation_required')
        self.assertEqual(result['direction_claim'],'none')
        decoded['skeleton_sha256']='3'*64
        with self.assertRaisesRegex(ValueError,'identity'):muzzle.origin_reference(b,decoded)

    def test_typed_big_endian_vector_rejects_layout_and_nonfinite(self):
        def node(values,kind=7,width=4,count=4):
            return Node('field',{'name':'InitialSpeed'},Value(kind,width,count,struct.pack('>4f',*values)),(),0)
        self.assertEqual(muzzle.vector(node([0,0,600,0])),[0,0,600])
        for n in [node([0,0,math.inf,0]),node([0,0,600,1]),node([0,0,600,0],count=3),node([0,0,600,0],kind=6)]:
            with self.assertRaises(ValueError):muzzle.vector(n)

    def test_shot_contract_retains_pellet_burst_counts_and_rejects_special_paths(self):
        document=json.loads((ROOT/'profiles/weapons54-muzzle/source-metadata.json').read_text())
        fields=next(iter(document['firing'].values()))['fields']
        for shell,shot in [(1,1),(12,1),(6,1),(1,2),(1,3)]:
            current=copy.deepcopy(fields);current['NumberOfBulletsPerShell']=shell;current['NumberOfBulletsPerShot']=shot
            self.assertTrue(muzzle.ordinary(current))
        mutations={'InitialDirection':[0,0,-1],'InitialPosition':[.01,0,0],'InitialSpeed':[0,1,600],
                   'InheritWeaponSpeedAmount':.1,'NumberOfBulletsPerShell':0,'NumberOfBulletsPerShot':0,
                   'ForceSpawnToCamera':True,'RelativeTargetAiming':True,'MuzzleExplosion':'some/explosion',
                   'ProjectileData':'null'}
        for key,value in mutations.items():
            with self.subTest(key=key):
                current=copy.deepcopy(fields);current[key]=value;self.assertFalse(muzzle.ordinary(current))


if __name__ == '__main__':
    unittest.main()
