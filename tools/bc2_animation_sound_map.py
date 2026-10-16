"""Read the exact current BC2 animation-to-sound map; no hooks or native calls.

Stores only identities and tiny code signatures, never game audio or meshes.
The receiver callback proof comes from executable disassembly; this snapshot
does not prove which callback runs during a physical gesture.
"""
from pathlib import Path
import argparse,json,struct,uuid
from read_bc2 import Process

WANTED={
 'Animations/Weapons/Handheld/UL_shg_SPAS12/1P/1P_BoltAction':('2d9cf41b-3aba-4a22-82a8-64051c180432','9515f482-793e-420d-b107-234f1c7ece0d'),
 'Animations/Weapons/Handheld/BU_sni_M95/1P/1p_BoltAction':('5f55f25e-d4b4-4a20-9f14-ca152a9610fa','4ffd6e7f-10ad-4b5f-912f-26e991250a06'),
}
def inspect(pid):
    p=Process(pid)
    def name(at):return p.read(p.u32(at+12),200).split(b'\0')[0].decode('ascii')
    def guid(at):return str(uuid.UUID(bytes_le=p.read(p.u32(at+4),16)))
    try:
        if p.base!=0x400000:raise ValueError('Image base differs')
        pe=p.base+p.u32(p.base+0x3c)
        if p.read(pe,4)!=b'PE\0\0' or p.u32(pe+8)!=1314210909 or p.u32(pe+24+56)!=0x1913000:
            raise ValueError('Executable identity differs')
        signatures={}
        for address,expected in [(0x881650,'568bf18b46048a887504000080e101384e50751a'),
                                 (0x881690,'568bf18b46048a887504000080e101384e507515'),
                                 (0x6ee6a0,'558bec83e4f081ec04020000535657')]:
            raw=p.read(address,len(bytes.fromhex(expected)))
            if raw.hex()!=expected:raise ValueError('Live audio callback differs or is already hooked')
            signatures[hex(address)]=raw.hex()
        manager=p.u32(0x1571420);asset=p.u32(manager+0x44)
        begin,end=struct.unpack('<II',p.read(asset+0x20,8))
        if not 0<end-begin<=128*28 or (end-begin)%28:raise ValueError('Category bound')
        rows=[]
        for category in range(begin,end,28):
            first,last=struct.unpack('<II',p.read(category+20,8))
            if not 0<=last-first<=4096*8 or (last-first)%8:raise ValueError('Pair bound')
            for pair in range(first,last,8):
                sound,animation=struct.unpack('<II',p.read(pair,8))
                if not sound or not animation:continue
                animation_name=name(animation)
                if animation_name not in WANTED:continue
                ids=guid(animation),guid(sound)
                if ids!=WANTED[animation_name]:raise ValueError('Known sound binding differs')
                rows.append({'animation':animation_name,'animation_guid':ids[0],'animation_address':animation,
                    'sound':name(sound),'sound_guid':ids[1],'sound_address':sound})
        if p.u32(0x1571420)!=manager or p.u32(manager+0x44)!=asset or p.read(asset+0x20,8)!=struct.pack('<II',begin,end):
            raise ValueError('Map changed during inspection')
        if len(rows)!=len(WANTED):raise ValueError('Required animation bindings not both present')
        return {'schema':'fvr.bc2.animation_sound_map.v1','pid':pid,'base':p.base,'manager':manager,'asset':asset,
                'signatures':signatures,'bindings':rows,'runtime_call_observed':False,'writes':0}
    finally:p.close()
def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--pid',type=int,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
    if a.output.resolve().drive.upper()!='G:':raise ValueError('Output must be on G:')
    result=inspect(a.pid);a.output.parent.mkdir(parents=True,exist_ok=True)
    a.output.write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8',newline='\n');print(json.dumps(result));return 0
if __name__=='__main__':raise SystemExit(main())
