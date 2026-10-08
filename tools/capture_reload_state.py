"""Bounded BC2 reload/pump observer. QUERY_INFORMATION | VM_READ only.

Does not call native functions, hook, suspend, send input, alter focus or write
process memory. Values not backed by a native interpretation retain offset names.
External reads validate identity but are not an atomic simulation-tick snapshot.
"""
from __future__ import annotations
import argparse
import ctypes
import datetime as dt
import hashlib
import json
import math
from pathlib import Path
import re
import struct
import time
from read_bc2 import Process

def configured_executable():
    import os
    game = os.environ.get("BC2_GAME_PATH")
    if not game:
        config = Path(__file__).resolve().parent.parent / "config/local.json"
        if not config.is_file():
            raise ValueError("Run Setup-BC2VRPreview.ps1 or set BC2_GAME_PATH first")
        game = json.loads(config.read_text(encoding="utf-8-sig"))["game_path"]
    path = Path(game)
    return path if path.name.lower() == "bfbc2game.exe" else path / "BFBC2Game.exe"


class Image:
    def __init__(self, path=None):
        self.path = Path(path) if path is not None else configured_executable()
        self.data = self.path.read_bytes()
        pe = struct.unpack_from('<I', self.data, 60)[0]
        if self.data[:2] != b'MZ' or self.data[pe:pe+4] != b'PE\0\0':
            raise ValueError('Invalid PE')
        opt = pe+24
        if struct.unpack_from('<H', self.data, opt)[0] != 0x10b:
            raise ValueError('Expected PE32')
        self.base = struct.unpack_from('<I', self.data, opt+28)[0]
        self.size = struct.unpack_from('<I', self.data, opt+56)[0]
        count = struct.unpack_from('<H', self.data, pe+6)[0]
        start = opt+struct.unpack_from('<H', self.data, pe+20)[0]
        self.sections = []
        for n in range(count):
            at = start+n*40
            size, rva, rawsize, raw = struct.unpack_from('<4I', self.data, at+8)
            flags = struct.unpack_from('<I', self.data, at+36)[0]
            if raw+rawsize > len(self.data):
                raise ValueError('Section bounds')
            self.sections.append((rva, size, raw, rawsize, flags))
    def section(self, rva, size=1):
        for section in self.sections:
            if section[0] <= rva and rva-section[0]+size <= section[1]:
                return section
        raise ValueError('Address outside image section')
    def read(self, rva, size):
        section = self.section(rva, size)
        offset = rva-section[0]
        if offset+size > section[3]:
            raise ValueError('Address not file backed')
        return self.data[section[2]+offset:section[2]+offset+size]
    def find(self, pattern):
        regex = re.compile(b''.join(b'.' if b == '??' else re.escape(bytes([int(b,16)]))
                                   for b in pattern.split()), re.DOTALL)
        found = []
        for rva, _, raw, size, flags in self.sections:
            if flags & 0x20000000:
                found += [rva+m.start() for m in regex.finditer(self.data[raw:raw+size])]
        if len(found) != 1:
            raise ValueError(f'Signature must be unique, found {len(found)}')
        return found[0]

class Inspector:
    def __init__(self, process, image):
        self.p, self.image = process, image
        self.types = {}
        if self.p.base != self.image.base:
            raise ValueError('Relocated image is not yet supported by this read-only tool')
        context = image.find('B8 01 00 00 00 84 05 ?? ?? ?? ?? 75 66 09 05 ?? ?? ?? ?? 50 B9 ?? ?? ?? ?? E8 ?? ?? ?? ?? 33 C0 68 ?? ?? ?? ?? C7 05')
        manager = image.find('51 53 55 8B 6C 24 10 56 57 55 8B F1 E8 ?? ?? ?? ?? 8D 4E 10 E8 ?? ?? ?? ?? 8D 7E 4C 8B CF E8 ?? ?? ?? ?? 8D 4F 14 33 DB')
        soldier = image.find('8B 81 54 0C 00 00 85 C0 74 0A 8B 00 85 C0 74 04 83 C0 FC C3 33 C0 C3')
        destructor = image.find('56 8B F1 C7 06 ?? ?? ?? ?? C7 46 04 ?? ?? ?? ?? 8B 46 18 85 C0 74 0E 3B 46 28 74 09 50 E8 ?? ?? ?? ?? 83 C4 04 8B 4E 14 85 C9 74 0C 8B 01 8B 90 8C 00 00 00 6A 01 FF D2 F6 44 24 08 01')
        self.signatures = {}
        for name, rva, size in [('context',context,0x79),('manager',manager,0x74),('soldier',soldier,0x1a),('firing_destructor',destructor,0x4e)]:
            code = image.read(rva,size)
            if self.p.read(self.p.base+rva,size) != code:
                raise ValueError(f'Live signature differs: {name}')
            self.signatures[name] = {'rva':rva,'bytes_hex':code.hex(),'unique':True}
        self.context = struct.unpack_from('<I',image.read(context,0x79),21)[0]
        section=image.section(self.context-image.base,0x5c)
        if not section[4]&0x80000000 or section[4]&0x20000000:
            raise ValueError('Context storage section')
        self.manager_table = struct.unpack_from('<I',image.read(manager,0x74),0x6f)[0]
        self.firing_table = struct.unpack_from('<I',image.read(destructor,0x4e),5)[0]
        self.image_pointer(self.manager_table,12)
        self.image_pointer(self.firing_table,12)
        for address in (self.manager_table,self.firing_table):
            if self.p.read(address,12)!=image.read(address-image.base,12):
                raise ValueError('Live vtable differs')
    def image_pointer(self, at, size=4):
        self.image.section(at-self.p.base,size)
        return at
    def text(self, at, limit=192):
        out=bytearray()
        for offset in range(limit):
            c=self.p.read(at+offset,1)[0]
            if not c:
                return out.decode('ascii')
            if not 32<=c<127:
                raise ValueError('Invalid ASCII metadata')
            out.append(c)
        raise ValueError('Unterminated metadata')
    def info(self, at):
        if at in self.types:
            return self.types[at]
        self.image_pointer(at,8)
        metadata=self.image_pointer(self.p.u32(at+4),24)
        name=self.text(self.image_pointer(self.p.u32(metadata)))
        flags,size=struct.unpack('<HH',self.p.read(metadata+4,4))
        count=self.p.read(metadata+13,1)[0]
        result={'address':at,'name':name,'flags':flags,'size':size,'fields':[]}
        self.types[at]=result
        if flags in (0x35,0x29,0x179):
            fields=self.p.u32(at+36) if flags==0x35 else self.p.u32(metadata+24)
            if count:
                self.image_pointer(fields,count*24)
            for n in range(count):
                record=self.p.read(fields+n*24,24)
                text,fieldflags,typeinfo,element,offset,_=struct.unpack('<6I',record)
                entry={'name':self.text(self.image_pointer(text)),'offset':offset,
                       'type_info':typeinfo,'element_info':element,'flags':fieldflags}
                if flags!=0x179:
                    entry['type']=self.info(typeinfo)['name']
                    if offset>=size:
                        raise ValueError('Reflected field outside type')
                result['fields'].append(entry)
        return result
    def object_info(self, at):
        table=self.image_pointer(self.p.u32(at),12)
        getter=self.image_pointer(self.p.u32(table+8),6)
        code=self.p.read(getter,6)
        if code[0]!=0xb8 or code[5]!=0xc3 or code!=self.image.read(getter-self.p.base,6):
            raise ValueError('Unsupported reflected getter')
        return self.info(struct.unpack_from('<I',code,1)[0])
    def require_type(self, at, expected):
        info=self.object_info(at)
        if info['name']!=expected:
            raise ValueError(f'Expected {expected}, got {info["name"]}')
        return info
    def values(self, address, info, depth=0):
        if depth>4:
            raise ValueError('Nested reflection bound')
        out={}
        for field in info['fields']:
            at=address+field['offset'];kind=self.info(field['type_info']);name=field['type']
            if name=='Boolean':value=bool(self.p.read(at,1)[0])
            elif name in ('Float32','Int32','Uint32'):
                value=struct.unpack({'Float32':'<f','Int32':'<i','Uint32':'<I'}[name],self.p.read(at,4))[0]
                if isinstance(value,float) and not math.isfinite(value):raise ValueError('Nonfinite config')
            elif kind['flags']==0x179:
                raw=struct.unpack('<i',self.p.read(at,4))[0]
                labels=[f['name'] for f in kind['fields'] if f['offset']==raw]
                value={'value':raw,'name':labels[0] if len(labels)==1 else None}
            elif kind['flags']==0x29:value=self.values(at,kind,depth+1)
            else:continue
            out[field['name']]=value
        return out
    def owner(self):
        manager=self.p.u32(self.context+8)
        if self.p.u32(manager)!=self.manager_table:raise ValueError('Manager identity')
        player=self.p.u32(manager+0xb4)
        if not self.p.read(player+0xccd,1)[0]&8:raise ValueError('Not local player')
        weak=self.p.u32(player+0xc54);actor=self.p.u32(weak)-4
        self.require_type(actor,'ClientSoldierEntity')
        if self.p.u32(actor+0x220)!=player or self.p.u32(player+0xc68)!=actor:
            raise ValueError('Not local on-foot actor')
        flags=self.p.read(actor+0x114,1)[0]
        inventory=self.p.u32(actor+(0x24c if flags&1 else 0x248))
        self.require_type(self.p.u32(inventory+4),'WeaponSwitchingData')
        begin,end=self.p.u32(actor+0x260),self.p.u32(actor+0x264)
        if not 0<end-begin<=256 or (end-begin)%4:raise ValueError('Inventory bounds')
        items=struct.unpack('<'+'I'*((end-begin)//4),self.p.read(begin,end-begin))
        selected=self.p.u32(inventory+0x14c)
        if selected>=len(items) or not items[selected]:raise ValueError('No selected item')
        return {'actor':actor,'player':player,'weak':weak,'flags':flags,'inventory':inventory,
                'selected_slot':selected,'selected_weapon':items[selected],'items':list(items)}
    def weapon(self, address, slot):
        data=self.p.u32(address+4);self.require_type(data,'SoldierWeaponData')
        name=self.text(self.p.u32(data+12));asset=self.text(self.p.u32(data+0x40))
        firing=self.p.u32(data+0x98);firing_info=self.require_type(firing,'WeaponFiringData')
        primary_field=next(f for f in firing_info['fields'] if f['name']=='PrimaryFire')
        primary=self.p.u32(firing+primary_field['offset']);primary_info=self.require_type(primary,'FiringFunctionData')
        fields={f['name']:f for f in primary_info['fields']}
        logic_field,ammo_field=fields['FireLogic'],fields['Ammo']
        state_array=self.p.read(data+0x88,16);_,typeinfo,begin,end=struct.unpack('<4I',state_array)
        state_info=self.info(typeinfo)
        if state_info['name']!='WeaponStateData' or not 0<end-begin<=state_info['size']*8 or (end-begin)%state_info['size']:
            raise ValueError('WeaponStateData bounds')
        states=[]
        for at in range(begin,end,state_info['size']):
            vals=self.values(at,state_info)
            states.append({k:v for k,v in vals.items() if k in ('IsPumpAction','SkipReloadAnimation','SkipFireAnimation','PlayDeployAfterFire')})
        return {'slot':slot,'weapon':address,'data':data,'asset_name':name,'asset_path':asset,
                'firing_data':firing,'primary_fire':primary,'ammo_address':primary+ammo_field['offset'],
                'fire_logic_address':primary+logic_field['offset'],
                'fire_logic':self.values(primary+logic_field['offset'],self.info(logic_field['type_info'])),
                'ammo_config':self.values(primary+ammo_field['offset'],self.info(ammo_field['type_info'])),
                'weapon_states':states,'abort_reload_on_sprint':bool(self.p.read(firing+0x44,1)[0])}
    def inherited_field(self, info, field_name):
        """Use the verified native ClassInfo parent constructor, never guessed inheritance."""
        constructor=self.image.find('8B 44 24 04 56 50 8B F1 E8 ?? ?? ?? ?? 8B 4C 24 10 8B 44 24 0C 8B 54 24 14 89 4E 10 33 C9 38 0D ?? ?? ?? ?? C7 06 ?? ?? ?? ?? 89 46 14 89 4E 18 66 89 4E 1C 66 89 4E 1E 89 4E 20 89 56 24')
        code=self.image.read(constructor,0x3e)
        if self.p.read(self.p.base+constructor,len(code))!=code:
            raise ValueError('ClassInfo parent constructor changed')
        chain=[];seen=set();matches=[];child_size=info['size']
        for _ in range(8):
            if info['address'] in seen:raise ValueError('Reflected inheritance cycle')
            seen.add(info['address'])
            if info['flags']!=0x35 or not 0<info['size']<=child_size:
                raise ValueError('Invalid reflected base class')
            chain.append({'type_info':info['address'],'name':info['name'],'size':info['size']})
            matches.extend((info,field) for field in info['fields'] if field['name']==field_name)
            parent=self.p.u32(info['address']+0x14)
            if parent==info['address']:
                if len(matches)!=1:raise ValueError('Inherited field missing or ambiguous')
                owner,field=matches[0]
                return field,chain,{'rva':constructor,'sha256':hashlib.sha256(code).hexdigest(),'parent_offset':0x14}
            child_size=info['size'];info=self.info(self.image_pointer(parent,0x28))
        raise ValueError('Reflected inheritance depth exceeded')

    def mesh_links(self, weapon):
        """Read exact inventory weapon -> state -> Meshes1p ownership; no native calls."""
        data=weapon['data'];address=weapon['weapon']
        if self.p.u32(address+4)!=data:raise ValueError('Weapon changed before mesh inspection')
        data_info=self.require_type(data,'SoldierWeaponData')
        fields=[f for f in data_info['fields'] if f['name']=='WeaponStates']
        if len(fields)!=1 or fields[0]['offset']!=0x88 or fields[0]['type']!='ArrayBase':
            raise ValueError('WeaponStates reflection mismatch')
        state_address=data+fields[0]['offset'];state_header=self.p.read(state_address,16)
        _,type_info,begin,end=struct.unpack('<4I',state_header)
        info=self.info(type_info)
        if type_info!=fields[0]['element_info'] or info['name']!='WeaponStateData' or info['size']!=0xc8 or info['flags']!=0x29:
            raise ValueError('WeaponStateData element identity mismatch')
        if begin<0x10000 or end>0xffffffff or not 0<end-begin<=8*info['size'] or (end-begin)%info['size']:
            raise ValueError('WeaponStateData array bounds')
        mesh_fields=[f for f in info['fields'] if f['name']=='Meshes1p']
        if len(mesh_fields)!=1 or mesh_fields[0]['offset']!=0x80 or mesh_fields[0]['type']!='ArrayBase':
            raise ValueError('Meshes1p reflection mismatch')
        field=mesh_fields[0];element=self.info(field['element_info'])
        if element['name']!='SkinnedMeshAsset' or element['size']!=0x44 or element['flags']!=0x35:
            raise ValueError('Meshes1p element is not verified SkinnedMeshAsset')
        name_field,inheritance,parent_proof=self.inherited_field(element,'Name')
        if name_field['offset']!=0xc or name_field['type']!='String':raise ValueError('Mesh inherited name reflection mismatch')
        states=[]
        for state in range(begin,end,info['size']):
            array=state+field['offset'];header=self.p.read(array,20)
            table,_,capacity,count,items=struct.unpack('<5I',header)
            self.image_pointer(table,12);table_bytes=self.p.read(table,12)
            if table_bytes!=self.image.read(table-self.p.base,12):raise ValueError('Meshes1p getter table changed')
            getters={}
            for label,slot,expected,offset in [('count',0,bytes.fromhex('8b410cc3'),12),('data',8,bytes.fromhex('8b4110c3'),16)]:
                getter=struct.unpack_from('<I',table_bytes,slot)[0];self.image_pointer(getter,4)
                if self.image.read(getter-self.p.base,4)!=expected or self.p.read(getter,4)!=expected:
                    raise ValueError('Unsupported Meshes1p '+label+' getter')
                getters[label]={'address':getter,'field_offset':offset,'code_hex':expected.hex()}
            if count>8 or count>capacity or capacity>1024 or (count and (items<0x10000 or items+count*4>0xffffffff)):
                raise ValueError('Meshes1p count/storage bounds')
            pointers=self.p.read(items,count*4) if count else b''
            meshes=[]
            for index in range(count):
                mesh=struct.unpack_from('<I',pointers,index*4)[0]
                actual=self.require_type(mesh,'SkinnedMeshAsset')
                if actual['address']!=element['address']:raise ValueError('Mesh reflected element owner mismatch')
                name_pointer=self.p.u32(mesh+name_field['offset']);name=self.text(name_pointer,512)
                if not name or '/' not in name:raise ValueError('Mesh asset path missing')
                meshes.append({'index':index,'address':mesh,'type_info':actual['address'],
                               'asset_path':name,'name_pointer':name_pointer})
            if self.p.read(array,20)!=header or (count and self.p.read(items,count*4)!=pointers):
                raise ValueError('Meshes1p array changed during read')
            for mesh in meshes:
                if self.object_info(mesh['address'])['address']!=element['address'] or self.p.u32(mesh['address']+name_field['offset'])!=mesh['name_pointer'] or self.text(mesh['name_pointer'],512)!=mesh['asset_path']:
                    raise ValueError('Mesh identity changed during read')
            states.append({'state_address':state,'array_address':array,'count':count,
                           'getter_table':table,'getter_proof':getters,'meshes':meshes})
        if self.p.read(state_address,16)!=state_header or self.p.u32(address+4)!=data:
            raise ValueError('Weapon state ownership changed during mesh inspection')
        return {'schema':'fvr.bc2.weapon_mesh_links','schema_version':1,
                'weapon':address,'data':data,'asset_name':weapon['asset_name'],
                'state_type_info':type_info,'state_field_offset':fields[0]['offset'],
                'mesh_field_offset':field['offset'],'mesh_element_type_info':element['address'],
                'mesh_name_field_offset':name_field['offset'],'mesh_inheritance':inheritance,
                'parent_constructor':parent_proof,'states':states,
                'identity_coherent':True,'atomic_native_snapshot':False,
                'visibility_and_selected_state_verified':False}

    def state(self, weapon, branch):
        if self.p.u32(weapon['weapon']+4)!=weapon['data'] or self.p.u32(weapon['data']+0x98)!=weapon['firing_data']:
            raise ValueError('Weapon configuration changed')
        address=self.p.u32(weapon['weapon']+branch)
        raw=self.p.read(address,0xb0)
        words=struct.unpack('<44I',raw)
        if words[0]!=self.firing_table or words[2]!=weapon['firing_data'] or words[3]!=weapon['ammo_address']:
            raise ValueError('Firing state ownership/config mismatch')
        if self.p.u32(weapon['weapon']+branch)!=address:raise ValueError('Firing state changed')
        return {'address':address,'vtable':words[0],'raw_hex':raw.hex(),
                'state_3c':words[0x3c//4],'state_40':words[0x40//4],'state_44':words[0x44//4],
                'counter_7c':words[0x7c//4],'counter_80':words[0x80//4],
                'flags_a4_a8':raw[0xa4:0xa9].hex()}


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--pid',type=int,required=True)
    parser.add_argument('--seconds',type=float,default=0)
    parser.add_argument('--interval-ms',type=float,default=10)
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--all-weapons',action='store_true',help='Also sample configured inactive weapons, marked separately from selected state')
    parser.add_argument('--mesh-links',action='store_true',help='Also record bounded reflected first-person mesh asset ownership for captured inventory weapons')
    parser.add_argument('--asset',action='append',default=[],help='Also inspect this exact selected/test asset; repeat for multiple names. Read-only, no native admission.')
    args=parser.parse_args()
    if not 0<=args.seconds<=60 or not 2<=args.interval_ms<=1000:parser.error('Bounded duration 0..60 seconds, interval 2..1000 ms')
    image=Image();process=Process(args.pid)
    try:
        inspect=Inspector(process,image);initial=inspect.owner();weapons=[]
        for slot,address in enumerate(initial['items']):
            if address:
                data=process.u32(address+4)
                inspect.require_type(data,'SoldierWeaponData')
                name=inspect.text(process.u32(data+12))
                if name in ('SPAS12_sp','XM8_sp_s','40mmgl') or name in args.asset:
                    weapons.append(inspect.weapon(address,slot))
        if args.mesh_links:
            for weapon in weapons:weapon['mesh_links']=inspect.mesh_links(weapon)
        if inspect.owner()!=initial:raise ValueError('Inventory changed during config capture')
        metadata={'schema':'fvr.bc2.reload_state','schema_version':1,'read_only':True,
                  'native_calls':False,'process_writes':False,'input_or_focus_changes':False,
                  'pid':args.pid,'utc':dt.datetime.now(dt.timezone.utc).isoformat(),
                  'executable':str(image.path),'executable_sha256':hashlib.sha256(image.data).hexdigest(),
                  'base':process.base,'signatures':inspect.signatures,'initial_owner':initial,
                  'weapons':weapons,'reflection':list(inspect.types.values()),'samples':[], 'rejected':[]}
        by_address={w['weapon']:w for w in weapons}
        ticks=ctypes.WinDLL('kernel32').GetTickCount64;ticks.restype=ctypes.c_ulonglong
        print(json.dumps({'ready':True,'pid':args.pid,'tick_ms':ticks(),'weapons':[w['asset_name'] for w in weapons]}),flush=True)
        started=time.perf_counter();deadline=started+args.seconds;index=0
        while True:
            tick=ticks();now=time.perf_counter_ns()
            try:
                owner=inspect.owner();item=by_address.get(owner['selected_weapon'])
                if item is None:raise ValueError('Selected asset not captured')
                branches={hex(branch):inspect.state(item,branch) for branch in (0x3c,0x40)}
                others=[]
                if args.all_weapons:
                    for other in weapons:
                        if other['weapon'] not in owner['items']:raise ValueError('Configured weapon left inventory')
                        states=branches if other['weapon']==item['weapon'] else {hex(b):inspect.state(other,b) for b in (0x3c,0x40)}
                        others.append({'weapon':other['weapon'],'asset_name':other['asset_name'],'selected':other['weapon']==item['weapon'],'states':states})
                if inspect.owner()!=owner:raise ValueError('Ownership changed while sampling')
                metadata['samples'].append({'sequence':index,'tick_ms':tick,'monotonic_ns':now,
                    'owner':owner,'asset_name':item['asset_name'],'eligibility_branch':'0x40' if owner['flags']&0x10 else '0x3c',
                    'states':branches,'all_weapon_states':others,'identity_coherent':True})
            except (OSError,ValueError,KeyError) as exc:
                metadata['rejected'].append({'sequence':index,'tick_ms':tick,'reason':str(exc)})
            index+=1
            if time.perf_counter()>=deadline:break
            time.sleep(min(args.interval_ms/1000,max(0,deadline-time.perf_counter())))
        metadata['finished_utc']=dt.datetime.now(dt.timezone.utc).isoformat()
        metadata['atomic_native_snapshot']=False
        args.output.parent.mkdir(parents=True,exist_ok=True)
        args.output.write_text(json.dumps(metadata,indent=2,allow_nan=False)+'\n',encoding='utf-8')
        print(json.dumps({'output':str(args.output),'samples':len(metadata['samples']),'rejected':len(metadata['rejected'])}),flush=True)
        return 0 if metadata['samples'] else 1
    finally:process.close()

if __name__=='__main__':raise SystemExit(main())
