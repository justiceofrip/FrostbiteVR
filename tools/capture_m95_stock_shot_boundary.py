"""Read-only paired M95 native-boundary capture for the finite original-shot test."""
import argparse,hashlib,json,time
from pathlib import Path
from capture_reload_server import capture,discover
from capture_reload_state import Image,Inspector
from read_bc2 import Process

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--pid',type=int,required=True);p.add_argument('--output',type=Path,required=True)
    a=p.parse_args()
    if a.output.exists():raise ValueError('Preserve existing evidence')
    report=dict(pid=a.pid,read_only=True,native_calls=False,process_writes=False,input_or_focus_changes=False,samples=[],passed=False,rejected=[])
    process=None
    try:
        image=Image();report['executable_sha256']=hashlib.sha256(image.data).hexdigest()
        if report['executable_sha256']!='3911fcc8914b0158c434cee36f19295316e715d4c9a0e70c857e8c39e3d54258':raise ValueError('Unexpected game executable')
        process=Process(a.pid);inspector=Inspector(process,image);binding=discover(image,process)
        for n in range(2):
            if n:time.sleep(.03)
            row=dict(monotonic_ns=time.perf_counter_ns(),**capture(inspector,binding))
            if row['asset_name']!='M95_sp' or row['asset_path']!='Objects/Weapons/Handheld/BU_sni_M95/SP_sni_M95':raise ValueError('Exact selected M95_sp required')
            state=row['state']
            if (state['current'],state['next'])!=(2,2) or not 1<=state['loaded']<=5 or state['reserve']<0:raise ValueError('Loaded native M95 idle required')
            row['state'].pop('raw_hex',None);report['samples'].append(row)
        left,right=report['samples']
        if any(left[k]!=right[k] for k in ('client_owner','server_player','server_soldier','server_item','server_firing','weapon_data','firing_data','ammo_address')):raise ValueError('Native owner changed')
        if any(left['state'][k]!=right['state'][k] for k in ('loaded','reserve')):raise ValueError('Counts changed during paired capture')
        report['passed']=True
    except (OSError,ValueError,KeyError,RuntimeError) as e:report['rejected'].append(str(e))
    finally:
        if process is not None:process.close()
    a.output.write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(dict(passed=report['passed'],rejected=report['rejected'],output=str(a.output))))
    return 0 if report['passed'] else 1
if __name__=='__main__':raise SystemExit(main())
