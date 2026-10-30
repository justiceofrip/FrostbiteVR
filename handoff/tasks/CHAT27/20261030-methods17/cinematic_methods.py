"""Bounded offline cinematic virtual-method inspection. Never opens the game."""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess

ROOT = Path(__file__).resolve().parents[4]
SCRATCH = ROOT / "reports/coordinator-20261030-implementation17/cinematic-methods"
SHA = "3911fcc8914b0158c434cee36f19295316e715d4c9a0e70c857e8c39e3d54258"
DUMPBIN = Path(r"C:\Program Files\Microsoft Visual Studio\18\Community\VC\Tools\MSVC\14.51.36231\bin\HostX64\x64\dumpbin.exe")
PRIOR_SHA = 'b5db47e429185f0a5ebe9d37986fe7ec4c4cb40de746bcd9f974f482147101d3'

def digest(data):
    return hashlib.sha256(data).hexdigest()

class Image:
    def __init__(self, path):
        self.path = path
        self.data = path.read_bytes()
        assert digest(self.data) == SHA, "Unexpected executable"
        pe = struct.unpack_from("<I", self.data, 0x3c)[0]
        assert self.data[pe:pe+4] == b"PE\0\0"
        count, opts = struct.unpack_from("<H", self.data, pe+6)[0], struct.unpack_from("<H", self.data, pe+20)[0]
        optional = pe+24
        assert struct.unpack_from("<H", self.data, optional)[0] == 0x10b
        self.base = struct.unpack_from("<I", self.data, optional+28)[0]
        self.sections = []
        for i in range(count):
            at = optional+opts+i*40
            virtual, rva, rawsize, raw = struct.unpack_from("<4I", self.data, at+8)
            flags = struct.unpack_from("<I", self.data, at+36)[0]
            self.sections.append((rva, virtual, raw, rawsize, flags))
    def read(self, rva, size):
        for start, _, raw, length, _ in self.sections:
            if start <= rva and rva+size <= start+length:
                return self.data[raw+rva-start:raw+rva-start+size]
        raise ValueError(hex(rva))
    def executable(self, rva):
        return any(start <= rva < start+length and flags & 0x20000000 for start, _, _, length, flags in self.sections)
    def record(self, rva, size):
        return dict(rva=hex(rva), size=size, sha256=digest(self.read(rva,size)))

def proof(im):
    inherited={}
    for label,path,expected in (
        ('work14','handoff/tasks/CHAT27/20261030-work14/proof01.json',PRIOR_SHA),
        ('implementation17','handoff/tasks/CHAT27/20261030-implementation17/proof01.json','54fe3d0b5c1eb60d0134be8b5a2e04f0f78e6b9ea56ca2d3ad9cfed60994904e')):
        raw=(ROOT/path).read_bytes()
        assert digest(raw)==expected
        inherited[label]=dict(path=path,sha256=expected)
        if label=='work14': prior=json.loads(raw)
    for family in prior['families']:
        for kind in ('setup','cleanup'):
            old=family[kind]
            assert im.record(int(old['rva'],0),old['size'])['sha256']==old['sha256']
    def exact(rva,encoded,meaning):
        expected=bytes.fromhex(encoded)
        assert im.read(rva,len(expected))==expected,hex(rva)
        return dict(rva=hex(rva),bytes=encoded,sha256=digest(expected),meaning=meaning)
    instructions=[exact(*row) for row in (
        (0x49dba6,'8bf1','Camera candidate preserves incoming whole-object ECX in ESI.'),
        (0x49dbad,'837e3404','Checks this+0x34 against4.'),
        (0x49dbb3,'8b8ed4000000','Receiver=[this+0xd4].'),
        (0x49dbd2,'c7463405000000','Stores state5 on the decoded conditional path.'),
        (0x4b9492,'8b7c240c','Loads original first stack argument after two register saves.'),
        (0x4b9496,'d907','Loads float at first argument.'),
        (0x4b9499,'d91c24','Copies float to outgoing stack argument.'),
        (0x4b949c,'8bf1','Preserves incoming ECX as this in ESI.'),
        (0x4b94a3,'8b8ee4000000','Loads optional receiver=[this+0xe4].'),
        (0x4b94ad,'d907','Loads same first-argument float before optional second call.'),
        (0x49dbe0,'8b410c','Loads authored-resource-shaped pointer [this+0xc]; semantic type not proved.'),
        (0x49dbe3,'8b80cc000000','Reads resource-shaped pointer+0xcc; no admitted metadata-field match.'),
        (0x4a32eb,'8dbedc000000','Inherited camera cleanup: EDI=this+0xdc.'),
        (0x4a331f,'c707d4644301','Inherited camera cleanup: assigns secondary table0x10364d4 at this+0xdc.'),
        (0x4af877,'c786dc00000050664301','Inherited entity setup: assigns secondary table0x1036650 at this+0xdc.'),
        (0x4a3404,'8bf1','Inherited entity cleanup: ESI=whole this.'),
        (0x4a3410,'8dbedc000000','Inherited entity cleanup: EDI=this+0xdc.'),
        (0x4a343a,'c70750664301','Inherited entity cleanup repeats table0x1036650 assignment at this+0xdc.'),
        (0x49dc10,'837c24041f','Camera callback accepts first stack discriminator0x1f.'),
        (0x49dc17,'837c24082f','Camera callback accepts second stack discriminator0x2f.'),
        (0x49dc1e,'8b442410','Loads original fourth stack argument.'),
        (0x49dc22,'50','Forwards fourth stack argument.'),
        (0x49dc23,'81c124ffffff','Converts incoming this+0xdc interface back to whole camera by subtracting0xdc.'),
        (0x4a4060,'81e9d4000000','Unowned table-adjacent thunk subtracts0xd4; not primary entity slot evidence.'),
    )]
    calls=[]
    for at,target,relation in (
        (0x49dba8,0x495d70,'camera candidate forwards its original stack argument, whole-object receiver'),
        (0x49dbbd,0x38d8f0,'camera candidate calls [this+0xd4] receiver'),
        (0x4b949e,0x4b4460,'entity candidate forwards first-argument float with whole-object receiver'),
        (0x4b94b3,0x433d80,'entity candidate forwards same float with optional [this+0xe4] receiver'),
        (0x49dbef,0x495a20,'camera candidate passes resource-shaped member+0xcc and whole-object pointer'),
        (0x49dc29,0x4955a0,'verified camera secondary callback converts this+0xdc to whole object and forwards fourth argument'),
    ):
        raw=im.read(at,5)
        assert raw[0]==0xe8 and at+5+struct.unpack_from('<i',raw,1)[0]==target
        calls.append(dict(instruction=im.record(at,5),bytes=raw.hex(),target_rva=hex(target),relationship=relation,callee_decoded=False))
    units=[]
    for rva,size,kind,meaning in (
        (0x49dba0,0x3d,'selected','camera table+0xb4 candidate: state transition and owned receiver check'),
        (0x4b9490,0x2d,'selected','entity table+0xb4 candidate: float argument forwarding'),
        (0x4b94c0,0x50,'adjacent spill partial','Unrelated adjacent method; ownership unproved and incomplete ending retained.'),
        (0x49dbe0,0x25,'selected','camera table+0xdc candidate: pointer predicate; no view join'),
        (0x49dc10,0x21,'adjacent spill later ownership-proved','Exact secondary camera table0x10364d4+4, incoming owner+0xdc.'),
        (0x4a4060,0xb,'selected later rejected ownership','Adjacent entity-table word+0xdc points to adjustment thunk; no primary ownership proof.'),
        (0x4a4070,8,'adjacent spill','Unowned adjustment thunk; no semantic promotion.'),
        (0x4a4080,0xb,'adjacent spill','Unowned adjustment thunk; no semantic promotion.'),
        (0x4a4090,0xb,'adjacent spill','Unowned adjustment thunk; no semantic promotion.'),
        (0x4a40a0,0x8a,'adjacent spill','Unowned adjacent method; no semantic promotion.'),
    ):
        units.append(dict(**im.record(rva,size),classification=kind,meaning=meaning))
    tables=[]
    for rva,size,kind in ((0x1036550,0xf4,'camera primary continuous function-pointer candidate span, before [active] string'),(0x10366c8,0xdc,'entity primary candidate prefix; later contiguous words not admitted'),(0x10364d4,0xc,'exact camera secondary interface at owner+0xdc'),(0x1036650,0xc,'exact entity secondary interface at owner+0xdc')):
        tables.append(dict(**im.record(rva,size),interpretation=kind))
    assert struct.unpack('<I',im.read(0x1036550+0xb4,4))[0]==im.base+0x49dba0
    assert struct.unpack('<I',im.read(0x10366c8+0xb4,4))[0]==im.base+0x4b9490
    assert struct.unpack('<I',im.read(0x10364d4+4,4))[0]==im.base+0x49dc10
    assert struct.unpack('<I',im.read(0x1036650+4,4))[0]==im.base+0x495a90
    decodes=[]
    for name in ('decode-0049dba0-40.txt','decode-004b9490-80.txt','decode-0049dbe0-60.txt','decode-004a4060-d0.txt'):
        path=SCRATCH/name
        raw=path.read_bytes()
        decodes.append(dict(path=str(path.relative_to(ROOT)),size=len(raw),sha256=digest(raw)))
    return dict(schema='chat27.cinematic_methods_static_code.v1',evidence_level='offline file-backed code',executable_sha256=SHA,preferred_base=hex(im.base),script_sha256=digest(Path(__file__).read_bytes()),inherited=inherited,
        scope=dict(selected_decode_starts=4,unintended_adjacent_function_units=6,total_new_function_units_observed=10,requested_maximum=8,ceiling_met=False,explanation='Ranges ended at next known table target, which was not the next function boundary. This unintentionally exposed six adjacent units. All retained; no further decoding after overrun recognized. Root authorized only exact table-assignment checks inside inherited hash-pinned functions.'),
        units=units,tables=tables,instructions=instructions,direct_calls=calls,
        new_chain='Exact camera owner+0xdc -> table0x10364d4 -> slot+4 callback0x49dc10; callback filters stack discriminators0x1f/0x2f, converts interface to whole owner by subtracting0xdc, then calls0x4955a0 with original fourth argument. No event name or active-view semantics assigned.',
        primary_table_limits='Known setup assignments prove primary table origins; selected +0xb4 entries sit in their continuous pointer prefixes. No external decoded virtual dispatch was found in this scope. Do not treat every later executable word as a primary slot: entity+0xdc thunk0x4a4060 is unowned here. Camera+0x100 starts the independently assigned entity secondary table0x1036650, not a camera primary method.',
        metadata_field_match=dict(matched=False,reason='Current reads at object+0x34/+0xd4/+0xe4 and resource-shaped+0xcc have no established authored-field name in inherited evidence. Numeric values0x1f/0x2f are dispatch discriminators only. No repeated type inventory.'),
        next_unresolved_edges=[dict(priority=1,rva='0x4955a0',known_caller='0x49dc29',input='whole ClientCutsceneCameraEntity receiver; original camera-callback fourth argument',question='Does the event-filtered target select/copy camera transform or join admitted active view/local player?'),dict(priority=2,rva='0x4b4460',known_caller='0x4b949e',input='whole ClientCutsceneEntity receiver; float from original first argument',question='Does the frame-like update perform current sequence/camera selection?'),dict(priority=3,rva='0x495a90',known_reference='0x1036654',input='ClientCutsceneEntity secondary interface at owner+0xdc; body unexamined',question='What event handling and owner conversion occur?')],
        source_implementation=dict(supported_correction=False,production_modified=False,runtime_admission_enabled=False,reason='No admitted local-player/active-view -> currently selected cinematic camera chain. Verified callback conversion improves a specific edge but does not discover the live selected instance.'),
        limitations='Static function roles beyond argument/state operations remain hypotheses; timing, generation, synchronization, blend authority, entry/exit/skip and headset outcome unproved.',saved_decodes=decodes,
        execution=dict(game=False,desktop_tests=False,builds=False,tests=False,process_access=False,profiles=False,installs=False,packages=False,subagents=False),
        verifier_failure_retained='First proof-generation attempt rejected a transcription error: ESI=ECX instruction was entered as0x4a3405 instead of0x4a3404. Corrected using inherited exact cleanup prefix; no proof file was emitted by the failed attempt.',
        verification='Exact executable/prior/function/instruction/table hashes and direct-call destinations checked by file-only verifier; no additional decoding during proof generation.')

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--exe", required=True, type=Path)
    ap.add_argument("--table", type=lambda x: int(x,0))
    ap.add_argument("--size", type=lambda x: int(x,0), default=0x80)
    ap.add_argument("--decode", type=lambda x: int(x,0))
    ap.add_argument("--ownership", action='store_true')
    ap.add_argument("--output", type=Path)
    args = ap.parse_args()
    im = Image(args.exe)
    if args.output:
        assert args.output.resolve().parent==Path(__file__).resolve().parent
        result=proof(im)
        raw=(json.dumps(result,indent=2)+'\n').encode('utf-8')
        with args.output.open('xb') as stream: stream.write(raw)
        print(json.dumps(dict(path=str(args.output.resolve()),size=len(raw),sha256=digest(raw),observed_units=len(result['units']))))
    elif args.ownership:
        prior_raw = (ROOT/'handoff/tasks/CHAT27/20261030-work14/proof01.json').read_bytes()
        assert digest(prior_raw) == PRIOR_SHA
        prior=json.loads(prior_raw)
        rows=[]
        for family in prior['families']:
            for kind in ('setup','cleanup'):
                saved=family[kind]
                start=int(saved['rva'],0)
                data=im.read(start,saved['size'])
                assert digest(data)==saved['sha256']
                candidates=[]
                for i in range(len(data)-6):
                    if data[i] != 0xc7: continue
                    modrm=data[i+1]
                    if modrm >> 3 & 7 != 0 or modrm & 7 in (4,5): continue
                    base=('eax','ecx','edx','ebx','esp','ebp','esi','edi')[modrm&7]
                    if modrm >> 6 == 0:
                        length,off=6,0
                    elif modrm >> 6 == 1:
                        length,off=7,struct.unpack_from('<b',data,i+2)[0]
                    elif modrm >> 6 == 2:
                        length,off=10,struct.unpack_from('<i',data,i+2)[0]
                    else: continue
                    if i+length>len(data):continue
                    value=struct.unpack_from('<I',data,i+length-4)[0]
                    if 0x1036300 <= value-im.base <= 0x1036900:
                        candidates.append(dict(instruction_rva=hex(start+i),register=base,displacement=hex(off),table_rva=hex(value-im.base),bytes=data[i:i+length].hex(),preceding_bytes=data[max(0,i-20):i].hex()))
                rows.append(dict(family=family['name'],kind=kind,hash_pinned_prior_function=saved,c7_esi_assignment_candidates=candidates))
        print(json.dumps(rows,indent=2))
    elif args.table is not None:
        assert args.table in (0x1036550,0x10366c8,0x10364d4,0x10364e0,0x1036650,0x103665c)
        assert args.size <= 0x178 and args.size % 4 == 0
        print(json.dumps(dict(table=im.record(args.table,args.size), entries=[dict(offset=hex(i), value=hex(v), target_rva=hex(v-im.base), executable=im.executable(v-im.base)) for i,v in zip(range(0,args.size,4),struct.unpack('<'+'I'*(args.size//4),im.read(args.table,args.size)))]),indent=2))
    elif args.decode is not None:
        assert 0 < args.size <= 0x1000
        SCRATCH.mkdir(parents=True, exist_ok=True)
        result = subprocess.run([str(DUMPBIN),'/NOLOGO','/DISASM:BYTES',f'/RANGE:0x{im.base+args.decode:x},0x{im.base+args.decode+args.size-1:x}',str(args.exe)],capture_output=True,check=True)
        destination = SCRATCH / f'decode-{args.decode:08x}-{args.size:x}.txt'
        destination.write_bytes(result.stdout)
        print(result.stdout.decode('utf-8',errors='replace'))
        print(json.dumps(dict(bytes=im.record(args.decode,args.size),decode=str(destination),decode_sha256=digest(result.stdout))))

if __name__ == '__main__':
    main()
