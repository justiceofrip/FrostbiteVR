"""Bounded read-only LMG ownership/configuration/state observation.

Reuses existing signature/reflection-verified BC2 readers. No hooks, native calls,
input, focus, or process writes. Not executed as part of offline job generation.
"""
import argparse,datetime,hashlib,json,math,sys,time
from pathlib import Path

def select_job(jobs,resource):
    if jobs.get('schema')!='fvr.bc2.lmg_binding_jobs.v1' or jobs.get('runtime_admission')!='none':raise ValueError('Unsupported/non-observational job source')
    matches=[(m,d) for m in jobs['models'] for d in m['definitions'] if d['resource']==resource]
    if len(matches)!=1:raise ValueError('Select one exact configuration resource, never native Name alone')
    model,definition=matches[0]
    if model['runtime_enabled'] is not False or definition['runtime_enabled'] is not False:raise ValueError('Job must remain observational')
    return model,definition

def read_sample(inspector,binding,definition,server_capture):
    before=time.perf_counter_ns()
    owner=inspector.owner();weapon=inspector.weapon(owner['selected_weapon'],owner['selected_slot']);selector=definition['selector']
    if weapon['asset_name']!=selector['asset_name'] or weapon['asset_path']!=selector['asset_path']:raise ValueError('Selected native asset does not match exact job')
    server=server_capture(inspector,binding)
    if server['client_owner']!=owner or server['weapon_data']!=weapon['data'] or server['firing_data']!=weapon['firing_data'] or server['ammo_address']!=weapon['ammo_address']:raise ValueError('Server/client configuration mismatch')
    states={str(branch):inspector.state(weapon,branch) for branch in (0x3c,0x40)}
    repeated={str(branch):inspector.state(weapon,branch) for branch in (0x3c,0x40)}
    if states!=repeated:raise ValueError('Client firing state changed during repeated read')
    firing=[states[str(branch)]['address'] for branch in (0x3c,0x40)]+[server['server_firing']]
    if len(set(firing))!=3:raise ValueError('Three distinct native firing copies required')
    if inspector.owner()!=owner or inspector.weapon(owner['selected_weapon'],owner['selected_slot'])!=weapon:raise ValueError('Owner/equipment/configuration changed while observing')
    end=time.perf_counter_ns()
    if end-before>100_000_000:raise ValueError('Observation exceeded100ms coherence window')
    server=dict(server);server['state']=dict(server['state']);server['state'].pop('raw_hex',None)
    return dict(begin_ns=before,end_ns=end,native_owner=owner,configuration=weapon,client_states=states,server=server,
        identity_coherent=True,atomic_three_copy_snapshot=False,native_authority=False)

def cohort(sample):
    # Immutable serialization of structural identities only. Native counts and
    # timers may progress; an actor/item/configuration/firing replacement may not.
    value=dict(owner=sample['native_owner'],configuration=sample['configuration'],
        client_firing=[sample['client_states'][str(branch)]['address'] for branch in (0x3c,0x40)],
        server={k:v for k,v in sample['server'].items() if k!='state'})
    return json.dumps(value,sort_keys=True,separators=(',',':'),allow_nan=False)

def retain_session(sample,baseline):
    if cohort(sample)!=baseline:raise ValueError('Session owner/item/configuration cohort changed; start a new capture')
    return {**sample,'session_identity_retained':True}

def open_readers(executable,pid,Image,Process,Inspector,discover):
    # Use exactly the selected executable for BOTH disk signatures and process
    # path admission. No dependency on a developer installation/local config.
    executable=Path(executable).resolve();image=Image(executable)
    process=Process(pid,expected_path=executable)
    try:
        inspector=Inspector(process,image);binding=discover(image,process)
        return image,process,inspector,binding
    except BaseException:
        process.close();raise

def configured_mesh_matches(links,configuration,expected):
    if links.get('identity_coherent') is not True or links.get('weapon')!=configuration['weapon'] or links.get('data')!=configuration['data'] or links.get('asset_name')!=configuration['asset_name']:return False
    return any(mesh.get('asset_path')==expected.removesuffix('.res') for state in links.get('states',[]) for mesh in state.get('meshes',[]))

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--executable',type=Path,required=True)
    p.add_argument('--tools',type=Path,required=True);p.add_argument('--jobs',type=Path,required=True);p.add_argument('--resource',required=True)
    p.add_argument('--pid',type=int,required=True);p.add_argument('--seconds',type=float,default=15);p.add_argument('--interval-ms',type=float,default=20);p.add_argument('--output',type=Path,required=True)
    a=p.parse_args()
    if a.pid<=0 or not math.isfinite(a.seconds) or not 0<=a.seconds<=20 or not math.isfinite(a.interval_ms) or not 20<=a.interval_ms<=1000:p.error('Require positive PID,0..20seconds,20..1000ms interval')
    raw=a.jobs.read_bytes()
    if len(raw)>32*1024*1024:raise ValueError('Job metadata bound')
    model,definition=select_job(json.loads(raw),a.resource);sys.path.insert(0,str(a.tools.resolve()))
    from capture_reload_state import Image,Inspector
    from capture_reload_server import discover,capture
    from read_bc2 import Process
    report=dict(schema='fvr.bc2.lmg_native_observation.v1',pid=a.pid,utc=datetime.datetime.now(datetime.timezone.utc).isoformat(),read_only=True,
        process_writes=False,native_calls=False,input_or_focus_changes=False,authority_proven=False,job_sha256=hashlib.sha256(raw).hexdigest(),
        exact_resource=definition['resource'],expected_mesh=model['mesh_resource'],samples=[],rejected=[])
    process=None
    try:
        image,process,inspector,binding=open_readers(a.executable,a.pid,Image,Process,Inspector,discover)
        report.update(executable_sha256=hashlib.sha256(image.data).hexdigest(),binding=binding)
        initial=read_sample(inspector,binding,definition,capture);links=inspector.mesh_links(initial['configuration'])
        if not configured_mesh_matches(links,initial['configuration'],model['mesh_resource']):raise ValueError('Expected authored body mesh absent from verified native configuration')
        if inspector.owner()!=initial['native_owner']:raise ValueError('Owner changed during initial mesh observation')
        report.update(configured_mesh_links=links,selected_state_or_visible_mesh_verified=False,executable=str(a.executable.resolve()))
        baseline=cohort(initial);report['samples'].append(retain_session(initial,baseline))
        print(json.dumps(dict(ready=True,pid=a.pid,resource=a.resource,seconds=a.seconds)),flush=True)
        deadline=time.perf_counter()+a.seconds
        while True:
            try:report['samples'].append(retain_session(read_sample(inspector,binding,definition,capture),baseline))
            except (OSError,ValueError,KeyError) as exc:report['rejected'].append(dict(observed_ns=time.perf_counter_ns(),reason=str(exc)))
            remaining=deadline-time.perf_counter()
            if remaining<=0:break
            time.sleep(min(a.interval_ms/1000,remaining))
    except (OSError,ValueError,KeyError,RuntimeError) as exc:report['rejected'].append(dict(reason=str(exc)))
    finally:
        if process is not None:process.close()
    report['reader_hashes']=[dict(name=name,sha256=hashlib.sha256((a.tools/name).read_bytes()).hexdigest()) for name in ('capture_reload_state.py','capture_reload_server.py','read_bc2.py')]
    a.output.write_text(json.dumps(report,indent=2,allow_nan=False)+'\n',encoding='utf-8')
    print(json.dumps(dict(samples=len(report['samples']),rejections=len(report['rejected']),output=str(a.output))))
    return 0 if report['samples'] else 1
if __name__=='__main__':raise SystemExit(main())
