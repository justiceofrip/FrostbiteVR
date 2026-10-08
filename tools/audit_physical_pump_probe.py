"""Offline audit of a finite controller-driven pump trial; never opens a process."""
from __future__ import annotations
import argparse, hashlib, json, math, struct
from pathlib import Path

EXECUTABLE_SHA256='3911fcc8914b0158c434cee36f19295316e715d4c9a0e70c857e8c39e3d54258'
IDENTITY=('firing','branch','wrapper_offset','player','soldier','weak','weapon','actor_generation','equip_generation','space')

def same_client_owner(a,b):
    if not isinstance(a,dict) or not isinstance(b,dict):return a==b
    # Soldier flags include mutable gameplay state. Only bit0 chooses the
    # inventory layout; every resolved ownership/selection pointer is retained.
    keys=('actor','player','weak','inventory','selected_slot','selected_weapon','items')
    return all(k in a and k in b and a[k]==b[k] for k in keys) and \
        type(a.get('flags')) is int and type(b.get('flags')) is int and (a['flags']&1)==(b['flags']&1)

def baseline(doc):
    if not (doc.get('passed') is True and doc.get('read_only') is True and
            doc.get('process_writes') is False and doc.get('native_calls') is False and doc.get('executable_sha256')==EXECUTABLE_SHA256): return None
    rows=doc.get('samples',[])
    if len(rows)!=2: return None
    a,b=rows
    try:
        if not 0<b['monotonic_ns']-a['monotonic_ns']<=500000000:return None
        for key in ('client_owner','server_player','server_soldier','server_item','server_firing','asset_name'):
            if a[key]!=b[key]:return None
        for r in rows:
            if r['asset_name']!='SPAS12_sp' or not r['identity_coherent'] or (r['state']['current'],r['state']['next'])!=(2,2):return None
        if (a['state']['loaded'],a['state']['reserve'])!=(b['state']['loaded'],b['state']['reserve']):return None
        return b
    except (KeyError,TypeError):return None

def own_update(row):
    return row.get('kind')==0 and row.get('finished') is True and row.get('identity_retained') is True and row.get('depth')==1 and \
        row.get('native_invocation',0)>0 and row.get('id',0)>0 and row.get('native_update')==row.get('native_invocation') and row.get('native_parent')==0

def held(row):
    if not own_update(row) or not row.get('hold_requested') or not row.get('hold_restored') or row.get('hold_unexpected_native_write') is not False or row.get('hold_before_restore_bits')!=0:return False
    a,b=row.get('before',{}),row.get('after',{})
    protected=('firing','branch','wrapper_offset','player','soldier','weak','weapon','actor_generation','equip_generation','space',
               'current','previous','next','timer','loaded','reserve','flags_a8')
    boundary=((a.get('current'),a.get('next'))==(7,8) and a.get('previous') in (6,7) and a.get('loaded',0)>0) or \
        ((a.get('current'),a.get('next'))==(6,1) and a.get('previous') in (5,6) and a.get('loaded')==0)
    if any(k not in a or k not in b for k in protected) or any(a.get(k)!=b.get(k) for k in protected) or not boundary:return False
    x,y=row.get('context_before',{}).get('bytes',[]),row.get('context_after',{}).get('bytes',[])
    if len(x)!=48 or len(y)!=48:return False
    try:
        x,y=bytes(x),bytes(y);delta=struct.unpack_from('<f',x,24)[0]
        return math.isfinite(delta) and 0<delta<=.10000001 and x[:20]==y[:20] and x[24:]==y[24:] and \
            int.from_bytes(x[24:28],'little')==row.get('hold_original_delta_bits') and not any(x[38:41]) and not int.from_bytes(x[44:48],'little')
    except (ValueError,TypeError,struct.error):return False

def same_identity(row):
    a,b=row.get('before',{}),row.get('after',{})
    return all(k in a and k in b and a[k]==b[k] for k in IDENTITY)

def shot(row):
    b=row.get('after',{});state=(b.get('current'),b.get('previous'),b.get('next'))
    return own_update(row) and same_identity(row) and row.get('hold_applied') is False and \
        0<row.get('begin_ns',0)<row.get('end_ns',0) and \
        ((b.get('loaded',0)>0 and state in ((6,5,7),(6,6,7),(7,6,7),(7,6,8),(7,7,7),(7,7,8),(8,7,1))) or \
         (b.get('loaded')==0 and state==(6,5,1)))

def direct_commit(row,parent):
    if not parent or not same_identity(row) or not same_identity(parent) or row.get('id',0)<=0 or row.get('native_invocation',0)<=parent.get('native_invocation',0):return False
    if row.get('parent')!=parent.get('id') or row.get('update')!=parent.get('id'):return False
    a,b=row.get('before',{}),row.get('after',{});identity=parent.get('before',{})
    if not all(a.get(k)==identity.get(k) for k in IDENTITY):return False
    if row.get('context')!=parent.get('context') or row.get('thread')!=parent.get('thread'):return False
    if not parent.get('begin_ns',0)<=row.get('begin_ns',0)<row.get('end_ns',0)<=parent.get('end_ns',0):return False
    if a.get('next')!=b.get('next') or any(a.get(k)!=b.get(k) for k in ('timer','loaded','reserve','flags_a8')):return False
    x,y=row.get('context_before',{}).get('bytes'),row.get('context_after',{}).get('bytes')
    return isinstance(x,list) and len(x)==48 and x==y and not any(x[38:41]) and not any(x[44:48])

def audit_checked(trace,receiver,before,after,completion,inputs):
    game=trace.get('gameplay',{});flow=game.get('reload_flow',{});native=flow.get('native_cycle_candidate',{})
    fixture=game.get('physical_pump_probe',{});physical=game.get('physical_pump',{});palette=game.get('rig_publication',{}).get('pump_presentation',{})
    checks={};details={};count=fixture.get('requested_cycles');cycles=fixture.get('cycles',[])
    checks['finite_real_fixture_completed']=count in (1,2,8) and fixture.get('completed') is True and fixture.get('failure')==0 and fixture.get('completed_cycles')==count and len(cycles)==count
    checks['measured_renderer_source_used']=fixture.get('original_raw_matches',0)>0 and fixture.get('generated_controller_poses',0)>0
    checks['native_release_and_ack_counts']=native.get('enabled') is True and native.get('releases')==native.get('acknowledgements')==count and physical.get('releases')==physical.get('acknowledgements')==count
    checks['default_admission_remains_off']=native.get('admitted') is False and native.get('headset_verified') is False
    checks['real_palette_pairs']=palette.get('pairs',0)>0 and palette.get('copies',0)>=2*palette.get('pairs',0) and palette.get('source_rejects')==0
    checks['ordinary_finite_receiver']=receiver.get('physical_pump_receiver') is True and receiver.get('consumed_pairs')==240 and receiver.get('async_timeouts')==0
    if count==8:checks['explicit_empty_cycle_stream']=receiver.get('physical_pump_empty_cycle') is True
    checks['clean_shutdown']=trace.get('hooks_disabled') is True and flow.get('drained') is True and flow.get('in_flight')==0
    checks['game_and_helpers_completed']=completion.get('trace_exit')==completion.get('receiver_exit')==completion.get('baseline_exit')==completion.get('postflight_exit')==0 and completion.get('responding') is True and completion.get('game_exited') is False
    origin=receiver.get('controls_started_ms',0);neutral=bool(inputs);last=0
    for r in inputs:
        hands=r.get('hands',[])
        if len(hands)!=2 or r.get('generation',0)<=last:neutral=False;continue
        last=r['generation'];left,right=hands
        allowed=left.get('held')==0 or (receiver.get('physical_pump_exit_vehicle') is True and left.get('held')==8 and 350<=r.get('tick_ms',0)-origin<=550)
        if not allowed or right.get('held')!=0 or any(h.get('axes')!=[0,0,0,0] for h in hands):neutral=False
    checks['receiver_never_fabricates_trigger_or_stroke']=neutral
    a,b=baseline(before),baseline(after)
    checks['external_before_after']=bool(a and b)
    if a and b and count in (1,2,8):
        checks['same_owner_and_native_counts']=before.get('pid')==after.get('pid')==trace.get('pid') and same_client_owner(a['client_owner'],b['client_owner']) and \
            all(a[k]==b[k] for k in ('server_player','server_soldier','server_item','server_firing')) and \
            (b['state']['loaded'],b['state']['reserve'])==(a['state']['loaded']-count,a['state']['reserve'])
    else:checks['same_owner_and_native_counts']=False
    if count==8:
        finishes=[max(c.get('ready_ns',0),c.get('support_observed_ns',0)) for c in cycles]
        checks['full_tube_to_empty']=bool(a and b and a['state']['loaded']==8 and b['state']['loaded']==0)
        checks['empty_observed_before_detach']=after.get('in_trial_empty_observation') is True and bool(finishes) and \
            all(max(finishes)<r.get('monotonic_ns',0)<flow.get('start_ns',0)+flow.get('window_seconds',0)*1000000000 for r in after.get('samples',[]))
    records=flow.get('records',[]);held_rows=[r for r in records if r.get('hold_applied') is True]
    # Recorder IDs and native invocation IDs are independent counters. A
    # recorder lock miss can separate them; the explicit native lineage remains
    # authoritative, while the recording-loss check still rejects completeness.
    ids=[r.get('id') for r in records];native_ids=[r.get('native_invocation') for r in records]
    unique=all(type(n) is int and n>0 for n in ids+native_ids) and len(set(ids))==len(ids) and len(set(native_ids))==len(native_ids)
    for thread in {r.get('thread') for r in records}:
        ordered=sorted((r for r in records if r.get('thread')==thread),key=lambda r:r.get('begin_ns',0))
        unique=unique and all(a.get('native_invocation',0)<b.get('native_invocation',0) for a,b in zip(ordered,ordered[1:]))
    checks['unique_native_and_record_lineage']=unique
    branch_holds=[sum(r.get('before',{}).get('branch')==n for r in held_rows) for n in range(3)]
    checks['actual_scoped_native_holds']=bool(held_rows) and all(map(held,held_rows)) and all(branch_holds) and branch_holds==native.get('held')
    details['held_rows_per_branch']=branch_holds
    invocations={r.get('native_invocation'):r for r in records if own_update(r)}
    shot_ids=[];tail_ids=[];cycle_ok=checks['finite_real_fixture_completed']
    for index,c in enumerate(cycles):
        loaded=c.get('loaded_after_shot');reserve=c.get('reserve');firing=c.get('firing',[]);request=c.get('requested_ns',0);ready=c.get('ready_ns',0)
        if len(firing)!=3 or len(set(firing))!=3 or not 0<request<=ready or c.get('verified_pairs',0)<=0 or c.get('rear_pair') is not True:cycle_ok=False;continue
        for branch in range(3):
            shots=[r for r in records if own_update(r) and r.get('before',{}).get('branch')==branch and r.get('before',{}).get('firing')==firing[branch] and
                r.get('before',{}).get('loaded')==loaded+1 and r.get('after',{}).get('loaded')==loaded and
                r.get('before',{}).get('reserve')==r.get('after',{}).get('reserve')==reserve and r.get('end_ns',0)<request and shot(r)]
            shot_ids.append([r.get('id') for r in shots]);cycle_ok=cycle_ok and len(shots)==1
            transitions=[];expected_tail=((6,1),(1,2)) if loaded==0 else ((7,8),(8,1),(1,2))
            for r in sorted(records,key=lambda r:r.get('begin_ns',0)):
                pre,post=r.get('before',{}),r.get('after') or {}
                if r.get('kind')!=1 or pre.get('firing')!=firing[branch] or not request<=r.get('begin_ns',0)<=r.get('end_ns',0)<=ready:continue
                if not direct_commit(r,invocations.get(r.get('native_parent'))):cycle_ok=False;continue
                if r.get('depth')!=2 or not r.get('native_parent') or r.get('native_parent')!=r.get('native_update') or not r.get('finished') or not r.get('identity_retained'):cycle_ok=False;continue
                if post.get('previous')!=pre.get('current') or post.get('current')!=pre.get('next') or post.get('loaded')!=loaded or post.get('reserve')!=reserve:cycle_ok=False;continue
                if len(transitions)<len(expected_tail) and (pre.get('current'),post.get('current'))==expected_tail[len(transitions)]:transitions.append(r.get('id'))
            tail_ids.append(transitions);cycle_ok=cycle_ok and len(transitions)==len(expected_tail)
        if index and (c.get('cycle')!=cycles[index-1].get('cycle',0)+1 or loaded!=cycles[index-1].get('loaded_after_shot',0)-1):cycle_ok=False
    checks['each_real_shot_and_direct_native_tail']=cycle_ok
    if fixture.get('requires_held_support_return') is True:
        hand=game.get('hand_ownership',{});events=hand.get('events',[]);returns=[]
        support_ok=physical.get('support_returns')==count and hand.get('events_dropped')==0
        for c in cycles:
            token,claim,source,observed,deadline=[c.get(k,0) for k in ('support_token','support_claim','support_source_sequence','support_observed_ns','support_deadline_ns')]
            valid=c.get('support_returned') is True and token>0 and claim>0 and source>c.get('source_sequence',0) and c.get('ready_ns',0)<=observed<deadline
            matches=[]
            for row in events:
                left,right=row.get('left') or {},row.get('right') or {}
                if row.get('reason')=='pump_support_return' and row.get('support_holding') is True and row.get('support_token')==token and \
                   left.get('id')==claim and left.get('kind')==2 and left.get('input_generation')==source and left.get('deadline_ns')==deadline and \
                   right.get('kind')==1 and left.get('prerequisite')==right.get('id') and left.get('item')==right.get('item')==row.get('physical_item') and \
                   left.get('generation')==right.get('generation')==row.get('equip_generation') and source<=row.get('raw_generation',0) and observed<=row.get('now_ns',0)<deadline:
                    matches.append(row.get('serial'))
            support_ok=support_ok and valid and len(matches)==1;returns.append(matches)
        checks['acknowledged_held_support_return']=support_ok and len({c.get('support_token') for c in cycles})==count
        details['support_return_hand_events']=returns

    details.update(shot_update_ids=shot_ids,native_tail_commit_ids=tail_ids)
    # Overall recording ends before the30s receiver drain. Bound the trial by
    # observed Fire/Ready timestamps, rather than declaring that drain complete.
    recording=recording_bounds(flow,fixture)
    checks['bounded_trial_recording_complete']=recording['bounded_trial_complete']
    details['recording']=recording
    return dict(schema='fvr.bc2.physical_pump_audit.v2',passed=all(checks.values()),checks=checks,details=details,
                recorder_complete=recording['recorder_complete'],bounded_trial_complete=recording['bounded_trial_complete'],
                headset_verified=False,normal_manual_pump_admitted=False)

def recording_bounds(flow,fixture):
    gaps=('dropped','rejected','record_lock_drops','record_begin_lock_drops','record_end_lock_drops',
          'owner_lock_drops','context_misses','nesting_misses')
    counts={k:flow.get(k) for k in gaps}
    start=flow.get('start_ns',0);duration=flow.get('window_seconds',0)*1000000000
    fires=[r['now_ns'] for r in fixture.get('transitions',[]) if r.get('phase')==2 and r.get('failure')==0]
    finishes=[r['ready_ns'] for r in fixture.get('cycles',[])]
    first=min(fires) if fires else 0;last=max(finishes) if finishes else 0
    valid_bounds=0<start<=first<=last<start+duration and len(fires)==len(finishes)==fixture.get('requested_cycles')
    journal=flow.get('observation_read_misses',{});rows=journal.get('rows',[])
    journal_valid=journal.get('schema')==1 and journal.get('drained') is True and journal.get('overflow')==0 and \
        journal.get('count')==len(rows)==flow.get('read_misses',-1)+flow.get('server_read_misses',-1) and \
        0<=len(rows)<=journal.get('capacity',-1) and all(type(r.get('now_ns')) is int and r['now_ns']>=start for r in rows)
    within=[r for r in rows if first<=r.get('now_ns',0)<=last] if valid_bounds else rows
    completion=flow.get('completion_journal',{})
    no_drops=all(value==0 for value in counts.values()) and all(completion.get(k)==0 for k in ('pending','overflow','rejected_drain'))
    complete=valid_bounds and journal_valid and not within and no_drops
    overall=no_drops and journal_valid and not rows and flow.get('window_expired_calls')==0
    return dict(recording_start_ns=start,recording_end_exclusive_ns=start+duration,first_fire_ns=first,last_ready_ns=last,
                bounds_valid=valid_bounds,journal_complete=journal_valid,in_window_read_misses=within,
                out_of_window_read_miss_count=len(rows)-len(within),recorder_complete=overall,bounded_trial_complete=complete,
                counters=counts,window_expired_calls=flow.get('window_expired_calls'),completion_journal=completion)

def audit(trace,receiver,before,after,completion,inputs):
    try:
        if not all(isinstance(doc,dict) for doc in (trace,receiver,before,after,completion)) or not isinstance(inputs,list):raise ValueError('Expected JSON objects and input row list')
        return audit_checked(trace,receiver,before,after,completion,inputs)
    except (KeyError,TypeError,ValueError,IndexError,AttributeError,OverflowError) as error:
        return dict(schema='fvr.bc2.physical_pump_audit.v2',passed=False,checks={'well_formed_evidence':False},
                    error=str(error),recorder_complete=False,bounded_trial_complete=False,
                    headset_verified=False,normal_manual_pump_admitted=False)

def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--run',type=Path,required=True);parser.add_argument('--output',type=Path,required=True);a=parser.parse_args()
    if a.output.exists():raise ValueError('Preserve earlier audit evidence')
    names=['native-trace.json','receiver/result.json','receiver/pump-preflight.json','receiver/pump-postflight.json','completion.json']
    paths=[a.run/n for n in names];inputs=a.run/'receiver/physical-pump-input.jsonl'
    try:
        docs=[json.loads(p.read_text(encoding='utf-8-sig')) for p in paths]
        rows=[json.loads(x) for x in inputs.read_text(encoding='utf-8-sig').splitlines() if x.strip()]
        result=audit(*docs,rows)
    except (OSError,ValueError) as error:
        result=dict(schema='fvr.bc2.physical_pump_audit.v2',passed=False,checks={'evidence_files_available':False},error=str(error),
                    recorder_complete=False,bounded_trial_complete=False,headset_verified=False,normal_manual_pump_admitted=False)
    result['source_sha256']={str(p.relative_to(a.run)):hashlib.sha256(p.read_bytes()).hexdigest() for p in [*paths,inputs] if p.is_file()}
    a.output.write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8');print(json.dumps({'passed':result['passed'],'failed':[k for k,v in result['checks'].items() if not v],'output':str(a.output)}))
    return 0 if result['passed'] else 2
if __name__=='__main__':raise SystemExit(main())
