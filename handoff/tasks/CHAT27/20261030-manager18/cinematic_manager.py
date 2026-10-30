"""Exact-edge, file-only x86 investigation; every decode stops at return/padding."""
from __future__ import annotations
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import struct
import subprocess

ROOT = Path(__file__).resolve().parents[4]
SCRATCH = ROOT / 'reports/coordinator-20261030-implementation18/cinematic-manager'
SHA = '3911fcc8914b0158c434cee36f19295316e715d4c9a0e70c857e8c39e3d54258'
DUMPBIN = Path(r'C:\Program Files\Microsoft Visual Studio\18\Community\VC\Tools\MSVC\14.51.36231\bin\HostX64\x64\dumpbin.exe')
UNITS = {0x3b1800: 0x1e2, 0x3b6300: 0x9e, 0x396d90: 0x73, 0x188b10: 0xd,
         0x394280: 0xe9, 0x1315e0: 0x56, 0x39cb30: 0x41, 0x3abc90: 0x1ef}
FUNCTIONS = (
    (0x3b1800,0x1e2,'Registration: append a 0x28-byte record and set interface membership byte.',True),
    (0x3b6300,0x9e,'Clear interface membership, then immediate or deferred removal.',True),
    (0x396d90,0x73,'Comparison transform: use singleton+0xac or fallback singleton+0x60.',True),
    (0x188b10,0xd,'Return KERNEL32!TlsGetValue result using stored TLS index.',True),
    (0x394280,0xe9,'Find matching registration, release subhandles, virtual+4 callback, swap-remove record.',True),
    (0x1315e0,0x56,'Copy twelve padded matrix floats from receiver+0x10..+0x48 to output.',True),
    (0x39cb30,0x41,'Partial caller tail: drain deferred removals unless interface membership was restored.',False),
    (0x3abc90,0x1ef,'Initialize the separate comparison-transform singleton; no selected-player join.',True),
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
        self.import_directory = struct.unpack_from('<II', self.data, optional+104)
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
        raw=bytearray()
        while True:
            value=self.read(rva+len(raw),1)
            if value==b'\0':
                return raw.decode('ascii')
            raw+=value
            assert len(raw)<512
    def import_at(self, rva):
        start,size=self.import_directory
        for at in range(start,start+size,20):
            original,_,_,name,first=struct.unpack('<5I',self.read(at,20))
            if not name:
                break
            if rva>=first and (rva-first)%4==0:
                index=(rva-first)//4
                if index>4096:
                    continue
                for i in range(index+1):
                    thunk=struct.unpack('<I',self.read((original or first)+i*4,4))[0]
                    if not thunk:
                        break
                else:
                    return dict(dll=self.cstring(name),name=self.cstring(thunk+2),
                        iat=self.record(rva,4),descriptor=self.record(at,20),
                        lookup=self.record((original or first)+index*4,4))
        raise ValueError(hex(rva))

def instruction(im, at):
    pattern=re.compile(r'^\s*([0-9A-Fa-f]{8}):\s+((?:[0-9A-Fa-f]{2} )+)(.*)$')
    for size in range(1,16):
        result=subprocess.run([str(DUMPBIN), '/NOLOGO', '/DISASM:BYTES',
            f'/RANGE:0x{im.base+at:x},0x{im.base+at+size-1:x}',str(im.path)],capture_output=True,check=True)
        lines=result.stdout.decode('utf-8',errors='replace').splitlines()
        found=[]
        for line in lines:
            match=pattern.match(line)
            if match and match[3].strip() and not re.fullmatch(r'[0-9A-Fa-f ]+',match[3].strip()):
                address=int(match[1],16)-im.base
                encoded=bytes.fromhex(match[2])
                if address==at:
                    found.append((address,encoded,match[3].strip(),line))
        if found:
            assert len(found)==1, (hex(at),lines)
            address,raw,text,line=found[0]
            index=lines.index(line)+1
            while index<len(lines) and re.fullmatch(r'\s{10,}(?:[0-9A-Fa-f]{2}\s*)+',lines[index]):
                raw+=bytes.fromhex(lines[index].strip())
                line+='\n'+lines[index]
                index+=1
            assert address==at and im.read(at,len(raw))==raw and len(raw)==size,(hex(at),size,lines)
            return raw,text,line
    raise ValueError((hex(at),lines))

def decode(im, at, count):
    assert len(UNITS)<=8 and 1<=count<=256
    assert any(start<=at<start+size for start,size in UNITS.items())
    start=at
    lines=[]
    for _ in range(count):
        raw,asm,line=instruction(im,at)
        lines.append(line)
        at+=len(raw)
        if asm.startswith(('ret','int','jmp')):
            break
    size=at-start
    assert any(base<=start and at<=base+limit for base,limit in UNITS.items())
    covered=set(range(start,at))
    for path in SCRATCH.glob('decode-*.json'):
        prior=json.loads(path.read_text(encoding='utf-8'))
        covered.update(range(int(prior['rva'],0),int(prior['rva'],0)+prior['size']))
    assert len(covered)<=65536
    output=('\n'.join(lines)+'\n').encode('utf-8')
    dest=SCRATCH/f'decode-{start:08x}-{size:x}.txt'
    assert not dest.exists(),'Preserve previous decode'
    dest.write_bytes(output)
    row=dict(**im.record(start,size),decode_sha256=digest(output),instruction_count=len(lines))
    dest.with_suffix('.json').write_text(json.dumps(row,indent=2)+'\n',encoding='utf-8')
    print(output.decode('utf-8'))
    print(json.dumps(row))

def proof(im):
    inherited_path=ROOT/'handoff/tasks/CHAT27/20261030-update18/proof01.json'
    inherited=inherited_path.read_bytes()
    assert digest(inherited)=='5b4a5c5c60e3577e6814c9d6a9cc373de3433de26d48f2540b4fde7ee0272f43'
    covered=set()
    decodes=[]
    for path in sorted(SCRATCH.glob('decode-*.json')):
        row=json.loads(path.read_text(encoding='utf-8'))
        rva=int(row['rva'],0)
        assert im.record(rva,row['size'])['sha256']==row['sha256']
        assert digest(path.with_suffix('.txt').read_bytes())==row['decode_sha256']
        covered.update(range(rva,rva+row['size']))
        decodes.append(dict(**row,path=str(path.with_suffix('.txt').relative_to(ROOT))))
    allowed=set()
    for rva,size in UNITS.items():
        allowed.update(range(rva,rva+size))
    assert len(FUNCTIONS)==8 and covered==allowed and len(covered)<=65536
    def exact(rva,encoded,meaning):
        raw=bytes.fromhex(encoded)
        assert im.read(rva,len(raw))==raw,hex(rva)
        return dict(**im.record(rva,len(raw)),bytes=encoded,meaning=meaning)
    instructions=[exact(*row) for row in (
        (0x3b1810,'807b0400','Entry ignores interfaces whose membership byte+4 is already set.'),
        (0x3b1829,'895c2438','Registration record begins with the incoming interface pointer.'),
        (0x3b1844,'8b501c','Optional comparison path reads interface virtual slot+0x1c.'),
        (0x3b188e,'8b522c','Optional comparison path reads interface virtual slot+0x2c.'),
        (0x3b199d,'8d4f04','Registration collection header is manager+4.'),
        (0x3b19aa,'8d5728','Record stride is0x28 bytes.'),
        (0x3b19b2,'b90a000000','Fast append copies ten dwords.'),
        (0x3b19bd,'c6430401','Fast append sets interface membership byte+4.'),
        (0x3b19d5,'c6430401','Growth append also sets interface membership byte+4.'),
        (0x3b6315,'c6470400','Removal clears interface membership byte+4 before taking either path.'),
        (0x3b6319,'807e5000','Manager+0x50 selects immediate versus queued removal; exact field semantics unproved.'),
        (0x3b6347,'8d4e14','Deferred removal collection header is manager+0x14.'),
        (0x3b635c,'8938','Deferred removal retains the same interface pointer.'),
        (0x3942b4,'390f','Immediate removal matches the first dword of each record against interface.'),
        (0x3942d1,'83c728','Immediate removal scans in0x28-byte strides.'),
        (0x394323,'8b2c2f','Matched record first dword supplies callback receiver.'),
        (0x39432d,'8b4204','Matched interface virtual+4 callback; target unresolved.'),
        (0x39435b,'f3a5','Last record replaces removed record.'),
        (0x39435d,'834308d8','Collection end moves back by0x28.'),
        (0x39cb32,'80780400','Deferred drain skips removal when membership byte has become nonzero again.'),
        (0x39cb39,'8bce','Deferred drain passes the same manager receiver to immediate removal.'),
        (0x39cb51,'895618','Deferred drain resets end to begin.'),
        (0x188b10,'a1a8715601','TLS index is read from RVA0x11671a8.'),
        (0x188b16,'ff1554534001','Imported call through IAT RVA0x1005354, verified KERNEL32!TlsGetValue.'),
        (0x396d93,'8b8fac000000','Separate comparison singleton+0xac supplies matrix-copy source.'),
        (0x396de2,'83c760','Fallback transform begins at comparison singleton+0x60.'),
        (0x1315e6,'d94110','Matrix copy starts reading receiver+0x10.'),
        (0x13162a,'d94148','Matrix copy final source float is receiver+0x48.'),
        (0x3abd0f,'899eac000000','Comparison singleton constructor initializes source+0xac to null.'),
    )]
    calls=[]
    for at,target,role in (
        (0x3b238f,0x3b1800,'Inherited first-use manager registration edge.'),
        (0x3b6ba1,0x3b6300,'Inherited last-use removal edge.'),
        (0x3b1868,0x3abc90,'Lazy initialize separate comparison singleton.'),
        (0x3b1887,0x396d90,'Read separate comparison transform.'),
        (0x3b191a,0x3abc90,'Second guarded comparison singleton initialization.'),
        (0x3b19d0,0x3ad020,'Record storage growth helper, undecoded.'),
        (0x3b6391,0x394280,'Immediate removal branch.'),
        (0x396da3,0x1315e0,'Copy selected comparison object matrix, object identity unproved.'),
        (0x396de8,0x3810,'Fallback matrix-copy helper, undecoded.'),
        (0x396dfa,0x3810,'Null-source fallback copy, undecoded.'),
        (0x3942e9,0x188b10,'Get TLS context for subhandle release service.'),
        (0x39430c,0xc38580,'Release record subhandle, undecoded.'),
        (0x39cb3b,0x394280,'Additional direct caller discovered by exact E8 target scan; loop-back proves local instruction boundary.'),
    ):
        raw=im.read(at,5)
        assert raw[0]==0xe8 and at+5+struct.unpack_from('<i',raw,1)[0]==target,hex(at)
        calls.append(dict(**im.record(at,5),target_rva=hex(target),role=role,callee_decoded=target in UNITS))
    iat=im.import_at(0x1005354)
    assert iat['dll']=='KERNEL32.dll' and iat['name']=='TlsGetValue'
    return dict(schema='chat27.cinematic_manager_static_code.v1',evidence_level='offline file-backed code',
        executable_sha256=SHA,preferred_base=hex(im.base),script_sha256=digest(Path(__file__).read_bytes()),
        inherited=dict(path=str(inherited_path.relative_to(ROOT)),sha256=digest(inherited),
            used='First/last companion uses pass companion+0x80 to0x3b1800/0x3b6300 on receiver[0x188b10()+0x14]. Relative angular output remains companion+0x90..+0xc8.'),
        scope=dict(maximum_units=8,new_function_units=8,complete_function_units=7,partial_function_units=1,
            partial_unit='0x39cb30..0x39cb71; caller entry unknown; only removal-drain loop and tail decoded',
            adjacent_units=0,unique_decoded_code_bytes=len(covered),sum_saved_decode_bytes=sum(row['size'] for row in decodes),
            maximum_code_bytes=65536,ceiling_met=True,stopped_at_unit_limit=True,
            range_strategy='Incremental byte ranges stop at complete single instructions; sequential chunks stop at return/unconditional jump. Explicit branches authorize continuation. Exact E8 scan authorized one additional caller tail; no backward prologue guessing.'),
        units=[dict(**im.record(rva,size),meaning=meaning,complete=complete) for rva,size,meaning,complete in FUNCTIONS],
        instructions=instructions,direct_calls=calls,tls_import=iat,
        findings=dict(
            retention='Yes.0x3b1800 retains incoming companion+0x80 in a0x28-byte manager+4 collection record. Interface+4 (companion+0x84) prevents duplicate registration and becomes1 after append.',
            comparison='When a global gate and interface virtual+0x1c allow it, registration calls interface virtual+0x2c for a transform, compares its translation to a separate singleton transform from0x396d90, and derives an integer field in the record. Helper0x1315e0 only copies12 floats; singleton source+0xac ownership is not established. This is a comparison source, not proof the companion is selected.',
            removal='0x3b6300 clears interface+4. If manager+0x50 is zero,0x394280 immediately locates the record, releases recorded subhandles, calls interface virtual+4, and swap-removes it. Otherwise it queues the interface in manager+0x14 with lock-shaped helper calls.',
            queued_drain='Verified caller tail0x39cb30 checks each queued interface+4, removes only still-cleared entries through0x394280, then resets the queue end to begin. Full caller entry and scheduling owner remain unproved.',
            tls='0x188b10 is KERNEL32!TlsGetValue(global TLS index RVA0x11671a8); manager is TLS-context+0x14, not established as local-player view manager.',
            lifetime='Inherited first/last-use count boundaries control registration and clearing/queueing of removal. They establish interface registration lifetime, not actual selected cinematic lifetime.',
            selection='No decoded chain connects retained interface output, inherited companion+0x90..+0xc8 angular matrix, or manager membership to the selected local-player view, active blend ownership, entry/exit view authority, or HUD suppression.'),
        source_implementation=dict(supported_correction=False,production_modified=False,
            reason='No supported selected-view/HUD predicate or source correction follows from registration lifetime. Do not gate production policy on camera state4, companion+0x84/+0xfd/count+0xdc, or manager collection membership.'),
        next_unresolved_edge=dict(callsite_rva='0x3b1898',dispatch='[vtable(companion+0x80)+0x2c]',
            argument='Output matrix storage passed on stack by registration path.',
            required='Resolve the constructor-assigned table for companion+0x80 and its exact+0x2c target, then identify the manager collection update consumer and its actual selected local-player-view join.',
            related_callsite='0x394332 invokes the same retained interface virtual+4 on removal; target remains unresolved.',
            limit='Registration, transform comparison, and deferred cleanup remain insufficient to derive active-cinematic or HUD conditions.'),
        execution=dict(game=False,desktop_tests=False,builds=False,tests=False,process_access=False,
            input=False,xr=False,profiles=False,installs=False,packages=False,subagents=False,git=False),
        verification='File hash, inherited proof hash, complete bounded decode coverage, saved decode hashes, exact instruction bytes, direct-call destinations and TLS import identity checked. Decoder development failures were parser/truncation handling and did not introduce additional function units.',
        saved_decodes=decodes)

def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--exe',type=Path,required=True)
    parser.add_argument('--decode',type=lambda x:int(x,0))
    parser.add_argument('--count',type=int,default=16)
    parser.add_argument('--data',type=lambda x:int(x,0))
    parser.add_argument('--iat',type=lambda x:int(x,0))
    parser.add_argument('--callers',type=lambda x:int(x,0))
    parser.add_argument('--size',type=lambda x:int(x,0),default=4)
    parser.add_argument('--output',type=Path)
    args=parser.parse_args()
    SCRATCH.mkdir(parents=True,exist_ok=True)
    for key in ('TEMP','TMP','TMPDIR'):
        os.environ[key]=str(SCRATCH)
    im=Image(args.exe)
    if args.output:
        assert args.output.resolve()==Path(__file__).resolve().parent/'proof01.json'
        result=proof(im)
        raw=(json.dumps(result,indent=2)+'\n').encode('utf-8')
        with args.output.open('xb') as stream:
            stream.write(raw)
        print(json.dumps(dict(path=str(args.output.resolve()),size=len(raw),sha256=digest(raw),scope=result['scope'])))
    elif args.decode is not None:
        decode(im,args.decode,args.count)
    elif args.data is not None:
        assert 0<args.size<=0x1000
        print(json.dumps(dict(**im.record(args.data,args.size),bytes=im.read(args.data,args.size).hex())))
    elif args.iat is not None:
        print(json.dumps(im.import_at(args.iat)))
    elif args.callers is not None:
        candidates=[]
        for rva,_,raw,size,flags in im.sections:
            if not flags&0x20000000:
                continue
            data=im.data[raw:raw+size]
            at=data.find(b'\xe8')
            while at>=0:
                if at+5<=size and rva+at+5+struct.unpack_from('<i',data,at+1)[0]==args.callers:
                    candidates.append(dict(**im.record(rva+at,5),target=hex(args.callers)))
                at=data.find(b'\xe8',at+1)
        print(json.dumps(dict(exact_relative_call_byte_candidates=candidates,
            limit='Opcode candidates require instruction-boundary validation before use.')))

if __name__=='__main__':
    main()
