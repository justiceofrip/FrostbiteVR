"""Read-only post-vehicle baseline; physical pump fixture cannot fire before its3s warmup."""
import argparse,ctypes,hashlib,json,sys,time
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--source',type=Path,required=True);p.add_argument('--receiver',type=Path,required=True);p.add_argument('--pid',type=int,required=True)
a=p.parse_args();sys.path.insert(0,str(a.source/'tools'))
from preflight_body_holster_probe import validate
from capture_reload_server import capture,discover
from capture_reload_state import Image,Inspector
from read_bc2 import Process
gettick=ctypes.windll.kernel32.GetTickCount64;gettick.restype=ctypes.c_ulonglong
output=a.receiver/'pump-preflight.json'
if output.exists():raise RuntimeError('Preserve baseline evidence')
report=dict(schema='fvr.bc2.body_holster_preflight.v1',pid=a.pid,read_only=True,process_writes=False,native_calls=False,input_or_focus_changes=False,samples=[],rejected=[],passed=False,attempts=[])
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
    report['controls_started_ms']=origin
    while gettick()<origin+1200:time.sleep(.01)
    process=Process(a.pid);inspect=Inspector(process,image);binding=discover(image,process)
    while gettick()<origin+2600:
        attempt=[]
        try:
            for n in range(2):
                if n:time.sleep(.03)
                row=dict(monotonic_ns=time.perf_counter_ns(),**capture(inspect,binding));row['state'].pop('raw_hex',None);attempt.append(row)
            trial=dict(report,samples=attempt);summary=validate(trial,'SPAS12_sp')
            if summary['loaded']<3:raise ValueError('At least three loaded rounds required')
            if gettick()>=origin+2800:raise ValueError('Baseline overlaps first fire preparation')
            report['samples']=attempt;report['summary']=summary;report['passed']=True;break
        except (ValueError,KeyError,RuntimeError,OSError) as e:report['attempts'].append(dict(tick_ms=gettick(),reason=str(e),samples=attempt));time.sleep(.04)
    if not report['passed']:raise ValueError('No coherent on-foot baseline before first fire window')
except (ValueError,KeyError,RuntimeError,OSError) as e:report['rejected'].append(str(e))
finally:
    if process is not None:process.close()
output.parent.mkdir(parents=True,exist_ok=True);output.write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps(dict(passed=report['passed'],output=str(output),attempts=len(report['attempts']),rejected=report['rejected'])))
raise SystemExit(not report['passed'])
