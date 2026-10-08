import copy
import math
from pathlib import Path
import sys
import unittest
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
import audit_reload_geometry as geometry


def matrix(x=0,y=0,z=0):
    return [1,0,0,0,0,1,0,0,0,0,1,0,x,y,z,1]


def row(sequence,actor=10,weapon=20,episode=1):
    # Moving generic node rigidly follows the native left hand. Translation uses
    # 2 engine units/metre so the measured wrist offset must normalize to.05m.
    x=sequence*.04
    return {'asset_name':'SyntheticItem','skeleton':'rig:one','actor':actor,'weapon':weapon,
            'owner_generation':1,'space':2,'capture_episode':episode,'capture_sequence':sequence,
            'attachment_pending':False,'captured_ms':1000+100*sequence,'units_per_meter':2,
            'bone_roles':{'weapon_root':'root'},'native':matrix(50,20,30),
            'native_left_wrist':matrix(50+x,20,30),'native_right_wrist':matrix(50,20,30),
            'weapon_bones_complete':True,'weapon_bones_dropped':0,
            'native_weapon_bones':[{'name':'root','parent_name':'outside','native':matrix(50,20,30),'hidden':False},
                {'name':'generic7','parent_name':'root','native':matrix(50+x+.1,20,30),'hidden':False}]}


def trace(rows):
    return {'gameplay':{'rig_publication':{'weapon_profile_samples':rows}}}


class ReloadGeometryTests(unittest.TestCase):
    def analyze(self,rows):
        return geometry.analyze(trace(rows),explicit_window=(0,100000))

    def test_measured_relation_remains_unidentified_and_readonly(self):
        rows=[row(n) for n in range(1,6)];before=copy.deepcopy(rows);result=self.analyze(rows)
        self.assertEqual(before,rows);self.assertFalse(result['issues']);self.assertIsNone(result['native_insertion_profile'])
        bone=next(b for b in result['groups'][0]['bones'] if b['name']=='generic7')
        self.assertTrue(bone['moves_during_reload']);self.assertIsNone(bone['semantic_role']);self.assertIsNone(bone['mesh_identity'])
        self.assertFalse(bone['native_profile_verified']);self.assertEqual(len(bone['left_relation_runs']),1)
        self.assertAlmostEqual(bone['left_relation_runs'][0]['observed_bone_from_wrist'][12],.05)
        self.assertFalse(bone['left_relation_runs'][0]['verified_attachment'])

    def test_identity_and_episode_do_not_merge(self):
        rows=[row(n) for n in range(1,5)]+[row(n,actor=11) for n in range(1,5)]+[row(n,weapon=21) for n in range(1,5)]+[row(n,episode=2) for n in range(1,5)]
        result=self.analyze(rows);self.assertEqual(len(result['groups']),4);self.assertFalse(result['issues'])

    def test_invalid_rigid_nonfinite_and_incomplete_reject(self):
        for mutate in (lambda r:r['native_weapon_bones'][1]['native'].__setitem__(0,float('nan')),
                       lambda r:r['native_weapon_bones'][1]['native'].__setitem__(0,0),
                       lambda r:r.__setitem__('weapon_bones_complete',False),
                       lambda r:r.__setitem__('units_per_meter',0),
                       lambda r:r.__setitem__('actor',0)):
            bad=row(1);mutate(bad);result=self.analyze([bad]);self.assertEqual(len(result['issues']),1);self.assertFalse(result['groups'])

    def test_hidden_leaf_is_not_attachment(self):
        rows=[row(n) for n in range(1,5)]
        for r in rows:
            b=r['native_weapon_bones'][1];b['hidden']=True
            for at in (0,5,10):b['native'][at]=.00001
        result=self.analyze(rows);self.assertFalse(result['issues']);bone=next(b for b in result['groups'][0]['bones'] if b['name']=='generic7')
        self.assertEqual(bone['hidden_samples'],4);self.assertIsNone(bone['weapon_relative']);self.assertFalse(bone['left_relation_runs'])

    def test_duplicate_topology_and_sequence_rejected(self):
        rows=[row(1),row(1),row(2)];rows[2]['native_weapon_bones'][1]['parent_name']='missing'
        result=self.analyze(rows);self.assertEqual(len(result['issues']),2)

    def test_native_window_requires_same_actor_item(self):
        rows=[row(n) for n in range(1,6)]+[row(n,actor=11) for n in range(1,6)]
        data=trace(rows);data['gameplay']['reload_flow']={'records':[{'kind':0,'finished':True,'identity_retained':True,
            'begin_tick_ms':1200,'end_tick_ms':1400,
            'before':{'soldier':10,'weapon':20,'actor_generation':1,'space':2,'current':11},
            'after':{'current':12}}]}
        result=geometry.analyze(data);self.assertEqual(len(result['groups']),1);self.assertEqual(result['groups'][0]['samples'],3)
        self.assertEqual(result['groups'][0]['actor'],10)

    def test_sparse_runs_cannot_manufacture_attachment(self):
        rows=[row(n) for n in (1,4,7,10)];result=self.analyze(rows)
        bone=next(b for b in result['groups'][0]['bones'] if b['name']=='generic7');self.assertFalse(bone['left_relation_runs'])


if __name__=='__main__':
    unittest.main()