"""Bounded file-only original cinematic queue ownership investigation."""
from __future__ import annotations
import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import re
import struct
import subprocess
import sys
sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[4]
SCRATCH = ROOT / 'reports/coordinator-20261030-cinematic-owner27'
SOURCE = ROOT / 'handoff/tasks/CHAT27/20261030-manager18/cinematic_manager.py'
INHERITED = ROOT / 'handoff/tasks/CHAT27/20261030-queue25/proof01.json'
assert hashlib.sha256(SOURCE.read_bytes()).hexdigest() == '1a6506e11576f290e70f847a65f307cf1cebffab928360120d4fee5e587857fb'
assert hashlib.sha256(INHERITED.read_bytes()).hexdigest() == '09a31eea86970d877e406ae258273f1188910571f494e6539e8c6c701b2772ab'
spec = importlib.util.spec_from_file_location('owner27_file_only_helper', SOURCE)
helper = importlib.util.module_from_spec(spec)
spec.loader.exec_module(helper)
UNITS = {
    0x3b5a00: (0x8fc, 'Enclosing complete function for exact reset/consumer candidates3b61da and3b620d; virtual+14 dispatch forwards its queue.'),
    0x211270: (0xd, 'Assigned companion interface table+0x0c; accepts pass index1.'),
    0x2112a0: (4, 'Assigned companion interface table+0x20; returns-1.'),
    0x3a6a40: (0x1f, 'Assigned companion interface table+0x10; count1 iff companion+0xe8 handle and pointee exist.'),
    0x38b760: (0x4b, 'Callback address explicitly installed at3b5cea; forwards retained registration interface and persistent B to virtual+14.'),
    0x3b3b30: (0xa4, 'Exact call3b5f0b: alternative local queue without direct known consumer call.'),
    0xb489d0: (6, 'Exact call3b5afc: return global byte selecting registration record partition.'),
    0xb489e0: (0xc, 'Exact call3b5c27: nonzero global byte gates persistent queue branch.'),
}

def save(name, value):
    dest = SCRATCH / name
    raw = (json.dumps(value, indent=2)+'\n').encode('utf-8')
    with dest.open('xb') as stream:
        stream.write(raw)
    return value

def scan(im, targets):
    for target in targets:
        hits = []
        for start, _, offset, length, flags in im.sections:
            if not flags & 0x20000000:
                continue
            raw = im.data[offset:offset+length]
            for pos in range(len(raw)-5):
                if raw[pos] in (0xe8,0xe9) and start+pos+5+struct.unpack_from('<i',raw,pos+1)[0] == target:
                    hits.append(dict(**im.record(start+pos,5),bytes=raw[pos:pos+5].hex()))
        save(f'raw-rel32-{target:08x}.json',dict(kind='Raw candidates only; not decoded instruction boundaries.',target=hex(target),hits=hits))
        print(json.dumps(dict(target=hex(target),hits=hits)))

def pointers(im, targets):
    for target in targets:
        hits=[]
        pattern=struct.pack('<I',im.base+target)
        for start, _, offset, length, flags in im.sections:
            raw=im.data[offset:offset+length]
            pos=raw.find(pattern)
            while pos >= 0:
                hits.append(dict(**im.record(start+pos,4),executable=bool(flags & 0x20000000)))
                pos=raw.find(pattern,pos+1)
        save(f'raw-pointer-{target:08x}.json',dict(kind='Raw pointer candidates only.',target=hex(target),hits=hits))
        print(json.dumps(dict(target=hex(target),hits=hits)))

def decode(im, at, unit, count):
    assert unit in UNITS and len(UNITS)<=8 and 1<=count<=1024
    bound=UNITS[unit][0]
    assert unit<=at<unit+bound
    start,lines=at,[]
    for _ in range(count):
        raw,asm,line=helper.instruction(im,at)
        assert at+len(raw)<=unit+bound,(hex(at),raw.hex(),asm,hex(unit+bound))
        lines.append(line)
        at+=len(raw)
        if asm.startswith(('ret','int','jmp')):
            break
    covered=set(range(start,at))
    for path in SCRATCH.glob('decode-*.json'):
        row=json.loads(path.read_bytes())
        covered.update(range(int(row['rva'],0),int(row['rva'],0)+row['size']))
    assert len(covered)<=65536
    raw=('\n'.join(lines)+'\n').encode('utf-8')
    dest=SCRATCH/f'decode-{start:08x}-{at-start:x}.txt'
    with dest.open('xb') as stream:
        stream.write(raw)
    row=dict(**im.record(start,at-start),unit=hex(unit),bytes=im.read(start,at-start).hex(),decode_sha256=helper.digest(raw),instruction_count=len(lines))
    save(dest.with_suffix('.json').name,row)
    print(raw.decode('utf-8'))
    print(json.dumps(row))

def complete(im, at, unit):
    assert unit in UNITS and len(UNITS)<=8
    end=unit+UNITS[unit][0]
    assert unit<=at<end
    result=subprocess.run([str(helper.DUMPBIN),'/NOLOGO','/DISASM:BYTES',f'/RANGE:0x{im.base+at:x},0x{im.base+end-1:x}',str(im.path)],capture_output=True,check=True)
    source=result.stdout.decode('utf-8',errors='replace').splitlines()
    pattern=re.compile(r'^\s*([0-9A-Fa-f]{8}):\s+((?:[0-9A-Fa-f]{2} )+)(.*)$')
    rows=[]
    lines=[]
    for line in source:
        m=pattern.match(line)
        if m and m[3].strip():
            rows.append(dict(rva=int(m[1],16)-im.base,raw=bytes.fromhex(m[2]),asm=m[3].strip()))
            lines.append(line)
        elif rows and re.fullmatch(r'\s{10,}(?:[0-9A-Fa-f]{2}\s*)+',line):
            rows[-1]['raw']+=bytes.fromhex(line.strip())
            lines.append(line)
    cursor=at
    for row in rows:
        assert row['rva']==cursor,(hex(cursor),row)
        assert im.read(cursor,len(row['raw']))==row['raw']
        cursor+=len(row['raw'])
    assert cursor==end and rows[-1]['asm'].startswith('ret'),(hex(cursor),hex(end),rows[-1])
    raw=('\n'.join(lines)+'\n').encode('utf-8')
    dest=SCRATCH/f'decode-{at:08x}-{end-at:x}.txt'
    with dest.open('xb') as stream:
        stream.write(raw)
    receipt=dict(**im.record(at,end-at),unit=hex(unit),bytes=im.read(at,end-at).hex(),decode_sha256=helper.digest(raw),instruction_count=len(rows))
    save(dest.with_suffix('.json').name,receipt)
    print(json.dumps(dict(path=str(dest),rva=hex(at),size=end-at,instructions=len(rows))))

def proof(im):
    inherited_specs=(
        ('queue25','09a31eea86970d877e406ae258273f1188910571f494e6539e8c6c701b2772ab'),
        ('constructor20','52bc97c398eb5823ad7b29d54c46187de842fb775067a8acb024753d32cdf852'),
        ('child21','c553d972c6fc54623be7e2222ec9426481d66f5e62b6116e1ff132a2cbd6d110'),
        ('manager18','4ff54dcefc7901926869f376322a023b438a8f1ab1f3477afdd421579936a432'),
        ('update18','5b4a5c5c60e3577e6814c9d6a9cc373de3433de26d48f2540b4fde7ee0272f43'),
    )
    inherited=[]
    prior={}
    for name,digest in inherited_specs:
        path=ROOT/f'handoff/tasks/CHAT27/20261030-{name}/proof01.json'
        assert helper.digest(path.read_bytes())==digest
        inherited.append(dict(path=str(path.relative_to(ROOT)),sha256=digest))
        prior[name]=json.loads(path.read_bytes())
    reused=[]
    for name,units in (
        ('queue25',{0x3b3270,0xb6bb00,0xb6c520,0xb505c0}),
        ('constructor20',{0x3b23c0,0x39cd30}),
        ('child21',{0x41e610,0xb50630}),
    ):
        reused.extend(r for r in prior[name]['saved_decodes'] if int(r['unit'],0) in units)
    reused.extend(r for r in prior['manager18']['saved_decodes'] if 0x3b1800<=int(r['rva'],0)<0x3b19e2)
    reused.extend(r for r in prior['update18']['saved_decodes'] if int(r['rva'],0)==0x3b228d)
    starts=set()
    for row in reused:
        at=int(row['rva'],0)
        assert im.record(at,row['size'])['sha256']==row['sha256']
        raw=(ROOT/row['path']).read_bytes()
        assert helper.digest(raw)==row['decode_sha256']
        starts.update(int(m,16)-im.base for m in re.findall(r'^\s*([0-9A-Fa-f]{8}):',raw.decode('utf-8'),re.M))
    covered=set()
    decodes=[]
    for path in sorted(SCRATCH.glob('decode-*.json')):
        row=json.loads(path.read_bytes())
        at,unit=int(row['rva'],0),int(row['unit'],0)
        assert unit in UNITS and unit<=at and at+row['size']<=unit+UNITS[unit][0]
        assert im.read(at,row['size']).hex()==row['bytes']
        assert im.record(at,row['size'])['sha256']==row['sha256']
        raw=path.with_suffix('.txt').read_bytes()
        assert helper.digest(raw)==row['decode_sha256']
        starts.update(int(m,16)-im.base for m in re.findall(r'^\s*([0-9A-Fa-f]{8}):',raw.decode('utf-8'),re.M))
        covered.update(range(at,at+row['size']))
        decodes.append(dict(**row,path=str(path.with_suffix('.txt').relative_to(ROOT))))
    expected={at+i for at,(size,_) in UNITS.items() for i in range(size)}
    assert covered==expected and len(covered)==2605 and len(UNITS)==8
    def exact(at,encoded,meaning,data=False):
        raw=bytes.fromhex(encoded)
        assert im.read(at,len(raw))==raw,hex(at)
        assert data or at in starts,hex(at)
        return dict(**im.record(at,len(raw)),bytes=encoded,meaning=meaning)
    pins=[exact(*r) for r in (
        (0x3b2385,'8d8e80000000','Inherited first-use registration passes the exact companion+0x80 interface.'),
        (0x3b238c,'8b4814','Inherited registration receiver is [TLS-context+0x14]; this does not itself prove a selected-view owner.'),
        (0x3b1829,'895c2438','Inherited registration stores the incoming interface as record first dword.'),
        (0x3b199d,'8d4f04','Inherited registration appends to receiver+4 collection.'),
        (0x3b19aa,'8d5728','Inherited registration record stride0x28.'),
        (0x3b5ab1,'8b7004','New enclosing function reads its receiver M+4 record begin.'),
        (0x3b5ad3,'8b0e8b118b4210ffd0','Read interface from record first dword and call its assigned count slot+0x10.'),
        (0x3b5aea,'8b4424208b520c50ffd2','Gate each record using assigned slot+0x0c and pass index0 or1.'),
        (0x3b5b05,'807e2500','Record+0x25 participates in branch partition; value is not a selected-view flag.'),
        (0x3b5b2e,'8b0e8b118b4220ffd0','Assigned slot+0x20 supplies partition category.'),
        (0x3b5bcf,'8930','Direct-consumer list fast append preserves the exact registration record pointer.'),
        (0x3b5c0d,'83c628','The new update function scans registration records in the inherited0x28-byte stride.'),
        (0x3b616a,'8d8c2400090000','With stable main-body ESP=P, local queue inline data starts at P+0x900.'),
        (0x3b6175,'898c24f0080000','Local B.inline=P+0x900 is stored at B+0x10.'),
        (0x3b6183,'899424e4080000','Local B.end is P+0x900; B itself is P+0x8e0.'),
        (0x3b6191,'898c24e8080000','Local B.capacity pointer starts at P+0x1000.'),
        (0x3b61a6,'898424e0080000','Local B.begin is P+0x900.'),
        (0x3b61ad,'89bc2400100000','Initialize the same B+0x720 insertion count to zero.'),
        (0x3b61ba,'8b34b9','Load the retained registration record from the direct-consumer list.'),
        (0x3b61d3,'8d8c24ec080000','After three reset arguments, ECX=P+0x8e0 is exactly local B.'),
        (0x3b61e6,'8b0e8b018b4014','Receiver is [record]; target is that exact interface assigned slot+0x14.'),
        (0x3b61ed,'528d9424e40800005251d91c24','Pass arguments(first scalar,B=P+0x8e0,record flag) to the interface update.'),
        (0x3b61fa,'ffd0','Virtual update resolves to3b23c0 for the inherited companion table; all three arguments are then forwarded to39cd30.'),
        (0x3b61ff,'8d8c24e00800006a015151d91c24','Immediately pass the identical B=P+0x8e0 as second stack argument to b6bb00(T,B,1).'),
        (0x3b6236,'518d8c24e4080000','Postprocessing receiver remains the same B after pushing one scalar argument.'),
        (0x3b62d2,'8b44242083c00183f802','Outer pass index is incremented and bounded to0,1; assigned companion gate accepts1.'),
        (0x3b5ca5,'8b7154','Persistent alternative queue base comes from [M+0x54].'),
        (0x3b5cac,'0374241c','Persistent queue B=[M+0x54]+current byte offset.'),
        (0x3b5cea,'6860b77800','Exact producer callback address38b760 is installed into the persistent queue job.'),
        (0x3b5d0b,'89be6007000089b664070000898668070000','Store retained record at B+0x760, same B at B+0x764, incoming scalar at B+0x768.'),
        (0x3b5da4,'56','Forward this same persistent B as second stack argument to b6b670.'),
        (0x3b5ddb,'8144241cd00d0000','Persistent queue slots advance by0xdd0 bytes.'),
        (0x38b789,'8b5214','Scheduled producer callback also invokes exact interface virtual+0x14.'),
        (0x38b790,'8b4424105051d91c24ffd2','Callback forwards its second incoming argument B to interface update without altering it.'),
        (0x3b6107,'8d942414100000','Other in-function local queue is B0=P+0x1010 after the flag push; it has no direct b6bb00 call.'),
        (0x3b3b92,'8d442414','Separate helper local queue is Q+0x10 after its flag push; no known queue consumer is called in that helper.'),
        (0xb489d0,'a0d9498901c3','Partition test reads global byte RVA14949d9.'),
        (0xb489e0,'33c03805d84989010f95c0c3','Persistent branch uses nonzero global byte RVA14949d8, the same global inspected by inherited b6c520 wrapper.'),
    )]
    tables=[]
    for offset,target,meaning in (
        (0xc,0x211270,'Accept pass1 only.'),
        (0x10,0x3a6a40,'Return1 iff interface+0x68 handle and pointee exist; equivalent to companion+0xe8.'),
        (0x14,0x3b23c0,'Inherited exact cinematic queue producer delegate.'),
        (0x20,0x2112a0,'Return-1; category is outside1..5.'),
    ):
        at=0x102c974+offset
        encoded=struct.pack('<I',im.base+target).hex()
        tables.append(dict(**exact(at,encoded,meaning,True),table_rva='0x102c974',offset=hex(offset),target_rva=hex(target)))
    calls=[]
    for at,target,meaning in (
        (0x3b238f,0x3b1800,'Inherited registration of exact companion interface into its TLS+14 manager.'),
        (0x3b2436,0x39cd30,'Inherited cinematic delegate forwards the actual incoming B unchanged.'),
        (0x39cdf5,0x41e610,'Inherited child update retains the B argument.'),
        (0x41e657,0xb50630,'Inherited child appends into that B.'),
        (0x3b5afc,0xb489d0,'Partition byte reader.'),
        (0x3b5c27,0xb489e0,'Persistent-queue branch global selector.'),
        (0x3b61da,0x3b3270,'Reset direct local B with count from companion slot+0x10, third incoming scalar and registration flag.'),
        (0x3b620d,0xb6bb00,'Consume the identical direct local B immediately after virtual+14 update.'),
        (0x3b6241,0xb505c0,'Postprocess identical direct local B after interface virtual+0x18.'),
        (0x3b5cbd,0x3b3270,'Reset persistent alternative B.'),
        (0x3b5daf,0xb6b670,'Same persistent B goes to the alternate consumer also reached by b6c520; scheduler/body not decoded here.'),
        (0x3b605c,0xb505c0,'Postprocess persistent B after scheduling synchronization.'),
        (0x3b5f0b,0x3b3b30,'Explicit alternative helper owns its own queue; no direct known consumer call.'),
        (0x3b60f3,0x3b3270,'Reset other local B0=P+0x1010.'),
        (0x3b3b81,0x3b3270,'Reset helper-local B with flag0.'),
    ):
        assert at in starts
        raw=im.read(at,5)
        assert raw[0]==0xe8 and at+5+struct.unpack_from('<i',raw,1)[0]==target
        calls.append(dict(**im.record(at,5),bytes=raw.hex(),target_rva=hex(target),meaning=meaning))
    scans=[]
    for path in sorted(SCRATCH.glob('raw-*.json')):
        row=json.loads(path.read_bytes())
        scans.append(dict(path=str(path.relative_to(ROOT)),sha256=helper.digest(path.read_bytes()),target=row['target'],candidate_count=len(row['hits']),treatment='Only candidates enclosed in admitted complete functions support instruction/call claims.'))
    return dict(schema='chat27.cinematic_original_queue_static.v1',evidence_level='Offline file-backed static code; conditional object/call dataflow, no native execution or headset acceptance.',
        supplied_head='6c42c3fd6d39715d47a07584061697b2be8a678e',git_queried=False,
        executable_sha256=helper.SHA,preferred_base=hex(im.base),script_sha256=helper.digest(Path(__file__).read_bytes()),
        helper=dict(path=str(SOURCE.relative_to(ROOT)),sha256=helper.digest(SOURCE.read_bytes())),inherited=inherited,
        scope=dict(maximum_new_complete_functions=8,new_complete_functions=8,partial_functions=0,new_unique_decoded_bytes=2605,maximum_decoded_bytes=65536,adjacent_spill_functions=0,ceiling_met=True,reused_bytes=sum(r['size'] for r in reused)),
        units=[dict(**im.record(at,size),bytes=im.read(at,size).hex(),complete=True,meaning=meaning) for at,(size,meaning) in UNITS.items()],
        saved_decodes=decodes,reused_decodes=reused,exact_instructions=pins,table_cells=tables,direct_calls=calls,raw_candidate_searches=scans,
        abi=dict(
            main='3b5a00 is thiscall with three stack scalars A,D,T and ret0x0c. M denotes incoming ECX. P denotes stable main-body ESP after saving EBX,ESI,EDI. These are symbolic addresses, not runtime captures.',
            direct_queue='B=P+0x8e0, inline=P+0x900, initial begin=end=inline, capacity=P+0x1000, insertion count[B+0x720]=0. Reset3b61da returns ret0x0c. Slot+0x14 call3b61fa gets (A,B,byte[R+0x25]); companion target3b23c0 returns ret0x0c and forwards B to39cd30. Call3b620d is b6bb00(T,B,1), with caller cleanup0x0c. Call3b6241 invokes b505c0(B,T).',
            persistent='For the pooled branch, B=[M+0x54]+i*0xdd0. This B is reset, stored at B+0x764 beside record and scalar payload, passed to b6b670 as argument2 and later used as b505c0 receiver. Callback38b760 is cdecl(record,B,A,TLS) and forwards its own B to the interface. Job helper invocation ABI/order is not independently decoded.',
            alternatives='Non-consuming paths pass B0=P+0x1010 at3b6113 or helper-local B=Q+0x10 at3b3b9b. Neither complete body invokes b6bb00/b6c520 for those queues; interface+0x18 effects remain unexamined.'),
        findings=dict(
            static_original_queue_join=True,
            exact_join='Given a registration record R whose first dword is the inherited companion+0x80 interface,3b5a00 direct branch passes its own B through3b61fa -> table102c974+14 ->3b23c0 ->3b2436/39cd30 ->41e610/b50630, then passes that exact B at3b620d to inherited consumer b6bb00. This is a same-object path from the cinematic delegate to the known consumer, independent of separate caller424370.',
            dispatch_qualification='Assigned companion table+0x0c accepts pass1, +0x10 returns1 iff handle/pointee at companion+0xe8 exist, and +0x20 returns-1.3b5a00 scans M+4..M+8 in0x28-byte records and preserves each record pointer in its partition lists. The loop and table methods therefore support the companion dispatch, subject to the recorded branch conditions.',
            direct_route_predicate='Let F=byte14949d9, G=byte14949d8, q=byte[R+0x25], p=(byte[M+0x58]!=0 && G!=0). Eligible companion records have count1 on pass1. If (!F || q!=0) and either !p or the pooled cumulative count would exceed32, R enters the direct-consumer list and uses B=P+0x8e0. Other eligible records use the separately described pooled or non-consuming alternatives.',
            no_unique_runtime_owner='No execution selects a concrete R, M, B or route. Multiple caller-owned queues can arrive at the exact inherited delegate. Do not label every incoming B as P+0x8e0 or as the separate424370 queue.',
            upstream_limit='Inherited registration uses M=[TLS+0x14]. The new function proves same-record behavior for its own incoming M, but equivalence of a concrete scheduled3b5a00 receiver to that exact TLS manager instance is not established. Raw callers13edbc and435cc1 are preserved as unadmitted candidates only.',
            consumer_limit='The direct route calls b6bb00 itself; it does not call b6c520. b6c520 remains only the previously proved wrapper. The b6b9c0 evaluator and downstream outputs are owned by a separate worker and were not decoded.',
            acceptance_limit='No selected local-player view, matrix composition, HUD, cutscene entry/skip/exit, gameplay-return or continuous head-rotation authority. No production correction/API proposal.'),
        next_unresolved_edge=dict(candidate_rva='0x435cc1',target_rva='0x3b5a00',status='Raw rel32 candidate only; enclosing caller not decoded in this job.',required='Admit the complete enclosing caller and verify its receiver identity against the exact [TLS+0x14] manager that retained companion+0x80. This is the remaining concrete scheduling-instance join, before any live-selected-view claim.'),
        verification_attempts='One initial slot+0x0c bound was12 bytes, one byte short of ret4. Complete-range parser rejected missing end/return before saving any decode. Corrected13-byte bound verified. No neighboring instruction was admitted. All8 complete-function coverage, inherited/new bytes, saved hashes, call targets and table cells pass final checks.',
        execution=dict(production_writes=False,git=False,builds=False,tests=False,game=False,process_access=False,input=False,xr=False,profiles=False,installs=False,packages=False,subagents=False),frozen=True)

def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--exe',type=Path,required=True)
    parser.add_argument('--calls',type=lambda x:int(x,0),nargs='+')
    parser.add_argument('--pointers',type=lambda x:int(x,0),nargs='+')
    parser.add_argument('--data',type=lambda x:int(x,0))
    parser.add_argument('--size',type=lambda x:int(x,0),default=64)
    parser.add_argument('--decode',type=lambda x:int(x,0))
    parser.add_argument('--unit',type=lambda x:int(x,0))
    parser.add_argument('--count',type=int,default=256)
    parser.add_argument('--complete',type=lambda x:int(x,0))
    parser.add_argument('--output',type=Path)
    args=parser.parse_args()
    SCRATCH.mkdir(parents=True,exist_ok=True)
    for key in ('TEMP','TMP','TMPDIR'):
        os.environ[key]=str(SCRATCH)
    im=helper.Image(args.exe)
    if args.output is not None:
        assert args.output.resolve()==Path(__file__).resolve().parent/'proof01.json'
        result=proof(im)
        raw=(json.dumps(result,indent=2)+'\n').encode('utf-8')
        with args.output.open('xb') as stream:
            stream.write(raw)
        print(json.dumps(dict(path=str(args.output.resolve()),sha256=helper.digest(raw),scope=result['scope'])))
    elif args.calls:
        scan(im,args.calls)
    elif args.pointers:
        pointers(im,args.pointers)
    elif args.data is not None:
        assert 0<args.size<=0x400
        print(json.dumps(dict(**im.record(args.data,args.size),bytes=im.read(args.data,args.size).hex())))
    elif args.decode is not None:
        decode(im,args.decode,args.unit or args.decode,args.count)
    elif args.complete is not None:
        complete(im,args.complete,args.unit or args.complete)

if __name__=='__main__':
    main()
