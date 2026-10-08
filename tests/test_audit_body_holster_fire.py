import copy,sys,unittest
from pathlib import Path
sys.dont_write_bytecode=True
sys.path.insert(0,str(Path(__file__).parents[1]/'tools'))
from test_audit_body_holster import Tests as BaseTests,Fixture
from audit_body_holster_fire import firing_evidence
class FireTests(BaseTests):
    def fire(self):
        self.f=Fixture(self.path);self.scoped_xm8();p=self.f.probe;g=self.f.trace['gameplay']
        self.f.manifest.update(body_holster_diagnostic_profile=3,body_holster_fire_probe=True);p['diagnostic_profile']=3
        p['post_draw_fire']=dict(requested=True,shot_verified=False,restored_claim=2,pulse_start_ns=5_000_000_000,pulse_end_ns=5_080_000_000,first_commit_ms=5000,last_commit_ms=5070,fire_cache_commits=8,release_cache_commits=50)
        for phase,stamp in [(13,5_010_000_000),(14,5_090_000_000),(11,5_600_000_000)]:
            row=copy.deepcopy(p['rows'][-1]);row.update(phase=phase,now_ns=stamp,observed_ns=stamp,deadline_ns=stamp+100_000_000,input=phase+10,
                consumed_trigger=1 if phase==13 else 0,fire_requested=phase==13,fire_cache_read=True,fire_cache=1 if phase==13 else 0)
            # Done is later than the appended fire phase enum values.
            if phase==11:row['input']=30
            p['rows'].append(row)
        for s in self.f.after['samples']:s['state']['loaded']=6
        records=[]
        for n in range(3):
            for stamp,count in [(4_950_000_000,7),(5_300_000_000,6)]:
                state={k:p[v] for k,v in [('player','player'),('soldier','soldier'),('weak','weak'),('weapon','weapon'),('actor_generation','actor_generation'),('equip_generation','native_equip_generation'),('space','space')]}
                state.update(branch=n,current=2,next=2,loaded=count,reserve=8,firing=4,server_player=1,server_soldier=2,server_item=3)
                records.append(dict(id=len(records)+1,begin_ns=stamp,end_ns=stamp+1000,before=state,after=copy.deepcopy(state),finished=True,identity_retained=True,transfer_path=None,hold_requested=False,hold_applied=False))
        g['reload_flow']=dict(started=True,drained=True,server_binding_verified=True,native_state_writes=False,record_lock_drops=0,dropped=0,records=records)
        g['fire_origin_observation']=dict(origin_write_failures=0,shot_source_changes=0,records=[dict(phase=2,ms=5010,weapon=p['weapon'],client_soldier=p['soldier'],weapon_name='XM8_sp_s',token_valid=True,shot_token=10,origin_written=True,tracked_shot_candidate=dict(event_generation=64,event_muzzle=[1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1]),client_path=client,server_player=1,server_soldier=2) for client in (True,False)])
        return self.f.run()
    def test_fire_pass_binds_native_receipts(self):
        r=self.fire();self.assertTrue(r['mechanical_verified'],r);e=r['sections']['native_ammo_counts']['evidence'];self.assertTrue(e['shot_verified']);self.assertTrue(e['native_observation_complete']);self.assertEqual(e['native_round_decrease'],1)
    def test_no_shot_cache_only_fails(self):
        self.fire();self.f.trace['gameplay']['fire_origin_observation']['records']=[];self.assertFalse(self.f.run()['mechanical_verified'])
    def test_missing_authoritative_server_shot_fails(self):
        self.fire();self.f.trace['gameplay']['fire_origin_observation']['records'].pop();self.assertFalse(self.f.run()['mechanical_verified'])
    def test_wrong_shot_owner_or_outside_window_fails(self):
        for field,value in [('weapon',7),('ms',4900),('token_valid',False),('origin_written',False)]:
            self.fire();self.f.trace['gameplay']['fire_origin_observation']['records'][0][field]=value;self.assertFalse(self.f.run()['mechanical_verified'])
    def test_cross_path_token_must_match_but_zero_is_valid(self):
        self.fire();rows=self.f.trace['gameplay']['fire_origin_observation']['records'];rows[0]['shot_token']=11
        self.assertFalse(self.f.run()['mechanical_verified'])
        for row in rows:row['shot_token']=0
        self.assertTrue(self.f.run()['mechanical_verified'])
    def test_unretained_after_never_supplies_authoritative_boundary(self):
        self.fire();records=self.f.trace['gameplay']['reload_flow']['records']
        row=records[-1];row['before']['current']=3;row['identity_retained']=False
        self.assertFalse(self.f.run()['mechanical_verified'])
    def test_native_count_failures(self):
        for field,value in [('loaded',7),('reserve',9),('current',10)]:
            self.fire()
            for s in self.f.after['samples']:s['state'][field]=value
            self.assertFalse(self.f.run()['mechanical_verified'])
    def test_missing_native_copy_or_transfer_fails(self):
        self.fire();self.f.trace['gameplay']['reload_flow']['records'].pop();self.assertFalse(self.f.run()['mechanical_verified'])
        self.fire();self.f.trace['gameplay']['reload_flow']['records'][0]['transfer_path']=0;self.assertFalse(self.f.run()['mechanical_verified'])
    def test_observer_gap_is_reported_separately(self):
        self.fire();f=self.f.trace['gameplay']['reload_flow'];f['record_lock_drops']=1
        r=self.f.run();self.assertTrue(r['mechanical_verified'],r);self.assertFalse(r['sections']['native_ammo_counts']['evidence']['native_observation_complete'])
    def test_selector_and_missing_release_fail(self):
        self.fire();self.f.manifest['body_holster_fire_probe']=False;self.assertFalse(self.f.run()['mechanical_verified'])
        self.fire();self.f.probe['post_draw_fire']['release_cache_commits']=0;self.assertFalse(self.f.run()['mechanical_verified'])
if __name__=='__main__':unittest.main()
