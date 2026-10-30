"""Bounded CHAT27 context investigation; read-only executable, no process access."""
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
HERE = Path(__file__).resolve().parent
SCRATCH = ROOT / 'reports/coordinator-20261030-cinematic-context32'
EXE_SHA = '3911fcc8914b0158c434cee36f19295316e715d4c9a0e70c857e8c39e3d54258'
HELPER = ROOT / 'handoff/tasks/CHAT27/20261030-entity31/cinematic_entity.py'
HELPER_SHA = 'c27ed332ec3a9fca19219bbc9f184ecfc08f6e8f1ac4915767ce4978107d63c2'
sha = lambda raw: hashlib.sha256(raw).hexdigest()
assert sha(HELPER.read_bytes()) == HELPER_SHA
spec = importlib.util.spec_from_file_location('context32_instruction_helper', HELPER)
helper = importlib.util.module_from_spec(spec)
spec.loader.exec_module(helper)
PRIORS = {
    'scheduler30': '75b8ad96767dc70e2da7705fb7e2749df65875f5922b9efe7000b4c912dfb397',
    'entity31': 'b52ab309e1186ca788f572af8b6ddd078f5f838866d48701f12437b01c3c1648',
}
SEEDS = {0x3ff110: (0x14140a, 'scheduler30'), 0x188a50: (0x3ff079, 'scheduler30')}
TAIL_EDGES = {0x23227: 0x228d0}
FORBIDDEN = {0x3ff060, 0x435c60, 0x3b5a00, 0xb5d3f0, 0xb5d310, 0xb59460, 0xb5a380}

def save(path, value, exclusive=False):
    raw = (json.dumps(value, indent=2) + '\n').encode('utf-8')
    with path.open('xb' if exclusive else 'wb') as stream:
        stream.write(raw)

def inherited(im):
    priors = {}
    records = []
    for name, digest in PRIORS.items():
        path = ROOT / f'handoff/tasks/CHAT27/20261030-{name}/proof01.json'
        raw = path.read_bytes()
        assert sha(raw) == digest
        p = priors[name] = json.loads(raw)
        records.append(dict(path=path.relative_to(ROOT).as_posix(), sha256=digest))
        if name == 'scheduler30':
            for row in p['saved_decodes']:
                assert sha(im.read(int(row['rva'],0),row['size'])) == row['sha256']
                assert sha((ROOT/row['path']).read_bytes()) == row['decode_sha256']
        else:
            for row in p['units']:
                assert sha(im.read(int(row['rva'],0),row['size'])) == row['sha256']
            for row in p['saved_decodes']:
                assert sha((ROOT/row['path']).read_bytes()) == row['sha256']
    for target,(callsite,_) in SEEDS.items():
        row = dict(rva=hex(callsite),bytes=im.read(callsite,5).hex())
        assert helper.direct_target(row) == target
    return priors, records

def saved_functions():
    return [json.loads(path.read_bytes()) for path in sorted(SCRATCH.glob('function-*.json'))]

def decode(im, start):
    assert not (HERE/'proof01.json').exists(), 'Frozen: no more decoding'
    priors,_ = inherited(im)
    old = saved_functions()
    assert len(old) < 4, 'Preferred four-function ceiling; no expansion without review'
    assert start not in FORBIDDEN
    assert start not in {int(r['rva'],0) for r in priors['scheduler30']['saved_decodes']}
    assert start not in {int(r['rva'],0) for r in priors['entity31']['units']}
    edges = {int(call['target_rva'],0) for fn in old for call in fn['direct_calls']+fn.get('direct_tail_calls',[])}
    assert start in SEEDS or start in edges, 'Only exact admitted call targets'
    dest = SCRATCH / f'function-{start:08x}.json'
    assert not dest.exists(), 'Saved decodes are immutable'
    pending = [start]
    rows = {}
    calls = []
    tails = []
    stops = []
    while pending:
        at = pending.pop()
        if at in rows:
            continue
        if not start <= at < start+0x4000:
            failure = dict(entry=hex(start),unadmitted_target=hex(at),instructions=list(rows.values()),pending=pending,
                reason='Branch leaves bounded function; stop for review; target not decoded')
            save(SCRATCH/f'boundary-stop-{start:08x}.json',failure)
            print(json.dumps(failure),flush=True)
            raise AssertionError('Branch leaves bounded function; stop for review')
        assert at not in FORBIDDEN
        row = helper.decode_instruction(im,at)
        rows[at] = row
        if len(rows) % 20 == 0:
            save(SCRATCH/f'progress-{start:08x}.json',dict(entry=hex(start),instructions=list(rows.values()),pending=pending))
            print(f'{start:#x}: {len(rows)} instructions; latest {at:#x}',flush=True)
        assert sum(item['size'] for item in rows.values()) + sum(fn['reachable_byte_count'] for fn in old) <= 65536
        mnemonic = row['mnemonic']
        target = helper.direct_target(row)
        if mnemonic == 'call' and target is not None:
            calls.append(dict(caller_rva=hex(at),target_rva=hex(target),bytes=row['bytes']))
        if mnemonic.startswith('ret') or mnemonic in ('int','int3','ud2'):
            stops.append(dict(rva=hex(at),kind=mnemonic))
            continue
        if mnemonic.startswith('j') or mnemonic.startswith('loop'):
            assert target is not None, 'Indirect branch needs separate review'
            if at in TAIL_EDGES:
                assert mnemonic == 'jmp' and target == TAIL_EDGES[at]
                tails.append(dict(caller_rva=hex(at),target_rva=hex(target),bytes=row['bytes']))
                stops.append(dict(rva=hex(at),kind='direct_tail_jump'))
                continue
            pending.append(target)
            if mnemonic == 'jmp':
                continue
        pending.append(at+row['size'])
    instructions = [rows[at] for at in sorted(rows)]
    occupied = set()
    for row in instructions:
        at = int(row['rva'],0)
        span = set(range(at,at+row['size']))
        assert not occupied.intersection(span)
        occupied.update(span)
        target = helper.direct_target(row)
        if row['mnemonic'].startswith('j') or row['mnemonic'].startswith('loop'):
            assert target in rows or TAIL_EDGES.get(at) == target
    result = dict(entry_rva=hex(start),instruction_count=len(rows),reachable_byte_count=len(occupied),
        end_exclusive_rva=hex(max(occupied)+1),instructions=instructions,direct_calls=calls,direct_tail_calls=tails,
        terminal_instructions=stops,complete_reachable_cfg=True,adjacent_function_units=0,
        whole_range=im.record(start,max(occupied)+1-start),
        skipped_ranges=[hex(at) for at in range(start,max(occupied)+1) if at not in occupied])
    save(dest,result,True)
    text = '\n'.join(f"{row['rva']:>10}: {row['bytes']:<30} {row['text']}" for row in instructions)+'\n'
    dest.with_suffix('.txt').write_text(text,encoding='utf-8')
    print(text)
    print(json.dumps({key:value for key,value in result.items() if key not in ('instructions','whole_range')}))

def proof(im):
    priors, inherited_records = inherited(im)
    expected = {0x3ff110:362, 0x188a50:82, 0x23210:31, 0x228d0:168}
    functions = saved_functions()
    assert {int(fn['entry_rva'],0) for fn in functions} == set(expected)
    units, artifacts, instructions, calls, tails = [], [], [], [], []
    for fn in functions:
        start = int(fn['entry_rva'],0)
        assert fn['complete_reachable_cfg'] and fn['adjacent_function_units'] == 0
        assert fn['reachable_byte_count'] == expected[start] and not fn['skipped_ranges']
        assert int(fn['end_exclusive_rva'],0) == start+expected[start]
        starts = {int(row['rva'],0) for row in fn['instructions']}
        occupied = set()
        for row in fn['instructions']:
            at = int(row['rva'],0)
            raw = im.read(at,row['size'])
            assert raw.hex() == row['bytes'] and sha(raw) == row['sha256']
            span = set(range(at,at+len(raw)))
            assert not occupied.intersection(span)
            occupied.update(span)
            target = helper.direct_target(row)
            if row['mnemonic'].startswith('j') or row['mnemonic'].startswith('loop'):
                assert target in starts or TAIL_EDGES.get(at) == target
        assert occupied == set(range(start,start+expected[start]))
        for edge in fn['direct_calls']+fn.get('direct_tail_calls',[]):
            row = next(row for row in fn['instructions'] if row['rva'] == edge['caller_rva'])
            assert helper.direct_target(row) == int(edge['target_rva'],0)
        assert im.record(start,expected[start]) == fn['whole_range']
        units.append(dict(**fn['whole_range'],instruction_count=len(fn['instructions']),complete_reachable_cfg=True))
        for extension in ('json','txt'):
            path = SCRATCH/f'function-{start:08x}.{extension}'
            artifacts.append(dict(path=path.relative_to(ROOT).as_posix(),sha256=sha(path.read_bytes()),size=path.stat().st_size))
        instructions.extend(fn['instructions'])
        calls.extend(fn['direct_calls'])
        tails.extend(fn.get('direct_tail_calls',[]))
    assert len(instructions) == 201 and sum(row['size'] for row in instructions) == 643
    new_by_address = {int(row['rva'],0):row for row in instructions}
    exact = []
    for at,encoded,meaning in (
        (0x3ff11b,'8bf1','ESI=C, incoming newly published container'),
        (0x3ff11d,'0f854b010000','Nonzero mode global RVA115cfec skips opaque operations and reaches C+c0 write'),
        (0x3ff1b5,'8b4820','Context field used here is G+20, not G+14'),
        (0x3ff247,'8b484c','Final direct helper receives ECX=[G+4c]'),
        (0x3ff251,'83c004','Nonzero [[C+9c]+4] is adjusted by +4 before1bb0f0'),
        (0x3ff25a,'c686c000000001','Direct container effect: byte[C+c0]=1'),
        (0x3ff26e,'c686c000000001','All other normal returns also directly set byte[C+c0]=1'),
        (0x188a54,'8bf1','ESI=G incoming context'),
        (0x188a56,'c70678984101','Base context vtable temporarily installed at G'),
        (0x188a68,'895e14','Set G+14 to zero'),
        (0x188a6c,'895e18','Set G+18 to zero before service factory'),
        (0x188a74,'8b4e18','Read current G+18 after opaque factory returns'),
        (0x188a77,'8bf8','EDI=B, factory result'),
        (0x188a79,'3bf9','Skip replacement virtual operations when old value equals B'),
        (0x188a83,'8b5004','Nonzero differing old service uses vtable+4'),
        (0x188a88,'895e18','Clear G+18 after old-service virtual+4'),
        (0x188a8d,'897e18','Store exact returned B to G+18'),
        (0x188a94,'8b10','Nonzero replacement B uses vtable+0'),
        (0x188a96,'8bcf','Virtual slot0 receiver is B'),
        (0x188a9f,'c20400','Initializer removes arg1; no instruction reads it'),
        (0x23214,'68701c0000','Factory arg1=1c70, alongside arg2=4,arg3=0'),
        (0x23225,'8bc8','Nonzero helper result becomes constructor ECX'),
        (0x23227,'e9a4f6ffff','Exact tail edge to228d0, not adjoining function spill'),
        (0x2322c,'33c0','Null helper result returns null'),
        (0x228d2,'8bf1','Constructor ESI=B'),
        (0x228e9,'c70688a24001','Install service vtable preferredVA140a288/RVA100a288'),
        (0x22962,'c786641c000001000000','Initialize B+1c64 to1; semantic field name unproved'),
        (0x22973,'8bc6','Constructor returns same B'),
    ):
        assert at in new_by_address and new_by_address[at]['bytes'] == encoded
        exact.append(dict(**im.record(at,len(bytes.fromhex(encoded))),meaning=meaning))
    inherited_pins = []
    for at,encoded,meaning in (
        (0x1413f3,'895634','Publish pending C to P+34 before old-object release'),
        (0x1413fe,'ffd2','Old nonnull C receives unresolved virtual slot0(1)'),
        (0x141400,'8b4e34','Reload published C as3ff110 receiver'),
        (0x14140a,'e801dd2b00','Exact admitted publication ->3ff110 call'),
        (0x3ff073,'50','Getter passes arg1=1 to188a50'),
        (0x3ff074,'b9d8135701','Initializer ECX is singleton G=VA15713d8'),
        (0x3ff079,'e8d299d8ff','Exact admitted getter ->188a50 call'),
        (0x429cd7,'897814','Initializer429b00 publishes exact C+88 pointer to G+14'),
        (0x486227,'8b4818','Entity effect receiver B=[G+18]'),
        (0x48622c,'8b5010','Entity effect virtual slot+10'),
        (0x486232,'ffd2','Entity effect call with arg1=1f,arg2=O+dc'),
    ):
        assert im.read(at,len(bytes.fromhex(encoded))).hex() == encoded
        inherited_pins.append(dict(**im.record(at,len(bytes.fromhex(encoded))),meaning=meaning))
    table = []
    for offset,target in ((0,0x144a0),(4,0x144b0),(0x10,0x26f70)):
        row = im.record(0x100a288+offset,4)
        assert struct.unpack('<I',bytes.fromhex(row['bytes']))[0] == im.base+target
        assert im.executable(target)
        table.append(dict(**row,slot_offset=hex(offset),target_rva=hex(target),target_decoded=False))
    failure_path = SCRATCH/'boundary-stop-00023210.json'
    failure = json.loads(failure_path.read_bytes())
    assert failure['unadmitted_target'] == '0x228d0'
    return dict(
        schema='chat27.cinematic_context_static.v1',
        evidence_level='Offline file-backed static code/data only; no native execution or headset acceptance',
        supplied_head='d1e69f7b67ded2a558b5c27e0d5a30230cd40b74',git_queried=False,
        executable=dict(path=str(im.path),sha256=EXE_SHA,preferred_base=hex(im.base)),
        script_sha256=sha(Path(__file__).read_bytes()),
        helper=dict(path=HELPER.relative_to(ROOT).as_posix(),sha256=HELPER_SHA,usage='Image parsing, instruction-exact disassembly, relative branch decoding only; helper main never executed'),
        inherited=inherited_records,
        inherited_verification='All scheduler30 saved complete-function bytes and decode hashes, all entity31 function bytes and artifact hashes checked; no prior function redecoded',
        scope=dict(new_complete_functions=4,new_instructions=201,new_code_bytes=643,data_slot_bytes=12,
            preferred_function_maximum=4,absolute_function_maximum=6,absolute_code_byte_maximum=65536,
            adjacent_spill_functions=0,partial_final_functions=0,generic_scans=0,
            getter_redecoded=False,transform29_reopened=False,scope_frozen=True,
            method='Instruction-exact ranges; only reachable intrafunction branches. Exact reviewed23227 tail jump ends23210; target228d0 separately admitted as fourth complete function.'),
        units=units,instructions=instructions,direct_calls=calls,direct_tail_calls=tails,
        exact_instructions=exact,inherited_exact_instructions=inherited_pins,service_vtable_slots=table,
        findings=dict(
            context_identity='Reused getter returns G=preferredVA15713d8/RVA11713d8; caller3ff079 supplies that G and ignored stack argument1 to188a50.',
            publication_helper=dict(entry='0x3ff110',incoming='ECX=C=[P+34] after old nonnull C virtual slot0(1)',
                direct_field_effects='Only direct nonstack stores in3ff110 are byte[C+c0]=1. No direct read or write of C+88 or G+14; no direct G+18 store. It does not explicitly restore G+14 from C+88.',
                context_uses=['Optional [G+20]+1c comparison','Helper1bb0f0 receives ECX=[G+4c] and arg1=([[C+9c]+4] ? [[C+9c]+4]+4 : 0)'],
                opaque_effects='Other direct callees and virtual calls are not decoded. Neither old-object release nor3ff110 callees are proved unable to mutate context/manager state.',
                scheduler_identity_conclusion='Creation-time assignment C+88 ->G+14 is retained as evidence; equality after old-object release and all opaque helper calls remains unproved. Scheduler still sources C+88 independently.'),
            singleton_initializer=dict(entry='0x188a50',abi='ECX=G, callee removes4 stack bytes, arg1 never read; returns G',
                direct_initialization='Installs base vtableVA1419878; zeros G+4,+8,+c,+10,+14,+18. Getter later replaces the context vtable withVA1431530 and initializes derived fields, using prior saved evidence.',
                manager14='Initializer clears G+14 and does not install any manager there. Separate429b00 assignment supplies manager from C+88.',
                service18='Calls23210 and stores returned B to G+18 if it differs from the post-call old value. A differing nonnull old value receives virtual+4, then field is cleared; nonnull new B receives virtual+0 after storing.',
                ownership_limit='This is a retained-pointer-shaped replace/release/acquire protocol. Slot0/slot4 implementations are not decoded; no exact reference-count increment/decrement claim. Fresh G+18 is set zero before the factory, but opaque factory/subcalls prevent assuming the reread can never differ.'),
            service_factory=dict(entry='0x23210',request='Calls unresolved20b90 with(0x1c70,4,0); null returns0; nonnull result becomes ECX for exact tail-call228d0.',
                constructor='228d0 returns its incoming B, installs final direct vtableVA140a288/RVA100a288, initializes256 records of stride1c from B+48, plus other fields. Opaque10ecf0 receives B+10.',
                service_vs_manager='G+18 service follows its own factory/construction path. No admitted pointer assignment equates B with manager[C+88] or G+14.'),
            entity_call_target=dict(callsite='0x486232',receiver='B=[G+18]',arguments=['0x1f','O+0xdc'],
                constructed_vtable='0x140a288',slot_offset='0x10',target_rva='0x26f70',
                evidence='Exact constructor vtable store plus exact table+10 word resolves the constructed service implementation. Target body remains undecoded.',
                condition='Conditional on entity effect observing this same constructed B with the assigned vtable; no live field/vtable identity, subsequent replacement or generation proof.'),
        ),
        conclusions=dict(initializer_manager_assignment='G+14 zeroed; separate429b00 later assigns C+88',
            publication_explicit_reconciliation=False,scheduler_instance_join=False,
            service_construction_chain=True,service_slot10_static_target='0x26f70',
            service_slot10_effect_proved=False,source_fix=False,selected_view_authority=False),
        next_exact_edge=dict(callsite_rva='0x486232',target_rva='0x26f70',
            derivation='Inherited B=[G+18], vtable slot+10; new constructor228e9 assignsVA140a288; file word atRVA100a298 isVA426f70',
            receiver='ECX=B',arguments=['0x1f','O+0xdc'],
            question='Admit complete26f70 to determine exact operation on the constructed service and O+dc; keep same-B/vtable runtime condition explicit.'),
        preserved_failures=dict(
            boundary_stop='23210 initially stopped at exact backward tail jump23227->228d0; two attempts rejected before decoding target. Saved second stop retained; review admitted tail as a terminal and228d0 separately. No spill occurred.',
            saved_boundary_stop=dict(path=failure_path.relative_to(ROOT).as_posix(),sha256=sha(failure_path.read_bytes())),
            auxiliary='A one-instruction inspection printed23210 push0 then TypeError when formatting its absent direct target; no conclusion depended on that formatter and no external target decoded.',
            inherited='All prior failures remain in original proof files; neither receipt overwritten.'),
        limitations=['No proof of G+14/C+88 identity after old-object virtual release or opaque callees.',
            'Service slot0/4/10 bodies not decoded; ownership and dispatch semantics beyond exact stores/calls remain unproved.',
            'No selected active view, HUD, camera correction, local-player identity, native lifetime/registration, game route, entry/skip/exit, gameplay-return or continuous-head-rotation claim.',
            'Transform29 virtual N_i+8 remains parked; no guessed implementation or transform redecoding.'],
        saved_decodes=artifacts,
        verification='Byte/hash checks for all new instructions, whole functions, exact fields/calls, internal branch targets, admitted tail edge, inherited receipts/decode bytes and exact service vtable words; proof assembly invokes no decoder.',
        execution=dict(production_writes=False,git=False,builds=False,tests=False,game=False,process_access=False,input=False,xr=False,install=False,subagents=False),
        frozen=True,retired=True)

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--exe',type=Path,required=True)
    parser.add_argument('--decode',type=lambda value:int(value,0))
    parser.add_argument('--proof',action='store_true')
    args = parser.parse_args()
    SCRATCH.mkdir(parents=True,exist_ok=True)
    for key in ('TEMP','TMP','TMPDIR'):
        os.environ[key] = str(SCRATCH)
    im = helper.Image(args.exe)
    inherited(im)
    if args.decode is not None:
        decode(im,args.decode)
    if args.proof:
        result = proof(im)
        dest = HERE/'proof01.json'
        save(dest,result,True)
        print(json.dumps(dict(path=str(dest),sha256=sha(dest.read_bytes()),scope=result['scope'])))

if __name__ == '__main__':
    main()
