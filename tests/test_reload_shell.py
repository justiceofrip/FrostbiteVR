"""Synthetic shell geometry tests; no game assets or live process use."""
import copy
from pathlib import Path
import sys
import unittest
import numpy as np
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
import audit_reload_shell as shell


def matrix(x=0,y=0,z=0):
    result=np.eye(4);result[3,:3]=[x,y,z];return result.reshape(-1).tolist()


def binding():
    return {'schema':'fvr.bc2.reload_shell_binding','schema_version':1,'expected_asset_name':'Synthetic','expected_skeleton_fingerprints':['rig:one'],
        'shell_skin_binding':{'native_bone_name':'ammo','all_128_vertices_rigid':True},
        'native_mesh_owner':{'identity_coherent':True,'selected_at_capture':False},
        'asset_space_landmark_candidates':{'bounds_center':[0,1,.1],'extent':[.02,.02,.06],
            'unsigned_long_axis_index':2,'endpoint_bounds_centers':[[0,1,.07],[0,1,.13]]}}


def row(n):
    x=n*.04;wrist=matrix(50+x,20,30)
    return {'asset_name':'Synthetic','skeleton':'rig:one','actor':10,'weapon':20,
        'owner_generation':1,'space':2,'capture_episode':1,'capture_sequence':n,
        'attachment_pending':False,'captured_ms':1000+100*n,'units_per_meter':2,
        'bone_roles':{'weapon_root':'root','left_wrist':'LeftHand'},
        'native':matrix(50,20,30),'native_left_wrist':wrist,'native_right_wrist':matrix(50,20,30),
        'weapon_bones_complete':True,'weapon_bones_dropped':0,
        'native_weapon_bones':[{'name':'root','parent_name':'outside','native':matrix(50,20,30),'hidden':False},
           {'name':'ammo','parent_name':'root','native':matrix(50+x+.1,20,30),'hidden':False,'inverse_bind':matrix(0,-2,.2)}],
        'left_hand_bones_captured':True,'left_hand_bones_complete':True,'left_hand_bones_dropped':0,
        'native_left_hand_bones':[{'name':'LeftHand','parent_name':'elbow','native':wrist,'hidden':False,'inverse_bind':matrix()},
           {'name':'LeftFinger','parent_name':'LeftHand','native':matrix(50+x+.06,20,30),'hidden':False,'inverse_bind':matrix()}]}


def trace(rows):return {'gameplay':{'rig_publication':{'weapon_profile_samples':rows}}}


class ShellGeometryTests(unittest.TestCase):
    def analyze(self,rows):return shell.analyze(trace(rows),binding(),'Synthetic',explicit_window=(0,100000))
    def test_basis_units_and_hand_relations(self):
        rows=[row(n) for n in range(1,6)];before=copy.deepcopy(rows);result=self.analyze(rows)
        self.assertEqual(before,rows);self.assertFalse(result['issues']);group=result['groups'][0]
        self.assertTrue(np.allclose(group['mapped_asset_bounds_in_shell_bone']['center_m'],[0,0,0]))
        self.assertTrue(np.allclose(group['mapped_asset_bounds_in_shell_bone']['unsigned_long_axis'],[0,0,-1]))
        self.assertAlmostEqual(group['mapped_asset_bounds_in_shell_bone']['axis_length_m'],.06)
        obs=group['observations'][0]['shell_center_in_hand_bones']
        self.assertAlmostEqual(obs['LeftHand'][12],.05);self.assertAlmostEqual(obs['LeftFinger'][12],.02)
        self.assertEqual(len(group['hand_relations']['LeftFinger']['stable_observed_runs']),1)
        self.assertFalse(group['hand_relations']['LeftFinger']['verified_grasp'])
        self.assertIsNone(result['native_insertion_profile'])
        self.assertFalse(result['basis']['live_vertex_space_correspondence_verified'])
    def test_legacy_missing_inverse_bind_is_explicit(self):
        rows=[row(1)];del rows[0]['native_weapon_bones'][1]['inverse_bind']
        result=self.analyze(rows);self.assertFalse(result['issues']);self.assertFalse(result['groups'])
        self.assertEqual(result['counts']['missing_inverse_bind'],1)
    def test_optional_and_incomplete_hand_never_invent_fingers(self):
        for missing in (True,False):
            sample=row(1)
            if missing:
                sample.pop('left_hand_bones_captured');sample.pop('native_left_hand_bones');sample.pop('left_hand_bones_complete');sample.pop('left_hand_bones_dropped')
            else:
                sample['left_hand_bones_complete']=False;sample['left_hand_bones_dropped']=1
                sample['native_left_hand_bones'][1]['native']=matrix();sample['native_left_hand_bones'][1]['native'][0]=0
            result=self.analyze([sample]);self.assertFalse(result['issues'])
            self.assertEqual(list(result['groups'][0]['observations'][0]['shell_center_in_hand_bones']),['LeftHand'])
    def test_malformed_complete_hand_rejected(self):
        for mutate in (lambda r:r['native_left_hand_bones'][1].__setitem__('parent_name','missing'),
                       lambda r:r['native_left_hand_bones'][0].__setitem__('native',matrix()),
                       lambda r:r['native_left_hand_bones'][1]['native'].__setitem__(0,float('nan'))):
            sample=row(1);mutate(sample);self.assertEqual(len(self.analyze([sample])['issues']),1)
    def test_bind_and_units_change_cannot_merge(self):
        for mutate in (lambda r:r['native_weapon_bones'][1]['inverse_bind'].__setitem__(12,.01),
                       lambda r:r.__setitem__('units_per_meter',1),lambda r:r.__setitem__('capture_sequence',1)):
            rows=[row(1),row(2)];mutate(rows[1]);result=self.analyze(rows)
            self.assertEqual(len(result['issues']),1);self.assertEqual(len(result['groups'][0]['observations']),1)
    def test_invalid_units_and_bounds(self):
        for value in (0,-1,float('nan'),True):
            sample=row(1);sample['units_per_meter']=value;self.assertEqual(len(self.analyze([sample])['issues']),1)
        bad=binding();bad['asset_space_landmark_candidates']['endpoint_bounds_centers'][0][0]=.1
        with self.assertRaises(ValueError):shell.analyze(trace([row(1)]),bad,'Synthetic')
    def test_hidden_shell_keeps_bind_but_no_hand_relation(self):
        sample=row(1);node=sample['native_weapon_bones'][1];node['hidden']=True
        for at in (0,5,10):node['native'][at]=.0001
        result=self.analyze([sample]);self.assertFalse(result['issues']);group=result['groups'][0]
        self.assertEqual(group['counts']['hidden_shell_rows'],1);self.assertFalse(group['observations'])
    def test_missing_reload_window_does_not_relabel_idle_as_reload(self):
        result=shell.analyze(trace([row(1)]),binding(),'Synthetic')
        self.assertFalse(result['groups'][0]['observations'])
        self.assertEqual(result['groups'][0]['counts']['missing_native_reload_window'],1)
    def test_wrong_asset_or_skeleton_rejected(self):
        with self.assertRaises(ValueError):shell.analyze(trace([row(1)]),binding(),'Other')
        sample=row(1);sample['skeleton']='rig:other'
        self.assertEqual(len(self.analyze([sample])['issues']),1)
    def test_hidden_finger_is_not_missing_topology(self):
        rows=[row(1),row(2)]
        rows[1]['native_left_hand_bones'][1]['hidden']=True
        for at in (0,5,10):rows[1]['native_left_hand_bones'][1]['native'][at]=.0001
        result=self.analyze(rows);self.assertFalse(result['issues'])
        self.assertNotIn('LeftFinger',result['groups'][0]['observations'][1]['shell_center_in_hand_bones'])
    def test_current_mesh_join_requires_owner_and_process(self):
        result=self.analyze([row(1)]);source=trace([row(1)]);source['pid']=99
        capture={'read_only':True,'native_calls':False,'process_writes':False,'pid':99,'utc':'synthetic','initial_owner':{'actor':10,'selected_weapon':30,'items':[20,30]},'weapons':[{'asset_name':'Synthetic','weapon':20,'data':40,'mesh_links':{'identity_coherent':True,'weapon':20,'data':40,'states':[{'state_address':50,'meshes':[{'asset_path':'Test/Mesh','address':60}]}]}}]}
        joined=shell.join_current_mesh_capture(capture,source,result,'Test/Mesh')
        self.assertEqual(joined['matching_native_groups'],1)
        self.assertFalse(result['groups'][0]['mesh_capture_join']['weapon_selected_in_mesh_snapshot'])
        capture['pid']=100
        with self.assertRaises(ValueError):shell.join_current_mesh_capture(capture,source,result,'Test/Mesh')
    def test_transfer_neighbors_are_observations_not_sockets(self):
        result=self.analyze([row(1),row(2)]);group={'identity':{'actor':10,'weapon':20,'owner_generation':1,'space':2},'observations':result['groups'][0]['observations']}
        before={'soldier':10,'weapon':20,'actor_generation':1,'space':2,'firing':30,'wrapper_offset':60,'loaded':2,'reserve':6};after=dict(before,loaded=3,reserve=5)
        record={'kind':2,'finished':True,'identity_retained':True,'before':before,'after':after,'end_tick_ms':1150,'id':1}
        found=shell.transfer_neighbors({'gameplay':{'reload_flow':{'records':[record]}}},group)
        self.assertEqual(found[0]['previous_visible_pose']['relative_to_transfer_ms'],-50)
        self.assertEqual(found[0]['following_visible_pose']['relative_to_transfer_ms'],50)
        self.assertFalse(found[0]['verified_seated_pose'])
        record['after']['reserve']=6
        self.assertFalse(shell.transfer_neighbors({'gameplay':{'reload_flow':{'records':[record]}}},group))
    def test_phase_owner_and_time_are_bounded(self):
        sample=row(1);base={'soldier':10,'weapon':20,'actor_generation':1,'space':2,'firing':30,'wrapper_offset':60,'current':11}
        good={'kind':0,'finished':True,'identity_retained':True,'end_tick_ms':1090,'before':base.copy(),'after':base.copy(),'id':1}
        wrong=copy.deepcopy(good);wrong['before']['soldier']=wrong['after']['soldier']=11;wrong['end_tick_ms']=1100
        stale=copy.deepcopy(good);stale['end_tick_ms']=700;stale['after']['current']=99
        future=copy.deepcopy(good);future['end_tick_ms']=1101
        records=shell.phase_records({'gameplay':{'reload_flow':{'records':[good,wrong,stale,future]}}})
        result=shell.phase_for(records,sample);self.assertEqual(len(result),1);self.assertEqual(result[0]['age_ms'],10)
        self.assertEqual(result[0]['state'],11)

if __name__=='__main__':unittest.main()
