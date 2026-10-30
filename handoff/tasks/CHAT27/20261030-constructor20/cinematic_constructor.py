"""Bounded, offline companion constructor/update-owner investigation."""
from __future__ import annotations
import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import struct

ROOT = Path(__file__).resolve().parents[4]
SCRATCH = ROOT / 'reports/coordinator-20261030-constructor20'
SOURCE = ROOT / 'handoff/tasks/CHAT27/20261030-manager18/cinematic_manager.py'
assert hashlib.sha256(SOURCE.read_bytes()).hexdigest() == '1a6506e11576f290e70f847a65f307cf1cebffab928360120d4fee5e587857fb'
spec = importlib.util.spec_from_file_location('manager18_file_only', SOURCE)
helper = importlib.util.module_from_spec(spec)
spec.loader.exec_module(helper)
Image, digest = helper.Image, helper.digest
UNITS = {
    0x3b63d0: (0x132, 'Exact unexamined base constructor called at0x3b69f9.'),
    0x38cce0: (0xae, 'Nested constructor at0x3b63e1 initializes companion base transform.'),
    0x38d900: (8, 'Candidate primary table0x102c9a8+0x38 target; extent not inferred from adjacency.'),
    0x3b0010: (0x39, 'Candidate primary table0x102c9a8+0x3c target; extent not inferred from adjacency.'),
    0x3b23c0: (0x80, 'Exact assigned secondary table0x102c974+0x14 target.'),
    0x3abfa0: (0xb2, 'Exact assigned secondary table0x102c974+0x08 target.'),
    0x3b0050: (0x12c, 'Direct update call0x3b241d with receiver companion and scalar first argument.'),
    0x39cd30: (0xd1, 'Direct update call0x3b2436 retaining companion+0x80 interface receiver and all three incoming arguments.'),
}

def decode(im, at, unit, count):
    assert unit in UNITS and len(UNITS) <= 8 and 1 <= count <= 256
    bound, _ = UNITS[unit]
    assert unit <= at < unit + bound
    start, lines = at, []
    for _ in range(count):
        raw, asm, line = helper.instruction(im, at)
        assert at + len(raw) <= unit + bound
        lines.append(line)
        at += len(raw)
        if asm.startswith(('ret', 'int', 'jmp')):
            break
    covered = set(range(start, at))
    for path in SCRATCH.glob('decode-*.json'):
        row = json.loads(path.read_text(encoding='utf-8'))
        covered.update(range(int(row['rva'], 0), int(row['rva'], 0) + row['size']))
    assert len(covered) <= 65536
    raw = ('\n'.join(lines) + '\n').encode('utf-8')
    dest = SCRATCH / f'decode-{start:08x}-{at-start:x}.txt'
    assert not dest.exists()
    dest.write_bytes(raw)
    row = dict(**im.record(start, at-start), unit=hex(unit), bytes=im.read(start, at-start).hex(),
               decode_sha256=digest(raw), instruction_count=len(lines))
    dest.with_suffix('.json').write_text(json.dumps(row, indent=2)+'\n', encoding='utf-8')
    print(raw.decode('utf-8'))
    print(json.dumps(row))

def proof(im):
    inherited=[]
    for folder,sha in (
        ('20261030-companion19','65363c36e5cae30e7b12ebe66567f0bed88a31ab7f446c059e5c0ebcada3f29f'),
        ('20261030-manager18','4ff54dcefc7901926869f376322a023b438a8f1ab1f3477afdd421579936a432'),
        ('20261030-update18','5b4a5c5c60e3577e6814c9d6a9cc373de3433de26d48f2540b4fde7ee0272f43'),
    ):
        path=ROOT/'handoff/tasks/CHAT27'/folder/'proof01.json'
        assert digest(path.read_bytes())==sha
        inherited.append(dict(path=str(path.relative_to(ROOT)),sha256=sha))
    covered=set()
    decodes=[]
    for path in sorted(SCRATCH.glob('decode-*.json')):
        row=json.loads(path.read_text(encoding='utf-8'))
        at,unit=int(row['rva'],0),int(row['unit'],0)
        assert im.record(at,row['size'])['sha256']==row['sha256']
        assert im.read(at,row['size']).hex()==row['bytes']
        assert digest(path.with_suffix('.txt').read_bytes())==row['decode_sha256']
        assert unit<=at and at+row['size']<=unit+UNITS[unit][0]
        covered.update(range(at,at+row['size']))
        decodes.append(dict(rva=row['rva'],size=row['size'],unit=row['unit'],
            sha256=row['sha256'],decode_sha256=row['decode_sha256'],
            path=str(path.with_suffix('.txt').relative_to(ROOT))))
    expected=set()
    for at,(size,_) in UNITS.items():
        expected.update(range(at,at+size))
    assert covered==expected and len(covered)==1360 and len(UNITS)==8
    def exact(at,encoded,meaning):
        raw=bytes.fromhex(encoded)
        assert im.read(at,len(raw))==raw,hex(at)
        return dict(**im.record(at,len(raw)),bytes=encoded,meaning=meaning)
    pins=[exact(*row) for row in (
        (0x3b6402,'c70628c94201','Base constructor temporarily assigns primary table0x102c928; derived constructor later replaces it.'),
        (0x3b6408,'c78680000000f4c84201','Base constructor temporarily assigns interface table0x102c8f4 at companion+0x80; derived constructor replaces it.'),
        (0x3b6412,'f30f118e90000000','Relative matrix diagonal begins at companion+0x90; constant is1.0f.'),
        (0x3b6474,'f30f1186c8000000','Relative matrix last translation field+0xc8 initialized zero.'),
        (0x3b647f,'68c0000000','Allocate0xc0 bytes for the resource-backed child.'),
        (0x3b64a6,'8b4f58','Child constructor first argument comes from resource+0x58.'),
        (0x3b64a9,'6a006a0051','Push child constructor arguments resource+0x58,0,0 in x86 right-to-left order.'),
        (0x3b64bc,'8986d0000000','Store constructed child or null at companion+0xd0.'),
        (0x3b64d3,'8986d4000000','Store first collection-registration return at companion+0xd4.'),
        (0x3b64f2,'8986d8000000','Store second collection-registration return at companion+0xd8.'),
        (0x38cd00,'f30f114e10','Base transform diagonal begins at companion+0x10; constant1.0f.'),
        (0x38cd3f,'f30f114648','Base transform final translation field+0x48 initialized zero.'),
        (0x38cd77,'894e70','Retain resource pointer at companion+0x70 before calling resource virtual0.'),
        (0x38d900,'c681fe00000001c3','Primary candidate+0x38 sets companion flag+0xfe then returns.'),
        (0x3b001b,'c681fe00000000','Primary candidate+0x3c clears companion flag+0xfe.'),
        (0x3b23c4,'8b4668','Interface update first tests companion+0xe8 handle, then its pointee.'),
        (0x3b2417,'8d4e80','Interface update converts receiver companion+0x80 to companion before event helper.'),
        (0x3b2431,'8bce','Interface update restores interface receiver for the delegated child update.'),
        (0x39cd47,'8b4608','Delegated update optionally resolves handle at companion+0x88.'),
        (0x39cdb7,'8b4044','Resolved handle owner virtual+0x44 can fill the local base matrix.'),
        (0x39cdcb,'8b400c','Scalar helper receiver is TLS-result+0x0c, distinct from manager membership TLS+0x14.'),
        (0x39cde5,'8b4e50','Delegated update reads interface+0x50, exactly companion+0xd0 child created by base constructor.'),
        (0x39cdeb,'d95c2404','Second child update argument is scalar returned by0x2ca490.'),
        (0x39cdef,'d94508','First child update argument is the original first scalar argument.'),
    )]
    calls=[]
    for at,target,meaning in (
        (0x3b69f9,0x3b63d0,'Inherited exact construction edge; no re-decode.'),
        (0x3b63e1,0x38cce0,'Nested base transform/resource initialization.'),
        (0x3b64b0,0x42dd10,'Create child from resource+0x58 and two zeros; body unexamined.'),
        (0x3b64ce,0x3f0bb0,'Register resource+0x4c collection on constructed child; body unexamined.'),
        (0x3b64ed,0x3f0bb0,'Register resource+0x40 collection on constructed child; body unexamined.'),
        (0x3b241d,0x3b0050,'Timed event helper with receiver companion.'),
        (0x3b2436,0x39cd30,'Delegate update with receiver companion+0x80 and three arguments.'),
        (0x39cdc1,0x188b10,'Inherited TLS helper; reused from manager18 without decoding.'),
        (0x39cde0,0x2ca490,'Compute scalar from optional local matrix and incoming arguments; body unexamined.'),
        (0x39cdf5,0x41e610,'Update the constructor-owned companion+0xd0 child; next exact unexamined edge.'),
    ):
        raw=im.read(at,5)
        assert raw[0]==0xe8 and at+5+struct.unpack_from('<i',raw,1)[0]==target
        calls.append(dict(**im.record(at,5),bytes=raw.hex(),target_rva=hex(target),
                          decoded=target in UNITS,meaning=meaning))
    slots=[]
    for table,offset,target,meaning in (
        (0x102c974,8,0x3abfa0,'Assigned companion+0x80 table; string-like output builder using resource+0x78, not a matrix update.'),
        (0x102c974,0x14,0x3b23c0,'Assigned companion+0x80 table; update delegation and event handling.'),
        (0x102c9a8,0x38,0x38d900,'Candidate primary table cell; table end not independently admitted, body only proves flag write.'),
        (0x102c9a8,0x3c,0x3b0010,'Candidate primary table cell; table end not independently admitted, body only proves flag/singleton writes.'),
    ):
        raw=im.read(table+offset,4)
        assert struct.unpack('<I',raw)[0]==im.base+target
        slots.append(dict(**im.record(table+offset,4),bytes=raw.hex(),table_rva=hex(table),
                          offset=hex(offset),target_rva=hex(target),meaning=meaning))
    data=[]
    for at,size,meaning in (
        (0x102c974,0x34,'Exact assigned secondary table up to inherited next primary table address; no neighboring code survey.'),
        (0x102c9a8,0x40,'Bounded candidate primary table cells; no inferred end or selected-view semantics.'),
        (0x10d94f0,4,'Shared constructor matrix diagonal constant.'),
    ):
        data.append(dict(**im.record(at,size),bytes=im.read(at,size).hex(),meaning=meaning))
    assert struct.unpack('<f',im.read(0x10d94f0,4))[0]==1.0
    return dict(schema='chat27.cinematic_constructor_static_code.v1',
        evidence_level='offline file-backed code',executable_sha256=helper.SHA,
        preferred_base=hex(im.base),script_sha256=digest(Path(__file__).read_bytes()),
        imported_helper=dict(path=str(SOURCE.relative_to(ROOT)),sha256=digest(SOURCE.read_bytes())),
        inherited=inherited,
        scope=dict(maximum_new_function_units=8,new_function_units=8,complete_new_function_units=8,
            unique_decoded_code_bytes=1360,maximum_code_bytes=65536,adjacent_spill_units=0,
            ceiling_met=True,stopped_at_unit_ceiling=True,
            strategy='Exact known construction edge, bounded constructor-owned table cells, then exact direct calls. Single-instruction dumpbin decoding stopped at returns or unconditional branches; in-unit destinations continued until all bytes were covered. No whole-image survey.'),
        units=[dict(**im.record(at,size),bytes=im.read(at,size).hex(),complete=True,meaning=meaning)
               for at,(size,meaning) in UNITS.items()],
        exact_instructions=pins,direct_calls=calls,table_cells=slots,data_reads=data,
        findings=dict(
            construction='0x3b63d0 calls0x38cce0 to initialize base transform+0x10..+0x48 and retain resource at+0x70. It initializes relative matrix+0x90..+0xc8 independently to identity. No composition occurs in either constructor.',
            owned_child='0x3b63d0 allocates0xc0 bytes, calls0x42dd10 with resource+0x58,0,0, stores returned child at companion+0xd0, and registers resource collections+0x4c/+0x40 to obtain handles+0xd4/+0xd8. Child constructor/registration bodies remain unexamined.',
            explicit_update_relationship='Derived companion+0x80 table0x102c974 slot+0x14 points to0x3b23c0. With non-null companion+0xe8 and pointee it handles a scalar through companion+0xe4, calls0x3b0050(companion,first_scalar), and calls0x39cd30(companion+0x80,all_three_arguments).',
            timed_events='0x3b0050 advances companion+0xf0, indexes0x50-byte resource records using+0xf4, and manages flags+0xfc/+0xfe and a separate singleton event. It does not read relative matrix or base transform storage.',
            child_tick='0x39cd30 initializes a local identity matrix, optionally fills it via resolved companion+0x88 handle-owner virtual+0x44, computes a scalar via0x2ca490 on TLS+0x0c, then calls0x41e610 with ECX=companion+0xd0, first incoming scalar and computed scalar. This is an explicit constructor-to-update ownership join for the child.',
            limits='No decoded update reads relative angular+0x90..+0xc8 or writes base transform+0x10..+0x48. Child tick effects and output consumers remain unknown. No selected local-player view, HUD state, active blend, entry/skip/exit or gameplay return authority is proved.',
            prior_negative='The inherited manager table+0x1c false gate remains negative. Registration membership was not re-examined or promoted into current-view authority.'),
        source_implementation=dict(supported_correction=False,production_modified=False,
            reason='Constructor-owned child update is now explicit, but transform composition and actual selected-view consumption are still absent.'),
        next_unresolved_edge=dict(callsite_rva='0x39cdf5',target_rva='0x41e610',
            receiver='[companion+0xd0], the exact child returned from0x42dd10 at constructor0x3b64b0',
            arguments=['first scalar argument received by companion interface update','scalar returned by0x2ca490'],
            required='Determine whether this child update produces an output explicitly consumed to combine companion relative+0x90..+0xc8 with base+0x10..+0x48; then establish actual selected local-player-view consumption. Existence, membership and update scheduling alone are insufficient.'),
        execution=dict(git=False,builds=False,tests=False,game=False,process_access=False,input=False,xr=False,
            profiles=False,installs=False,packages=False,subagents=False,production_writes=False),
        verification='Executable/inherited proof/helper hashes, exact full-unit bytes, preserved decode hashes, complete coverage, instruction pins, E8 destinations and table cells verified file-only.',
        saved_decodes=decodes,frozen=True)

def main():
    p = argparse.ArgumentParser()
    p.add_argument('--exe', type=Path, required=True)
    p.add_argument('--decode', type=lambda x:int(x,0))
    p.add_argument('--unit', type=lambda x:int(x,0))
    p.add_argument('--count', type=int, default=120)
    p.add_argument('--data', type=lambda x:int(x,0))
    p.add_argument('--size', type=lambda x:int(x,0), default=4)
    p.add_argument('--output', type=Path)
    args=p.parse_args()
    SCRATCH.mkdir(parents=True, exist_ok=True)
    for name in ('TEMP','TMP','TMPDIR'):
        os.environ[name]=str(SCRATCH)
    prior=ROOT/'handoff/tasks/CHAT27/20261030-companion19/proof01.json'
    assert digest(prior.read_bytes())=='65363c36e5cae30e7b12ebe66567f0bed88a31ab7f446c059e5c0ebcada3f29f'
    im=Image(args.exe)
    if args.output is not None:
        assert args.output.resolve()==Path(__file__).resolve().parent/'proof01.json'
        result=proof(im)
        raw=(json.dumps(result,indent=2)+'\n').encode('utf-8')
        with args.output.open('xb') as stream:
            stream.write(raw)
        print(json.dumps(dict(path=str(args.output.resolve()),sha256=digest(raw),scope=result['scope'])))
    elif args.decode is not None:
        decode(im,args.decode,args.unit or args.decode,args.count)
    elif args.data is not None:
        assert 0 < args.size <= 0x100
        print(json.dumps(dict(**im.record(args.data,args.size),bytes=im.read(args.data,args.size).hex())))

if __name__=='__main__':
    main()
