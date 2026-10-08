"""Offline joined pump capture checks. No native calls or runtime authority."""
from __future__ import annotations
import argparse, hashlib, importlib.util, json, math
from pathlib import Path

OWNER=('player','soldier','weak','weapon','actor_generation','equip_generation','space')
SERVER=('server_player','server_soldier','server_item')
EXPECTED_HOLD_AUDITOR_SHA256='8838ad170f102857d1035fa4749b8e60806c8864849a26f0f77aa98bd35c4293'
def integer(v): return type(v) is int
def same(a,b,keys): return all(a.get(k)==b.get(k) for k in keys)
def rigid(m):
    if not isinstance(m,list) or len(m)!=4 or any(not isinstance(r,list) or len(r)!=4 for r in m):return False
    if any(type(x) not in (int,float) or not math.isfinite(x) for r in m for x in r):return False
    if any(abs(m[i][3])>1e-4 for i in range(3)) or abs(m[3][3]-1)>1e-4:return False
    dot=lambda a,b:sum(x*y for x,y in zip(a,b))
    if any(abs(dot(m[i][:3],m[j][:3])-(i==j))>1e-3 for i in range(3) for j in range(3)):return False
    a,b,c=(m[i][:3] for i in range(3));cross=[a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]]
    return abs(dot(cross,c)-1)<1e-3
def load_auditor(path):
    if hashlib.sha256(path.read_bytes()).hexdigest()!=EXPECTED_HOLD_AUDITOR_SHA256:
        raise ValueError('Hold auditor differs from the reviewed baseline; import refused')
    spec=importlib.util.spec_from_file_location('pump_hold_baseline',path);module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module);return module
def audit(trace,receiver,before,after,hold_audit):
    base=hold_audit(trace,receiver,before,after)
    result=dict(passed=False,hold_audit=base,checks={},capture_observations_verified=False,
                normal_manual_pump_admitted=False,headset_verified=False,submitted_mesh_proven=False)
    checks=result['checks'];checks['native_hold_audit_passed']=base.get('passed') is True
    if not checks['native_hold_audit_passed']:return result
    flow=trace['gameplay']['reload_flow'];hold=flow['diagnostic_hold'];records=flow['records']
    capture=trace['gameplay'].get('rig_publication',{}).get('pump_part_capture',{})
    samples=capture.get('samples',[])
    checks['complete_capture']=capture.get('schema_version')==1 and capture.get('runtime_authority') is False and capture.get('submitted_mesh_proven') is False and integer(capture.get('rows')) and capture['rows']==len(samples) and 0<len(samples)<=1024 and capture.get('dropped')==0 and capture.get('rejected')==0
    if not checks['complete_capture']:return result
    held=next(r for r in records if r.get('hold_applied') is True);identity=held['before'];firing=hold['firing'];counts=(hold['loaded'],hold['reserve'])
    invalid=[];ambiguous=[];held_samples=[];resume=[[],[],[]];previous_time=0;cohort=None
    for ix,row in enumerate(samples):
        try:
            pre,post=row['before'],row['after'];start=pre['observed_ns'];end=post['completed_ns']
            valid=all(integer(t) and t>0 for t in (start,end,pre['completed_ns'],post['observed_ns'],pre['deadline_ns'],post['deadline_ns'],row['selected_observed_ns'],row['selected_deadline_ns'],row['input_deadline_ns']))
            valid=valid and start<=pre['completed_ns']<=post['observed_ns']<=end and end-start<=50_000_000
            valid=valid and (not previous_time or start-previous_time>=15_000_000);previous_time=start
            observed=row['input_observed_ns'];valid=valid and integer(observed) and (observed==0 or 0<observed<=start) and row['input_deadline_ns']>end
            valid=valid and row['selected_observed_ns']<=start and end<row['selected_deadline_ns']<=row['selected_observed_ns']+250_000_000
            valid=valid and all(s['observed_ns']<=end<s['deadline_ns']<=s['observed_ns']+250_000_000 and same(s,identity,OWNER+SERVER) for s in (pre,post))
            valid=valid and same(pre,post,('weapon_data','firing_data')) and all(integer(pre[k]) and pre[k]>=0x10000 for k in ('weapon_data','firing_data'))
            key=tuple(pre[k] for k in ('weapon_data','firing_data'))+tuple(row[k] for k in ('rig_fingerprint','rig_animation','rig_skeleton','rig_pose'))
            valid=valid and all(integer(v) and v>0 for v in key) and (cohort is None or key==cohort);cohort=key if cohort is None else cohort
            valid=valid and integer(row['input_sequence']) and row['input_sequence']>0 and rigid(row['part_from_weapon_m']) and rigid(row['wrist_from_weapon_m'])
            stable=True;valid=valid and len(pre['branches'])==len(post['branches'])==3
            for n,(a,b) in enumerate(zip(pre['branches'],post['branches'])):
                valid=valid and a['firing']==b['firing']==firing[n]
                stable=stable and same(a,b,('current','previous','next','loaded','reserve'))
                for s in (a,b):
                    valid=valid and all(integer(s[k]) and 0<=s[k]<=15 for k in ('current','previous','next')) and type(s['timer']) in (int,float) and math.isfinite(s['timer'])
                    valid=valid and (s['loaded'],s['reserve']) in (counts,(counts[0]-1,counts[1]),(counts[0]+1,counts[1]))
                # Same native copy/state/counts must actually appear near this rig bracket.
                valid=valid and any(r['before']['branch']==n and min(abs(r['begin_ns']-start),abs(r['end_ns']-end))<=15_000_000 and any(same(a,s,('current','previous','next','loaded','reserve')) for s in (r['before'],r['after'])) for r in records)
            valid=valid and row['stable_state_bracket'] is stable
            if not valid:invalid.append(ix);continue
            if not stable:ambiguous.append(ix);continue
            branches=pre['branches']
            if hold['begin_ns']<=start<=end<=hold['deadline_ns'] and all((s['current'],s['previous'],s['next'],s['loaded'],s['reserve'])==(7,6,8,*counts) for s in branches):
                # Join held rig sample to an actual applied/restored original Update receipt per copy.
                if all(any(r.get('hold_applied') is True and r['before']['branch']==n and min(abs(r['begin_ns']-start),abs(r['end_ns']-end))<=15_000_000 for r in records) for n in range(3)):held_samples.append(ix)
            if start>=hold['deadline_ns']:
                for n,s in enumerate(branches):
                    if (s['loaded'],s['reserve'])==counts:resume[n].append(s['current'])
        except (KeyError,TypeError,ValueError,OverflowError,AttributeError,IndexError):invalid.append(ix)
    def collapse(values):return [v for i,v in enumerate(values) if not i or v!=values[i-1]]
    def cycles(values):
        seq=collapse(values);return sum(seq[i:i+3]==[8,1,2] for i in range(len(seq)-2))
    completion=[]
    for n in range(3):
        boundaries=[]
        for r in sorted(records,key=lambda r:r['begin_ns']):
            if r['before']['branch']!=n:continue
            for time,s in ((r['begin_ns'],r['before']),(r['end_ns'],r['after'])):
                if time>=hold['deadline_ns'] and (s['loaded'],s['reserve'])==counts:boundaries.append((time,s['current']))
        completion.append(cycles([v for _,v in sorted(boundaries)]))
    checks['valid_capture_rows']=not invalid
    checks['stable_held_original_receipt_samples']=len(held_samples)>=2
    # Fifteen-ms rig sampling may skip short states8/1. Native recorder, not
    # interpolated rig samples, proves those transitions. Require captured ready.
    checks['captured_native_ready_each_copy']=all(2 in v for v in resume)
    checks['exactly_one_native_completion_each_copy']=completion==[1,1,1]
    result['details']=dict(invalid_rows=invalid,ambiguous_rows=ambiguous,held_rows=held_samples,completion_count_per_branch=completion,input_observed_time_missing=sum(isinstance(r,dict) and r.get('input_observed_ns')==0 for r in samples),sampled_rig_is_not_submitted_mesh=True)
    result['passed']=result['capture_observations_verified']=all(checks.values())
    return result
def main(argv=None):
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--hold-auditor',type=Path,required=True);p.add_argument('--trace',type=Path,required=True);p.add_argument('--receiver',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args(argv)
    paths=[a.trace,a.receiver/'result.json',a.receiver/'pump-hold-preflight.json',a.receiver/'pump-hold-postflight.json']
    for source in paths+[a.hold_auditor]:
        collision=a.output.resolve()==source.resolve()
        try:collision=collision or a.output.samefile(source)
        except OSError:pass
        if collision:p.error('Output must not overwrite source evidence or the reviewed auditor')
    try:
        raw=[x.read_bytes() for x in paths];report=audit(*(json.loads(x) for x in raw),load_auditor(a.hold_auditor).audit)
        report['sources']=[dict(path=str(path),sha256=hashlib.sha256(data).hexdigest()) for path,data in zip(paths,raw)]+[dict(path=str(a.hold_auditor),sha256=hashlib.sha256(a.hold_auditor.read_bytes()).hexdigest())]
    except (OSError,ValueError,KeyError,TypeError,OverflowError,AttributeError,IndexError) as e:report=dict(passed=False,error=str(e),normal_manual_pump_admitted=False)
    a.output.parent.mkdir(parents=True,exist_ok=True);a.output.write_text(json.dumps(report,indent=2,allow_nan=False)+'\n');return 0 if report['passed'] else 1
if __name__=='__main__':raise SystemExit(main())

