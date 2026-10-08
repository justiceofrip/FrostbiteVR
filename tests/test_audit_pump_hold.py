"""Synthetic recorder-schema cases; none of these are native acceptance evidence."""
import copy,json,struct,subprocess,sys,unittest,uuid
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from audit_pump_hold import audit,EXE_SHA

N=1_000_000_000
def fixture():
    own=dict(player=0x10000,soldier=0x20000,weak=0x30000,weapon=0x40000,actor_generation=2,equip_generation=3,space=4)
    server=dict(server_player=0x50000,server_soldier=0x60000,server_item=0x70000,server_firing=0xa0000)
    firing=[0x80000,0x90000,0xa0000]
    def preflight(loaded,t):
        s=dict(monotonic_ns=t,client_owner=dict(player=own['player'],actor=own['soldier'],weak=own['weak'],selected_weapon=own['weapon']),
            **server,asset_name='SPAS12_sp',identity_coherent=True,state=dict(current=2,next=2,loaded=loaded,reserve=24))
        a=copy.deepcopy(s);a['monotonic_ns']-=30_000_000
        return dict(passed=True,read_only=True,process_writes=False,native_calls=False,pid=1234,executable_sha256=EXE_SHA,samples=[a,s],rejected=[])
    def state(branch,loaded,cur=2,prev=1,nxt=2,timer=0):
        return dict(**own,**{k:v for k,v in server.items() if k!='server_firing'},firing=firing[branch],branch=branch,
            wrapper_offset=[0x3c,0x40,0x10][branch],current=cur,previous=prev,next=nxt,timer=timer,loaded=loaded,reserve=24,flags_a8=1)
    def context(flags=0):
        raw=bytearray(48);struct.pack_into('<f',raw,24,.005);struct.pack_into('<f',raw,32,1);struct.pack_into('<I',raw,44,flags)
        return dict(bytes=list(raw),decoded_valid=True,delta_seconds=.005,reload_multiplier=1,input_flags=flags)
    rows=[]
    def row(n,t,pre,post=None,kind=0,flags=0,held=False):
        cb=context(flags);ca=copy.deepcopy(cb);ca['bytes'][20]=27 # Native-owned non-delta output is intentionally different.
        r=dict(id=len(rows)+1,kind=kind,before=pre,after=copy.deepcopy(pre if post is None else post),
            begin_ns=t,end_ns=t+200_000,begin_tick_ms=100000+round((t-10*N)/1_000_000),
            finished=True,identity_retained=True,context_before=cb,context_after=ca,hold_requested=held,
            hold_applied=held,hold_restored=held,hold_original_delta_bits=struct.unpack('<I',struct.pack('<f',.005))[0] if held else 0,
            hold_before_restore_bits=0,hold_unexpected_native_write=False)
        rows.append(r)
    for n in range(3):
        row(n,12*N+n*100_000,state(n,8))
        row(n,13*N+50_000_000+n*100_000,state(n,8),state(n,7,7,6,8,.5),flags=1)
        # First arming call entry legitimately precedes the policy begin.
        row(n,13*N+299_900_000+n*500_000,state(n,7,7,6,8,.2),held=True)
        row(n,13*N+640_000_000+n*500_000,state(n,7,7,6,8,.2),held=True)
        row(n,13*N+860_000_000+n*100_000,state(n,7,7,6,8,0),state(n,7,8,7,8,0),kind=1)
        row(n,13*N+861_000_000+n*100_000,state(n,7,8,7,1,0),state(n,7,1,8,1,0),kind=1)
        row(n,13*N+862_000_000+n*100_000,state(n,7,1,8,2,0),state(n,7,2,1,2,0),kind=1)
        row(n,16*N+50_000_000+n*100_000,state(n,7),state(n,6,7,6,8,.5),flags=1)
        row(n,17*N+n*100_000,state(n,6))
    hold=dict(enabled=True,code_verified=True,target=1,phase=3,reason=1,begin_ns=13*N+300_000_000,deadline_ns=13*N+650_000_000,
        duration_ns=350_000_000,client_weapon=own['weapon'],**{k:v for k,v in server.items() if k!='server_firing'},
        firing=firing,loaded=7,reserve=24,applied=[2,2,2],restored=[2,2,2],original_calls=6,patch_failures=0,restore_failures=0)
    flow=dict(drained=True,in_flight=0,diagnostic_hold=hold,records=rows,owner_misses=15,
        **{k:0 for k in ('owner_lock_drops','record_lock_drops','dropped','rejected','window_expired_calls','read_misses','server_read_misses','context_misses','nesting_misses')})
    return dict(pid=1234,hooks_disabled=True,gameplay=dict(reload_flow=flow)),dict(pump_hold_fixture=True,consumed_pairs=240,async_timeouts=0,pump_fire_input_samples=10,controls_started_ms=100000),preflight(8,10*N),preflight(6,18*N)

class AuditTests(unittest.TestCase):
    def setUp(self):
        self.args=fixture();self.t,self.receiver,self.pre,self.post=self.args;self.f=self.t['gameplay']['reload_flow'];self.h=self.f['diagnostic_hold']
    def result(self):return audit(*self.args)
    def held(self):return next(r for r in self.f['records'] if r['hold_applied'])
    def assertRejected(self):self.assertFalse(self.result()['passed'])
    def test_valid_three_copy_hold_and_native_resume(self):
        r=self.result();self.assertTrue(r['passed'],r);self.assertEqual(r['details']['held_rows_per_branch'],[2,2,2]);self.assertFalse(r['headset_verified']);self.assertFalse(r['normal_manual_pump_admitted'])
    def test_original_non_delta_context_output_not_overwritten(self):
        self.assertNotEqual(self.held()['context_before']['bytes'][20],self.held()['context_after']['bytes'][20]);self.assertTrue(self.result()['passed'])
    def test_arming_entry_before_policy_timestamp_is_allowed(self):
        self.assertLess(self.held()['begin_ns'],self.h['begin_ns']);self.assertTrue(self.result()['passed'])
    def test_old_record_cannot_borrow_later_hold(self):
        r=self.held();r['begin_ns']-=N;r['end_ns']-=N;self.assertRejected()
    def test_wrong_prev_next_or_branch_rejected(self):
        for key,val in [('previous',5),('next',9),('branch',3),('firing',0xb0000),('wrapper_offset',0x44)]:
            with self.subTest(key=key):
                args=fixture();r=next(r for r in args[0]['gameplay']['reload_flow']['records'] if r['hold_applied']);r['before'][key]=val;self.assertFalse(audit(*args)['passed'])
    def test_held_state_mutation_rejected(self):
        for key,val in [('timer',.19),('loaded',6),('reserve',25),('space',5),('server_item',0xb0000)]:
            with self.subTest(key=key):
                args=fixture();r=next(r for r in args[0]['gameplay']['reload_flow']['records'] if r['hold_applied']);r['after'][key]=val;self.assertFalse(audit(*args)['passed'])
    def test_delta_restore_wrong_bytes_rejected(self):
        self.held()['context_after']['bytes'][24]^=1;self.assertRejected()
    def test_safe_printed_delta_cannot_hide_nan_bytes(self):
        r=self.held();r['context_before']['bytes'][24:28]=list(struct.pack('<f',float('nan')));self.assertRejected()
    def test_unsafe_flags_rejected(self):
        r=self.held();r['context_before']['bytes'][40]=1;self.assertRejected()
    def test_unsafe_native_firing_flag_rejected(self):
        r=self.held();r['before']['flags_a8']=r['after']['flags_a8']=9;self.assertRejected()
    def test_nonzero_before_restore_or_restore_failure_rejected(self):
        self.held()['hold_before_restore_bits']=1;self.assertRejected()
    def test_hold_must_be_original_once_and_all_three(self):
        self.h['original_calls']+=1;self.assertRejected();self.assertFalse(self.result()['mechanical_observations_verified'])
    def test_coherent_extra_counter_with_missing_record_is_incomplete(self):
        self.h['applied'][0]+=1;self.h['restored'][0]+=1;self.h['original_calls']+=1
        r=self.result();self.assertTrue(r['mechanical_observations_verified']);self.assertFalse(r['observer_complete']);self.assertFalse(r['passed'])
    def test_native_patch_or_restore_failure_is_mechanical_failure(self):
        for key in ('patch_failures','restore_failures'):
            with self.subTest(key=key):
                args=fixture();args[0]['gameplay']['reload_flow']['diagnostic_hold'][key]=1;r=audit(*args);self.assertFalse(r['mechanical_observations_verified']);self.assertFalse(r['passed'])
    def test_missing_native_error_counter_is_not_zero(self):
        del self.h['restore_failures'];self.assertFalse(self.result()['mechanical_observations_verified'])
    def test_firing_objects_are_x86_addresses(self):
        self.h['firing'][0]=0x100000000
        for r in self.f['records']:
            if r['before']['branch']==0:r['before']['firing']=r['after']['firing']=0x100000000
        self.assertFalse(self.result()['checks']['three_exact_firing_objects']);self.assertRejected()
    def test_missing_branch_receipts_rejected(self):
        self.f['records']=[r for r in self.f['records'] if r['before']['branch']!=1];self.assertRejected()
    def test_abort_instead_of_expiry_rejected(self):
        self.h['phase']=4;self.h['reason']=4;self.assertRejected()
    def test_missing_resume_transition_is_not_assumed(self):
        self.f['records']=[r for r in self.f['records'] if not (r['before']['branch']==2 and r['before']['current']==8)]
        # Remove the only remaining boundary that directly sees state1.
        for r in self.f['records']:
            if r['before']['branch']==2 and r['before']['current']==1:r['before']['current']=2
        self.assertRejected()
    def test_wrong_native_owner_even_same_counts_rejected(self):
        self.held()['before']['equip_generation']+=1;self.assertRejected()
    def test_preflight_owner_change_rejected(self):
        self.post['samples'][1]['client_owner']['selected_weapon']+=4;self.assertRejected()
    def test_bad_executable_rejected(self):
        self.post['executable_sha256']='0'*64;self.assertRejected()
    def test_native_trace_wrong_process_rejected(self):
        self.t['pid']=2345;self.assertRejected()
    def test_missing_or_invalid_process_rejected(self):
        for pid in (None,0,-1,True,'1234'):
            with self.subTest(pid=pid):
                args=fixture();args[0]['pid']=args[2]['pid']=args[3]['pid']=pid;self.assertFalse(audit(*args)['passed'])
    def test_extra_or_reserve_ammo_change_rejected(self):
        self.post['samples'][0]['state']['reserve']=self.post['samples'][1]['state']['reserve']=23;self.assertRejected()
    def test_fire_delta_without_ordinary_trigger_rejected(self):
        r=next(r for r in self.f['records'] if r['before']['loaded']!=r['after']['loaded']);r['context_before']['bytes'][44]=0;r['context_before']['input_flags']=0;self.assertRejected()
    def test_shot_outside_prescribed_window_rejected(self):
        r=next(r for r in self.f['records'] if r['before']['loaded']!=r['after']['loaded']);r['begin_tick_ms']+=300;self.assertRejected()
    def test_extra_count_decrement_rejected(self):
        r=copy.deepcopy(self.f['records'][1]);r['id']=100;self.f['records'].append(r);self.assertRejected()
    def test_unretained_after_is_not_authority(self):
        self.f['records'][-1]['identity_retained']=False;r=self.result();self.assertFalse(r['passed']);self.assertFalse(r['checks']['each_copy_has_before_and_final_idle_counts']);self.assertFalse(r['observer_complete'])
    def test_gaps_separate_observed_success_from_completeness(self):
        for key in ('owner_lock_drops','record_lock_drops','dropped','rejected','window_expired_calls','read_misses','server_read_misses','context_misses','nesting_misses'):
            with self.subTest(key=key):
                args=fixture();args[0]['gameplay']['reload_flow'][key]=1;r=audit(*args);self.assertTrue(r['mechanical_observations_verified']);self.assertFalse(r['observer_complete']);self.assertFalse(r['passed'])
    def test_missing_counter_is_incomplete(self):
        del self.f['record_lock_drops'];self.assertFalse(self.result()['observer_complete'])
    def test_reload_transfer_rejected(self):
        r=copy.deepcopy(self.f['records'][0]);r['id']=100;r['kind']=2;self.f['records'].append(r);self.assertRejected()
    def test_unclean_shutdown_rejected(self):
        self.t['hooks_disabled']=False;self.assertRejected()
    def test_hold_wrong_duration_rejected(self):
        self.h['deadline_ns']+=1;self.assertRejected()
    def test_only_initial_hold_cannot_prove_full_interval(self):
        self.f['records']=[r for r in self.f['records'] if not (r['hold_applied'] and r['begin_ns']>self.h['begin_ns']+50_000_000)]
        self.h['applied']=self.h['restored']=[1,1,1];self.h['original_calls']=3;self.assertFalse(self.result()['checks']['three_copy_hold_covers_bounded_interval'])
    def test_timer_decrement_between_exact_restores_rejected(self):
        r=next(r for r in self.f['records'] if r['hold_applied'] and r['begin_ns']>self.h['begin_ns']+50_000_000)
        r['before']['timer']=r['after']['timer']=.19;self.assertRejected()
    def test_unheld_selected_update_inside_hold_rejected(self):
        r=copy.deepcopy(self.held());r.update(id=100,begin_ns=self.h['begin_ns']+100_000_000,end_ns=self.h['begin_ns']+100_200_000,hold_applied=False,hold_requested=False,hold_restored=False)
        self.f['records'].append(r);self.assertRejected()
    def test_server_object_change_cannot_borrow_pinned_client_owner(self):
        r=self.f['records'][-1];r['before']['server_item']=r['after']['server_item']=0xc0000;self.assertRejected()
    def test_duplicate_native_record_id_rejected(self):
        self.f['records'][-1]['id']=self.f['records'][0]['id'];self.assertRejected()
    def test_observation_after_postflight_cannot_be_used(self):
        self.f['records'][-1]['end_ns']=19*N;self.assertRejected()
    def test_empty_input_rejected(self):
        self.assertFalse(audit({},{},{},{})['passed'])
    def test_cli_actual_files_hash_all_inputs(self):
        root=Path(__file__).resolve().parent;p=root/('test-cli-'+uuid.uuid4().hex);p.mkdir()
        names=['trace.json','result.json','pump-hold-preflight.json','pump-hold-postflight.json']
        try:
            for name,data in zip(names,self.args):(p/name).write_text(json.dumps(data))
            r=subprocess.run([sys.executable,'-B',str(Path(__file__).resolve().parents[1]/'tools'/'audit_pump_hold.py'),'--trace',str(p/names[0]),'--receiver',str(p),'--output',str(p/'audit.json')],capture_output=True,text=True)
            self.assertEqual(r.returncode,0,r.stderr+r.stdout);out=json.loads((p/'audit.json').read_text());self.assertEqual(len(out['sources']),4);self.assertTrue(all(len(x['sha256'])==64 for x in out['sources']))
        finally:
            self.assertEqual(p.resolve().parent,root)
            for name in names+['audit.json']:(p/name).unlink(missing_ok=True)
            p.rmdir()

if __name__=='__main__':unittest.main(verbosity=2)
