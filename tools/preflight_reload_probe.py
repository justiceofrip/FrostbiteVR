"""Read-only selected weapon/ammo preflight; no input, hooks or native writes.

Reuses capture_reload_server's exact executable and client/server ownership
verification. This is a launch precondition, never a native reload capability.
"""
from __future__ import annotations
import argparse, datetime as dt, hashlib, json, math, struct, time
from pathlib import Path


def validate(report, prepare_shots=False, request_probe=False, expected_asset='SPAS12_sp'):
    def need(ok, why):
        if not ok:
            raise ValueError(why)
    need(isinstance(expected_asset,str) and 0<len(expected_asset)<128 and
         all(32<=ord(c)<=126 for c in expected_asset), 'explicit selected asset required')
    need(report.get('read_only') is True and report.get('native_calls') is False and
         report.get('process_writes') is False, 'read-only capture evidence missing')
    samples=report.get('samples', [])
    need(len(samples)==2 and not report.get('rejected'), 'two coherent samples required')
    before,after=samples
    identity=('client_owner','server_player','server_soldier','server_item','server_firing',
              'asset_name','asset_path','weapon_data','firing_data','ammo_address')
    need(all(before.get(k)==after.get(k) for k in identity), 'owner changed during preflight')
    need(0 < before['monotonic_ns'] < after['monotonic_ns'] and
         after['monotonic_ns']-before['monotonic_ns'] <= 500_000_000, 'preflight capture interval invalid')
    for sample in samples:
        need(sample.get('identity_coherent') is True, 'coherent ownership required')
        need(sample.get('asset_name')==expected_asset, 'selected asset must be '+expected_asset)
        branches=sample.get('branches', [])
        need(len(branches)==3, 'all three native firing copies required')
        need(all(b==branches[0] for b in branches), 'native phase/count/capacity copies disagree')
        state=branches[0]
        for k in ('loaded','reserve','capacity','current','next'):
            need(type(state.get(k)) is int, 'native integer field malformed: '+k)
        need(state['current']==2 and state['next']==2 and state.get('finite_ammo') is True,
             'selected weapon must be idle with finite ammunition')
        need(0 <= state['loaded'] <= state['capacity'] <= 1_000_000 and state['capacity']>0 and
             0 < state['reserve'] <= 1_000_000, 'invalid or exhausted native ammunition')
        need(state['loaded']==state['capacity'] and state['loaded']>=2 if prepare_shots else
             state['capacity']-state['loaded']==2 if request_probe else state['loaded']<state['capacity'],
             'full weapon required before two preparation shots' if prepare_shots else
             'request fixture requires exactly two empty slots' if request_probe else 'weapon full; use approved preparation shots')
        need(state['reserve']>=2 if request_probe else state['reserve']>=1, 'insufficient reserve for fixture')
    need(before['branches']==after['branches'], 'native ammunition changed during preflight')
    b=after['branches'][0]
    return {'asset':after['asset_name'],'loaded':b['loaded'],'reserve':b['reserve'],
            'capacity':b['capacity'],'prepare_shots':bool(prepare_shots),
            'authority_proven':False,'native_reload_verified':False}


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--pid',type=int,required=True)
    ap.add_argument('--output',type=Path,required=True)
    ap.add_argument('--prepare-shots',action='store_true')
    ap.add_argument('--request-probe',action='store_true')
    ap.add_argument('--expected-asset',default='SPAS12_sp',
                    help='Exact expected native asset; read-only precondition, not native admission')
    args=ap.parse_args()
    report={'schema':'fvr.bc2.reload_probe_preflight.v1','utc':dt.datetime.now(dt.timezone.utc).isoformat(),
            'pid':args.pid,'read_only':True,'native_calls':False,'process_writes':False,
            'input_or_focus_changes':False,'samples':[],'rejected':[],'passed':False}
    process=None
    try:
        from capture_reload_server import capture, discover, chain
        from capture_reload_state import Image, Inspector
        from read_bc2 import Process
        image=Image();process=Process(args.pid);inspect=Inspector(process,image)
        binding=discover(image,process)
        report['executable_sha256']=hashlib.sha256(image.data).hexdigest()
        for n in range(2):
            if n:time.sleep(.03)
            sample={'monotonic_ns':time.perf_counter_ns(),**capture(inspect,binding)}
            owner=sample['client_owner']
            weapon=inspect.weapon(owner['selected_weapon'],owner['selected_slot'])
            raw_branches=[bytes.fromhex(inspect.state(weapon,b)['raw_hex']) for b in (0x3c,0x40)]
            raw_branches.append(bytes.fromhex(sample['state']['raw_hex']))
            # Same verified native capacity interpretation as Bc2ReloadState:
            # explicit override, else exact integral base * multiplier. Never round.
            number,base=struct.unpack('<ii',process.read(weapon['ammo_address']+0x10,8))
            rows=[]
            for raw in raw_branches:
                override=struct.unpack_from('<i',raw,0x98)[0]
                multiplier=struct.unpack_from('<f',raw,0x74)[0]
                product=base*multiplier
                capacity=override if override>=0 else int(product) if base>=0 and math.isfinite(product) and product.is_integer() else -1
                rows.append({'current':struct.unpack_from('<I',raw,0x3c)[0],
                             'next':struct.unpack_from('<I',raw,0x44)[0],
                             'loaded':struct.unpack_from('<i',raw,0x7c)[0],
                             'reserve':struct.unpack_from('<i',raw,0x80)[0],
                             'capacity':capacity,'finite_ammo':number>=0 and not bool(raw[0xa8]&8)})
            current=chain(inspect,binding)
            if current!={k:sample[k] for k in current}:
                raise ValueError('owner changed across all-three read')
            sample['branches']=rows
            # Avoid retaining game object bytes. Identity/count metadata suffices.
            sample['state'].pop('raw_hex',None)
            report['samples'].append(sample)
        report['summary']=validate(report,args.prepare_shots,args.request_probe,args.expected_asset);report['passed']=True
    except (OSError,ValueError,KeyError,RuntimeError,OverflowError) as exc:
        report['rejected'].append(str(exc))
    finally:
        if process is not None:process.close()
    args.output.parent.mkdir(parents=True,exist_ok=True)
    args.output.write_text(json.dumps(report,indent=2,allow_nan=False)+'\n',encoding='utf-8')
    print(json.dumps({'passed':report['passed'],'summary':report.get('summary'),'rejected':report['rejected'],'output':str(args.output)}))
    return 0 if report['passed'] else 1


if __name__=='__main__':raise SystemExit(main())
