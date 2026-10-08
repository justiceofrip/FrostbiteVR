"""Synthetic audit mutations, not native or headset acceptance evidence."""
import copy
import json
import struct
import unittest
from pathlib import Path
import audit_physical_bolt_probe as audit

MS = 1_000_000


def fixture(count=1):
    """Construct explicitly synthetic, independently joined native/renderer receipts."""
    n = dict(player=0x10000, soldier=0x11000, weak=0x12000, weapon=0x13000,
             actor_generation=11, equipment_generation=12, space=13,
             firing=[0x14000,0x15000,0x16000], server_player=0x17000, server_soldier=0x18000, server_item=0x19000)
    lease_identity = dict(actor=(n['weak'] << 32)|n['soldier'], actor_generation=11, equipment_generation=99,
                          space=13, item=n['weapon'], item_generation=99, mechanism=audit.PROFILE, mechanism_generation=99)
    records, cycles, targets, events, custody, hand_events = [], [], [], [], [], []

    def boundary(branch, loaded, current=8, previous=7, next_=1, timer=2.3):
        return dict(snapshot_sequence=100, **{k:n[k] for k in audit.NATIVE}, equip_generation=n['equipment_generation'],
                    firing=n['firing'][branch], branch=branch, wrapper_offset=(60,64,16)[branch], soldier_flags=11,
                    current=current, previous=previous, next=next_, timer=timer, loaded=loaded, reserve=45, flags_a8=2,
                    **{k:n[k] if branch==2 else 0 for k in ('server_player','server_soldier','server_item')})

    def context(fire=False):
        raw=bytearray(48);struct.pack_into('<f',raw,24,.016);struct.pack_into('<f',raw,32,1);struct.pack_into('<I',raw,44,int(fire))
        return dict(bytes=list(raw),decoded_valid=True)

    def record(branch, begin, duration, before, after, kind=0, hold=False, parent=None):
        ctx=context(before['loaded']>after['loaded']) if kind!=3 else None
        r=dict(id=1,kind=kind,parent=0,update=0,native_invocation=1,native_parent=0,native_update=1,
               thread=100+branch,depth=2 if kind==1 else 1,caller=0x20000,context=0x30000+branch*0x100,
               begin_ns=begin,end_ns=begin+duration,finished=True,identity_retained=True,before=before,after=after,
               context_before=ctx,context_after=copy.deepcopy(ctx),snapshot_before=None,snapshot_after=None,
               restore_fields_matched=False,hold_requested=hold,hold_applied=hold,hold_restored=hold,
               hold_original_delta_bits=int.from_bytes(bytes(ctx['bytes'][24:28]),'little') if hold else 0,
               hold_before_restore_bits=0,hold_unexpected_native_write=False)
        if parent is not None:r['_parent']=parent
        if kind==3:
            raw=bytearray(64)
            for off,key,fmt in ((0,'current','I'),(4,'next','I'),(8,'timer','f'),(24,'loaded','i'),(28,'reserve','i')):struct.pack_into('<'+fmt,raw,off,after[key])
            r['snapshot_before']=dict(bytes=list(raw),decoded_valid=True,**{k:after[k] for k in ('current','next','timer','loaded','reserve')})
            r['snapshot_after']=copy.deepcopy(r['snapshot_before']);r['restore_fields_matched']=True;r['native_update']=0
        records.append(r);return r

    def claim(id_,hand,kind,parent,seq,observed):
        return dict(id=id_,hand=hand,kind=kind,parent=parent,item=n['weapon'],item_generation=99,
                    contact=1+hand,contact_generation=99,source_sequence=seq,deadline_ns=observed+100*MS)

    for i in range(count):
        base=(1000+i*8000)*MS;seqbase=i*1000;loaded=4-i
        # Actual phase-change events, not a replacement for native or target proofs.
        offsets={3:0,4:100,5:100,6:101,7:790,8:800,9:1500,10:2400,11:3300,12:4010,13:4160,14:4840,15:4850,16:6330}
        times={p:base+off*MS for p,off in offsets.items()}
        if i==0:events.extend(dict(phase=p,failure=0,now_ns=base-(3-p)*100*MS) for p in (1,2))
        events.extend(dict(phase=p,failure=0,now_ns=times[p]) for p in range(3,17))
        release=base+4000*MS;ready=base+6300*MS
        lease=dict(**lease_identity,sequence=seqbase+900,observed_ns=release+5,deadline_ns=release+50*MS)
        c=dict(cycle=i+1,shot=i+1,request=i+1,loaded=loaded,reserve=45,release_ns=release,ready_ns=ready,pairs=100,
               custody_returned=True,release_input_sequence=seqbase+800,release_deadline_ns=release+50*MS,
               ready_sequence=seqbase+901,ready_deadline_ns=ready+50*MS,native_ready=True,unchanged_ammunition=True,
               held=lease,native=copy.deepcopy(n));cycles.append(c)
        eg,mech,rg,rs=10+i*10,11+i*10,12+i*10,13+i*10
        enter=times[5]+MS//2;returned=times[14]+5*MS
        custody.extend([
            dict(phase=2,input_sequence=seqbase+200,observed_ns=enter,deadline_ns=enter+100*MS,now_ns=enter,
                 gun=claim(eg,0,1,0,seqbase+199,enter-MS),companion=None,released_ids=[1+i*10,2+i*10]),
            dict(phase=3,input_sequence=seqbase+850,observed_ns=returned,deadline_ns=returned+100*MS,now_ns=returned,
                 gun=claim(rg,1,1,0,seqbase+849,returned-MS),companion=claim(rs,0,2,rg,seqbase+849,returned-MS),released_ids=[eg,0])])
        hand_events.append(dict(serial=i+1,reason='bolt_support_return',support_holding=True,support_token=i+1,
            left=dict(id=rs,kind=2,prerequisite=rg,item=n['weapon'],generation=99,input_generation=seqbase+849,deadline_ns=returned+90*MS),
            right=dict(id=rg,kind=1,prerequisite=0,item=n['weapon'],generation=99,input_generation=seqbase+850,deadline_ns=returned+100*MS),
            physical_item=n['weapon'],equip_generation=99,actor=n['soldier'],weak=n['weak'],rig_epoch=11,space=13,weapon=n['weapon'],
            raw_generation=seqbase+850,now_ns=returned+100))
        for phase,(offset_a,offset_b,travel_a,travel_b,rotation_a,rotation_b) in enumerate((
            (801,1490,0,0,0,audit.UNLOCK),(1510,2390,0,audit.STROKE,audit.UNLOCK,audit.UNLOCK),
            (2410,3290,audit.STROKE,0,audit.UNLOCK,audit.UNLOCK),(3310,3990,0,0,audit.UNLOCK,0)),start=2):
            for edge,off,tr,rot,seq in (('first',offset_a,travel_a,rotation_a,seqbase+300+phase*20),('last',offset_b,travel_b,rotation_b,seqbase+310+phase*20)):
                now=base+off*MS
                targets.append(dict(edge=edge,phase_pairs=10,mechanism_phase=phase,cycle=i+1,shot=i+1,profile=audit.PROFILE,revision=235,rig_fingerprint=audit.FINGERPRINT,
                    packed_ns=now,draw_serial=seq,travel=tr,rotation=rot,source_sequence=seq,source_observed_ns=now-MS,source_deadline_ns=now+50*MS,
                    input_sequence=seq,input_observed_ns=now-MS,input_deadline_ns=now+50*MS,held_sequence=seq,held_observed_ns=now-MS,held_deadline_ns=now+50*MS,
                    **lease_identity,mechanism_claim=mech,mechanism_hand=1,mechanism_parent=eg,gun_claim=eg,gun_hand=0,native_weapon=n['weapon']))
        for branch in range(3):
            bump=branch*1000
            record(branch,base+MS+bump,1000,boundary(branch,loaded+1,2,2,2,0),boundary(branch,loaded))
            previous=7
            if branch<2:
                record(branch,base+20*MS+bump,1000,boundary(branch,loaded),boundary(branch,loaded,8,8,1,2.29),3)
                previous=8
            for off in (50,2000,3999):
                state=boundary(branch,loaded,8,previous,1,2.29)
                record(branch,base+off*MS+bump,1000,state,copy.deepcopy(state),hold=True)
            for tail_index,(cur,nxt) in enumerate(((8,1),(1,2))):
                begin=base+(6200+tail_index*20)*MS+bump
                pre=boundary(branch,loaded,cur,previous if cur==8 else 8,nxt,0)
                post=boundary(branch,loaded,nxt,cur,nxt,0)
                parent=record(branch,begin,5000,copy.deepcopy(pre),copy.deepcopy(post))
                record(branch,begin+1000,1000,copy.deepcopy(pre),copy.deepcopy(post),1,parent=parent)
    events.append(dict(phase=17,failure=0,now_ns=events[-1]['now_ns']+300*MS))
    records.sort(key=lambda r:r['begin_ns'])
    for i,r in enumerate(records):r['id']=i+101;r['native_invocation']=i+1
    for r in records:
        if r['kind']==0:r['native_update']=r['native_invocation']
        if r['kind']==1:
            parent=r.pop('_parent');r['parent']=r['update']=parent['id'];r['native_parent']=r['native_update']=parent['native_invocation']
    flow=dict(native_cycle_candidate=dict(enabled=True,releases=count,acknowledgements=count,failure=0,admitted=False,headset_verified=False,held=[3*count]*3),
              records=records,drained=True,in_flight=0,start_ns=100*MS,window_seconds=30,
              observation_read_misses=dict(schema=1,drained=True,overflow=0,count=0,capacity=100,rows=[]),read_misses=0,server_read_misses=0,
              completion_journal=dict(pending=0,overflow=0,rejected_drain=0),window_expired_calls=3,
              **{k:0 for k in ('dropped','rejected','record_lock_drops','record_begin_lock_drops','record_end_lock_drops','owner_lock_drops','context_misses','nesting_misses')})
    probe=dict(phase=17,failure=0,requested=count,completed=count,inputs=1000,raw_matches=500,controller_poses=500,
               profile=audit.PROFILE,revision=235,rig_fingerprint=audit.FINGERPRINT,event_drops=0,custody_drops=0,
               native_animation_handle_written=False,native_ammo_written=False,cycles=cycles,events=events,custody_events=custody)
    palette=dict(contacts=500,pairs=200*count,copies=400*count,evidence_drops=0,evidence_rejected=0,paired_targets=targets)
    trace=dict(pid=123,hooks_disabled=True,gameplay=dict(reload_flow=flow,physical_bolt_probe=probe,rig_publication=dict(bolt_presentation=palette),hand_ownership=dict(events_dropped=0,events=hand_events)))
    receiver=dict(physical_bolt_receiver=True,consumed_pairs=240,async_timeouts=0,native_tracking_transport_verified=True,headset_tested=False,
                  **{k:False for k in ('m95_stock_shot_neutral','physical_pump_receiver','resource_inventory_receiver','resource_magazine_receiver','shot_probe_requested','vehicle_fixture','pump_hold_fixture')})
    owner=dict(actor=n['soldier'],player=n['player'],weak=n['weak'],flags=11,inventory=0x40000,selected_slot=0,selected_weapon=n['weapon'],items=[n['weapon']])
    def baseline(loaded,now):
        row=dict(monotonic_ns=now,client_owner=copy.deepcopy(owner),**{k:n[k] for k in ('server_player','server_soldier','server_item')},
                 server_firing=n['firing'][2],weapon_data=0x50000,firing_data=0x60000,ammo_address=0x70000,asset_name='M95_sp',asset_path=audit.ASSET,
                 identity_coherent=True,state=dict(current=2,previous=1,next=2,timer=0,loaded=loaded,reserve=45))
        row2=copy.deepcopy(row);row2['monotonic_ns']+=30*MS
        return dict(pid=123,passed=True,read_only=True,process_writes=False,native_calls=False,input_or_focus_changes=False,
                    executable_sha256=audit.common.EXECUTABLE_SHA256,rejected=[],samples=[row,row2])
    before,after=baseline(5,100*MS),baseline(5-count,events[-1]['now_ns']+1000*MS)
    completion=dict(pid=123,headset_verified=False,manual_bolt_admitted=False,trace_exit=0,receiver_exit=0,postflight_exit=0,
                    receiver_exited=True,responding=True,game_exited=False,requested_cycles=count)
    inputs=[dict(generation=i+1,tick_ms=1000+i*10,space=13,focused=True,head_valid=True,head=[0,0,0,0,0,0,1],reference_head=[0,0,0,0,0,0,1],
                 hands=[dict(active=127,held=0,touch_active=0,touched=0,grip_tracked=True,aim_tracked=True,axes=[0,0,0,0],grip=[0,0,0,0,0,0,1],aim=[0,0,0,0,0,0,1]) for _ in range(2)]) for i in range(1000*count)]
    return [trace,receiver,before,after,completion,inputs]


def snapshot(r):
    """Explicitly synthetic raw snapshot matching the selected Restore output."""
    raw = bytearray(64)
    for off, key, fmt in ((0,'current','I'), (4,'next','I'), (8,'timer','f'), (24,'loaded','i'), (28,'reserve','i')):
        struct.pack_into('<'+fmt, raw, off, r['after'][key])
    s = dict(bytes=list(raw), decoded_valid=True, **{k:r['after'][k] for k in ('current','next','timer','loaded','reserve')})
    r['snapshot_before'] = s; r['snapshot_after'] = copy.deepcopy(s)


def reindex(records):
    # Preserve real parent relationships while inserting explicitly synthetic rows.
    by_native = {r['native_invocation']:r for r in records}
    parents = {id(r):by_native[r['native_parent']] for r in records if r['kind'] == 1}
    records.sort(key=lambda r:r['begin_ns'])
    for i, r in enumerate(records):
        r['id'] = i+101; r['native_invocation'] = i+1
    for r in records:
        if r['kind'] == 0:
            r['native_update'] = r['native_invocation']
        elif r['kind'] == 1:
            parent = parents[id(r)]
            r['parent'] = r['update'] = parent['id']
            r['native_parent'] = r['native_update'] = parent['native_invocation']


def progression_fixture(count=1, cycle=0):
    """Full synthetic trial with the captured242 6->8/previous6 progression shape."""
    data = fixture(count); rows = data[0]['gameplay']['reload_flow']['records']
    base = (1000+cycle*8000)*MS
    shot = next(r for r in rows if r['kind'] == 0 and r['before']['branch'] == 0 and r['begin_ns'] == base+MS)
    normal = next(r for r in rows if r['kind'] == 3 and r['before']['branch'] == 0 and r['begin_ns'] == base+20*MS)
    shot['after'].update(current=6, previous=5, next=7, timer=.000862539)
    advance = copy.deepcopy(normal)
    advance.update(id=10001, native_invocation=10001, begin_ns=base+10*MS, end_ns=base+10*MS+1000)
    advance['before'] = copy.deepcopy(shot['after'])
    advance['after'].update(current=8, previous=6, next=1, timer=2.29874)
    snapshot(advance)
    own = copy.deepcopy(shot)
    own.update(id=10002, native_invocation=10002, native_update=10002, begin_ns=base+15*MS, end_ns=base+15*MS+1000)
    own['before'] = copy.deepcopy(advance['after']); own['after'] = copy.deepcopy(advance['after']); own['after']['timer'] = 2.27915
    own['context_before']['bytes'][44] = own['context_after']['bytes'][44] = 0
    normal['before'] = copy.deepcopy(own['after'])
    rows.extend((advance, own)); reindex(rows)
    return data, advance, own, normal


class BoltAuditTests(unittest.TestCase):
    def test_captured242_exact_snapshot_progression(self):
        path = Path(__file__).parent/'fixtures'/'m95_prehold_progression242.json'
        captured = json.loads(path.read_text())
        c = captured['cycle']; rows = {r['native_invocation']:r for r in captured['records']}
        self.assertEqual(captured['trace_sha256'], '1e8264766e4f68e78c8834e9eee75d628c9cd8a2d96e21b82fe48b149772d5ce')
        self.assertTrue(audit.restore(rows[2904], c, 0, progression=True))
        self.assertFalse(audit.restore(rows[2904], c, 0))
        self.assertEqual(rows[2897]['after']['current'], rows[2904]['before']['current'])
        self.assertTrue(audit.update(rows[2906], c, 0))
        self.assertTrue(audit.restore(rows[2910], c, 0))
        self.assertTrue(audit.held(rows[2916], c, 0, rows[2910]['after']['previous']))

    def test_forward_progression_keeps_independent_native_tails(self):
        for count, cycle in ((1,0), (2,1)):
            data, advance, _, _ = progression_fixture(count, cycle)
            result = self.assert_passes(data)
            proof = result['details']['cycles'][cycle]['branches'][0]['prehold_progressions']
            self.assertEqual(proof[0]['invocation'], advance['native_invocation'])
            self.assertEqual((proof[0]['before_current'], proof[0]['after_current'], proof[0]['after_previous']), (6,8,6))
            rows = data[0]['gameplay']['reload_flow']['records']
            rows.remove(next(r for r in rows if r['kind'] == 1 and r['begin_ns'] > advance['end_ns']))
            self.assertFalse(self.run_audit(data)['checks']['all_three_original_shots_holds_restores_tails'])

    def test_progression_snapshot_identity_count_and_timer_mutations(self):
        mutations = [
            lambda r:r.update(restore_fields_matched=False), lambda r:r.update(finished=False),
            lambda r:r.update(identity_retained=False), lambda r:r.update(native_parent=1),
            lambda r:r.update(native_update=1), lambda r:r.update(depth=2),
            lambda r:r.update(hold_requested=True), lambda r:r['after'].update(weapon=999),
            lambda r:r['after'].update(snapshot_sequence=999), lambda r:r['after'].update(previous=7),
            lambda r:r['before'].update(timer=.101), lambda r:r['before'].update(timer=0),
            lambda r:r['before'].update(next=8), lambda r:r['after'].update(timer=2.31),
            lambda r:r['after'].update(loaded=3), lambda r:r['before'].update(reserve=46),
            lambda r:r['after'].update(flags_a8=4),
            lambda r:r['snapshot_after']['bytes'].__setitem__(63,1),
            lambda r:r['snapshot_before'].update(decoded_valid=False),
            lambda r:r['snapshot_before'].update(timer=2.29873)]
        for index, mutation in enumerate(mutations):
            with self.subTest(mutation=index):
                data, advance, _, _ = progression_fixture(); mutation(advance)
                self.assertFalse(self.run_audit(data)['passed'])

    def test_progression_requires_preceding_state_and_no_open_update(self):
        for mode in ('wrong_preceding_state', 'overlap'):
            data, advance, _, _ = progression_fixture()
            shot = next(r for r in data[0]['gameplay']['reload_flow']['records'] if r['kind'] == 0 and r['before']['branch'] == 0)
            if mode == 'wrong_preceding_state': shot['after'].update(current=8, previous=7, next=1, timer=2.3)
            else: shot['end_ns'] = advance['begin_ns']+1
            self.assertFalse(self.run_audit(data)['checks']['all_three_original_shots_holds_restores_tails'])

    def test_other_branch_hold_closes_global_progression_window(self):
        data, advance, _, _ = progression_fixture()
        rows = data[0]['gameplay']['reload_flow']['records']
        server_hold = next(r for r in rows if r['hold_applied'] and r['before']['branch'] == 2)
        server_hold['begin_ns'] = advance['begin_ns']-2000; server_hold['end_ns'] = advance['begin_ns']-1000
        reindex(rows)
        self.assertFalse(self.run_audit(data)['checks']['all_three_original_shots_holds_restores_tails'])

    def test_previous6_hold_requires_actual_progression_proof(self):
        data, advance, _, normal = progression_fixture()
        rows = data[0]['gameplay']['reload_flow']['records']; rows.remove(normal)
        for r in rows:
            if r['hold_applied'] and r['before']['branch'] == 0:
                r['before']['previous'] = r['after']['previous'] = 6
        self.assert_passes(data)
        rows.remove(advance)
        self.assertFalse(self.run_audit(data)['checks']['all_three_original_shots_holds_restores_tails'])

    def test_bidirectional_progression_is_ordered_not_replayed(self):
        data, advance, _, _ = progression_fixture()
        rows = data[0]['gameplay']['reload_flow']['records']
        shot = next(r for r in rows if r['kind'] == 0 and r['before']['branch'] == 0)
        shot['after'].update(current=8, previous=7, next=1, timer=2.3)
        rewind = copy.deepcopy(advance)
        rewind.update(id=10003, native_invocation=10003, begin_ns=advance['begin_ns']-5*MS, end_ns=advance['end_ns']-5*MS)
        rewind['before'] = copy.deepcopy(shot['after'])
        rewind['after'] = copy.deepcopy(advance['before']); rewind['after']['previous'] = 8
        snapshot(rewind); advance['before'] = copy.deepcopy(rewind['after'])
        rows.append(rewind); reindex(rows)
        result = self.assert_passes(data)
        self.assertEqual([p['after_current'] for p in result['details']['cycles'][0]['branches'][0]['prehold_progressions']], [6,8])
        replay = copy.deepcopy(advance)
        replay.update(id=10004, native_invocation=10004, begin_ns=advance['begin_ns']+MS, end_ns=advance['end_ns']+MS)
        rows.append(replay); reindex(rows)
        self.assertFalse(self.run_audit(data)['checks']['all_three_original_shots_holds_restores_tails'])

    def test_progression_does_not_waive_missing_hold_or_recording_drop(self):
        data, _, _, _ = progression_fixture(2,1)
        flow = data[0]['gameplay']['reload_flow']; rows = flow['records']
        rows.remove(next(r for r in rows if r['hold_applied'] and r['before']['branch'] == 0))
        flow['record_begin_lock_drops'] = 1
        result = self.run_audit(data)
        self.assertTrue(result['checks']['all_three_original_shots_holds_restores_tails'])
        self.assertFalse(result['checks']['all_applied_holds_accounted_for'])
        self.assertFalse(result['checks']['bounded_trial_recording_complete'])
        self.assertFalse(result['passed'])

    def test_ordinary_status_rejects_explicit_custody_fixture(self):
        trace = fixture(2)[0]
        trace['gameplay']['reload_flow']['native_cycle_candidate'].update(phase=5, blocks_fire=False)
        self.assertFalse(audit.mechanism_status(trace, 2, True)['runtime_completed'])
        probe = trace['gameplay'].pop('physical_bolt_probe')
        probe.update(input_only=True, references_ready=True, selected_mode_ready=True,
                     startup_draw=dict(phase=4, failure=0, gun_claim=19), input_edge_drops=0,
                     input_edges=[{} for _ in range(8)])
        trace['gameplay']['ordinary_bolt_input_probe'] = probe
        self.assertTrue(audit.mechanism_status(trace, 2, True)['runtime_completed'])
        for key, value in [('input_only', False), ('references_ready', False), ('selected_mode_ready', False), ('input_edge_drops', 1)]:
            changed = copy.deepcopy(trace); changed['gameplay']['ordinary_bolt_input_probe'][key] = value
            self.assertFalse(audit.mechanism_status(changed, 2, True)['runtime_completed'])
        changed = copy.deepcopy(trace); changed['gameplay']['physical_bolt_probe'] = probe
        self.assertFalse(audit.mechanism_status(changed, 2, True)['runtime_completed'])

    def test_two_shots_do_not_substitute_for_second_bolt_cycle(self):
        trace, _, before, after, _, _ = fixture(2)
        self.assertEqual(before['samples'][1]['state']['loaded'] - after['samples'][1]['state']['loaded'], 2)
        probe = trace['gameplay']['physical_bolt_probe']
        probe.update(phase=18, failure=5, completed=1)
        native = trace['gameplay']['reload_flow']['native_cycle_candidate']
        native.update(phase=6, failure=7, failure_invocation=2903, releases=1, acknowledgements=1, blocks_fire=True)
        status = audit.mechanism_status(trace, 2)
        self.assertFalse(status['runtime_completed'])
        self.assertEqual(status['native_failure_invocation'], 2903)

    def test_runtime_completion_is_distinct_from_missing_recording(self):
        data = fixture(2); trace = data[0]
        flow = trace['gameplay']['reload_flow']
        flow['native_cycle_candidate'].update(phase=5, blocks_fire=False)
        flow['record_begin_lock_drops'] = 1
        status = audit.mechanism_status(trace, 2)
        self.assertTrue(status['runtime_completed'])
        self.assertFalse(status['callback_evidence_verified'])
        self.assertFalse(audit.audit(*data)['passed'])

    def run_audit(self,data):return audit.audit(*data)
    def assert_passes(self,data):
        result=self.run_audit(data);self.assertTrue(result['passed'],json.dumps(result,indent=2));return result
    def reject(self,mutate,check=None):
        data=fixture();mutate(data);result=self.run_audit(data)
        self.assertFalse(result['passed'],str(mutate))
        if check:self.assertFalse(result['checks'].get(check,False),json.dumps(result,indent=2))
    def test_one_and_two_cycles(self):
        for count in (1,2):
            with self.subTest(count=count):self.assert_passes(fixture(count))
    def test_actual_cpp_transport_bool_encoding(self):
        d=fixture();d[1]['native_tracking_transport_verified']=1;self.assert_passes(d)
        self.reject(lambda d:d[1].update(native_tracking_transport_verified=2),'ordinary_neutral_receiver')
    def test_completed_first_cycle_cannot_promote_failed_two_cycle_trial(self):
        d=fixture();f=d[0]['gameplay']['physical_bolt_probe'];f.update(requested=2,phase=18,failure=9)
        f['events'][-1].update(phase=18,failure=9);d[4]['requested_cycles']=2
        r=self.run_audit(d);self.assertFalse(r['passed']);self.assertFalse(r['bounded_trial_complete'])
        self.assertTrue(r['checks']['paired_renderer_evidence_complete']);self.assertFalse(r['checks']['paired_targets_cover_requested_cycles'])
        self.assertTrue(r['details']['retained_completed_cycles_native_complete']);self.assertTrue(r['details']['retained_completed_cycles_gesture_complete'])
    def test_post_window_drain_is_not_overall_completeness(self):
        r=self.assert_passes(fixture());self.assertTrue(r['bounded_trial_complete']);self.assertFalse(r['recorder_complete'])
    def test_recorder_ids_are_independent_from_native_ids(self):
        data=fixture();self.assertNotEqual(data[0]['gameplay']['reload_flow']['records'][0]['id'],data[0]['gameplay']['reload_flow']['records'][0]['native_invocation']);self.assert_passes(data)
    def test_original_release_input_can_precede_latest_lease(self):
        data=fixture();c=data[0]['gameplay']['physical_bolt_probe']['cycles'][0];self.assertGreater(c['held']['observed_ns'],c['release_ns']);self.assert_passes(data)
    def test_missing_restores_cannot_prove_client_previous8(self):
        self.reject(lambda d:d[0]['gameplay']['reload_flow'].update(records=[r for r in d[0]['gameplay']['reload_flow']['records'] if r['kind']!=3]),'all_three_original_shots_holds_restores_tails')
    def test_restore_snapshot_bytes_and_projection(self):
        for key in ('snapshot_before','snapshot_after'):
            with self.subTest(key=key):self.reject(lambda d:next(r for r in d[0]['gameplay']['reload_flow']['records'] if r['kind']==3)[key]['bytes'].__setitem__(8,1))
    def test_native_six_digit_timer_encoding_is_exact_not_epsilon(self):
        raw=struct.unpack('<f',bytes([205,29,19,64]))[0]
        self.assertTrue(audit.snapshot_timer_matches(raw,2.29869))
        self.assertFalse(audit.snapshot_timer_matches(raw,2.29868))
        self.assertFalse(audit.snapshot_timer_matches(raw,2.29870))
    def test_prediction_rewind_requires_exact_original_snapshot(self):
        d=fixture();r=copy.deepcopy(next(r for r in d[0]['gameplay']['reload_flow']['records'] if r['kind']==3));c=d[0]['gameplay']['physical_bolt_probe']['cycles'][0]
        r['after'].update(current=6,previous=8,next=7,timer=.05)
        raw=bytearray(r['snapshot_before']['bytes']);struct.pack_into('<I',raw,0,6);struct.pack_into('<I',raw,4,7);struct.pack_into('<f',raw,8,.05)
        for key in ('snapshot_before','snapshot_after'):r[key].update(bytes=list(raw),current=6,next=7,timer=.05)
        self.assertTrue(audit.restore(r,c,0,True));self.assertFalse(audit.restore(r,c,0))
        r['after']['previous']=7;self.assertFalse(audit.restore(r,c,0,True))
    def test_hold_context_protected_bytes(self):
        for offset in list(range(20))+list(range(24,48)):
            with self.subTest(offset=offset):self.reject(lambda d:next(r for r in d[0]['gameplay']['reload_flow']['records'] if r['hold_applied'])['context_after']['bytes'].__setitem__(offset,99))
    def test_original_update_scratch_is_permitted(self):
        d=fixture();next(r for r in d[0]['gameplay']['reload_flow']['records'] if r['hold_applied'])['context_after']['bytes'][20:24]=[1,2,3,4];self.assert_passes(d)
    def test_hold_shape_mutations(self):
        for key,value in (('hold_restored',False),('hold_requested',False),('hold_before_restore_bits',1),('hold_original_delta_bits',0),('hold_unexpected_native_write',True)):
            with self.subTest(key=key):self.reject(lambda d:next(r for r in d[0]['gameplay']['reload_flow']['records'] if r['hold_applied']).__setitem__(key,value))
    def test_server_previous8_is_not_client_prediction(self):
        def mutation(d):
            r=next(r for r in d[0]['gameplay']['reload_flow']['records'] if r['hold_applied'] and r['before']['branch']==2)
            r['before']['previous']=r['after']['previous']=8
        self.reject(mutation)
    def test_wrong_ammo_and_last_round(self):
        self.reject(lambda d:d[0]['gameplay']['physical_bolt_probe']['cycles'][0].update(loaded=0))
        self.reject(lambda d:next(r for r in d[0]['gameplay']['reload_flow']['records'] if r['hold_applied'])['after'].update(reserve=46))
    def test_missing_tail_and_wrong_direct_parent(self):
        self.reject(lambda d:d[0]['gameplay']['reload_flow']['records'].remove(next(r for r in d[0]['gameplay']['reload_flow']['records'] if r['kind']==1)))
        self.reject(lambda d:next(r for r in d[0]['gameplay']['reload_flow']['records'] if r['kind']==1).update(native_parent=999))
    def test_duplicate_native_lineage(self):
        self.reject(lambda d:d[0]['gameplay']['reload_flow']['records'][1].update(native_invocation=1),'independent_unique_native_lineage')
    def test_phase_counters_do_not_replace_actual_targets(self):
        self.reject(lambda d:d[0]['gameplay']['rig_publication']['bolt_presentation'].update(paired_targets=[]))
    def test_incomplete_lift_back_forward_lock(self):
        for phase,key,value in ((2,'rotation',0),(3,'travel',0),(4,'travel',audit.STROKE),(5,'rotation',audit.UNLOCK)):
            with self.subTest(phase=phase):self.reject(lambda d:next(r for r in d[0]['gameplay']['rig_publication']['bolt_presentation']['paired_targets'] if r['mechanism_phase']==phase and r['edge']=='last').update({key:value}),'ordered_actual_gesture_and_custody_return')
    def test_target_stale_claim_wrong_owner_or_source(self):
        for key,value in (('source_deadline_ns',1),('held_deadline_ns',1),('input_deadline_ns',1),('mechanism_parent',999),('gun_hand',1),('actor',99),('source_sequence',100000),('native_weapon',123)):
            with self.subTest(key=key):self.reject(lambda d:d[0]['gameplay']['rig_publication']['bolt_presentation']['paired_targets'][0].update({key:value}))
    def test_custody_missing_wrong_parent_and_stale_source(self):
        self.reject(lambda d:d[0]['gameplay']['physical_bolt_probe']['custody_events'].pop())
        self.reject(lambda d:d[0]['gameplay']['physical_bolt_probe']['custody_events'][1]['companion'].update(parent=99))
        self.reject(lambda d:d[0]['gameplay']['physical_bolt_probe']['custody_events'][1]['gun'].update(deadline_ns=1))
    def test_ready_not_fabricated_from_phase(self):
        for key,value in (('native_ready',False),('unchanged_ammunition',False),('ready_deadline_ns',1),('custody_returned',False)):
            with self.subTest(key=key):self.reject(lambda d:d[0]['gameplay']['physical_bolt_probe']['cycles'][0].update({key:value}))
    def test_dropped_recording_or_journals(self):
        for group,key in (('reload_flow','record_begin_lock_drops'),('physical_bolt_probe','event_drops'),('physical_bolt_probe','custody_drops')):
            with self.subTest(key=key):self.reject(lambda d:d[0]['gameplay'][group].update({key:1}))
        self.reject(lambda d:d[0]['gameplay']['rig_publication']['bolt_presentation'].update(evidence_drops=1))
    def test_in_trial_read_miss(self):
        def mutate(d):
            f=d[0]['gameplay']['reload_flow'];f['read_misses']=1;f['observation_read_misses'].update(count=1,rows=[dict(now_ns=2000*MS)])
        self.reject(mutate,'bounded_trial_recording_complete')
    def test_out_of_trial_read_miss_preserves_scope(self):
        d=fixture();f=d[0]['gameplay']['reload_flow'];f['read_misses']=1;f['observation_read_misses'].update(count=1,rows=[dict(now_ns=29000*MS)])
        r=self.assert_passes(d);self.assertFalse(r['recorder_complete'])
    def test_wrong_external_owner_family_ammo_or_timing(self):
        for mutate in (lambda d:d[3]['samples'][0].update(asset_name='SPAS12_sp'),lambda d:d[3]['samples'][0]['state'].update(reserve=44),
                       lambda d:d[3]['samples'][0]['client_owner'].update(selected_weapon=999),lambda d:d[3]['samples'][0].update(monotonic_ns=1000)):
            self.reject(mutate)
    def test_receiver_extraneous_actions_or_missing_tracking(self):
        self.reject(lambda d:d[5][0]['hands'][1].update(held=8))
        self.reject(lambda d:d[5][0]['hands'][1].update(axes=[0,0,1,0]))
        self.reject(lambda d:d[5][0].update(focused=False))
    def test_source_sequence_must_exist_in_original_transport(self):
        self.reject(lambda d:d[5].pop(339),'source_sequences_exist_in_transport')
    def test_custody_does_not_prove_ordinary_support_by_itself(self):
        self.reject(lambda d:d[0]['gameplay']['hand_ownership'].update(events=[]),'ordinary_support_return_after_custody')
        self.reject(lambda d:d[0]['gameplay']['hand_ownership']['events'][0]['left'].update(prerequisite=999))
    def test_unselected_updates_and_native_reload_are_still_checked(self):
        def mutation(d):
            rows=d[0]['gameplay']['reload_flow']['records'];r=copy.deepcopy(rows[0]);r['id']=999;r['native_invocation']=r['native_update']=999
            r['begin_ns']+=500000;r['end_ns']+=500000;r['thread']=999;r['context_before']['bytes'][44]=r['context_after']['bytes'][44]=4
            rows.append(r)
        self.reject(mutation)
    def test_malformed_data_fails_closed(self):
        for value in (None,[],{},float('nan')):
            with self.subTest(value=value):self.assertFalse(audit.audit(value,{}, {}, {}, {}, [])['passed'])


if __name__=='__main__':unittest.main()
