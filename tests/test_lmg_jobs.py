"""Portable synthetic contracts only; no installed metadata or process access."""
import copy,json,sys,unittest
from pathlib import Path
from unittest.mock import patch
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from build_lmg_jobs import build
from capture_lmg_native import read_sample,select_job,configured_mesh_matches,cohort,retain_session,open_readers

def sources(family='belt_feed',label='Test LMG'):
    mesh='Objects/Weapons/Fixture/Body.res';resource='Objects/Weapons/Fixture/Definition.dbx';h='a'*64
    inventory=dict(schema='fvr.bc2.installed_weapon_inventory',asset_profiles=[dict(resource=mesh,geometry_variants=[dict(sha256=h)],sources=[dict(archive='test.fbrb')])],
        archives=[dict(archive='test.fbrb',index_sha256='b'*64,animations=[dict(name='TestReload.res',kind='GrannyAnimation',bytes=100)])])
    blueprint=dict(schema='fvr.weapon_interaction_blueprints',profiles=[dict(resource=mesh,family_proposal=family,display_intent=label,
        candidate_parts=[dict(lod=0,mesh_sha256=h,role='unassigned_weighted_part')])])
    weapon=dict(resource=resource,instance_guid='TEST-GUID',resource_sha256='c'*64,native_name='TestLmg',weapon_class='wcLmg',
        firing_resource='Firing.dbx',firing_sha256='d'*64,firing_guid='FIRE-GUID',function_resource='Function.dbx',function_sha256='e'*64,function_guid='FUNCTION-GUID',
        fields={'Ammo.MagazineCapacity':dict(value=100),'FireLogic.ReloadTime':dict(value=8),'FireLogic.ReloadThreshold':dict(value=.7)},weapon_states=[],abort_reload_on_sprint=False,missing_fields=[])
    config=dict(schema='fvr.bc2.authored_weapon_configuration',archive='test.fbrb',index_sha256='b'*64,resolved_weapons=[weapon])
    binding=dict(schema='fvr.bc2.authored_weapon_mesh_bindings',weapons=[dict(resource=resource,instance_guid='TEST-GUID',resource_sha256='c'*64,native_name='TestLmg',
        states=[dict(meshes=[dict(mesh_resource=mesh,status='authored_mesh_resolved',geometry_variants=[dict(mesh_sha256=h)])])])])
    return inventory,blueprint,config,binding
def jobs():return build(*sources())
def definition():return jobs()['models'][0]['definitions'][0]
class Inspector:
    def __init__(self):self.calls=0;self.changed=False
    def owner(self):
        self.calls+=1
        return dict(actor=0x90000,selected_weapon=0x10000 if not(self.changed and self.calls>1) else 0x20000,selected_slot=0)
    def weapon(self,address,slot):return dict(weapon=address,asset_name='TestLmg',asset_path='Objects/Weapons/Fixture/Definition',data=0x30000,firing_data=0x40000,ammo_address=0x50000)
    def state(self,weapon,branch):return dict(address=0x60000+branch,state_3c=2,state_40=1,state_44=2,counter_7c=90,counter_80=100)
def server(i,b):return dict(client_owner=dict(actor=0x90000,selected_weapon=0x10000,selected_slot=0),weapon_data=0x30000,firing_data=0x40000,ammo_address=0x50000,server_firing=0x70000,
    server_soldier=0xa0000,server_item=0xb0000,state=dict(current=2,next=2,loaded=90,reserve=100))
class JobsTests(unittest.TestCase):
    def sample(self):return read_sample(Inspector(),{},definition(),server)
    def test_exact_join_retains_disabled_roles_and_four_jobs(self):
        j=jobs();self.assertEqual(j['runtime_admission'],'none');self.assertEqual(j['unresolved'],[]);m=j['models'][0]
        self.assertEqual(len(m['capture_jobs']),4);self.assertFalse(m['runtime_enabled']);self.assertFalse(m['contacts_verified']);self.assertFalse(m['definitions'][0]['runtime_enabled'])
        self.assertEqual(m['candidate_parts'][0]['role'],'unassigned_weighted_part')
    def test_magazine_plan_does_not_become_belt(self):
        for label in ('MG36','XM8 LMG'):
            m=build(*sources('detachable_magazine',label))['models'][0];self.assertEqual(m['physical_parts_to_identify'],['magazine','magazine_well','release','charge'])
    def test_authored_time_is_hint_not_ack(self):
        h=definition()['observation_schedule_hint'];self.assertAlmostEqual(h['nominal_transfer_seconds'],5.6);self.assertTrue(h['native_multiplier_required']);self.assertTrue(h['nominal_timing_is_not_acknowledgement'])
    def test_same_name_requires_exact_resource(self):
        j=jobs()
        with self.assertRaises(ValueError):select_job(j,'TestLmg')
        self.assertEqual(select_job(j,definition()['resource'])[1]['selector']['asset_name'],'TestLmg')
    def test_duplicate_definition_and_enabled_job_rejected(self):
        j=jobs();j['models'][0]['definitions']*=2
        with self.assertRaises(ValueError):select_job(j,definition()['resource'])
        j=jobs();j['runtime_admission']='all'
        with self.assertRaises(ValueError):select_job(j,definition()['resource'])
    def test_exact_configuration_hash_and_join_identity(self):
        args=sources();args[3]['weapons'][0]['resource_sha256']='0'*64
        with self.assertRaisesRegex(ValueError,'source disagree'):build(*args)
        args=sources();args[3]['weapons']*=2
        with self.assertRaisesRegex(ValueError,'Ambiguous'):build(*args)
    def test_wrong_part_and_body_mesh_variant_rejected(self):
        args=sources();args[1]['profiles'][0]['candidate_parts'][0]['mesh_sha256']='0'*64
        with self.assertRaisesRegex(ValueError,'different mesh'):build(*args)
        args=sources();args[3]['weapons'][0]['states'][0]['meshes'][0]['geometry_variants'][0]['mesh_sha256']='0'*64
        with self.assertRaisesRegex(ValueError,'digest differs'):build(*args)
    def test_reader_is_non_atomic_and_non_authoritative(self):
        s=self.sample();self.assertTrue(s['identity_coherent']);self.assertFalse(s['native_authority']);self.assertFalse(s['atomic_three_copy_snapshot'])
    def test_reader_name_and_path_both_exact(self):
        d=definition();d['selector']['asset_path']+='Other'
        with self.assertRaisesRegex(ValueError,'exact job'):read_sample(Inspector(),{},d,server)
    def test_reader_repeated_owner_change_rejected(self):
        i=Inspector();i.changed=True
        with self.assertRaisesRegex(ValueError,'Owner/equipment'):read_sample(i,{},definition(),server)
    def test_reader_client_server_configuration_must_match(self):
        def bad(i,b):r=server(i,b);r['weapon_data']+=4;return r
        with self.assertRaisesRegex(ValueError,'configuration mismatch'):read_sample(Inspector(),{},definition(),bad)
    def test_mutable_copy_gap_is_not_coherence(self):
        class Moving(Inspector):
            def __init__(self):super().__init__();self.states=0
            def state(self,w,b):self.states+=1;r=super().state(w,b);r['counter_7c']-=self.states;return r
        with self.assertRaisesRegex(ValueError,'repeated read'):read_sample(Moving(),{},definition(),server)
    def test_duplicate_firing_copies_rejected(self):
        def bad(i,b):r=server(i,b);r['server_firing']=0x6003c;return r
        with self.assertRaisesRegex(ValueError,'distinct'):read_sample(Inspector(),{},definition(),bad)
    def test_mesh_exact_owner_path_only_not_visibility(self):
        c=dict(weapon=1,data=2,asset_name='Test');l=dict(**c,identity_coherent=True,states=[dict(meshes=[dict(asset_path='Objects/Test')])])
        self.assertTrue(configured_mesh_matches(l,c,'Objects/Test.res'));self.assertFalse(configured_mesh_matches(l,c,'Objects/Other.res'))
        l['data']+=1;self.assertFalse(configured_mesh_matches(l,c,'Objects/Test.res'))
    def test_session_accepts_progressing_counts_not_new_actor(self):
        s=self.sample();baseline=cohort(s);next=copy.deepcopy(s);next['client_states']['60']['counter_7c']-=1;next['server']['state']['loaded']-=1
        self.assertTrue(retain_session(next,baseline)['session_identity_retained'])
        next['native_owner']['actor']+=4
        with self.assertRaisesRegex(ValueError,'Session owner'):retain_session(next,baseline)
    def test_session_new_config_or_firing_object_rejected(self):
        for which in (0,1,2,3):
            s=self.sample();baseline=cohort(s)
            if which==0:s['configuration']['data']+=4
            if which==1:s['client_states']['64']['address']+=4
            if which==2:s['server']['server_soldier']+=4
            if which==3:s['server']['server_item']+=4
            with self.assertRaises(ValueError):retain_session(s,baseline)
    def test_full_observation_window_includes_first_owner_read(self):
        with patch('capture_lmg_native.time.perf_counter_ns',side_effect=[100,100_000_101]):
            with self.assertRaisesRegex(ValueError,'100ms'):self.sample()
    def test_explicit_executable_binds_disk_and_process(self):
        calls=[];path=Path(__file__).resolve().parents[1]/'never-opened-fixture.exe'
        def image(p):calls.append(('image',p));return 'IMAGE'
        def process(pid,expected_path):calls.append(('process',pid,expected_path));return 'PROCESS'
        def inspector(p,i):self.assertEqual((p,i),('PROCESS','IMAGE'));return 'INSPECTOR'
        def discover(i,p):self.assertEqual((i,p),('IMAGE','PROCESS'));return 'BINDING'
        self.assertEqual(open_readers(path,123,image,process,inspector,discover),('IMAGE','PROCESS','INSPECTOR','BINDING'))
        self.assertEqual(calls,[('image',path.resolve()),('process',123,path.resolve())])
    def test_initial_discovery_failure_closes_owned_reader(self):
        closed=[]
        class Process:
            def __init__(self,*a,**kw):pass
            def close(self):closed.append(True)
        def fail(*a):raise ValueError('synthetic reader failure')
        with self.assertRaises(ValueError):open_readers(Path('fake.exe'),1,lambda p:None,Process,fail,None)
        self.assertEqual(closed,[True])
if __name__=='__main__':unittest.main(verbosity=2)
