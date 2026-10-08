import copy,sys,uuid,unittest
from pathlib import Path
sys.dont_write_bytecode=True
sys.path.insert(0,str(Path(__file__).parents[1]/'tools'))
from audit_body_holster import audit
from preflight_body_holster_probe import validate
class Fixture:
    def __init__(self,folder):
        self.folder=folder
        self.manifest=dict(pid=123,body_holster_probe=True,body_inventory=True,physical_reload=True,body_holster_input_accepted=False,duration_ms=15000,until_host_exit=False,game_sha256='a'*64,probe_sha256='b'*64,bootstrap_sha256='c'*64)
        for k in ('weapon_visibility_probe','physical_reload_probe','physical_reload_repeat_probe','reload_request_probe','reload_hold_probe','reload_round_probe','optic_filter_observe','death_probe','equip_probe','rig_pulse','sight_flip','pass_evidence'):self.manifest[k]=False
        self.completion=dict(bootstrap_exit=0,game_exited=False,new_crash_report=False,game_responding=True,stability_observation_ms=15000)
        self.receiver=dict(consumed_pairs=240,async_timeouts=0,body_holster_fixture=True,headset_tested=False)
        self.probe=dict(requested=True,phase=11,failure=0,baseline_claim=1,hidden_request=2,show_request=3,production_input_accepted=False,gpu_visibility_verified=False,headset_verified=False,rows=[])
        for k in ('player','soldier','weak','weapon','actor_generation','native_equip_generation','physical_equip_generation','space'):self.probe[k]=12345
        masks=[0xffffffff,0xffffffff,(1<<12)|(1<<14)|(1<<29),0x72]
        self.probe['challenge']=dict(staged=True,committed=True,unrelated_preserved=True,input=10,native_tick=20,cache=0x60000,request=2,before=[0,0,0,0],challenged=[0x3f800000,0x3f800000,masks[2],masks[3]],written=[0]*4,observed=[0]*4)
        rig=dict(native_animation_written=False,source_changes=0,packing_failures=0,free_right_poses=100,free_right_paired_copies=100,weapon_visibility=dict(verified_hidden_copies=100,verified_shown_copies=100))
        self.trace=dict(pid=123,hooks_disabled=True,gameplay=dict(body_holster_probe=self.probe,rig_publication=rig,body_holster=dict(suppression_failures=0,suppression_commits=100)),native_stream=dict(published=240,consumed=240,captured_eyes=480,camera_restore_failures=0,gpu_failure_code=0),body_holster_eyes=[],world_color_captures=[dict(captured=True,hresult=0,target=dict(width=2,height=2,format=28,samples=1)) for _ in range(3)])
        self.pairs=[dict(pair=n,native_frame=1000+n,tracking=100+n) for n in range(240)]
        for seq,(phase,pair) in enumerate(((1,0),(5,24),(6,48),(10,72)),1):
            now=seq*1_000_000_000;empty=phase in (5,6)
            row=dict(phase=phase,now_ns=now,input=seq,observed_ns=now,deadline_ns=now+100_000_000,native_tick=seq,left_claim=0,right_claim=0 if empty else 1 if phase==1 else 2,body_phase=3 if empty else 1,free_right=empty,hidden=empty,suppressed=empty,request=2 if empty else 0 if phase==1 else 3,draw_serial=seq,paired_free_copies=seq*10,consumed_grip=[.4,-.1,-.4] if phase==6 else [.1,-.3,-.4])
            self.probe['rows'].append(row)
            for eye in (0,1):
                self.trace['body_holster_eyes'].append(dict(row,native_frame=1000+pair,eye=eye,hidden_receipt=empty,paired_pack_receipt=True,receipt_deadline_ns=now+100_000_000))
                (folder/f'pair-{pair}-eye-{eye}.rgba').write_bytes(bytes(range(16)))
        sample=dict(monotonic_ns=1_000_000_000,client_owner=dict(selected_weapon=12345),server_player=1,server_soldier=2,server_item=3,server_firing=4,asset_name='SPAS12_sp',identity_coherent=True,state=dict(current=2,next=2,loaded=7,reserve=8))
        other=copy.deepcopy(sample);other['monotonic_ns']+=30_000_000
        self.before=dict(pid=123,executable_sha256='e'*64,read_only=True,process_writes=False,native_calls=False,samples=[sample,other],rejected=[])
        self.after=copy.deepcopy(self.before)
    def run(self):return audit(self.trace,self.manifest,self.completion,self.receiver,self.pairs,self.folder,self.before,self.after)
class Tests(unittest.TestCase):
    def setUp(self):
        self.root=(Path(__file__).parents[1]/'test-temp').resolve();self.root.mkdir(exist_ok=True);self.path=self.root/uuid.uuid4().hex;self.path.mkdir();self.f=Fixture(self.path)
    def tearDown(self):
        self.assertEqual(self.path.resolve().parent,self.root)
        for file in self.path.iterdir():self.assertTrue(file.is_file());file.unlink()
        self.path.rmdir()
    def test_pass_never_promotes_gpu_or_production_acceptance(self):
        r=self.f.run();self.assertTrue(r['mechanical_verified'],r);self.assertTrue(r['visual_review_ready']);self.assertFalse(r['gpu_visibility_verified']);self.assertFalse(r['production_input_accepted']);self.assertEqual(len(r['sampleable_eye_pairs']),4)
    def scoped_xm8(self):
        self.f.manifest['body_holster_diagnostic_profile']=2
        self.f.probe.update(diagnostic_profile=2,asset='XM8_sp_s')
        self.f.probe['rows'][-1].update(blocks_actions=False,allows_gun_hold=True)
        for report in (self.f.before,self.f.after):
            for sample in report['samples']:sample['asset_name']='XM8_sp_s'
    def test_explicit_scoped_xm8_is_auditable_without_acceptance(self):
        self.scoped_xm8();r=self.f.run()
        self.assertTrue(r['mechanical_verified'],r)
        self.assertEqual(r['sections']['native_ammo_counts']['evidence']['asset'],'XM8_sp_s')
        self.assertFalse(r['production_input_accepted']);self.assertFalse(r['gpu_visibility_verified']);self.assertFalse(r['headset_verified'])
    def test_xm8_wrong_profile_asset_or_promoted_production_rejects(self):
        self.scoped_xm8();self.f.manifest['body_holster_diagnostic_profile']=1
        self.assertFalse(self.f.run()['mechanical_verified'])
        self.f.manifest['body_holster_diagnostic_profile']=2;self.f.probe['asset']='SPAS12_sp'
        self.assertFalse(self.f.run()['mechanical_verified'])
        self.f.probe['asset']='XM8_sp_s';self.f.manifest['body_holster_profile_mask']=2
        self.assertFalse(self.f.run()['mechanical_verified'])
    def test_xm8_restored_action_gate_must_be_explicitly_clear(self):
        self.scoped_xm8();self.f.probe['rows'][-1]['blocks_actions']=True
        self.assertFalse(self.f.run()['mechanical_verified'])
        self.f.probe['rows'][-1].pop('blocks_actions')
        self.assertFalse(self.f.run()['mechanical_verified'])
    def test_xm8_count_change_idle_or_preflight_asset_rejects(self):
        self.scoped_xm8();self.assertEqual(validate(self.f.before,'XM8_sp_s')['asset'],'XM8_sp_s')
        with self.assertRaises(ValueError):validate(self.f.before)
        with self.assertRaises(ValueError):validate(self.f.before,'invented')
        self.f.after['samples'][0]['state']['loaded']+=1
        self.assertFalse(self.f.run()['sections']['native_ammo_counts']['passed'])
        self.f.after=copy.deepcopy(self.f.before);self.f.after['samples'][0]['state']['current']=9
        self.assertFalse(self.f.run()['sections']['native_ammo_counts']['passed'])
    def test_any_timeout_remains_failure(self):
        self.f.receiver['async_timeouts']=1;self.assertFalse(self.f.run()['mechanical_verified'])
    def test_same_claim_after_draw_rejects(self):
        self.f.probe['rows'][-1]['right_claim']=1;self.assertFalse(self.f.run()['sections']['actual_consumer_lifecycle']['passed'])
    def test_queued_free_pose_or_missing_pack_rejects(self):
        self.f.probe['rows'][1]['right_claim']=1;self.assertFalse(self.f.run()['mechanical_verified'])
        self.f.probe['rows'][1]['right_claim']=0;self.f.trace['gameplay']['rig_publication']['free_right_paired_copies']=0;self.assertFalse(self.f.run()['mechanical_verified'])
    def test_no_challenge_or_one_uncleared_bit_rejects(self):
        c=self.f.probe['challenge'];c['observed'][3]=0x20;self.assertFalse(self.f.run()['sections']['native_cache_challenge']['passed']);self.f.probe['challenge']=None;self.assertFalse(self.f.run()['mechanical_verified'])
    def test_count_change_owner_and_unsupported_asset(self):
        self.f.after['samples'][0]['state']['loaded']=6;self.assertFalse(self.f.run()['sections']['native_ammo_counts']['passed'])
        self.f.after=copy.deepcopy(self.f.before);self.f.after['samples'][0]['asset_name']='XM8_sp_s'
        with self.assertRaises(ValueError):validate(self.f.after)
    def test_missing_eye_or_wrong_byte_count(self):
        (self.path/'pair-24-eye-0.rgba').write_bytes(b'bad');r=self.f.run();self.assertTrue(r['mechanical_verified']);self.assertFalse(r['visual_review_ready'])
    def test_source_restamp_and_source_mutation(self):
        row=copy.deepcopy(self.f.probe['rows'][1]);row['observed_ns']-=1;self.f.probe['rows'].insert(2,row);self.assertFalse(self.f.run()['mechanical_verified'])
        self.f.probe['rows'].pop(2);self.f.trace['gameplay']['rig_publication']['source_changes']=1;self.assertFalse(self.f.run()['mechanical_verified'])
if __name__=='__main__':unittest.main()
