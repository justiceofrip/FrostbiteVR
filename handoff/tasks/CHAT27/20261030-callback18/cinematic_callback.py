"""Bounded file-only examination of the exact camera callback callee."""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess

ROOT = Path(__file__).resolve().parents[4]
SCRATCH = ROOT / 'reports/coordinator-20261030-implementation18/cinematic-callback'
SHA = '3911fcc8914b0158c434cee36f19295316e715d4c9a0e70c857e8c39e3d54258'
DUMPBIN = Path(r'C:\Program Files\Microsoft Visual Studio\18\Community\VC\Tools\MSVC\14.51.36231\bin\HostX64\x64\dumpbin.exe')
UNITS = (
    (0x4955a0,0x478,'Filtered camera callback callee; two angular channels and padded matrix update.'),
    (0x38c640,0x7a,'Copies twelve input floats into receiver+0x90..+0xc8, skipping padding lanes.'),
    (0x2128f0,0x7e,'Numeric input selector; float array or packed-bit result. Input type is unproved.'),
    (0x212b80,7,'Returns float at incoming payload+0xa0; field name unproved.'),
    (0x3af9e0,0x30,'Returns fixed singleton RVA0x1170d80 with guarded lazy initialization.'),
    (0x38d550,0x3a,'Returns a selected scalar divided by receiver+0xcc; no object type admitted.'),
    (0x8521b0,6,'Import thunk to MSVCR80.dll!_CIcos.'),
    (0x8521b6,6,'Import thunk to MSVCR80.dll!_CIsin.'),
)

def digest(raw):
    return hashlib.sha256(raw).hexdigest()

class Image:
    def __init__(self, path):
        self.path = path
        self.data = path.read_bytes()
        assert digest(self.data) == SHA, 'Unexpected executable'
        pe = struct.unpack_from('<I', self.data, 0x3c)[0]
        assert self.data[pe:pe+4] == b'PE\0\0'
        count = struct.unpack_from('<H', self.data, pe+6)[0]
        optional_size = struct.unpack_from('<H', self.data, pe+20)[0]
        optional = pe+24
        assert struct.unpack_from('<H', self.data, optional)[0] == 0x10b
        self.base = struct.unpack_from('<I', self.data, optional+28)[0]
        self.import_rva = struct.unpack_from('<I', self.data, optional+104)[0]
        self.sections = []
        for i in range(count):
            at = optional+optional_size+i*40
            virtual, rva, rawsize, raw = struct.unpack_from('<4I', self.data, at+8)
            flags = struct.unpack_from('<I', self.data, at+36)[0]
            self.sections.append((rva, virtual, raw, rawsize, flags))
    def read(self, rva, size):
        for start, _, raw, length, _ in self.sections:
            if start <= rva and rva+size <= start+length:
                return self.data[raw+rva-start:raw+rva-start+size]
        raise ValueError(hex(rva))
    def record(self, rva, size):
        return dict(rva=hex(rva), size=size, sha256=digest(self.read(rva,size)))
    def cstring(self, rva):
        return self.read(rva,256).split(b'\0',1)[0].decode('ascii')
    def imports(self, targets):
        found = []
        at = self.import_rva
        while True:
            original, stamp, chain, name, iat = struct.unpack('<5I',self.read(at,20))
            if not any((original,stamp,chain,name,iat)):
                break
            thunk = original or iat
            index = 0
            while True:
                value = struct.unpack('<I',self.read(thunk+index*4,4))[0]
                if not value:
                    break
                if iat+index*4 in targets:
                    assert not value & 0x80000000
                    found.append(dict(iat_rva=hex(iat+index*4),dll=self.cstring(name),
                        name=self.cstring(value+2),lookup_entry=self.record(thunk+index*4,4)))
                index += 1
            at += 20
        assert len(found) == len(targets)
        return found

def facts(im):
    constants=[]
    for rva,fmt in ((0x10d94f0,'f'),(0x10d9420,'f'),(0x10d9410,'f'),
                    (0x1009d00,'f'),(0x10d9640,'d'),(0x100a0b0,'d')):
        size=struct.calcsize(fmt)
        constants.append(dict(**im.record(rva,size),value=struct.unpack('<'+fmt,im.read(rva,size))[0]))
    return dict(imports=im.imports({0x1005528,0x100552c}),constants=constants)

def proof(im):
    inherited_path=ROOT/'handoff/tasks/CHAT27/20261030-methods17/proof01.json'
    inherited=inherited_path.read_bytes()
    assert digest(inherited)=='af327be2ac45a67eaa428b99a559c9f9c596caa79922f47ed889da624cc491b8'
    decodes=[]
    covered=set()
    for path in sorted(SCRATCH.glob('decode-*.json')):
        row=json.loads(path.read_text(encoding='utf-8'))
        rva=int(row['rva'],0)
        assert im.record(rva,row['size'])['sha256']==row['sha256']
        raw=path.with_suffix('.txt').read_bytes()
        assert digest(raw)==row['decode_sha256']
        covered.update(range(rva,rva+row['size']))
        decodes.append(dict(**row,path=str(path.with_suffix('.txt').relative_to(ROOT))))
    assert len(UNITS)==8 and len(covered)<=65536
    for rva,size,_ in UNITS:
        assert set(range(rva,rva+size)) <= covered
    def exact(rva,encoded,meaning):
        raw=bytes.fromhex(encoded)
        assert im.read(rva,len(raw))==raw,hex(rva)
        return dict(**im.record(rva,len(raw)),bytes=encoded,meaning=meaning)
    instructions=[exact(*row) for row in (
        (0x4955b0,'8b86e0000000','Read camera+0xe0 channel pointer.'),
        (0x4955bb,'3986e4000000','If both channel pointers are zero, return without writing output.'),
        (0x4955cc,'8b4e0c','Read camera+0x0c resource-shaped pointer; its type is not re-admitted here.'),
        (0x4955cf,'8bb9e0000000','Channel settings pointer is [[camera+0x0c]+0xe0].'),
        (0x4955dd,'8b5d08','EBX receives the forwarded fourth callback argument.'),
        (0x49562e,'d98794000000','First angular bound reads settings+0x94.'),
        (0x49564e,'6a04','First numeric selector is4.'),
        (0x4956b8,'80bf9800000000','Settings+0x98 selects averaged-relative versus accumulated/clamped mode.'),
        (0x49570d,'d996e8000000','First accumulated channel stores at camera+0xe8.'),
        (0x49578d,'d99ee8000000','Final first angle stores at camera+0xe8.'),
        (0x495874,'d98790000000','Second angular bound reads settings+0x90.'),
        (0x495894,'6a05','Second numeric selector is5.'),
        (0x4959ca,'d99eec000000','Final second angle stores at camera+0xec.'),
        (0x4959ff,'8b8ed4000000','Final matrix recipient is [camera+0xd4].'),
        (0x495a05,'8d542420','Final matrix source is the local padded four-row array.'),
        (0x38c64b,'d99990000000','Setter writes first float at recipient+0x90.'),
        (0x38c6ae,'d999c8000000','Setter writes twelfth float at recipient+0xc8.'),
        (0x212921,'d9449e08','Selector array path returns float[payload+8+selector*4].'),
        (0x212b80,'d981a0000000','Second payload helper returns float[payload+0xa0].'),
        (0x3afa0a,'b8800d5701','Singleton helper returns preferred VA0x1570d80/RVA0x1170d80.'),
        (0x38d551,'8b81ac000000','Scalar helper reads first optional object at singleton+0xac.'),
        (0x38d55b,'f30f104074','Positive scalar candidate comes from that object+0x74.'),
        (0x38d569,'8b81b0000000','Fallback object is singleton+0xb0.'),
        (0x38d56f,'f30f104030','Fallback scalar comes from fallback object+0x30.'),
        (0x38d57c,'d8b1cc000000','Selected scalar is divided by singleton+0xcc.'),
    )]
    direct_calls=[]
    for at,target,role in (
        (0x495638,0x3af9e0,'Singleton for first channel scalar'),
        (0x49563f,0x38d550,'First channel scalar ratio'),
        (0x495664,0x2128f0,'Forwarded payload with selector4'),
        (0x4956e4,0x212b80,'Forwarded payload scalar+0xa0'),
        (0x4957bc,0x8521b0,'cos(first angle)'),
        (0x4957d1,0x8521b6,'sin(first angle)'),
        (0x495858,0x13db40,'First local matrix operation; callee not decoded'),
        (0x495862,0x3810,'First local matrix copy operation; callee not decoded'),
        (0x49587e,0x3af9e0,'Singleton for second channel scalar'),
        (0x495885,0x38d550,'Second channel scalar ratio'),
        (0x4958aa,0x2128f0,'Forwarded payload with selector5'),
        (0x49591b,0x212b80,'Forwarded payload scalar+0xa0'),
        (0x4959e9,0x12ee00,'Second local angular operation, selector0; callee not decoded'),
        (0x4959f0,0x13db40,'Second local matrix operation; callee not decoded'),
        (0x4959fa,0x3810,'Second local matrix copy operation; callee not decoded'),
        (0x495a0a,0x38c640,'Write local matrix through camera+0xd4'),
        (0x212907,0x852790,'Numeric selector bit helper; callee not decoded'),
        (0x3af9f8,0x3abc90,'Singleton initializer; callee not decoded'),
        (0x3afa02,0x852315,'Singleton termination registration; semantic role unproved'),
    ):
        raw=im.read(at,5)
        assert raw[0]==0xe8 and at+5+struct.unpack_from('<i',raw,1)[0]==target,hex(at)
        direct_calls.append(dict(**im.record(at,5),target_rva=hex(target),role=role,
            callee_decoded=any(target==start for start,_,_ in UNITS)))
    return dict(schema='chat27.cinematic_callback_static_code.v1',
        evidence_level='offline file-backed code', executable_sha256=SHA, preferred_base=hex(im.base),
        script_sha256=digest(Path(__file__).read_bytes()),
        inherited=dict(path=str(inherited_path.relative_to(ROOT)),sha256=digest(inherited),
            used='Previously proved camera+0xdc interface callback0x49dc10 converts to whole camera and forwards fourth argument for event discriminators0x1f/0x2f.'),
        scope=dict(maximum_units=8,new_function_units=8,adjacent_units=0,
            unique_decoded_code_and_padding_bytes=len(covered),
            sum_requested_decode_bytes=sum(d['size'] for d in decodes),
            function_bytes=sum(size for _,size,_ in UNITS),maximum_code_bytes=65536,
            ceiling_met=True,stopped_at_unit_limit=True,
            range_strategy='Small instruction-boundary continuations; branches locate same-function blocks; stop at explicit returns and padding. Truncated final instruction fragments are retained and re-decoded from the same instruction start. No next-table-target end bounds.'),
        units=[dict(**im.record(rva,size),meaning=meaning) for rva,size,meaning in UNITS],
        facts=facts(im),instructions=instructions,direct_calls=direct_calls,
        supported_chain='Event-filtered exact camera -> forwarded payload selectors4/5 -> two angle accumulators camera+0xe8/+0xec -> local padded transform -> [camera+0xd4]+0x90..+0xc8. First channel explicitly constructs a Y-axis rotation using _CIcos/_CIsin.',
        channel_behavior=dict(
            gate='Both camera+0xe0 and camera+0xe4 null => immediate return. Pointer presence is channel availability, not proved cinematic activity.',
            averaging='Each present channel stores capacity/count/sum/mean at offsets0/4/8/0xc. At capacity, subtract prior mean and decrement count before incorporating new sample.',
            bounds='settings=[[camera+0xc]+0xe0]; first bound=settings+0x94, second=settings+0x90; each multiplied by pi/180 and singleton scalar ratio.',
            relative='When byte[settings+0x98]!=0: first angle=-mean4*bound94, second angle=mean5*bound90.',
            accumulated='Otherwise first angle subtracts payload value4*payload scalar+0xa0*pi and second adds value5*same scalar*pi, each clamped to its signed bound.',
            remaining_names='Input payload type, selector names, payload+0xa0 field name, settings type/field names and singleton object type remain unproved.'),
        singleton_relation=dict(rva='0x1170d80',
            scalar='If object=[singleton+0xac] exists and float[object+0x74]>0, use it; otherwise use float[[singleton+0xb0]+0x30]. Divide by float[singleton+0xcc].',
            limitation='No equality/ownership join between either singleton member and camera or [camera+0xd4]. These fields must not be promoted to active-camera identity.'),
        source_implementation=dict(supported_correction=False,production_modified=False,
            reason='This callback handles relative camera angular input. It neither proves the live selected cinematic camera nor supplies cinematic entry/exit/skip/HUD authority.'),
        next_unresolved_edge=dict(rva='0x495d70',known_caller='0x49dba8',
            inherited_relation='Camera primary-table+0xb4 candidate invokes this with the whole camera and original first argument.',
            question='Does the ordinary camera update consume [camera+0xd4]+0x90 and join its resulting base/animated transform to a selected local-player view?',
            separate_limit='A matching transform consumer still needs the active-view/local-player lifetime and blend-authority join before runtime admission.'),
        execution=dict(game=False,desktop_tests=False,builds=False,tests=False,process_access=False,
            profiles=False,installs=False,packages=False,subagents=False,git=False),
        tool_limits='Initial python command hit the Windows Store alias without running the script; existing py -3 launcher then ran file-only inspection. No package installation. Two source-search wildcard path errors returned no affected-path results; subsequent rg used directory paths and glob flags.',
        verification='Exact file hashes, preserved decode hashes, inherited proof hash, all eight function spans, selected instruction bytes, direct-call destinations, import entries and scalar constants checked without game execution.',
        saved_decodes=decodes)

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--exe', type=Path, required=True)
    parser.add_argument('--decode', type=lambda x:int(x,0))
    parser.add_argument('--size', type=lambda x:int(x,0), default=0x40)
    parser.add_argument('--data', type=lambda x:int(x,0))
    parser.add_argument('--facts', action='store_true')
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    im = Image(args.exe)
    if args.output:
        assert args.output.resolve()==Path(__file__).resolve().parent/'proof01.json'
        result=proof(im)
        raw=(json.dumps(result,indent=2)+'\n').encode('utf-8')
        with args.output.open('xb') as stream:
            stream.write(raw)
        print(json.dumps(dict(path=str(args.output.resolve()),size=len(raw),sha256=digest(raw),scope=result['scope'])))
    elif args.facts:
        print(json.dumps(facts(im),indent=2))
    elif args.decode is not None:
        assert 0 < args.size <= 0x1000
        assert any(start<=args.decode and args.decode+args.size<=((start+size+15)//16)*16
                   for start,size,_ in UNITS), 'Only the eight observed function spans and their trailing alignment padding are permitted.'
        SCRATCH.mkdir(parents=True, exist_ok=True)
        prior = list(SCRATCH.glob('decode-*.json'))
        budget = sum(json.loads(p.read_text())['size'] for p in prior)
        assert budget+args.size <= 65536
        dest = SCRATCH / f'decode-{args.decode:08x}-{args.size:x}.txt'
        assert not dest.exists(), 'Preserve previous decode'
        result = subprocess.run([str(DUMPBIN), '/NOLOGO', '/DISASM:BYTES',
            f'/RANGE:0x{im.base+args.decode:x},0x{im.base+args.decode+args.size-1:x}', str(args.exe)],
            capture_output=True, check=True)
        dest.write_bytes(result.stdout)
        info = dict(**im.record(args.decode,args.size), decode_sha256=digest(result.stdout))
        dest.with_suffix('.json').write_text(json.dumps(info,indent=2)+'\n',encoding='utf-8')
        print(result.stdout.decode('utf-8',errors='replace'))
        print(json.dumps(info))
    elif args.data is not None:
        assert 0 < args.size <= 0x1000
        print(json.dumps(dict(**im.record(args.data,args.size),bytes=im.read(args.data,args.size).hex())))

if __name__ == '__main__':
    main()
