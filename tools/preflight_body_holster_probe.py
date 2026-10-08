"""Read-only selected holster-profile/count observation; no input, hooks or writes."""
from __future__ import annotations
import argparse,hashlib,json,time
from pathlib import Path

def validate(report, asset="SPAS12_sp"):
    if asset not in ("SPAS12_sp", "XM8_sp_s"):raise ValueError("unsupported diagnostic profile")
    if report.get('read_only') is not True or report.get('process_writes') is not False or report.get('native_calls') is not False:
        raise ValueError('read-only capture required')
    samples=report.get('samples',[])
    if len(samples)!=2 or report.get('rejected'):raise ValueError('two complete samples required')
    a,b=samples
    keys=('client_owner','server_player','server_soldier','server_item','server_firing','asset_name')
    if any(a.get(k)!=b.get(k) for k in keys):raise ValueError('owner changed')
    if not 0<b['monotonic_ns']-a['monotonic_ns']<=500_000_000:raise ValueError('capture duration invalid')
    for s in samples:
        if s.get('identity_coherent') is not True or s.get('asset_name')!=asset:raise ValueError('selected coherent '+asset+' required')
        state=s['state']
        if state['current']!=2 or state['next']!=2:raise ValueError('ordinary idle weapon required')
        if not all(type(state[k]) is int and 0<=state[k]<=1_000_000 for k in ('loaded','reserve')):raise ValueError('finite nonnegative counts required')
    if any(a['state'][k]!=b['state'][k] for k in ('loaded','reserve')):raise ValueError('native counts changed')
    return dict(asset=b['asset_name'],loaded=b['state']['loaded'],reserve=b['state']['reserve'],authority_proven=False)

def main():
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('--pid',type=int,required=True);ap.add_argument('--output',type=Path,required=True)
    ap.add_argument('--asset',choices=('SPAS12_sp','XM8_sp_s'),default='SPAS12_sp')
    args=ap.parse_args();process=None
    report=dict(schema='fvr.bc2.body_holster_preflight.v1',pid=args.pid,read_only=True,process_writes=False,native_calls=False,input_or_focus_changes=False,samples=[],rejected=[],passed=False)
    try:
        from capture_reload_server import capture,discover
        from capture_reload_state import Image,Inspector
        from read_bc2 import Process
        image=Image();process=Process(args.pid);inspect=Inspector(process,image);binding=discover(image,process)
        report['executable_sha256']=hashlib.sha256(image.data).hexdigest()
        for n in range(2):
            if n:time.sleep(.03)
            sample=dict(monotonic_ns=time.perf_counter_ns(),**capture(inspect,binding));sample['state'].pop('raw_hex',None);report['samples'].append(sample)
        report['summary']=validate(report,args.asset);report['passed']=True
    except (OSError,ValueError,KeyError,RuntimeError,OverflowError) as exc:report['rejected'].append(str(exc))
    finally:
        if process is not None:process.close()
    args.output.parent.mkdir(parents=True,exist_ok=True);args.output.write_text(json.dumps(report,indent=2,allow_nan=False)+'\n',encoding='utf-8')
    print(json.dumps(dict(passed=report['passed'],output=str(args.output),rejected=report['rejected'])))
    return 0 if report['passed'] else 1
if __name__=='__main__':raise SystemExit(main())
