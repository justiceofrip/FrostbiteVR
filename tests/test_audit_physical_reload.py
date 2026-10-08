import copy,json,sys,unittest
from pathlib import Path
sys.path.insert(0,str(Path(__file__).parents[1]/'tools'))
from audit_physical_reload import audit,unique_object

def fixture():
    identity=dict(player=65536,soldier=131072,weak=196608,weapon=262144,actor_generation=5,equip_generation=3,
                  space=7,firing=[327680,393216,458752],server_player=524288,server_soldier=589824,server_item=655360)
    def lease(seq,ns,loaded,reserve):return dict(identity=copy.deepcopy(identity),cycle=1,sequence=seq,observed_ns=ns,
            deadline_ns=ns+100_000_000,loaded=loaded,reserve=reserve,capacity=8,verified=True,all_three_held=True)
    def inp(seq,ns):return dict(sequence=seq,observed_ns=ns,deadline_ns=ns+100_000_000,
            actor=(identity['weak']<<32)|identity['soldier'],actor_generation=5,equip_generation=17,space=7)
    t=dict(item_id=1000,item_generation=1,claim=2,weapon=identity['weapon'],physical_equip_generation=17,
           pool=identity['server_item'],pool_generation=3,seat=1,request=1,cycle=1,source_sequence=100114,
           started_ns=1_260_000_000,units=1,reserve_before=8,operation=3,submitted_ns=1_260_000_000,resolved_ns=1_360_000_000,
           resolved=True,original_input=inp(13,1_240_000_000),current_input=inp(14,1_260_000_000),
           before=lease(114,1_260_000_000,2,8),after=lease(119,1_360_000_000,3,7),reserve_after=lease(100119,1_360_000_000,3,7))
    t['ack']=dict(identity=copy.deepcopy(identity),cycle=1,request=1,sample_sequence=119,server_invocation=200,
                  observed_ns=1_340_000_000,deadline_ns=1_440_000_000,operation=3,status=1,verified=True)
    events=[]
    for event,ns,reason in [(1,1_020_000_000,0),(2,1_020_000_000,0),(10,1_080_000_000,1),(10,1_260_000_000,2),(3,1_260_000_000,0),(4,1_360_000_000,0)]:
        events.append(dict(event=event,now_ns=ns,reason=reason,cycle=0 if event==1 else 1,item_id=1000,
                           item_generation=1,claim=2,request=1 if event==3 else 0,
                           seat=1 if event==10 and reason==2 else 0,
                           source_sequence=13 if event==10 and reason==2 else 0))
    consumer=dict(enabled=True,consumer_code_integrated=True,submitted=1,completed=1,dropped=0,transaction_dropped=0,
                  events=events,transactions=[t])
    rows=[dict(phase=phase,reason=0,now_ns=ns,input=seq,raw=seq-1,position_error_m=.002,angle_error_rad=.01,rail_m=rail)
          for phase,ns,seq,rail in [(3,1_060_000_000,4,-.09),(4,1_160_000_000,9,-.02),(5,1_250_000_000,13,.05)]]
    probe=dict(enabled=True,synthetic_input=True,headset_verified=False,phase=7,failure=0,actual_consumer_completed=True,
               submitted=1,completed=1,loaded_before=2,reserve_before=8,rows=rows)
    records=[]
    for branch in range(3):
        b={k:identity[k] for k in ('player','soldier','weak','weapon','actor_generation','equip_generation','space')}
        b.update(branch=branch,firing=identity['firing'][branch],loaded=2,reserve=8)
        records.append(dict(kind=0,native_invocation=100+branch,begin_ns=1_200_000_000,before=b,
                            hold_applied=True,hold_restored=True,hold_unexpected_native_write=False))
    b=copy.deepcopy(records[-1]['before']);b.update({k:identity[k] for k in ('server_player','server_soldier','server_item')})
    a=copy.deepcopy(b);a.update(loaded=3,reserve=7)
    records.append(dict(kind=2,native_invocation=200,transfer_path=0,finished=True,identity_retained=True,
                        begin_ns=1_280_000_000,end_ns=1_290_000_000,before=b,after=a))
    flow=dict(drained=True,dropped=0,records=records,diagnostic_hold=dict(applied=[1,1,1],restored=[1,1,1],patch_failures=0,restore_failures=0),
              request_cycle=dict(phase=6,failure=9,read_failures=0,contention=0,cancellations=[dict(now_ns=1_400_000_000,reason=6)]))
    palette=dict(verified_private_packs=[4,4,0],verified_paired_private_packs=[2,2,0],verified_fallback_packs=2,
                 verified_paired_fallback_packs=1,native_animation_written=False,native_hidden_contacts=5,owned_shell_visibility_poses=2)
    rig=dict(reload_presentation=palette,source_changes=0,packing_failures=0,native_animation_written=False)
    trace=dict(pid=123,hooks_disabled=True,gameplay=dict(physical_reload=consumer,physical_reload_probe=probe,reload_flow=flow,rig_publication=rig),
               native_stream=dict(published=240,consumed=240,captured_eyes=480,camera_restore_failures=0,gpu_failure_code=0))
    manifest=dict(pid=123,physical_reload=True,physical_reload_probe=True,reload_request_probe=False,duration_ms=30000,until_host_exit=False,
                  game_sha256='a'*64,probe_sha256='b'*64,bootstrap_sha256='c'*64)
    completion=dict(bootstrap_exit=0,game_exited=False,new_crash_report=False,game_responding=True,stability_observation_ms=15000)
    receiver=dict(physical_reload_fixture=True,headset_tested=False,consumed_pairs=240,native_tracking_transport_verified=1,async_timeouts=0)
    pairs=[dict(pair=n,native_frame=n+1000,tracking=n+100) for n in range(240)]
    return trace,manifest,completion,receiver,pairs

def two_shell_fixture():
    f=fixture();g=f[0]['gameplay'];c=g['physical_reload'];p=g['physical_reload_probe'];flow=g['reload_flow']
    t=copy.deepcopy(c['transactions'][0]);t.update(item_generation=2,claim=3,seat=2,request=2,reserve_before=7)
    for key in ('started_ns','submitted_ns','resolved_ns'):t[key]+=1_000_000_000
    t['source_sequence']+=100
    for key in ('original_input','current_input','before','after','ack','reserve_after'):
        row=t[key];row['observed_ns']+=1_000_000_000;row['deadline_ns']+=1_000_000_000
        if 'sequence' in row:row['sequence']+=100
        if 'loaded' in row:row['loaded']+=1;row['reserve']-=1
    t['ack'].update(request=2,sample_sequence=219,server_invocation=1200)
    c['transactions'].append(t);c.update(acquired=2,cycles=1,submitted=2,completed=2)
    second=[]
    for row in c['events']:
        if row['event']==2:continue
        row=copy.deepcopy(row);row.update(cycle=1,item_generation=2,claim=3);row['now_ns']+=1_000_000_000
        if row['event']==3:row['request']=2
        if row['event']==10 and row['reason']==2:row['seat']=2;row['source_sequence']+=100
        second.append(row)
    c['events']+=second
    extra=copy.deepcopy(flow['records'])
    for row in extra:
        row['native_invocation']+=1000;row['begin_ns']+=1_000_000_000
        if 'end_ns' in row:row['end_ns']+=1_000_000_000
        for key in ('before','after'):
            if key in row:row[key]['loaded']+=1;row[key]['reserve']-=1
    flow['records']+=extra;flow['diagnostic_hold'].update(applied=[2,2,2],restored=[2,2,2])
    flow['request_cycle']['cancellations'][0]['now_ns']+=1_000_000_000
    extra=copy.deepcopy(p['rows'])
    for row in extra:row['now_ns']+=1_000_000_000;row['input']+=100;row['raw']+=100
    p['rows']+=extra;p.update(submitted=2,completed=2,requested_rounds=2,
        rounds=[dict(number=1,acquired=1,completed_ns=1_360_000_100,grab_input=2,first_neutral_input=0,last_neutral_input=0,loaded=3,reserve=7),
                dict(number=2,acquired=2,completed_ns=2_360_000_100,grab_input=26,first_neutral_input=20,last_neutral_input=25,loaded=4,reserve=6)])
    f[1].update(physical_reload_repeat_probe=True,physical_reload_requested_rounds=2)
    return f

class AuditTests(unittest.TestCase):
    def test_current_authored_approach_and_old_trajectory_rejected(self):
        self.assertTrue(audit(*two_shell_fixture())['sections']['authored_raw_motion']['passed'])
        old=two_shell_fixture()
        for row in old[0]['gameplay']['physical_reload_probe']['rows']:
            if row['rail_m']==-.09:row['rail_m']=-.06
        self.assertFalse(audit(*old)['sections']['authored_raw_motion']['passed'])
    def test_current_approach_does_not_bypass_contact_proof(self):
        for fault in range(4):
            f=two_shell_fixture();rows=f[0]['gameplay']['physical_reload_probe']['rows']
            if fault==0:
                for row in rows:row['position_error_m']=.1;row['angle_error_rad']=.3
            if fault==1:
                for row in rows:row['raw']=0
            if fault==2:rows[0]['rail_m']=-.11
            if fault==3:
                for row in rows:
                    if row['rail_m']==-.02:row['rail_m']=0
            self.assertFalse(audit(*f)['sections']['authored_raw_motion']['passed'],fault)

    def test_two_shells_one_cycle_distinct_native_receipts(self):
        r=audit(*two_shell_fixture());self.assertTrue(r['passed'],r)
        self.assertEqual(r['requested_rounds'],2);self.assertFalse(r['headset_verified'])
        self.assertEqual([v['server_invocation'] for v in r['sections']['native_receipt']['evidence']['rounds']],[200,1200])
    def test_repeat_mode_requires_explicit_consistent_declarations(self):
        for mode in range(8):
            f=two_shell_fixture();m=f[1];p=f[0]['gameplay']['physical_reload_probe']
            if mode==0:m.pop('physical_reload_repeat_probe')
            if mode==1:m.pop('physical_reload_requested_rounds')
            if mode==2:p.pop('requested_rounds')
            if mode==3:m['physical_reload_repeat_probe']=False
            if mode==4:m['physical_reload_requested_rounds']=1
            if mode==5:p['requested_rounds']=True
            if mode==6:m['physical_reload_repeat_probe']=1
            if mode==7:p['requested_rounds']=3
            self.assertFalse(audit(*f)['actual_native_consumer_verified'],mode)
        f=fixture();f[1].update(physical_reload_repeat_probe=False,physical_reload_requested_rounds=1)
        f[0]['gameplay']['physical_reload_probe']['requested_rounds']=1
        self.assertTrue(audit(*f)['passed'])
        for missing in ('physical_reload_repeat_probe','physical_reload_requested_rounds','requested_rounds'):
            changed=copy.deepcopy(f)
            (changed[0]['gameplay']['physical_reload_probe'] if missing=='requested_rounds' else changed[1]).pop(missing)
            self.assertFalse(audit(*changed)['actual_native_consumer_verified'],missing)
    def test_repeated_receipt_or_claim_cannot_count_twice(self):
        for key in ('item_generation','claim','seat','request','cycle','original_input'):
            f=two_shell_fixture();a,b=f[0]['gameplay']['physical_reload']['transactions']
            b[key]=a[key] if key!='cycle' else 2
            self.assertFalse(audit(*f)['actual_native_consumer_verified'],key)
        for mode in range(5):
            f=two_shell_fixture();g=f[0]['gameplay'];a,b=g['physical_reload']['transactions']
            if mode==0:b['ack']['server_invocation']=a['ack']['server_invocation']
            if mode==1:b['before']['loaded']+=1
            if mode==2:b['before']['reserve']+=1
            if mode==3:b['source_sequence']=a['source_sequence']
            if mode==4:g['reload_flow']['records']=g['reload_flow']['records'][:4]
            self.assertFalse(audit(*f)['actual_native_consumer_verified'],mode)
    def test_each_shell_needs_new_neutral_contact_stroke_and_hold(self):
        for mode in range(10):
            f=two_shell_fixture();g=f[0]['gameplay'];p=g['physical_reload_probe'];a,b=p['rounds']
            if mode==0:p.pop('rounds')
            if mode==1:b['first_neutral_input']=14
            if mode==2:b['last_neutral_input']=b['first_neutral_input']
            if mode==3:b['grab_input']=b['last_neutral_input']
            if mode==4:b['grab_input']=114
            if mode==5:b['completed_ns']=2_359_999_999
            if mode==6:b['loaded']=3
            if mode==7:p['rows']=p['rows'][:3]
            if mode==8:g['reload_flow']['records'][4]['hold_applied']=False
            if mode==9:g['physical_reload']['events'].append(copy.deepcopy(g['physical_reload']['events'][1]))
            self.assertFalse(audit(*f)['actual_native_consumer_verified'],mode)
    def test_complete_and_independent_sequences(self):
        f=fixture();r=audit(*f);self.assertTrue(r['passed'],r);self.assertFalse(r['headset_verified'])
        # Reserve sequence is independent, and must not be compared to ack sample.
        t=f[0]['gameplay']['physical_reload']['transactions'][0]
        t['source_sequence']=1;t['reserve_after']['sequence']=2
        self.assertTrue(audit(*f)['passed'])
    def test_wrong_fixture_does_not_pass(self):
        f=fixture();f[1]['physical_reload']=False;f[1]['reload_request_probe']=True
        r=audit(*f);self.assertFalse(r['actual_native_consumer_verified'])
    def test_missing_counterfeit_or_duplicate_receipt(self):
        for mode in range(7):
            f=fixture();c=f[0]['gameplay']['physical_reload'];t=c['transactions'][0]
            if mode==0:t.pop('ack')
            if mode==1:t['ack']['request']+=1
            if mode==2:t['ack']['identity']['equip_generation']+=1
            if mode==3:t['ack']['server_invocation']=201
            if mode==4:t['ack']['verified']=False
            if mode==5:c['submitted']=2
            if mode==6:c['submitted']=True
            self.assertFalse(audit(*f)['actual_native_consumer_verified'],mode)
    def test_counts_cannot_grant_or_fail_to_consume_reserve(self):
        for mode in range(4):
            f=fixture();t=f[0]['gameplay']['physical_reload']['transactions'][0]
            if mode==0:t['after']['loaded']=4
            if mode==1:t['after']['reserve']=8
            if mode==2:t['reserve_after']['reserve']=6
            if mode==3:f[0]['gameplay']['reload_flow']['records'][-1]['after']['loaded']=4
            self.assertFalse(audit(*f)['actual_native_consumer_verified'],mode)
    def test_old_source_original_item_and_time_cannot_be_restamped(self):
        for mode in range(5):
            f=fixture();t=f[0]['gameplay']['physical_reload']['transactions'][0]
            if mode==0:t['original_input']['observed_ns']=t['current_input']['observed_ns']
            if mode==1:t['original_input']['deadline_ns']=t['submitted_ns']
            if mode==2:t['reserve_after']['observed_ns']=t['ack']['observed_ns']-1
            if mode==3:t['reserve_after']['sequence']=t['source_sequence']
            if mode==4:t['item_generation']=2
            self.assertFalse(audit(*f)['actual_native_consumer_verified'],mode)
    def test_native_clock_transfer_and_cleanup(self):
        for mode in range(7):
            f=fixture();flow=f[0]['gameplay']['reload_flow']
            if mode==0:flow['request_cycle']['clock_failure']={'now_ns':1}
            if mode==1:flow['request_cycle']['cancellations'][0]['now_ns']=1_300_000_000
            if mode==2:flow['records'][-1]['transfer_path']=1
            if mode==3:flow['records'][-1]['end_ns']=1_270_000_000
            if mode==4:flow['records'].append(copy.deepcopy(flow['records'][-1]))
            if mode==5:flow['diagnostic_hold']['restored'][1]=0
            if mode==6:f[0]['hooks_disabled']=False
            self.assertFalse(audit(*f)['actual_native_consumer_verified'],mode)
    def test_delivery_and_geometry_fail_closed(self):
        for mode in range(5):
            f=fixture()
            if mode==0:f[4][-1]['pair']=238
            if mode==1:f[4][-1]['native_frame']=1000
            if mode==2:f[3]['async_timeouts']=1
            if mode==3:f[0]['gameplay']['physical_reload_probe']['rows'][1]['raw']=100
            if mode==4:f[0]['gameplay']['physical_reload_probe']['rows'][-1]['rail_m']=0
            self.assertFalse(audit(*f)['actual_native_consumer_verified'],mode)
    def test_palette_plans_are_not_actual_pack_and_fallback_is_separate(self):
        f=fixture();p=f[0]['gameplay']['rig_publication']['reload_presentation'];p['verified_private_packs']=[0,0,0]
        r=audit(*f);self.assertTrue(r['actual_native_consumer_verified']);self.assertFalse(r['passed']);self.assertFalse(r['actual_private_palette_pack_verified'])
        f=fixture();f[0]['gameplay']['rig_publication']['reload_presentation']['verified_fallback_packs']=0
        r=audit(*f);self.assertTrue(r['actual_private_palette_pack_verified']);self.assertFalse(r['ordinary_palette_fallback_verified'])
    def test_original_receipt_survives_replacement_event(self):
        f=fixture();c=f[0]['gameplay']['physical_reload'];c['events'][-1].update(item_generation=2,claim=3)
        self.assertTrue(audit(*f)['passed'])
    def test_duplicate_json_fields_reject(self):
        with self.assertRaises(ValueError):json.loads('{"submitted":1,"submitted":0}',object_pairs_hook=unique_object)

    def test_delayed_seat_requires_original_identity_and_time(self):
        f=fixture();events=f[0]['gameplay']['physical_reload']['events']
        seat=next(e for e in events if e['event']==10 and e['reason']==2)
        seat['now_ns']=1_250_000_000
        self.assertTrue(audit(*f)['actual_native_consumer_verified'])
        for key,value in [('seat',2),('source_sequence',14),('item_generation',2),('claim',3),
                          ('now_ns',1_239_999_999),('now_ns',1_260_000_001)]:
            changed=copy.deepcopy(f)
            row=next(e for e in changed[0]['gameplay']['physical_reload']['events'] if e['event']==10 and e['reason']==2)
            row[key]=value
            self.assertFalse(audit(*changed)['actual_native_consumer_verified'],(key,value))
        duplicate=copy.deepcopy(f)
        duplicate[0]['gameplay']['physical_reload']['events'].insert(4,copy.deepcopy(seat))
        self.assertFalse(audit(*duplicate)['actual_native_consumer_verified'])

if __name__=='__main__':unittest.main()
