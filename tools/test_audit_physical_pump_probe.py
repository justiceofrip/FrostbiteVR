"""Synthetic audit mutation tests: no native/game completion evidence is created."""
import copy,struct,unittest
from audit_physical_pump_probe import audit,EXECUTABLE_SHA256,same_client_owner

def fixture():
    sec=1000000000;start=sec;fire=2*sec;request=3*sec;ready=4*sec
    context=list(struct.pack('<12f',0,0,0,0,0,0,.016,0,1,0,0,0))
    records=[];next_id=0
    def snapshot(branch,current=7,previous=6,next_state=8,loaded=7):
        return dict(firing=101+branch,branch=branch,wrapper_offset=(0x18,0x40,0x10)[branch],player=201,soldier=202,weak=203,weapon=204,
                    actor_generation=1,equip_generation=2,space=3,current=current,previous=previous,next=next_state,timer=.2,loaded=loaded,reserve=24,flags_a8=0)
    def record(kind,branch,begin,end,pre,post,parent=None):
        nonlocal next_id
        next_id+=1
        r=dict(id=next_id,kind=kind,finished=True,identity_retained=True,depth=2 if parent else 1,native_invocation=next_id,
               native_update=parent['id'] if parent else next_id,native_parent=parent['id'] if parent else 0,
               parent=parent['id'] if parent else 0,update=parent['id'] if parent else 0,begin_ns=begin,end_ns=end,
               before=pre,after=post,context=301,thread=401,context_before={'bytes':context.copy()},context_after={'bytes':context.copy()},
               hold_applied=False,hold_requested=False,hold_restored=False,hold_unexpected_native_write=False,hold_before_restore_bits=0,
               hold_original_delta_bits=struct.unpack('<I',struct.pack('<f',.016))[0])
        records.append(r);return r
    for branch in range(3):
        # Branches can share a native worker but each has sequential invocation
        # identity on that thread, including its direct commit children.
        record(0,branch,fire+100,fire+200,snapshot(branch,loaded=8),snapshot(branch,6,5,7))
        hold=record(0,branch,fire+300,fire+400,snapshot(branch),snapshot(branch))
        hold.update(hold_applied=True,hold_requested=True,hold_restored=True)
        for n,(old,new) in enumerate(((7,8),(8,1),(1,2))):
            begin=request+1000+n*1000
            pre=snapshot(branch,old,6,new);post=snapshot(branch,new,old,new)
            parent=record(0,branch,begin,begin+500,copy.deepcopy(pre),copy.deepcopy(post))
            record(1,branch,begin+100,begin+200,pre,post,parent)
    for row in records:row['thread']+=row['before']['branch']
    flow=dict(records=records,drained=True,in_flight=0,start_ns=start,window_seconds=20,window_expired_calls=100,
              read_misses=1,server_read_misses=0,observation_read_misses=dict(schema=1,drained=True,count=1,capacity=256,overflow=0,rows=[{'now_ns':start+100}]),
              completion_journal=dict(pending=0,overflow=0,rejected_drain=0))
    for key in ('dropped','rejected','record_lock_drops','record_begin_lock_drops','record_end_lock_drops','owner_lock_drops','context_misses','nesting_misses'):flow[key]=0
    flow['native_cycle_candidate']=dict(enabled=True,admitted=False,headset_verified=False,releases=1,acknowledgements=1,held=[1,1,1])
    cycle=dict(cycle=1,shot=1,request=1,source_sequence=10,requested_ns=request,ready_sequence=20,ready_ns=ready,ready_deadline_ns=ready+50000000,
               loaded_after_shot=7,reserve=24,capacity=8,verified_pairs=5,rear_pair=True,firing=[101,102,103])
    driver=dict(requested_cycles=1,completed=True,failure=0,completed_cycles=1,cycles=[cycle],original_raw_matches=30,generated_controller_poses=20,
                transitions=[dict(phase=2,failure=0,now_ns=fire)])
    trace=dict(pid=123,hooks_disabled=True,gameplay=dict(reload_flow=flow,physical_pump_probe=driver,physical_pump=dict(releases=1,acknowledgements=1),
              rig_publication={'pump_presentation':dict(pairs=5,copies=10,source_rejects=0)}))
    receiver=dict(physical_pump_receiver=True,consumed_pairs=240,async_timeouts=0,controls_started_ms=100,physical_pump_exit_vehicle=False)
    def baseline(loaded):
        rows=[]
        for stamp in (1,2):rows.append(dict(monotonic_ns=stamp,client_owner=[1,2,3],server_player=4,server_soldier=5,server_item=6,server_firing=103,
                     asset_name='SPAS12_sp',identity_coherent=True,state=dict(current=2,next=2,loaded=loaded,reserve=24)))
        return dict(pid=123,passed=True,read_only=True,process_writes=False,native_calls=False,executable_sha256=EXECUTABLE_SHA256,samples=rows)
    completion=dict(trace_exit=0,receiver_exit=0,baseline_exit=0,postflight_exit=0,responding=True,game_exited=False)
    inputs=[dict(generation=1,tick_ms=100,hands=[dict(held=0,axes=[0,0,0,0]),dict(held=0,axes=[0,0,0,0])])]
    return [trace,receiver,baseline(8),baseline(7),completion,inputs]

class AuditTests(unittest.TestCase):
    def test_resolved_owner_survives_gameplay_flags_but_not_layout_or_identity_change(self):
        owner=dict(actor=1,player=2,weak=3,inventory=4,selected_slot=0,selected_weapon=5,items=[5,6],flags=3)
        moved=dict(owner,flags=11)
        self.assertTrue(same_client_owner(owner,moved))
        self.assertFalse(same_client_owner(owner,dict(moved,flags=10)))
        for key in ('actor','player','weak','inventory','selected_slot','selected_weapon'):
            self.assertFalse(same_client_owner(owner,dict(moved,**{key:99})))
        self.assertFalse(same_client_owner(owner,dict(moved,items=[5,7])))
    def test_eight_cycles_require_exact_empty_boundary_and_tail(self):
        docs=fixture();game=docs[0]['gameplay'];flow=game['reload_flow'];driver=game['physical_pump_probe']
        original=copy.deepcopy(flow['records']);original_cycle=copy.deepcopy(driver['cycles'][0])
        for index in range(1,8):
            rows=copy.deepcopy(original)
            if index==7:
                # Empty pump bypasses positive-ammunition states7/8 entirely.
                rows=[r for r in rows if (r['before']['current'],r['after']['current'])!=(7,8)]
            for row in rows:
                row['id']+=index*100;row['native_invocation']+=index*100;row['native_update']+=index*100
                if row['native_parent']:row['native_parent']+=index*100
                if row['parent']:row['parent']+=index*100;row['update']+=index*100
                row['begin_ns']+=index*3*10**9;row['end_ns']+=index*3*10**9
                for side in ('before','after'):row[side]['loaded']-=index
                if index==7:
                    if row['hold_applied']:
                        for side in ('before','after'):row[side].update(current=6,previous=5,next=1)
                    elif row['before']['loaded']==1:row['after'].update(current=6,previous=5,next=1)
                    elif row['before']['current']==8:
                        row['before'].update(current=6,previous=5,next=1);row['after'].update(current=1,previous=6,next=1)
            flow['records']+=rows
            cycle=copy.deepcopy(original_cycle);cycle.update(cycle=index+1,shot=index+1,request=index+1,
                requested_ns=(3+index*3)*10**9,ready_ns=(4+index*3)*10**9,loaded_after_shot=7-index)
            driver['cycles'].append(cycle);driver['transitions'].append(dict(phase=2,failure=0,now_ns=(2+index*3)*10**9))
        driver.update(requested_cycles=8,completed_cycles=8);flow['window_seconds']=60
        flow['native_cycle_candidate'].update(releases=8,acknowledgements=8,held=[8,8,8])
        game['physical_pump'].update(releases=8,acknowledgements=8)
        docs[1]['physical_pump_empty_cycle']=True
        docs[3]['in_trial_empty_observation']=True
        for n,row in enumerate(docs[3]['samples']):row['state']['loaded']=0;row['monotonic_ns']=(26*10**9)+n
        result=audit(*docs);self.assertTrue(result['passed'],result)
        for case in range(4):
            bad=copy.deepcopy(docs);records=bad[0]['gameplay']['reload_flow']['records']
            if case==0:bad[1]['physical_pump_empty_cycle']=False
            if case==1:next(r for r in records if r['hold_applied'] and r['after']['loaded']==0)['after']['next']=8
            if case==2:next(r for r in records if r['kind']==1 and r['before']['loaded']==0 and r['before']['current']==6)['after']['current']=8
            if case==3:
                for row in bad[3]['samples']:row['state']['reserve']-=1
            self.assertFalse(audit(*bad)['passed'],case)
    def test_complete_bounded_trial_does_not_claim_whole_recording(self):
        result=audit(*fixture());self.assertTrue(result['passed'],result);self.assertTrue(result['bounded_trial_complete']);self.assertFalse(result['recorder_complete'])
    def test_two_cycles_retain_distinct_shot_and_tail_receipts(self):
        docs=fixture();game=docs[0]['gameplay'];flow=game['reload_flow'];driver=game['physical_pump_probe']
        second=copy.deepcopy(flow['records'])
        for row in second:
            row['id']+=100;row['native_invocation']+=100;row['native_update']+=100
            if row['native_parent']:row['native_parent']+=100
            if row['parent']:row['parent']+=100;row['update']+=100
            row['begin_ns']+=3*10**9;row['end_ns']+=3*10**9
            row['before']['loaded']-=1;row['after']['loaded']-=1
        flow['records']+=second;flow['native_cycle_candidate'].update(releases=2,acknowledgements=2,held=[2,2,2]);game['physical_pump'].update(releases=2,acknowledgements=2)
        cycle=copy.deepcopy(driver['cycles'][0]);cycle.update(cycle=2,shot=2,request=2,requested_ns=6*10**9,ready_ns=7*10**9,loaded_after_shot=6)
        driver.update(requested_cycles=2,completed_cycles=2);driver['cycles'].append(cycle);driver['transitions'].append(dict(phase=2,failure=0,now_ns=5*10**9))
        for row in docs[3]['samples']:row['state']['loaded']=6
        result=audit(*docs);self.assertTrue(result['passed'],result)
        cycle['cycle']=1;self.assertFalse(audit(*docs)['passed'])
    def test_held_support_return_matches_actual_custody_event(self):
        docs=fixture();game=docs[0]['gameplay'];driver=game['physical_pump_probe'];driver['requires_held_support_return']=True
        game['physical_pump']['support_returns']=1;cycle=driver['cycles'][0]
        cycle.update(support_returned=True,support_token=1,support_claim=40,support_source_sequence=31,support_observed_ns=4100000000,support_deadline_ns=4190000000)
        event=dict(serial=5,reason='pump_support_return',support_holding=True,support_token=1,physical_item=204,equip_generation=1,raw_generation=32,now_ns=4100000010,
                   left=dict(id=40,kind=2,item=204,generation=1,prerequisite=20,input_generation=31,deadline_ns=4190000000),right=dict(id=20,kind=1,item=204,generation=1))
        game['hand_ownership']=dict(events_dropped=0,events=[event]);result=audit(*docs);self.assertTrue(result['passed'],result)
        for case in range(6):
            wrong=copy.deepcopy(docs);g=wrong[0]['gameplay'];e=g['hand_ownership']['events'][0]
            if case==0:g['hand_ownership']['events']=[]
            if case==1:e['left']['input_generation']+=1
            if case==2:e['left']['deadline_ns']+=1
            if case==3:e['support_holding']=False
            if case==4:g['physical_pump_probe']['cycles'][0]['support_returned']=False
            if case==5:e['left']['prerequisite']+=1
            self.assertFalse(audit(*wrong)['passed'],case)
    def test_recorder_and_native_invocations_are_distinct_domains(self):
        docs=fixture();flow=docs[0]['gameplay']['reload_flow']
        for row in flow['records']:
            row['native_invocation']+=100;row['native_update']+=100
            if row['native_parent']:row['native_parent']+=100
        result=audit(*docs);self.assertTrue(result['passed'],result)
        flow['record_begin_lock_drops']=1
        result=audit(*docs);self.assertFalse(result['passed'])
        self.assertTrue(result['checks']['each_real_shot_and_direct_native_tail'])
        self.assertFalse(result['checks']['bounded_trial_recording_complete'])
    def test_overall_complete_is_possible_without_drain_gaps(self):
        docs=fixture();flow=docs[0]['gameplay']['reload_flow'];flow.update(window_expired_calls=0,read_misses=0)
        flow['observation_read_misses'].update(count=0,rows=[])
        result=audit(*docs);self.assertTrue(result['passed'],result);self.assertTrue(result['recorder_complete'])
    def test_trial_bounds_and_miss_journal_mutations(self):
        for case in range(12):
            docs=fixture();flow=docs[0]['gameplay']['reload_flow'];journal=flow['observation_read_misses'];driver=docs[0]['gameplay']['physical_pump_probe']
            if case==0:flow['start_ns']=3*10**9
            if case==1:flow['window_seconds']=3
            if case==2:driver['transitions']=[]
            if case==3:journal['rows'][0]['now_ns']=3*10**9
            if case==4:journal['overflow']=1
            if case==5:journal['count']=2
            if case==6:journal['drained']=False
            if case==7:flow['record_begin_lock_drops']=1
            if case==8:flow['completion_journal']['pending']=1
            if case==9:flow['read_misses']=2
            if case==10:journal['rows'][0]['now_ns']=0
            if case==11:flow['context_misses']=1
            result=audit(*docs);self.assertFalse(result['passed'],case);self.assertFalse(result['bounded_trial_complete'],case)
    def test_native_identity_hold_and_parent_mutations(self):
        for case in range(12):
            docs=fixture();records=docs[0]['gameplay']['reload_flow']['records'];shot,hold,parent,commit=records[:4]
            if case==0:shot['after']['loaded']=8
            if case==1:shot['after'].update(current=2,previous=2,next=2)
            if case==2:shot['after']['weapon']+=1
            if case==3:hold['context_after']['bytes'][24]=0
            if case==4:hold['hold_restored']=False
            if case==5:hold['before'].pop('firing')
            if case==6:commit['native_parent']=99999
            if case==7:commit['begin_ns']=parent['end_ns']+1
            if case==8:commit['after']['next']=7
            if case==9:commit['context_after']['bytes'][20]=99
            if case==10:commit['before']['current']=2;commit['after']['previous']=2
            if case==11:commit['native_invocation']=9999
            self.assertFalse(audit(*docs)['passed'],case)
    def test_external_counts_receiver_and_finite_completion(self):
        for case in range(8):
            docs=fixture()
            if case==0:docs[1]['consumed_pairs']=257
            if case==1:docs[2]['executable_sha256']='wrong'
            if case==2:docs[3]['samples'][1]['state']['loaded']=6
            if case==3:docs[4]['responding']=False
            if case==4:docs[5][0]['hands'][1]['held']=1
            if case==5:docs[5][0]['hands'][0]['axes']=[0,0,1,0]
            if case==6:docs[0]['gameplay']['physical_pump_probe']['completed']=False
            if case==7:docs[0]['gameplay']['physical_pump_probe']['requested_cycles']=3
            self.assertFalse(audit(*docs)['passed'],case)
    def test_malformed_evidence_fails_closed_without_crash(self):
        for slot,bad in ((0,None),(1,[]),(2,'x'),(4,42),(5,{})):
            docs=fixture();docs[slot]=bad;self.assertFalse(audit(*docs)['passed'])
        for field,bad in (('cycles',None),('cycles',[{}]),('transitions',[None]),('requested_cycles',{})):
            docs=fixture();docs[0]['gameplay']['physical_pump_probe'][field]=bad;self.assertFalse(audit(*docs)['passed'])

if __name__=='__main__':unittest.main()
