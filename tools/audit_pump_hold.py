"""Offline evidence audit for the isolated SPAS pump hold. Never accesses a process."""
from __future__ import annotations
import argparse,hashlib,json,math,struct
from pathlib import Path

EXE_SHA='3911fcc8914b0158c434cee36f19295316e715d4c9a0e70c857e8c39e3d54258'
OWNER=('player','soldier','weak','weapon','actor_generation','equip_generation','space')
def integer(v):return type(v) is int
def counts(s):return (s.get('loaded'),s.get('reserve'))
def owner(s):return tuple(s.get(k) for k in OWNER)
def retained(r):return r.get('finished') is True and r.get('identity_retained') is True and isinstance(r.get('after'),dict)
def context(c):
    raw=c.get('bytes',[])
    if c.get('decoded_valid') is not True or len(raw)!=48 or not all(integer(v) and 0<=v<=255 for v in raw):return None
    raw=bytes(raw);delta=struct.unpack_from('<f',raw,24)[0];multiplier=struct.unpack_from('<f',raw,32)[0];flags=int.from_bytes(raw[44:48],'little')
    if not math.isfinite(delta) or not 0<delta<=.05 or not math.isfinite(multiplier) or not 0<multiplier<=4 or flags&~7 or any(v>1 for v in raw[36:41]):return None
    # Serialized decimal floats are rounded; the exact restore proof uses bytes.
    if c.get('input_flags')!=flags:return None
    return raw,delta,multiplier,flags
def preflight(p):
    if p.get('passed') is not True or p.get('read_only') is not True or p.get('process_writes') is not False or p.get('native_calls') is not False or p.get('rejected'):
        return None
    samples=p.get('samples',[])
    if len(samples)!=2:return None
    a,b=samples
    try:
        if not 0<b['monotonic_ns']-a['monotonic_ns']<=500000000:return None
        fields=('client_owner','server_player','server_soldier','server_item','server_firing','asset_name')
        if any(a.get(k)!=b.get(k) for k in fields):return None
        for s in samples:
            t=s['state']
            if s.get('identity_coherent') is not True or s['asset_name']!='SPAS12_sp' or (t['current'],t['next'])!=(2,2):return None
            if not all(integer(t[k]) and 0<=t[k]<=1000000 for k in ('loaded','reserve')):return None
        if counts(a['state'])!=counts(b['state']):return None
        return b
    except (KeyError,TypeError):return None

def audit(trace,receiver,before,after):
    checks={};details={};flow=trace.get('gameplay',{}).get('reload_flow',{});hold=flow.get('diagnostic_hold',{})
    records=flow.get('records',[]);a,b=preflight(before),preflight(after)
    checks['preflight_and_postflight']=bool(a and b)
    if not a or not b:return {'passed':False,'mechanical_observations_verified':False,'observer_complete':False,'checks':checks,'details':details,'headset_verified':False}
    c=a['client_owner'];co=(c.get('player'),c.get('actor'),c.get('weak'),c.get('selected_weapon'))
    post=b['client_owner'];postco=(post.get('player'),post.get('actor'),post.get('weak'),post.get('selected_weapon'))
    loaded,reserve=counts(a['state']);final=counts(b['state'])
    pid=trace.get('pid')
    checks['same_process_and_native_owner']=all(integer(v) and v>0 for v in (pid,before.get('pid'),after.get('pid'))) and before.get('pid')==after.get('pid')==pid and co==postco and all(a[k]==b[k] for k in ('server_player','server_soldier','server_item','server_firing'))
    checks['native_owner_shape']=all(integer(v) and 0x10000<=v<=0xffffffff for v in co+(a['server_player'],a['server_soldier'],a['server_item'],a['server_firing']))
    checks['supported_executable']=before.get('executable_sha256','').lower()==after.get('executable_sha256','').lower()==EXE_SHA
    checks['positive_round_scope']=loaded>=3
    checks['two_native_rounds_no_reserve_change']=final==(loaded-2,reserve)
    checks['receiver_complete']=receiver.get('pump_hold_fixture') is True and receiver.get('consumed_pairs')==240 and receiver.get('async_timeouts')==0 and receiver.get('pump_fire_input_samples',0)>0
    checks['clean_shutdown']=trace.get('hooks_disabled') is True and flow.get('drained') is True and flow.get('in_flight')==0
    begin,end=hold.get('begin_ns',0),hold.get('deadline_ns',0)
    checks['finite_exact_hold']=hold.get('enabled') is True and hold.get('target')==1 and hold.get('code_verified') is True and hold.get('phase')==3 and hold.get('reason')==1 and begin>0 and end-begin==350000000 and hold.get('duration_ns')==350000000
    firing=hold.get('firing',[])
    checks['three_exact_firing_objects']=len(firing)==3 and len(set(firing))==3 and all(integer(v) and 0x10000<=v<=0xffffffff for v in firing) and firing[2]==a['server_firing'] and hold.get('client_weapon')==co[3] and all(hold.get(k)==a[k] for k in ('server_player','server_soldier','server_item'))
    checks['held_counts_after_first_shot']=(hold.get('loaded'),hold.get('reserve'))==(loaded-1,reserve)
    applied=hold.get('applied',[]);restored=hold.get('restored',[])
    held=[r for r in records if r.get('hold_applied') is True]
    held_ok=bool(held);branch_held=[0,0,0];owners=set();invalid_held=[]
    for r in held:
        pre=r.get('before') or {};post=r.get('after') or {};n=pre.get('branch',-1)
        cb=r.get('context_before') or {};ca=r.get('context_after') or {};raw0=cb.get('bytes',[]);raw1=ca.get('bytes',[])
        original=r.get('hold_original_delta_bits');decoded=context(cb);decoded_after=context(ca)
        delta=int.from_bytes(decoded[0][24:28],'little') if decoded else None
        okay=(integer(n) and 0<=n<3 and len(firing)==3 and r.get('kind')==0 and retained(r) and
            r.get('hold_requested') is True and r.get('hold_restored') is True and r.get('hold_unexpected_native_write') is False and r.get('hold_before_restore_bits')==0 and
            decoded is not None and decoded_after is not None and raw0[24:28]==raw1[24:28] and integer(original) and delta==original and
            decoded[3]==0 and not any(decoded[0][38:41]) and
            pre.get('firing')==firing[n] and (pre.get('current'),pre.get('previous'),pre.get('next'))==(7,6,8) and
            isinstance(pre.get('timer'),(int,float)) and math.isfinite(pre['timer']) and 0<pre['timer']<=.5 and integer(pre.get('flags_a8')) and not pre['flags_a8']&(8|16) and
            counts(pre)==(loaded-1,reserve) and all(pre.get(k)==post.get(k) for k in OWNER+('firing','branch','wrapper_offset','server_player','server_soldier','server_item','current','previous','next','timer','loaded','reserve','flags_a8')) and
            pre.get('wrapper_offset')==(0x3c if n==0 else 0x40 if n==1 else 0x10) and
            all(integer(pre.get(k)) and pre[k]>0 for k in OWNER[4:]) and
            tuple(pre.get(k) for k in OWNER[:4])==co and
            (n!=2 or all(pre.get(k)==a[k] for k in ('server_player','server_soldier','server_item'))) and
            begin-50000000<=r.get('begin_ns',0)<end and begin<=r.get('end_ns',0)<=end+50000000 and 0<=r.get('end_ns',0)-r.get('begin_ns',0)<=50000000)
        if okay:branch_held[n]+=1;owners.add(owner(pre))
        else:held_ok=False;invalid_held.append(r.get('id'))
    checks['held_original_once_delta_and_state_receipts']=held_ok and all(branch_held) and len(owners)==1
    checks['no_patch_or_restore_failure']=all(integer(hold.get(k)) and hold[k]==0 for k in ('patch_failures','restore_failures'))
    checks['native_hold_counter_consistency']=len(applied)==3 and applied==restored and all(integer(v) and v>0 for v in applied) and sum(applied)==hold.get('original_calls')
    checks['all_hold_receipts_recorded']=applied==restored==branch_held and hold.get('original_calls')==len(held)
    coverage=[]
    for n in range(3):
        rows=sorted((r for r in held if (r.get('before') or {}).get('branch')==n),key=lambda r:r.get('begin_ns',0))
        coverage.append(bool(rows) and rows[0]['begin_ns']<=begin+50000000 and rows[-1]['end_ns']>=end-50000000 and
            all(r['before'].get('timer')==rows[0]['before'].get('timer') for r in rows))
    escaped=[r.get('id') for r in records if r.get('kind')==0 and begin<=r.get('begin_ns',0)<end and r.get('hold_applied') is not True]
    checks['three_copy_hold_covers_bounded_interval']=all(coverage) and not escaped
    pinned=next(iter(owners),None);observations=[[],[],[]];shot_updates=[[],[],[]];unretained=[];unexpected=[]
    for r in records:
        pre=r.get('before') or {};post=r.get('after') or {};n=pre.get('branch',-1)
        if not (integer(n) and 0<=n<3):unexpected.append(r.get('id'));continue
        if not retained(r):unretained.append(r.get('id'));continue
        if pinned is None or owner(pre)!=pinned or owner(post)!=pinned or len(firing)!=3 or pre.get('firing')!=firing[n] or post.get('firing')!=firing[n] or post.get('branch')!=n or pre.get('wrapper_offset')!=post.get('wrapper_offset') or pre.get('wrapper_offset')!=(0x3c if n==0 else 0x40 if n==1 else 0x10) or not a['monotonic_ns']<r.get('begin_ns',0)<=r.get('end_ns',0)<b['monotonic_ns'] or (n==2 and any(pre.get(k)!=a[k] or post.get(k)!=a[k] for k in ('server_player','server_soldier','server_item'))):
            unexpected.append(r.get('id'));continue
        for side,t in ((pre,r.get('begin_ns',0)),(post,r.get('end_ns',0))):
            if counts(side) not in ((loaded,reserve),(loaded-1,reserve),(loaded-2,reserve)):unexpected.append(r.get('id'))
            observations[n].append((t,side))
        if r.get('kind')==0 and pre.get('loaded')!=post.get('loaded'):shot_updates[n].append(r)
    checks['observed_owner_and_counts_retained']=pinned is not None and not unexpected
    checks['record_ids_unique']=all(integer(r.get('id')) and r['id']>0 for r in records) and len({r['id'] for r in records})==len(records)
    checks['no_reload_transfer']=all(r.get('kind')!=2 for r in records)
    resumed=[];baselines=[];finals=[]
    for n,rows in enumerate(observations):
        rows.sort(key=lambda x:x[0]);phase=0;ids=[]
        for t,s in rows:
            if t<end or counts(s)!=(loaded-1,reserve):continue
            if phase==0 and s.get('current')==8:phase=1;ids.append(t)
            elif phase==1 and s.get('current')==1:phase=2;ids.append(t)
            elif phase==2 and (s.get('current'),s.get('next'))==(2,2):phase=3;ids.append(t)
        resumed.append(phase==3);details.setdefault('resume_times_ns',[]).append(ids)
        baselines.append(any(t<begin and counts(s)==(loaded,reserve) and (s.get('current'),s.get('next'))==(2,2) for t,s in rows))
        finals.append(bool(rows) and counts(rows[-1][1])==final and (rows[-1][1].get('current'),rows[-1][1].get('next'))==(2,2))
    checks['each_copy_has_before_and_final_idle_counts']=all(baselines) and all(finals)
    checks['each_copy_resumes_native_8_1_2']=all(resumed)
    origin=receiver.get('controls_started_ms');shot_ok=integer(origin)
    for rows in shot_updates:
        rows.sort(key=lambda r:r.get('begin_ns',0))
        if len(rows)!=2:shot_ok=False;continue
        for n,r in enumerate(rows):
            pre,post=r['before'],r['after'];start=origin+(3000 if n==0 else 6000) if integer(origin) else 0
            shot_context=context(r.get('context_before') or {})
            if counts(pre)!=(loaded-n,reserve) or counts(post)!=(loaded-n-1,reserve) or not start<=r.get('begin_tick_ms',-1)<=start+230 or shot_context is None or shot_context[3]!=1:shot_ok=False
    checks['two_ordinary_shot_windows_each_copy']=shot_ok
    gap_names=('owner_lock_drops','record_lock_drops','dropped','rejected','window_expired_calls','read_misses','server_read_misses','context_misses','nesting_misses')
    gaps={**{k:flow.get(k) for k in gap_names},
          'unretained_record_ids':unretained,'unexpected_owner_or_counts':sorted(set(unexpected))}
    complete=all(gaps[k]==0 for k in gap_names) and not unretained and not unexpected and checks['all_hold_receipts_recorded']
    details.update(gaps=gaps,invalid_held_ids=invalid_held,escaped_hold_update_ids=escaped,held_rows_per_branch=branch_held,shot_update_ids=[[r['id'] for r in x] for x in shot_updates],
        unrelated_or_unleased_native_calls=flow.get('owner_misses'),
        dynamic_bone_state_correlation='Requires separate state-labelled part/hand capture review; this audit does not establish it.')
    mechanical=all(v for k,v in checks.items() if k not in ('all_hold_receipts_recorded',))
    return {'passed':all(checks.values()) and complete,'mechanical_observations_verified':mechanical,'observer_complete':complete,
        'checks':checks,'details':details,'headset_verified':False,'normal_manual_pump_admitted':False}

def main():
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('--trace',type=Path,required=True);ap.add_argument('--receiver',type=Path,required=True);ap.add_argument('--output',type=Path,required=True);args=ap.parse_args()
    paths=[args.trace,args.receiver/'result.json',args.receiver/'pump-hold-preflight.json',args.receiver/'pump-hold-postflight.json']
    try:
        raw=[p.read_bytes() for p in paths];report=audit(*(json.loads(x) for x in raw));report['sources']=[{'path':str(p),'sha256':hashlib.sha256(v).hexdigest()} for p,v in zip(paths,raw)]
    except (OSError,ValueError,KeyError,TypeError,OverflowError) as e:report={'passed':False,'observer_complete':False,'error':str(e)}
    args.output.parent.mkdir(parents=True,exist_ok=True);args.output.write_text(json.dumps(report,indent=2,allow_nan=False)+'\n');print(json.dumps({'passed':report['passed'],'output':str(args.output)}));return 0 if report['passed'] else 1
if __name__=='__main__':raise SystemExit(main())
