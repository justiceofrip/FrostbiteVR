"""Read-only empty-tube evidence while the finite mod is still inhibiting stock reload."""
import argparse,ctypes,hashlib,json,sys,time
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--source',type=Path,required=True);p.add_argument('--receiver',type=Path,required=True);p.add_argument('--pid',type=int,required=True)
a=p.parse_args();sys.path.insert(0,str(a.source/'tools'))
from preflight_body_holster_probe import validate
from capture_reload_server import capture,discover
from capture_reload_state import Image,Inspector
from read_bc2 import Process
tick=ctypes.windll.kernel32.GetTickCount64;tick.restype=ctypes.c_ulonglong
output=a.receiver/'pump-postflight.json'
if output.exists():raise RuntimeError('Preserve existing evidence')
report=dict(schema='fvr.bc2.body_holster_preflight.v1',pid=a.pid,read_only=True,process_writes=False,native_calls=False,
    input_or_focus_changes=False,in_trial_empty_observation=True,samples=[],rejected=[],passed=False,attempts=[])
process=None
try:
    image=Image();report['executable_sha256']=hashlib.sha256(image.data).hexdigest()
    if report['executable_sha256']!='3911fcc8914b0158c434cee36f19295316e715d4c9a0e70c857e8c39e3d54258':raise ValueError('Game executable changed')
    until=time.monotonic()+10;origin=None
    while origin is None and time.monotonic()<until:
        try:
            with (a.receiver/'physical-pump-input.jsonl').open() as f:origin=json.loads(f.readline())['tick_ms']
        except (OSError,ValueError,KeyError):time.sleep(.02)
    if origin is None:raise ValueError('Receiver input clock unavailable')
    while tick()<origin+30000:time.sleep(.05)
    before=json.loads((a.receiver/'pump-preflight.json').read_text())
    initial=validate(before,'SPAS12_sp')
    if initial['loaded']!=8:raise ValueError('Eight-shot trial requires an actual full tube')
    process=Process(a.pid);inspect=Inspector(process,image);binding=discover(image,process)
    settled=None
    while tick()<origin+55000:
        try:
            rows=[]
            for n in range(2):
                if n:time.sleep(.03)
                row=dict(monotonic_ns=time.perf_counter_ns(),**capture(inspect,binding));row['state'].pop('raw_hex',None);rows.append(row)
            summary=validate(dict(report,samples=rows),'SPAS12_sp')
            if summary['loaded']!=0 or summary['reserve']!=initial['reserve']:raise ValueError('Awaiting unchanged-reserve empty idle')
            if settled is None:settled=time.monotonic()
            if time.monotonic()-settled>=.5:
                report.update(samples=rows,summary=summary,passed=True);break
        except (ValueError,KeyError,RuntimeError,OSError) as e:
            settled=None
            if len(report['attempts'])<300:report['attempts'].append(dict(tick_ms=tick(),reason=str(e)))
        time.sleep(.08)
    if not report['passed']:raise ValueError('No stable empty idle before finite session end')
except (ValueError,KeyError,RuntimeError,OSError) as e:report['rejected'].append(str(e))
finally:
    if process is not None:process.close()
output.write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps(dict(passed=report['passed'],output=str(output),rejected=report['rejected'])))
raise SystemExit(not report['passed'])
