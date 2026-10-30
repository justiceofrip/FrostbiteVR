"""Bounded file-only investigation of the exact child21 submission queue."""
from __future__ import annotations
import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import struct
import sys
sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[4]
SCRATCH = ROOT / 'reports/coordinator-20261030-cinematic-queue25'
INHERITED = ROOT / 'handoff/tasks/CHAT27/20261030-child21/proof01.json'
SOURCE = ROOT / 'handoff/tasks/CHAT27/20261030-manager18/cinematic_manager.py'
assert hashlib.sha256(INHERITED.read_bytes()).hexdigest() == 'c553d972c6fc54623be7e2222ec9426481d66f5e62b6116e1ff132a2cbd6d110'
assert hashlib.sha256(SOURCE.read_bytes()).hexdigest() == '1a6506e11576f290e70f847a65f307cf1cebffab928360120d4fee5e587857fb'
spec = importlib.util.spec_from_file_location('queue25_file_only_helper', SOURCE)
helper = importlib.util.module_from_spec(spec)
spec.loader.exec_module(helper)
UNITS = {
    0x3b3270: (0x82, 'Exact count+0x720 reset and common direct caller with producer.'),
    0x424370: (0xd5, 'Exact direct caller of reset and inherited producer; stack-local queue.'),
    0xb6c520: (0xaf, 'Exact queue consumer edge at0x424419.'),
    0xb505c0: (0x63, 'Exact queue postprocessing edge at0xb6c5b9.'),
    0xb6bb00: (0x1e0, 'Exact record consumer at0xb6c546 when the checked global byte is zero.'),
}

def save(name, value):
    raw = (json.dumps(value, indent=2)+'\n').encode('utf-8')
    dest = SCRATCH/name
    assert not dest.exists(), str(dest)
    dest.write_bytes(raw)
    return value

def scan(im, target):
    hits = []
    for start, _, offset, length, flags in im.sections:
        if not flags & 0x20000000:
            continue
        raw = im.data[offset:offset+length]
        if target is None:
            pattern = bytes.fromhex('20070000')
            pos = raw.find(pattern)
            while pos >= 0:
                at = start+pos
                hits.append(dict(rva=hex(at), context_rva=hex(at-4), context=im.read(at-4,16).hex()))
                pos = raw.find(pattern,pos+1)
        else:
            for pos in range(len(raw)-5):
                if raw[pos] in (0xe8,0xe9) and start+pos+5+struct.unpack_from('<i',raw,pos+1)[0] == target:
                    hits.append(dict(**im.record(start+pos,5),bytes=raw[pos:pos+5].hex()))
    name = 'raw-count720-candidates.json' if target is None else f'raw-rel32-{target:08x}.json'
    save(name,dict(kind='raw byte candidates; not admitted instruction boundaries or ownership',target=None if target is None else hex(target),hits=hits))
    print(json.dumps(dict(count=len(hits),hits=hits)))

def decode(im, at, unit, count):
    assert unit in UNITS and len(UNITS)<=8 and 1<=count<=512
    bound = UNITS[unit][0]
    assert unit<=at<unit+bound
    start,lines=at,[]
    for _ in range(count):
        raw,asm,line=helper.instruction(im,at)
        assert at+len(raw)<=unit+bound, (hex(at),raw.hex(),asm,hex(unit+bound))
        lines.append(line)
        at+=len(raw)
        if asm.startswith(('ret','int','jmp')):
            break
    covered=set(range(start,at))
    for path in SCRATCH.glob('decode-*.json'):
        row=json.loads(path.read_text(encoding='utf-8'))
        covered.update(range(int(row['rva'],0),int(row['rva'],0)+row['size']))
    assert len(covered)<=65536
    raw=('\n'.join(lines)+'\n').encode('utf-8')
    dest=SCRATCH/f'decode-{start:08x}-{at-start:x}.txt'
    assert not dest.exists()
    dest.write_bytes(raw)
    row=dict(**im.record(start,at-start),unit=hex(unit),bytes=im.read(start,at-start).hex(),decode_sha256=helper.digest(raw),instruction_count=len(lines))
    save(dest.with_suffix('.json').name,row)
    print(raw.decode('utf-8'))
    print(json.dumps(row))

def proof(im):
    inherited=json.loads(INHERITED.read_text(encoding='utf-8'))
    constructor_path=ROOT/'handoff/tasks/CHAT27/20261030-constructor20/proof01.json'
    assert helper.digest(constructor_path.read_bytes())=='52bc97c398eb5823ad7b29d54c46187de842fb775067a8acb024753d32cdf852'
    constructor=json.loads(constructor_path.read_text(encoding='utf-8'))
    reused=[]
    for prior,units in ((inherited,{0x41e610,0xb50630}),(constructor,{0x3b23c0,0x39cd30})):
        for row in prior['saved_decodes']:
            if int(row['unit'],0) not in units:
                continue
            at=int(row['rva'],0)
            assert im.record(at,row['size'])['sha256']==row['sha256']
            assert helper.digest((ROOT/row['path']).read_bytes())==row['decode_sha256']
            reused.append(row)
    covered=set()
    decodes=[]
    for path in sorted(SCRATCH.glob('decode-*.json')):
        row=json.loads(path.read_text(encoding='utf-8'))
        at,unit=int(row['rva'],0),int(row['unit'],0)
        assert unit in UNITS and unit<=at and at+row['size']<=unit+UNITS[unit][0]
        assert im.read(at,row['size']).hex()==row['bytes']
        assert im.record(at,row['size'])['sha256']==row['sha256']
        assert helper.digest(path.with_suffix('.txt').read_bytes())==row['decode_sha256']
        covered.update(range(at,at+row['size']))
        decodes.append(dict(**row,path=str(path.with_suffix('.txt').relative_to(ROOT))))
    expected={at+i for at,(size,_) in UNITS.items() for i in range(size)}
    assert covered==expected and len(covered)==1097 and len(UNITS)==5
    def exact(at,encoded,meaning):
        raw=bytes.fromhex(encoded)
        assert im.read(at,len(raw))==raw,hex(at)
        return dict(**im.record(at,len(raw)),bytes=encoded,meaning=meaning)
    pins=[exact(*row) for row in (
        (0x3b327c,'899e20070000','Verified instruction boundary: zero the same +0x720 insertion index used by b50630.'),
        (0x3b3287,'8b4e042b0e','Reset loop length comes from B.end-B.begin, divided by0x70 by the following instructions.'),
        (0x3b32b8,'88504cf30f11405889584089585c88584d','Reset fills R+0x4c from argument3, +0x58 from argument2, clears +0x40/+0x5c/+0x4d.'),
        (0x3b32e4,'83c570','Reset record stride is0x70.'),
        (0x424397,'8d442430','At stable post-scalar-call ESP=P, inline storage begins at P+0x30.'),
        (0x4243b6,'8d4c241c','After three reset arguments, ECX=P+0x10=B; B+0x20 is inline storage.'),
        (0x4243c2,'c784243c07000000000000','Initialize stack-local B+0x720 to zero.'),
        (0x4243f0,'8b4e54','Separate caller submits its receiver+0x54 state pointer.'),
        (0x4243f7,'8d4c2430','After eight append arguments, ECX=P+0x10 is exactly the same stack-local B.'),
        (0x42440c,'8d54241052518d4c2410','Consumer receives B=P+0x10 as stack argument2 and separate context E=P+8 in ECX.'),
        (0xb6c52c,'803dd849890100','Global byte at RVA0x14949d8 selects two consumer implementations; runtime value is unknown.'),
        (0xb6bb04,'8b5c2428','Cdecl consumer loads incoming second stack argument B into EBX.'),
        (0xb6bb08,'8b4b042b0b','Record count derives from B.end-B.begin, not B+0x720.'),
        (0xb6bb50,'817c2434000e0000','Direct path stops when byte record offset reaches0xe00, at most32 records.'),
        (0xb6bbea,'8b3303742434','R=B.begin+current byte offset.'),
        (0xb6bbf1,'8b46408b48382b4834','Read R+0x40 state and its +0x34/+0x38 pointer span.'),
        (0xb6bc06,'d946440fb6564d8b4e40','Load R+0x44 scalar, R+0x4d flag, and ECX=R+0x40 for the exact processing call.'),
        (0xb6bc11,'5683ec08','Pass R itself as third argument to0xb6b9c0; it is not merely a copied state pointer.'),
        (0xb6bc25,'807e4c00','R+0x4c gates the optional following processing branch.'),
        (0xb6bc72,'8344243870','Advance the direct processing loop by exactly0x70 bytes.'),
        (0xb505c5,'8b378b4f042bce','Postprocessing also uses B.begin/B.end.'),
        (0xb505e6,'8b4c2e408b492c','Postprocessing loads Q=[[R+0x40]+0x2c]; only Q is null-checked.'),
        (0xb505f1,'8b118b4204ffd0','Invoke Q virtual+4; exact dynamic target remains unresolved.'),
        (0xb50615,'83c570','Postprocessing record stride is0x70.'),
        (0x102c988,'c0237b00','Inherited assigned companion+0x80 vtable+0x14 points to0x3b23c0.'),
    )]
    call_specs=(
        (0x3b2436,0x39cd30,'Inherited delegate forwards second incoming argument B unchanged; concrete caller-owned B remains unknown.'),
        (0x41e657,0xb50630,'Inherited cinematic producer uses that original B; not the stack-local B proved below.'),
        (0x3b3282,0x3b26b0,'Reset/growth helper; body not decoded.'),
        (0x42438f,0x41e590,'Reused child indexed-state update.'),
        (0x4243cd,0x3b3270,'Reset local B with arguments1,T,1.'),
        (0x4243fb,0xb50630,'Append to the same local B using the inherited producer.'),
        (0x424404,0xb69db0,'Initialize separate local consumer context E; body not decoded.'),
        (0x424419,0xb6c520,'Consume local B with receiver E and arguments(T,B).'),
        (0x424422,0xb6aa60,'Context cleanup; body not decoded.'),
        (0xb6c546,0xb6bb00,'When global byte is zero, cdecl process(T,B,1); ECX is not read by this callee.'),
        (0xb6c585,0xb6b670,'Nonzero-global branch forwards same B; target not decoded and its processing effects unclaimed.'),
        (0xb6c5b9,0xb505c0,'After the nonzero-global branch, postprocess same B; scalar argument is not read by this callee.'),
        (0xb6bc20,0xb6b9c0,'Actual record processor: ECX=[R+0x40], arguments(T,[R+0x44],R,byte[R+0x4d],W); body not decoded.'),
        (0xb6bc3a,0xb68890,'Conditional per-state processing into local list; body not decoded.'),
        (0xb6bc5e,0xb68aa0,'Conditional follow-on receives R and work/list/scalar fields; body not decoded.'),
    )
    calls=[]
    for at,target,meaning in call_specs:
        raw=im.read(at,5)
        assert raw[0]==0xe8 and at+5+struct.unpack_from('<i',raw,1)[0]==target,hex(at)
        calls.append(dict(**im.record(at,5),bytes=raw.hex(),target_rva=hex(target),new_target_decoded=target in UNITS,meaning=meaning))
    scans=[]
    for path in sorted(SCRATCH.glob('raw-*.json')):
        row=json.loads(path.read_text(encoding='utf-8'))
        scans.append(dict(path=str(path.relative_to(ROOT)),sha256=helper.digest(path.read_bytes()),candidate_count=len(row['hits']),treatment='Raw candidates only. Only exact instructions in admitted complete functions support claims.'))
    return dict(schema='chat27.cinematic_queue_static_code.v1',
        evidence_level='Offline file-backed static code; no native execution or headset acceptance.',
        executable_sha256=helper.SHA,preferred_base=hex(im.base),script_sha256=helper.digest(Path(__file__).read_bytes()),
        inherited=[dict(path=str(INHERITED.relative_to(ROOT)),sha256=helper.digest(INHERITED.read_bytes())),dict(path=str(constructor_path.relative_to(ROOT)),sha256=helper.digest(constructor_path.read_bytes()))],
        helper=dict(path=str(SOURCE.relative_to(ROOT)),sha256=helper.digest(SOURCE.read_bytes())),
        scope=dict(maximum_new_complete_functions=8,new_complete_functions=5,partial_functions=0,new_unique_decoded_bytes=1097,maximum_decoded_bytes=65536,adjacent_spill_functions=0,reused_bytes=sum(r['size'] for r in reused),ceiling_met=True,
            strategy='Exact +0x720 displacement candidate search; exact producer/reset/consumer rel32 candidate searches; complete caller proves selected instruction boundaries. Stop after direct record consumer, with original B provenance unresolved.'),
        units=[dict(**im.record(at,size),bytes=im.read(at,size).hex(),complete=True,meaning=meaning) for at,(size,meaning) in UNITS.items()],
        saved_decodes=decodes,reused_decodes=reused,exact_instructions=pins,direct_calls=calls,raw_candidate_searches=scans,
        abi=dict(
            original='0x39cd30(A,B,C), reached through0x3b2436, forwards B to child seventh argument and0xb50630 receiver. This incoming B has no newly proven concrete owner.',
            separate_caller='0x424370 is a complete thiscall function with six stack arguments and ret0x18. It owns local B=P+0x10 and E=P+8, with P the stable ESP after0x41e590 returns; B.begin=B.end=B.inline=P+0x30 before reset, B.capacity=P+0x730, and B+0x720=0.',
            reset='0x3b3270(B,1,T,1) resets insertion index, delegates sizing to0x3b26b0, initializes records and returns ret0x0c.',
            local_append='0xb50630(B,[receiver+0x54],arg3,arg4,arg5,arg6,0,-1.0f,-1) consumes eight stack words; its third argument is unused as established by child21.',
            consume='0xb6c520(E,T,B) returns ret8. When global byte RVA0x14949d8 is zero,0xb6bb00(T,B,1) is cdecl and does not read incoming ECX. When nonzero, the other path calls0xb6b670 and then0xb505c0(B,T), whose body does not read T and returns ret4.',
            direct_record='0xb6bb00 computes R=[B]+i*0x70, capped at32 records, and calls0xb6b9c0 with ECX=[R+0x40] and stack arguments(T,[R+0x44],R,byte[R+0x4d],W). The exact target body, its cleanup and its transform effects remain undecoded.'),
        findings=dict(
            new_count_edge='0x3b327c is a verified zero write to B+0x720. The complete0x424370 caller passes the same stack-local B to this reset and to inherited producer0xb50630.',
            consumer='0x424419 explicitly passes this same B to0xb6c520. Its zero-global branch0xb6bb00 reads the exact0x70-byte record and state+0x40, then passes R and its state to0xb6b9c0. The nonzero branch has a verified postprocessor0xb505c0 that calls virtual+4 on [[R+0x40]+0x2c].',
            loop_boundary='The checked consumers derive record length from [B+4]-[B], not insertion count+0x720. Exact original queue sizing and record fill remain necessary when joining a different caller.',
            provenance_limit='The consumer is proved for a separate stack-local queue created in0x424370. No evidence equates it with the incoming B in0x39cd30. This closes queue-format and separate-caller consumption, not the original cinematic ownership chain.',
            output_limit='No newly decoded function proves evaluated matrix composition or selected local-player-view consumption. Processing call0xb6b9c0 remains undecoded; state/class semantic names are not inferred.',
            acceptance_limit='No active camera, HUD, cutscene entry/skip/exit, gameplay-return, or continuous head-rotation authority follows. No production correction or API is proposed.'),
        next_unresolved_edge=dict(callsite_rva='0x3b2436',target_rva='0x39cd30',receiver='companion+0x80',argument='Second stack argument B, forwarded unchanged by0x3b23c0 assigned at vtable RVA0x102c974 slot+0x14.',
            required='Identify the actual incoming B at this exact inherited delegate edge and join that same object to the now-verified record consumer. Do not substitute0x424370 stack-local B for it.'),
        verification_attempts='Three early reset-decode attempts rejected an undersized local bound at the final ret0x0c before saving a decode. Exact return end0x3b32f2 corrected the bound. No neighboring function was decoded; the final complete5-function coverage and byte/hash/ABI checks pass.',
        execution=dict(git=False,builds=False,tests=False,game=False,game_process_access=False,input=False,xr=False,profiles=False,installs=False,packages=False,subagents=False,production_writes=False),
        frozen=True)

def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--exe',type=Path,required=True)
    parser.add_argument('--scan720',action='store_true')
    parser.add_argument('--calls',type=lambda x:int(x,0))
    parser.add_argument('--data',type=lambda x:int(x,0))
    parser.add_argument('--size',type=lambda x:int(x,0),default=64)
    parser.add_argument('--decode',type=lambda x:int(x,0))
    parser.add_argument('--unit',type=lambda x:int(x,0))
    parser.add_argument('--count',type=int,default=120)
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
    elif args.scan720:
        scan(im,None)
    elif args.calls is not None:
        scan(im,args.calls)
    elif args.data is not None:
        assert 0<args.size<=0x200
        print(json.dumps(dict(**im.record(args.data,args.size),bytes=im.read(args.data,args.size).hex())))
    elif args.decode is not None:
        decode(im,args.decode,args.unit or args.decode,args.count)

if __name__=='__main__':
    main()
