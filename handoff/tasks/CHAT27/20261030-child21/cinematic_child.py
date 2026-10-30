"""Bounded file-only trace of the exact constructor-owned child update."""
from __future__ import annotations
import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import struct

ROOT = Path(__file__).resolve().parents[4]
SCRATCH = ROOT / 'reports/coordinator-20261030-cinematic-child21'
INHERITED = ROOT / 'handoff/tasks/CHAT27/20261030-constructor20/proof01.json'
assert hashlib.sha256(INHERITED.read_bytes()).hexdigest() == '52bc97c398eb5823ad7b29d54c46187de842fb775067a8acb024753d32cdf852'
SOURCE = ROOT / 'handoff/tasks/CHAT27/20261030-manager18/cinematic_manager.py'
assert hashlib.sha256(SOURCE.read_bytes()).hexdigest() == '1a6506e11576f290e70f847a65f307cf1cebffab928360120d4fee5e587857fb'
spec = importlib.util.spec_from_file_location('manager18_file_only', SOURCE)
helper = importlib.util.module_from_spec(spec)
spec.loader.exec_module(helper)
Image, digest = helper.Image, helper.digest
UNITS = {
    0x41e610: (0x50, 'Exact update edge at0x39cdf5 with constructor-owned child receiver.'),
    0x41e590: (0x77, 'Direct call from child update at0x41e625; retains child receiver.'),
    0xb50630: (0x95, 'Direct call from child update at0x41e657; append a matrix/state record to caller-supplied queue.'),
    0x2ca490: (0x43, 'Exact scalar call at0x39cde0; no stack arguments, preserves five prepared child arguments.'),
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
    inherited=json.loads(INHERITED.read_text(encoding='utf-8'))
    reused=[]
    for row in inherited['saved_decodes']:
        if int(row['unit'],0)!=0x39cd30:
            continue
        at=int(row['rva'],0)
        assert im.record(at,row['size'])['sha256']==row['sha256']
        assert digest((ROOT/row['path']).read_bytes())==row['decode_sha256']
        reused.append(row)
    assert sum(row['size'] for row in reused)==0xd1
    covered=set()
    decodes=[]
    for path in sorted(SCRATCH.glob('decode-*.json')):
        row=json.loads(path.read_text(encoding='utf-8'))
        at,unit=int(row['rva'],0),int(row['unit'],0)
        assert unit in UNITS
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
    assert covered==expected and len(covered)==415 and len(UNITS)==4
    def exact(at,encoded,meaning):
        raw=bytes.fromhex(encoded)
        assert im.read(at,len(raw))==raw,hex(at)
        return dict(**im.record(at,len(raw)),bytes=encoded,meaning=meaning)
    pins=[exact(*row) for row in (
        (0x2ca491,'80792800','Scalar accessor chooses between object double+0x20 and unsigned dword ratio+0/+4 using flag+0x28.'),
        (0x2ca4a7,'c3','First scalar-accessor return leaves the five caller-prepared stack words untouched.'),
        (0x2ca4d2,'c3','Second scalar-accessor return also has no stack cleanup.'),
        (0x39cdce,'51','Inherited caller first prepares its second incoming argument B, later child seventh argument.'),
        (0x39cdd2,'6a00','Inherited caller next prepares zero, later child sixth argument.'),
        (0x39cdd4,'8d54241852','Inherited caller prepares pointer to local matrix M, later child fifth argument.'),
        (0x39cdd9,'5151','Inherited caller prepares third incoming argument C, then a word overwritten with0.0f.'),
        (0x39cddd,'d91c24','Store0.0f in future child third argument; scalar accessor does not consume it.'),
        (0x41e5bd,'83be8400000000','Indexed scalar writes execute only while child+0x84 is nonzero.'),
        (0x41e5df,'f30f110481','First incoming scalar is stored through array [[child+0x58]+0x10]+0x10, indexed by [child+0x64]+0x0c.'),
        (0x41e5fd,'f30f110490','Accessor scalar is stored through the same array, indexed by [child+0x60]+0x0c.'),
        (0x41e649,'8b4654','The queued state pointer comes from child+0x54.'),
        (0x41e64f,'8b4c243c','Submission receiver is child seventh argument, original companion-update second argument B.'),
        (0x41e65d,'c21c00','Child update consumes seven stack arguments.'),
        (0xb50636,'8b8120070000','Read current submission count at receiver B+0x720.'),
        (0xb50644,'6bc0700301','Compute record address R=[B]+old_count*0x70.'),
        (0xb50649,'899120070000','Advance submission count; no selected-view predicate is tested here.'),
        (0xb50652,'894840','Record+0x40 receives child+0x54 state pointer.'),
        (0xb50667,'d918','Start copying twelve padded matrix floats from M into R; no matrix multiplication occurs.'),
        (0xb506c2,'c22000','Queue helper consumes eight stack arguments.'),
    )]
    calls=[]
    for at,target,meaning in (
        (0x39cde0,0x2ca490,'Inherited scalar accessor call; ECX=[TLS+0x0c], zero stack arguments.'),
        (0x39cdf5,0x41e610,'Inherited exact starting edge; ECX=child and seven arguments, with five retained across scalar accessor.'),
        (0x41e625,0x41e590,'Pass the first two child arguments to indexed-state updater; ECX remains child.'),
        (0x41e5aa,0x40ff80,'Drain pending eight-byte child+0x68..+0x6c entries using pointer and byte; target not decoded and its deeper effects are not claimed.'),
        (0x41e657,0xb50630,'Append state/matrix record with ECX=B, the original companion-update second argument.'),
    ):
        raw=im.read(at,5)
        assert raw[0]==0xe8 and at+5+struct.unpack_from('<i',raw,1)[0]==target
        calls.append(dict(**im.record(at,5),bytes=raw.hex(),target_rva=hex(target),
            decoded=target in UNITS,meaning=meaning))
    constants=[]
    for at,expected_float,meaning in (
        (0x10d9420,-1.0,'Queued scalar is exactly-1.0f; its semantic role is not inferred.'),
        (0x10d95f8,4294967296.0,'Unsigned-conversion correction used by scalar ratio accessor.'),
    ):
        raw=im.read(at,4)
        value=struct.unpack('<f',raw)[0]
        assert value==expected_float
        constants.append(dict(**im.record(at,4),bytes=raw.hex(),float_value=value,meaning=meaning))
    return dict(schema='chat27.cinematic_child_static_code.v1',
        evidence_level='offline file-backed code; no actual-game or headset check',
        executable_sha256=helper.SHA,preferred_base=hex(im.base),
        script_sha256=digest(Path(__file__).read_bytes()),
        imported_helper=dict(path=str(SOURCE.relative_to(ROOT)),sha256=digest(SOURCE.read_bytes())),
        inherited=dict(path=str(INHERITED.relative_to(ROOT)),sha256=digest(INHERITED.read_bytes()),
            treatment='Immutable predecessor. Its child ownership remains valid; this proof refines the previously incomplete argument list and scalar-accessor description.'),
        scope=dict(maximum_new_function_units=8,new_function_units=4,complete_new_function_units=4,
            unique_decoded_code_bytes=415,reused_code_bytes=209,total_code_bytes=624,
            maximum_code_bytes=65536,adjacent_spill_units=0,ceiling_met=True,
            stopped_at_unit_ceiling=False,
            stop_reason='Exact branch produces a queue submission; neither transform composition nor its selected-view consumer is reached. Stop at this ownership boundary instead of spending remaining budget on unrelated child internals.',
            strategy='Exact direct edges only; one-instruction dumpbin reads stop at returns/unconditional branches; explicit in-unit branch continuation completes coverage. No whole-image code or xref scan.'),
        units=[dict(**im.record(at,size),complete=True,meaning=meaning)
            for at,(size,meaning) in UNITS.items()],
        exact_instructions=pins,direct_calls=calls,constants=constants,
        abi=dict(original='0x39cd30 receives (A, B, C); A is incoming scalar; types of B/C beyond this flow are not independently established.',
            local_matrix='M is the existing stack identity matrix, optionally filled by resolved companion+0x88 owner virtual+0x44. It is distinct storage from companion relative+0x90..+0xc8 and base+0x10..+0x48.',
            scalar_accessor='T=0x2ca490([TLS+0x0c]); zero stack arguments. If byte+0x28 is set, return double+0x20 rounded to float; otherwise return unsigned dword+0 divided by unsigned dword+4, rounded to float. No matrix or caller scalar is read.',
            child_call=dict(receiver='[companion+0xd0]',arguments=['A','T','0.0f','C','M','0','B'],cleanup_bytes=28),
            queue_call=dict(receiver='B',arguments=['[child+0x54]','0.0f','C (not read in this helper)','M','0','0','-1.0f','-1'],cleanup_bytes=32)),
        findings=dict(
            child_state='0x41e590 drains child pending entries via undecoded0x40ff80 and resets the end pointer. When child+0x84 is nonzero and descriptors exist, it stores A/T into the indexed float array reached through child+0x58 using child+0x64/+0x60 descriptors. Descriptor semantics are unproved.',
            submission='0x41e610 then calls0xb50630 with ECX=B. That leaf helper increments B+0x720 and writes R=[B]+old_count*0x70. R+0x00,+04,+08,+10,+14,+18,+20,+24,+28,+30,+34,+38 copy M; R+0x40=[child+0x54], +0x44=0.0f, +0x48=-1.0f, +0x4d=0, +0x50=0, +0x54=-1. Remaining record fields are untouched by this helper.',
            ownership_limit='This proves a constructor-owned child state update and matrix/state submission, not an evaluated transform output. No decoded new function composes companion relative+0x90..+0xc8 with base+0x10..+0x48. The queued record consumer and concrete B ownership are unknown; deeper effects of pending-entry helper0x40ff80 are unexamined.',
            acceptance_limit='No selected local-player view, active camera, HUD, cutscene entry/skip/exit or gameplay-return authority is proved. No source fix or continuous head-rotation override is supported.',
            prior_negative='Manager membership and its inherited false gate were not retraced or promoted into active-camera evidence.'),
        source_implementation=dict(supported_correction=False,production_modified=False,
            proposed_files=[],proposed_apis=[],reason='No transform-composition/output-to-selected-view ownership join.'),
        next_unresolved_edge=dict(producer_callsite_rva='0x41e657',producer_target_rva='0xb50630',
            exact_record='R=[B]+old_[B+0x720]*0x70, where B is original0x39cd30 second argument; record+0x40=[child+0x54] and its first twelve padded matrix floats copy M.',
            unresolved='Consumer of this exact submitted record -> evaluated transform output -> selected local-player view. Consumer target is not identified; no new call target is invented.',
            required='Identify an explicit consumer/owner before any further queue or child expansion; queue scheduling alone cannot establish camera ownership.'),
        execution=dict(git=False,builds=False,tests=False,game=False,game_process_access=False,
            input=False,xr=False,profiles=False,installs=False,packages=False,subagents=False,
            production_writes=False),
        verification='Executable, predecessor/helper, reused decode and new decode hashes; complete exact unit coverage; instruction bytes, E8 destinations, constants and ABI stack layout verified offline.',
        verification_attempts='Initial proof generation refused a mistaken+1.0 constant expectation before writing output. Exact four-byte read000080bf established-1.0, the expectation was corrected, and final proof generation passed.',
        reused_decodes=reused,saved_decodes=decodes,frozen=True)

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
