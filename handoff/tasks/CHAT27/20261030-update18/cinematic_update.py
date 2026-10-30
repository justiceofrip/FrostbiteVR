"""Bounded file-only examination of exact camera update and direct callees."""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess

ROOT = Path(__file__).resolve().parents[4]
SCRATCH = ROOT / 'reports/coordinator-20261030-implementation18/cinematic-update'
SHA = '3911fcc8914b0158c434cee36f19295316e715d4c9a0e70c857e8c39e3d54258'
DUMPBIN = Path(r'C:\Program Files\Microsoft Visual Studio\18\Community\VC\Tools\MSVC\14.51.36231\bin\HostX64\x64\dumpbin.exe')
UNITS = {0x495d70: 0x13c, 0x227640: 0x164, 0xc02950: 0x40,
         0xc02990: 0x10, 0x49dc40: 0x53, 0x486120: 0x38,
         0x3b2240: 0x171, 0x3b6b00: 0xbb}
FUNCTIONS = (
    (0x495d70,0x139,'Whole-camera timed scalar and numeric state transition update.',True),
    (0x227640,0x162,'Writes receiver word+0x24 and updates four virtual-gated collections.',True),
    (0xc02950,0x40,'Constructs event-shaped local payload and forwards through receiver+0x10.',True),
    (0xc02990,0x10,'Accidental adjacent prefix; incomplete and retired without following edges.',False),
    (0x49dc40,0x53,'Primary table+0xe8 candidate: starts companion and adds event1f callback on success.',True),
    (0x486120,0x35,'Primary table+0xec candidate: stops companion and removes event1f callback.',True),
    (0x3b2240,0x171,'Companion start: resource checks, handles, flags, count and first-use manager registration.',True),
    (0x3b6b00,0xbb,'Companion stop: release handles, clear flag, last-use manager removal and count decrement.',True),
)

def digest(raw):
    return hashlib.sha256(raw).hexdigest()

class Image:
    def __init__(self, path):
        self.data = path.read_bytes()
        assert digest(self.data) == SHA, 'Unexpected executable'
        pe = struct.unpack_from('<I', self.data, 0x3c)[0]
        assert self.data[pe:pe+4] == b'PE\0\0'
        count = struct.unpack_from('<H', self.data, pe+6)[0]
        optional_size = struct.unpack_from('<H', self.data, pe+20)[0]
        optional = pe+24
        assert struct.unpack_from('<H', self.data, optional)[0] == 0x10b
        self.base = struct.unpack_from('<I', self.data, optional+28)[0]
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

def proof(im):
    inherited_path=ROOT/'handoff/tasks/CHAT27/20261030-callback18/proof01.json'
    inherited=inherited_path.read_bytes()
    assert digest(inherited)=='0f8b8f1ee38729d7103ac63df99363daf505a05cbf6ab6b45b78271032b22227'
    covered=set()
    decodes=[]
    for path in sorted(SCRATCH.glob('decode-*.json')):
        row=json.loads(path.read_text(encoding='utf-8'))
        rva=int(row['rva'],0)
        assert im.record(rva,row['size'])['sha256']==row['sha256']
        assert digest(path.with_suffix('.txt').read_bytes())==row['decode_sha256']
        covered.update(range(rva,rva+row['size']))
        decodes.append(dict(**row,path=str(path.with_suffix('.txt').relative_to(ROOT))))
    assert len(FUNCTIONS)==8 and len(covered)<=65536
    allowed=set()
    for rva,size in UNITS.items():
        allowed.update(range(rva,rva+size))
    assert covered<=allowed
    for rva,size,_,_ in FUNCTIONS:
        assert set(range(rva,rva+size))<=covered
    def exact(rva,encoded,meaning):
        raw=bytes.fromhex(encoded)
        assert im.read(rva,len(raw))==raw,hex(rva)
        return dict(**im.record(rva,len(raw)),bytes=encoded,meaning=meaning)
    instructions=[exact(*row) for row in (
        (0x495d77,'f30f10464c','Read timed scalar remainder camera+0x4c.'),
        (0x495d8e,'d8b0c4000000','Normalize remainder by resource+0xc4.'),
        (0x495db6,'d95e50','Store interpolated scalar at camera+0x50.'),
        (0x495dbc,'d821','Subtract first float of incoming update argument from remainder.'),
        (0x495ddc,'8b4634','Dispatch on numeric camera state+0x34.'),
        (0x495df8,'8b82ec000000','State5 invokes virtual slot+0xec.'),
        (0x495e06,'686edbc8d4','State5 event-shaped payload discriminator0xd4c8db6e.'),
        (0x495e27,'c7463400000000','State5 completion sets numeric state0.'),
        (0x495e33,'8b4e3c','State4 reads camera+0x3c and calls its first virtual slot.'),
        (0x495e4a,'807e5400','State4 transition additionally requires byte camera+0x54 nonzero.'),
        (0x495e5c,'c7463405000000','Eligible state4 advances to numeric state5.'),
        (0x495e6a,'8b82e8000000','State2 invokes virtual slot+0xe8 and requires AL nonzero.'),
        (0x495e82,'68fd40fa32','State2 success event-shaped discriminator0x32fa40fd.'),
        (0x495e94,'c6465400','State2 success clears camera+0x54.'),
        (0x495e9d,'c7463404000000','State2 success sets numeric state4.'),
        (0x227655,'66894624','Direct transition helper writes word receiver+0x24.'),
        (0xc0297a,'8b4910','Payload dispatch receiver is camera+0x10.'),
        (0x49dc46,'8b8ed4000000','Start method reads the same camera+0xd4 companion from inherited angular setter chain.'),
        (0x49dc55,'8b81cc000000','Start method passes resource+0xcc to undecoded0x495a20.'),
        (0x49dc84,'81c6dc000000','Successful start uses camera+0xdc callback interface.'),
        (0x49dc8b,'6a1f','Successful start passes event discriminator0x1f.'),
        (0x486148,'81c6dc000000','Stop uses identical camera+0xdc callback interface.'),
        (0x48614f,'6a1f','Stop passes identical event discriminator0x1f.'),
        (0x3b2247,'8b0d44c45401','Companion start reads unknown global object pointer at RVA0x114c444.'),
        (0x3b2250,'8b7e70','Start checks object reached via companion+0x70 then+0x60 then+0xc.'),
        (0x3b228d,'8d8e88000000','Start assigns a handle at companion+0x88 via undecoded helper.'),
        (0x3b22ed,'8dbee8000000','Start uses another handle at companion+0xe8.'),
        (0x3b2354,'8986ec000000','Optional lookup result goes to companion+0xec.'),
        (0x3b2377,'c686fd00000001','Start sets companion+0xfd to1; this is not selected-view authority.'),
        (0x3b2385,'8d8e80000000','First start forwards companion+0x80 to manager operation0x3b1800.'),
        (0x3b2394,'8386dc00000001','Start increments companion+0xdc count.'),
        (0x3b6b26,'c7868800000000000000','Stop clears companion+0x88 after reference-release sequence.'),
        (0x3b6b78,'c786e800000000000000','Stop clears companion+0xe8 after reference-release sequence.'),
        (0x3b6b89,'c686fd00000000','Stop clears companion+0xfd.'),
        (0x3b6b9a,'8d9680000000','Last counted stop forwards companion+0x80 to manager operation0x3b6300.'),
        (0x3b6bb3,'8986dc000000','Positive companion+0xdc count is decremented.'),
    )]
    calls=[]
    for at,target,role in (
        (0x495e0d,0xc02950,'State5 payload dispatch'),
        (0x495e22,0x227640,'State5 word-state transition with arguments0,1'),
        (0x495e89,0xc02950,'State2 success payload dispatch'),
        (0x495e98,0x227640,'State2 success word-state transition with arguments1,1'),
        (0xc02985,0xc00e80,'Event-shaped payload downstream dispatch; undecoded'),
        (0x49dc4d,0x3a69f0,'Companion setter with camera+0x3c; undecoded'),
        (0x49dc5d,0x495a20,'Resource+0xcc and whole-camera resolver; undecoded'),
        (0x49dc6c,0x3b2240,'Companion start with resolver result'),
        (0x49dc77,0x3ff060,'Global source for event service+0x18; undecoded'),
        (0x486129,0x3b6b00,'Companion stop'),
        (0x486136,0x3a69f0,'Companion setter with null; undecoded'),
        (0x48613b,0x3ff060,'Same event-service source; undecoded'),
        (0x3b2284,0x228aa0,'Create/acquire handle from incoming resolver result; undecoded'),
        (0x3b2293,0x191fc0,'Assign companion+0x88 handle; undecoded'),
        (0x3b22bf,0x417ea0,'Operation on companion+0xd0; undecoded'),
        (0x3b22f3,0x428d50,'Operation on companion+0xd0 with resource and output arguments; undecoded'),
        (0x3b22fb,0x222190,'Assign companion+0xe8 handle; undecoded'),
        (0x3b234f,0x21dcc0,'Optional named lookup using constantVA0x141c80c; undecoded'),
        (0x3b2380,0x188b10,'First-use global manager source; undecoded'),
        (0x3b238f,0x3b1800,'First-use manager operation on companion+0x80; undecoded'),
        (0x3b6b50,0x41e660,'Stop operation on companion+0xd0 with resource and zero arguments; undecoded'),
        (0x3b6b92,0x188b10,'Last-use global manager source; undecoded'),
        (0x3b6ba1,0x3b6300,'Last-use manager operation on companion+0x80; undecoded'),
    ):
        raw=im.read(at,5)
        assert raw[0]==0xe8 and at+5+struct.unpack_from('<i',raw,1)[0]==target,hex(at)
        calls.append(dict(**im.record(at,5),target_rva=hex(target),role=role,
            callee_decoded=target in UNITS and target!=0xc02990))
    primary=0x1036550
    slots=[]
    for offset,target in ((0xe8,0x49dc40),(0xec,0x486120)):
        assert struct.unpack('<I',im.read(primary+offset,4))[0]==im.base+target
        slots.append(dict(**im.record(primary+offset,4),table_rva=hex(primary),slot=hex(offset),target_rva=hex(target)))
    return dict(schema='chat27.cinematic_update_static_code.v1',evidence_level='offline file-backed code',
        executable_sha256=SHA,preferred_base=hex(im.base),script_sha256=digest(Path(__file__).read_bytes()),
        inherited=dict(path=str(inherited_path.relative_to(ROOT)),sha256=digest(inherited),
            used='Camera+0xdc callback forwards to0x4955a0; angular output is stored through camera+0xd4 at+0x90..+0xc8; exact next update0x495d70 known caller0x49dba8.'),
        table_origin='Root supplied prior methods17 primary table RVA0x1036550 and boundary0x1036650. Only new exact slots+0xe8/+0xec were read; constructor identity was reused rather than decoded.',
        scope=dict(maximum_units=8,new_function_units=8,complete_deliberate_units=7,adjacent_partial_units=1,
            adjacent_rva='0xc02990',adjacent_bytes=16,unique_decoded_code_and_padding_bytes=len(covered),
            sum_requested_decode_bytes=sum(r['size'] for r in decodes),
            complete_function_bytes=sum(size for _,size,_,complete in FUNCTIONS if complete),
            maximum_code_bytes=65536,ceiling_met=True,stopped_at_unit_limit=True,
            range_strategy='Initial64-byte exact root then instruction-boundary continuations and exact direct/virtual edges. One event-helper continuation crossed its return into16 adjacent bytes; counted as an eighth unit, retained and not followed.'),
        units=[dict(**im.record(rva,size),meaning=meaning,complete=complete) for rva,size,meaning,complete in FUNCTIONS],
        instructions=instructions,direct_calls=calls,virtual_slots=slots,
        update_behavior=dict(
            scalar='If camera+0x4c>0, t=1-oldRemainder/resource+0xc4; camera+0x50=(1-t)*old50+t*camera48. Subtract float[updateArgument] from remainder; if nonpositive, set remainder0 and scalar50=scalar48. Field names and temporal unit remain unproved.',
            state2='Call primary+0xe8; if AL nonzero and state is not already4, send discriminator0x32fa40fd, clear byte+0x54, call0x227640(1,1), set state4.',
            state4='If object+0x3c exists, virtual0 returns ordered >=1, byte+0x54 is set, and possibly-mutated state is neither0 nor5, set state5.',
            state5='Call primary+0xec, send discriminator0xd4c8db6e, then if possibly-mutated state!=0 call0x227640(0,1) and set state0.',
            other_states='No state-path action; scalar step precedes every state.',
            limit='The numeric values are not admitted as active-cinematic/selected-player-view enums.'),
        companion_lifecycle=dict(
            start='0x49dc40 calls0x3a69f0(companion,camera3c), resolves resource+cc with0x495a20(resourcecc,camera), then0x3b2240(companion,result). On success it passes(camera+dc,0x1f) to [0x3ff060()+0x18] virtual+8 and returns1.',
            stop='0x486120 calls0x3b6b00(companion),0x3a69f0(companion,null), then passes(camera+dc,0x1f) to the same event-service virtual+0x10.',
            event_pair='Complementary slots+8/+0x10 support add/remove interpretation for inherited event callback, but service implementations and event names remain undecoded.',
            start_internals='Unknown global object RVA0x114c444 virtual+0x18 looks up resource-shaped key companion70->60->0c, result virtual+0x14 must succeed. Handles+88/+e8 are assigned; flags+fc/+fd and counters+f0/+f4 initialized; count+dc increments. On first use,0x3b1800 receives companion+80 and receiver[0x188b10()+14].',
            stop_internals='Reference-release sequences clear handles+88/+e8;0x41e660 receives resource companion70->60 through companion+d0. Flag+fd clears. If count+dc==1,0x3b6300 receives companion+80 and the same manager; positive count decrements.',
            transform='None of these seven complete decoded functions reads companion+0x90..+0xc8, the inherited relative angular matrix. Any downstream transform consumption remains in undecoded callees or another update.',
            authority_limit='Start/stop and subscription presence are proved for this companion; local-player identity, current view selection, blend ownership, skip and HUD authority remain unproved.'),
        source_implementation=dict(supported_correction=False,production_modified=False,
            reason='Object lifecycle and numeric states do not establish selected cinematic view or HUD authority; no supported production policy or hook proposal.'),
        next_unresolved_edge=dict(rva='0x3b1800',known_caller='0x3b238f',
            relation='Called on first companion start with receiver[0x188b10()+0x14] and argument companion+0x80; paired last-stop operation0x3b6300.',
            question='Determine which update interface/collection receives companion+0x80 and whether its consumer joins the animated/relative transform to an actual selected local-player view.',
            limit='Even scheduling membership is not selected-view authority; do not promote count+dc, flag+fd or camera state4 to an active-cinematic predicate.'),
        execution=dict(game=False,desktop_tests=False,builds=False,tests=False,process_access=False,
            input=False,xr=False,profiles=False,installs=False,packages=False,subagents=False,git=False),
        verification='Executable/inherited proof and preserved decode hashes, exact instruction bytes, direct-call targets, primary table slots and all complete function spans checked file-only.',
        saved_decodes=decodes)

def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--exe',type=Path,required=True)
    parser.add_argument('--decode',type=lambda x:int(x,0))
    parser.add_argument('--size',type=lambda x:int(x,0),default=0x40)
    parser.add_argument('--data',type=lambda x:int(x,0))
    parser.add_argument('--output',type=Path)
    args=parser.parse_args()
    im=Image(args.exe)
    assert len(UNITS)<=8
    if args.output:
        assert args.output.resolve()==Path(__file__).resolve().parent/'proof01.json'
        result=proof(im)
        raw=(json.dumps(result,indent=2)+'\n').encode('utf-8')
        with args.output.open('xb') as stream:
            stream.write(raw)
        print(json.dumps(dict(path=str(args.output.resolve()),size=len(raw),sha256=digest(raw),scope=result['scope'])))
    elif args.decode is not None:
        assert 0<args.size<=0x1000
        assert any(start<=args.decode and args.decode+args.size<=start+size for start,size in UNITS.items())
        SCRATCH.mkdir(parents=True,exist_ok=True)
        covered=set(range(args.decode,args.decode+args.size))
        for path in SCRATCH.glob('decode-*.json'):
            row=json.loads(path.read_text(encoding='utf-8'))
            covered.update(range(int(row['rva'],0),int(row['rva'],0)+row['size']))
        assert len(covered)<=65536
        dest=SCRATCH/f'decode-{args.decode:08x}-{args.size:x}.txt'
        assert not dest.exists(),'Preserve previous decode'
        result=subprocess.run([str(DUMPBIN),'/NOLOGO','/DISASM:BYTES',f'/RANGE:0x{im.base+args.decode:x},0x{im.base+args.decode+args.size-1:x}',str(args.exe)],capture_output=True,check=True)
        dest.write_bytes(result.stdout)
        info=dict(**im.record(args.decode,args.size),decode_sha256=digest(result.stdout))
        dest.with_suffix('.json').write_text(json.dumps(info,indent=2)+'\n',encoding='utf-8')
        print(result.stdout.decode('utf-8',errors='replace'))
        print(json.dumps(info))
    elif args.data is not None:
        assert 0<args.size<=0x1000
        print(json.dumps(dict(**im.record(args.data,args.size),bytes=im.read(args.data,args.size).hex())))

if __name__=='__main__':
    main()
