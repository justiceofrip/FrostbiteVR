"""Replay audited native callback metadata through the new resource ledger.

Offline only: no game access, commands, native calls or admission. Replay
context generations are local fixture values, not proof of live holster mapping.
"""
import argparse
import hashlib
import json
import subprocess
from pathlib import Path
from audit_native_ammo_move import audit

OWNER=('player','soldier','weak','weapon','actor_generation','equip_generation','space')
def payload(document):
    verdict=audit(document)
    if verdict['status']!='bounded_native_verified':raise ValueError('Native evidence is not verified: '+str(verdict['reasons']))
    flow=document['gameplay']['reload_flow'];p=flow['ammo_move_probe'];operations=p['operations'];records=flow['records']
    own=[next(r for r in records if r['kind']==0 and r['native_invocation']==op['invocation']) for op in operations]
    identity=own[0]['before'];firings=p['firing']
    samples=[s for s in document['gameplay']['reload_observer']['samples'] if s['weapon']==identity['weapon'] and
             [b['address'] for b in s['branches']]==firings[:2]]
    if p.get('refill') and not any(s['reload_type']==1 for s in samples):raise ValueError('Whole-magazine replay requires native magazine refill')
    capacities={b['effective_capacity'] for s in samples for b in s['branches']}
    if len(capacities)!=1:raise ValueError('Capacity changed or was not observed')
    capacity=capacities.pop();lines=[]
    def line(values):lines.append(' '.join(str(int(v)) if isinstance(v,bool) else str(v) for v in values))
    line([identity[k] for k in OWNER]+firings+[identity[k] for k in ('server_player','server_soldier','server_item')])
    line([p['original_loaded'],p['original_reserve'],capacity])
    for index,(op,native) in enumerate(zip(operations,own)):
        limit=operations[1]['begin_ns'] if index==0 else op['end_ns']+100_000_000
        rows=sorted((r for r in records if r.get('kind')==0 and r.get('before') and r.get('after') and
                     r['before']['firing'] in firings and r['after']['weapon']==identity['weapon'] and
                     op['end_ns']<=r.get('end_ns',0)<=limit),key=lambda r:r['end_ns'])
        line([0 if index==0 else 2 if p.get('refill') else 1,op['invocation'],op['begin_ns'],op['end_ns'],len(rows)])
        line([native[part][key] for part in ('before','after') for key in ('loaded','reserve')])
        line([True,bool(op['exact']),bool(op['context_unchanged'])])
        for r in rows:
            a,b=r['after'],r['before'];context=r.get('context_before',{})
            neutral=context.get('decoded_valid') is True and context.get('input_flags')==0
            line([b[k] for k in OWNER]+[a[k] for k in OWNER]+
                 [a['firing'],a.get('server_player',0),a.get('server_soldier',0),a.get('server_item',0),
                  r['native_invocation'],r['begin_ns'],r['end_ns'],a['branch'],r['depth'],
                  b['loaded'],b['reserve'],a['loaded'],a['reserve'],a['current'],a['next'],a['timer'],
                  bool(r['finished']),bool(r['identity_retained']),neutral,bool(r.get('hold_applied'))])
    return '\n'.join(lines)+'\n'

def main():
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('trace',type=Path)
    ap.add_argument('--runner',type=Path,required=True);ap.add_argument('--output',type=Path,required=True);args=ap.parse_args()
    if args.output.exists():ap.error('Preserve existing evidence')
    raw=args.trace.read_bytes();document=json.loads(raw.decode('utf-8-sig'));text=payload(document)
    result=subprocess.run([str(args.runner.resolve()),'--replay'],input=text,text=True,capture_output=True,timeout=20)
    report=dict(schema=1,offline=True,game_access=False,player_dispatch_verified=False,headset_verified=False,
                trace_sha256=hashlib.sha256(raw).hexdigest(),runner_sha256=hashlib.sha256(args.runner.read_bytes()).hexdigest(),
                normalized_input_sha256=hashlib.sha256(text.encode()).hexdigest(),exit_code=result.returncode,
                stdout=result.stdout,stderr=result.stderr,passed=result.returncode==0)
    args.output.write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report));return result.returncode

if __name__=='__main__':raise SystemExit(main())
