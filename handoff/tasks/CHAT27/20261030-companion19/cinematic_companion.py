"""Bounded file-only companion ownership and exact virtual dispatch investigation."""
from __future__ import annotations
import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import struct

ROOT = Path(__file__).resolve().parents[4]
SCRATCH = ROOT / 'reports/coordinator-20261030-implementation18/cinematic-companion'
SOURCE = ROOT / 'handoff/tasks/CHAT27/20261030-manager18/cinematic_manager.py'
assert hashlib.sha256(SOURCE.read_bytes()).hexdigest() == '1a6506e11576f290e70f847a65f307cf1cebffab928360120d4fee5e587857fb'
spec = importlib.util.spec_from_file_location('manager18_file_only', SOURCE)
helper = importlib.util.module_from_spec(spec)
spec.loader.exec_module(helper)
Image = helper.Image
digest = helper.digest
UNITS = {
    0x4b4340: (280, False, 'Inherited camera constructor; new member+0xd4 assignment question only.'),
    0x211390: (0x67, True, 'Direct constructor call at camera0x4b435e with receiver camera+0xd0.'),
    0x3b7d30: (0xf0, True, 'Factory called at0x2113b6; return stored at camera+0xd4.'),
    0x3b69f0: (0xa3, True, 'Factory last success branch0x3b7e15 constructs0x100-byte object.'),
    0x38c6c0: (0x56, True, 'Exact companion+0x80 assigned table0x102c974 slot+0x2c.'),
    0x38c780: (3, True, 'Exact companion+0x80 assigned table0x102c974 slot+0x1c.'),
    0x3a6b20: (0x1e0, True, 'Factory first success branch0x3b7d66 constructs0x1b0-byte object.'),
    0x3979b0: (0xed, True, 'Factory second success branch0x3b7da8 constructs0xf0-byte object.'),
    0x3b7840: (0x262, True, 'Factory third success branch0x3b7de1 constructs0x200-byte object.'),
}

def decode(im, at, unit, count):
    assert unit in UNITS and sum(new for _,new,_ in UNITS.values()) <= 8
    limit, _, _ = UNITS[unit]
    assert unit <= at < unit+limit and 1 <= count <= 256
    start = at
    lines = []
    for _ in range(count):
        raw, asm, line = helper.instruction(im, at)
        assert at+len(raw) <= unit+limit
        lines.append(line)
        at += len(raw)
        if asm.startswith(('ret', 'int', 'jmp')):
            break
    covered = set(range(start, at))
    for path in SCRATCH.glob('decode-*.json'):
        prior = json.loads(path.read_text(encoding='utf-8'))
        covered.update(range(int(prior['rva'],0),int(prior['rva'],0)+prior['size']))
    assert len(covered) <= 65536
    output = ('\n'.join(lines)+'\n').encode('utf-8')
    dest = SCRATCH / f'decode-{start:08x}-{at-start:x}.txt'
    assert not dest.exists()
    dest.write_bytes(output)
    row = dict(**im.record(start,at-start),unit=hex(unit),decode_sha256=digest(output),instruction_count=len(lines))
    dest.with_suffix('.json').write_text(json.dumps(row,indent=2)+'\n',encoding='utf-8')
    print(output.decode('utf-8'))
    print(json.dumps(row))

def proof(im):
    inherited=[]
    for folder,sha in (
        ('20261030-manager18','4ff54dcefc7901926869f376322a023b438a8f1ab1f3477afdd421579936a432'),
        ('20261030-work14','b5db47e429185f0a5ebe9d37986fe7ec4c4cb40de746bcd9f974f482147101d3'),
        ('20261030-update18','5b4a5c5c60e3577e6814c9d6a9cc373de3433de26d48f2540b4fde7ee0272f43'),
    ):
        path=ROOT/'handoff/tasks/CHAT27'/folder/'proof01.json'
        assert digest(path.read_bytes())==sha
        inherited.append(dict(path=str(path.relative_to(ROOT)),sha256=sha))
    covered=set()
    decodes=[]
    for path in sorted(SCRATCH.glob('decode-*.json')):
        row=json.loads(path.read_text(encoding='utf-8'))
        at=int(row['rva'],0)
        unit=int(row['unit'],0)
        assert im.record(at,row['size'])['sha256']==row['sha256']
        assert digest(path.with_suffix('.txt').read_bytes())==row['decode_sha256']
        assert unit<=at and at+row['size']<=unit+UNITS[unit][0]
        covered.update(range(at,at+row['size']))
        decodes.append(dict(**row,path=str(path.with_suffix('.txt').relative_to(ROOT))))
    expected=set()
    for at,(size,_,_) in UNITS.items():
        expected.update(range(at,at+size))
    assert covered==expected and len(covered)==2202 and len(covered)<=65536
    assert sum(new for _,new,_ in UNITS.values())==8
    def exact(at,encoded,meaning):
        raw=bytes.fromhex(encoded)
        assert im.read(at,len(raw))==raw,hex(at)
        return dict(**im.record(at,len(raw)),bytes=encoded,meaning=meaning)
    pins=[exact(*row) for row in (
        (0x4b4355,'8d9ed0000000','Inherited constructor receiver camera+0xd0.'),
        (0x2113ad,'8b80e0000000','Resource passed to factory is [camera constructor second argument+0xe0].'),
        (0x2113bd,'894604','Factory return stored at member receiver+4, hence camera+0xd4.'),
        (0x3b7df1,'68302dc001','Final factory predicate receives storage VA0x1c02d30; runtime TypeInfo contents not present in file.'),
        (0x3b7dff,'741b','Failed final predicate returns null.'),
        (0x3b7e01,'6800010000','Final factory success requests0x100-byte allocation.'),
        (0x3b6a0d,'c706a8c94201','Constructor assigns primary table RVA0x102c9a8.'),
        (0x3b6a13,'c7868000000074c94201','Constructor explicitly assigns companion+0x80 table RVA0x102c974.'),
        (0x3b6a57,'8886fd000000','Constructor initializes companion+0xfd, matching inherited lifecycle field shape.'),
        (0x38c6c6,'d94190','Getter source starts interface-0x70, hence companion+0x10.'),
        (0x38c70a,'d941c8','Getter final source is interface-0x38, hence companion+0x48.'),
        (0x38c780,'32c0c3','Exact+0x1c method always returns AL=0.'),
        (0x3b1844,'8b501c','Inherited manager loads interface+0x1c target.'),
        (0x3b184b,'84c0','Inherited manager tests AL returned by+0x1c.'),
        (0x3b184d,'0f8441010000','Zero AL bypasses the matrix comparison to0x3b1994.'),
        (0x3b188e,'8b522c','Inherited manager loads interface+0x2c only after the gate.'),
        (0x3b1898,'ffd2','Inherited indirect matrix call bypassed for table0x102c974.'),
        (0x3b189a,'d9842490000000','If another interface passes the gate, consumer reads output translation element+0x30.'),
        (0x3a6b44,'899e80000000','First factory constructor explicitly zeroes object+0x80; it does not assign the lifecycle interface table.'),
        (0x3979dc,'898680000000','Second factory constructor explicitly zeroes object+0x80; different layout.'),
        (0x3b7868,'899e80000000','Third factory constructor explicitly zeroes object+0x80; later helper writes remain unexamined.'),
    )]
    calls=[]
    for at,target,role in (
        (0x4b435e,0x211390,'Camera member construction.'),
        (0x2113b6,0x3b7d30,'Companion factory; returned pointer retained at camera+0xd4.'),
        (0x3b7d66,0x3a6b20,'First conditional factory constructor.'),
        (0x3b7da8,0x3979b0,'Second conditional factory constructor.'),
        (0x3b7de1,0x3b7840,'Third conditional factory constructor.'),
        (0x3b7e15,0x3b69f0,'Fourth conditional factory constructor; explicit secondary table assignment.'),
        (0x3b69f9,0x3b63d0,'Undecoded companion base constructor; next exact construction edge.'),
        (0x3b7a6a,0x3b6e00,'Undecoded helper on third factory variant; no final+0x80 value asserted for that branch.'),
    ):
        raw=im.read(at,5)
        assert raw[0]==0xe8 and at+5+struct.unpack_from('<i',raw,1)[0]==target
        calls.append(dict(**im.record(at,5),target_rva=hex(target),role=role,callee_decoded=target in UNITS))
    slots=[]
    for offset,target in ((0x1c,0x38c780),(0x2c,0x38c6c0),(4,0x211260)):
        at=0x102c974+offset
        assert struct.unpack('<I',im.read(at,4))[0]==im.base+target
        slots.append(dict(**im.record(at,4),table_rva='0x102c974',offset=hex(offset),target_rva=hex(target),decoded=target in UNITS))
    return dict(schema='chat27.cinematic_companion_static_code.v1',evidence_level='offline file-backed code',
        executable_sha256=helper.SHA,preferred_base=hex(im.base),script_sha256=digest(Path(__file__).read_bytes()),
        imported_helper=dict(path=str(SOURCE.relative_to(ROOT)),sha256=digest(SOURCE.read_bytes())),inherited=inherited,
        scope=dict(maximum_new_function_units=8,new_function_units=8,complete_new_function_units=8,
            inherited_constructor_reused=1,inherited_constructor_bytes=280,new_unique_code_bytes=1922,
            total_unique_decoded_code_bytes=2202,maximum_code_bytes=65536,adjacent_spill_units=0,ceiling_met=True,
            new_question='Constructor camera+0xd4 assignment and exact companion interface slots; old service/matrix helpers not redecoded.',
            decoding='Incremental single-instruction dumpbin ranges; chunks stop at return or unconditional branch. Conditional destinations are continued inside the same unit. All final units covered exactly.'),
        units=[dict(**im.record(at,size),new=new,complete=True,meaning=meaning) for at,(size,new,meaning) in UNITS.items()],
        exact_instructions=pins,direct_calls=calls,exact_assigned_table_slots=slots,
        inspected_type_storage=dict(**im.record(0x1802d30,16),bytes=im.read(0x1802d30,16).hex(),
            limit='Zero-filled on-disk storage gives no runtime resource name or selection authority.'),
        findings=dict(
            creation='0x4b4340 calls0x211390(camera+0xd0,P). That helper calls0x3b7d30([P+0xe0],0,0) and stores its return at camera+0xd4.',
            factory='Four predicate-selected allocation/constructor alternatives exist:0x1b0/0x3a6b20,0xf0/0x3979b0,0x200/0x3b7840,0x100/0x3b69f0; otherwise null. Exclusive runtime resource selection is not proved.',
            assigned_table='The fourth constructed variant explicitly assigns primary0x102c9a8 and interface at+0x80 to0x102c974 after its base constructor. It initializes+fc/+fd/+fe consistent with inherited start/stop shape. This is a real constructor assignment, not a contiguous-pointer-table inference.',
            other_variants='Other three constructors explicitly zero+0x80 and use different primary tables. Their called helpers were not chased, so no claim that they can never later write+0x80. Generic factory return alone is insufficient to admit the lifecycle interface.',
            exact_matrix='Assigned table+0x2c resolves0x38c6c0. With ECX=companion+0x80, it copies12 floats from companion+0x10..+0x48 into padded matrix output+0..+0x38 and returns the output pointer. It does not read inherited relative angular storage+0x90..+0xc8.',
            exact_gate='Assigned table+0x1c resolves0x38c780, which returns AL=0. At manager0x3b184d this unconditionally selects0x3b1994, bypassing indirect call0x3b1898 and its matrix comparison for this assigned table.',
            output_consumer='The inherited optional path consumes returned translation+0x30/+0x34/+0x38 to compute distance against its comparison singleton. For this table that consumer is unreachable through registration. No selected-view output consumer follows from the edge.',
            removal='Assigned table+4 resolves0x211260 for inherited removal edge0x394332; target intentionally undecoded at unit ceiling.',
            authority='No admitted selected local-player/current-view chain, active blend ownership, HUD condition, entry/exit/skip lifetime, or runtime table state. No cinematic fix follows.'),
        source_implementation=dict(supported_correction=False,production_modified=False,
            reason='The proposed manager transform edge is gated off for the explicitly constructed companion interface and cannot establish current cinematic/view authority.'),
        next_unresolved_edge=dict(kind='missing output-consumer join',
            missing='Find the companion update that combines relative angular matrix+0x90..+0xc8 with animated/base transform+0x10..+0x48, then its actual selected local-player-view consumer. The registration comparison edge is closed negative for assigned table0x102c974.',
            exact_new_construction_edge=dict(callsite_rva='0x3b69f9',target_rva='0x3b63d0',receiver='companion',argument='resource',
                required='If continuing construction analysis, inspect only this called base constructor for an explicit update-owner relationship; constructor existence alone is not a selected-view join.'),
            exact_unread_removal_edge=dict(callsite_rva='0x394332',target_rva='0x211260',receiver='companion+0x80',slot='+4',
                limit='Cleanup callback target resolved; no active-view authority implied.')),
        execution=dict(git=False,builds=False,tests=False,game=False,process_access=False,input=False,xr=False,
            profiles=False,installs=False,packages=False,subagents=False,production_writes=False),
        verification='Executable/inherited proof/helper hashes, exact new decode coverage, preserved decode hashes, instruction bytes, direct E8 destinations and constructor-owned slots verified file-only.',
        frozen=True,saved_decodes=decodes)

def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--exe',type=Path,required=True)
    parser.add_argument('--decode',type=lambda x:int(x,0))
    parser.add_argument('--unit',type=lambda x:int(x,0))
    parser.add_argument('--count',type=int,default=32)
    parser.add_argument('--data',type=lambda x:int(x,0))
    parser.add_argument('--size',type=lambda x:int(x,0),default=4)
    parser.add_argument('--output',type=Path)
    args=parser.parse_args()
    SCRATCH.mkdir(parents=True,exist_ok=True)
    for name in ('TEMP','TMP','TMPDIR'):
        os.environ[name]=str(SCRATCH)
    im=Image(args.exe)
    assert im.record(0x4b4340,280)['sha256']=='f55f0a4383abf562545f840fd82a8274cf570552174416cf0699ba5bd1b80b60'
    if args.output:
        assert args.output.resolve()==Path(__file__).resolve().parent/'proof01.json'
        result=proof(im)
        raw=(json.dumps(result,indent=2)+'\n').encode('utf-8')
        with args.output.open('xb') as stream:
            stream.write(raw)
        print(json.dumps(dict(path=str(args.output.resolve()),sha256=digest(raw),scope=result['scope'])))
    elif args.decode is not None:
        decode(im,args.decode,args.unit or args.decode,args.count)
    elif args.data is not None:
        assert 0 < args.size <= 0x1000
        print(json.dumps(dict(**im.record(args.data,args.size),bytes=im.read(args.data,args.size).hex())))

if __name__=='__main__':
    main()
