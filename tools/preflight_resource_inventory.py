"""Read-only held-start inventory preflight; exact asset supplied by the test recipe."""
import argparse,json,math,struct,time
from pathlib import Path
from capture_reload_state import Image,Inspector
from read_bc2 import Process

def sample(p, inspect, asset):
    manager=p.u32(inspect.context+8);player=p.u32(manager+0xb4)
    weak=p.u32(player+0xc54);actor=p.u32(weak)-4;controlled=p.u32(player+0xc68)
    inspect.require_type(actor,'ClientSoldierEntity')
    if not p.read(player+0xccd,1)[0]&8 or p.u32(actor+0x220)!=player or controlled!=actor:
        raise ValueError('Held-start requires the exact on-foot local soldier')
    flags=p.read(actor+0x114,1)[0];inventory=p.u32(actor+(0x24c if flags&1 else 0x248))
    inspect.require_type(p.u32(inventory+4),'WeaponSwitchingData')
    begin=p.u32(actor+0x260);end=p.u32(actor+0x264);slot=p.u32(inventory+0x14c)
    if not 0<end-begin<=256 or slot!=1 or slot>=(end-begin)//4:
        raise ValueError('Combined fixture requires paired shotgun0/rifle1 slots')
    weapon=inspect.weapon(p.u32(begin+slot*4),slot)
    paired=inspect.weapon(p.u32(begin),0)
    if weapon['asset_name']!=asset or paired['asset_name']!='SPAS12_sp':
        raise ValueError('Exact selected rifle/paired SPAS identity differs')
    states=[bytes.fromhex(inspect.state(weapon,b)['raw_hex']) for b in (0x3c,0x40)]
    counts=[struct.unpack_from('<ii',state,0x7c) for state in states]
    capacities=[]
    number,base=struct.unpack('<ii',p.read(weapon['ammo_address']+0x10,8))
    for state in states:
        override=struct.unpack_from('<i',state,0x98)[0]
        product=base*struct.unpack_from('<f',state,0x74)[0]
        capacity=override if override>=0 else int(product) if math.isfinite(product) and product.is_integer() else -1
        capacities.append(capacity)
        if number<0 or state[0xa8]&8 or struct.unpack_from('<I',state,0x3c)[0]!=2 or struct.unpack_from('<I',state,0x44)[0]!=2:
            raise ValueError('Idle finite rifle required')
    if counts[0]!=counts[1] or capacities[0]!=capacities[1] or not 2<=counts[0][0]==capacities[0]<=1000000 or counts[0][1]<=0:
        raise ValueError('Full coherent rifle client magazine required')
    return dict(player=player,actor=actor,weak=weak,controlled=controlled,inventory=inventory,
                weapon=weapon['weapon'],paired_weapon=paired['weapon'],asset=asset,
                loaded=counts[0][0],reserve=counts[0][1],capacity=capacities[0])

def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--pid',type=int,required=True);ap.add_argument('--expected-asset',required=True)
    ap.add_argument('--output',type=Path,required=True);args=ap.parse_args()
    if args.output.exists():raise ValueError('Preserve existing evidence')
    report=dict(read_only=True,process_writes=False,native_calls=False,os_input=False,passed=False,
                vehicle=False,authority_proven=False,external_reads_are_not_atomic=True,samples=[],rejected=[])
    process=None
    try:
        process=Process(args.pid);inspect=Inspector(process,Image())
        report['samples'].append(sample(process,inspect,args.expected_asset));time.sleep(.03)
        report['samples'].append(sample(process,inspect,args.expected_asset))
        if report['samples'][0]!=report['samples'][1]:raise ValueError('Inventory identity changed')
        report['passed']=True
    except (OSError,ValueError,KeyError,RuntimeError,OverflowError) as error:report['rejected'].append(str(error))
    finally:
        if process is not None:process.close()
        args.output.write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    print(json.dumps(report));return 0 if report['passed'] else 1

if __name__=='__main__':raise SystemExit(main())
