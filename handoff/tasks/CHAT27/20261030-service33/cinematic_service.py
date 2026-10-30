"""CHAT27 service33: bounded, file-only service-effect evidence. No native execution."""
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
SCRATCH = ROOT / 'reports/coordinator-20261030-cinematic-service33'
EXE = Path(r'G:\New folder (2)\Battlefield Bad Company 2\BFBC2Game.exe')
EXE_SHA = '3911fcc8914b0158c434cee36f19295316e715d4c9a0e70c857e8c39e3d54258'
HELPER = ROOT / 'handoff/tasks/CHAT27/20261030-entity31/cinematic_entity.py'
HELPER_SHA = 'c27ed332ec3a9fca19219bbc9f184ecfc08f6e8f1ac4915767ce4978107d63c2'
PRIORS = {
    'context32': '566a2562acc27a5dc0cd725ca496cb395c87abc77ae29a5712ac64b27f4ffea9',
    'entity31': 'b52ab309e1186ca788f572af8b6ddd078f5f838866d48701f12437b01c3c1648',
}
sha = lambda raw: hashlib.sha256(raw).hexdigest()
assert sha(HELPER.read_bytes()) == HELPER_SHA
spec = importlib.util.spec_from_file_location('service33_instruction_helper', HELPER)
helper = importlib.util.module_from_spec(spec)
spec.loader.exec_module(helper)
FORBIDDEN = {0x3ff060,0x3ff110,0x188a50,0x23210,0x228d0,0x435c60,0x3b5a00,0xb5d3f0,0xb5d310,0xb59460,0xb5a380}

def save(path, value, exclusive=False):
    with path.open('xb' if exclusive else 'wb') as stream:
        stream.write((json.dumps(value,indent=2)+'\n').encode('utf-8'))

def inherited(im):
    records = []
    for name, expected in PRIORS.items():
        path = ROOT/f'handoff/tasks/CHAT27/20261030-{name}/proof01.json'
        raw = path.read_bytes()
        assert sha(raw) == expected
        prior = json.loads(raw)
        for unit in prior['units']:
            assert sha(im.read(int(unit['rva'],0),unit['size'])) == unit['sha256']
        for artifact in prior.get('artifacts',[])+prior.get('saved_decodes',[]):
            assert sha((ROOT/artifact['path']).read_bytes()) == artifact['sha256']
        records.append(dict(path=path.relative_to(ROOT).as_posix(),sha256=expected))
    assert im.read(0x486232,2).hex() == 'ffd2'
    assert struct.unpack('<I',im.read(0x100a298,4))[0] == im.base+0x26f70
    return records

def saved():
    return [json.loads(path.read_bytes()) for path in sorted(SCRATCH.glob('function-*.json'))]

def decode(im,start):
    inherited(im)
    old = saved()
    assert len(old)<3 and start not in FORBIDDEN
    edges = {int(edge['target_rva'],0) for fn in old for edge in fn['direct_calls']}
    assert start == 0x26f70 or start in edges, 'Only admitted exact service callees'
    dest = SCRATCH/f'function-{start:08x}.json'
    assert not dest.exists(), 'No repeated decode'
    pending, rows, calls, terminals = [start], {}, [], []
    while pending:
        at = pending.pop()
        if at in rows:
            continue
        if not start <= at < start+0x4000 or at in FORBIDDEN:
            save(SCRATCH/f'boundary-stop-{start:08x}.json',dict(entry=hex(start),target=hex(at),rows=list(rows.values()),pending=pending))
            raise AssertionError('Function boundary requires review; no target decoded')
        row = helper.decode_instruction(im,at)
        rows[at] = row
        assert sum(item['size'] for item in rows.values())+sum(fn['reachable_byte_count'] for fn in old)<=65536
        mnemonic = row['mnemonic']
        target = helper.direct_target(row)
        if mnemonic == 'call' and target is not None:
            calls.append(dict(caller_rva=hex(at),target_rva=hex(target),bytes=row['bytes']))
        if mnemonic.startswith('ret') or mnemonic in ('int','int3','ud2'):
            terminals.append(dict(rva=hex(at),kind=mnemonic))
            continue
        if mnemonic.startswith('j') or mnemonic.startswith('loop'):
            assert target is not None, 'Indirect jump requires review'
            pending.append(target)
            if mnemonic == 'jmp':
                continue
        pending.append(at+row['size'])
        if len(rows)%20 == 0:
            save(SCRATCH/f'progress-{start:08x}.json',dict(entry=hex(start),rows=list(rows.values()),pending=pending))
            print(f'{start:#x}: {len(rows)} instructions',flush=True)
    instructions = [rows[at] for at in sorted(rows)]
    occupied = set()
    for row in instructions:
        at = int(row['rva'],0)
        span = set(range(at,at+row['size']))
        assert not occupied.intersection(span)
        occupied.update(span)
        if row['mnemonic'].startswith('j') or row['mnemonic'].startswith('loop'):
            assert helper.direct_target(row) in rows
    result = dict(entry_rva=hex(start),instruction_count=len(rows),reachable_byte_count=len(occupied),
        end_exclusive_rva=hex(max(occupied)+1),instructions=instructions,direct_calls=calls,
        terminal_instructions=terminals,complete_reachable_cfg=True,adjacent_function_units=0,
        whole_range=im.record(start,max(occupied)+1-start),
        skipped_ranges=[hex(at) for at in range(start,max(occupied)+1) if at not in occupied])
    save(dest,result,True)
    text = '\n'.join(f"{row['rva']:>10}: {row['bytes']:<30} {row['text']}" for row in instructions)+'\n'
    dest.with_suffix('.txt').write_text(text,encoding='utf-8')
    print(text)
    print(json.dumps({key:value for key,value in result.items() if key not in ('instructions','whole_range')}))

def proof(im):
    priors = inherited(im)
    functions = saved()
    expected = {0x26f70:291,0x14800:189,0x1f6b0:74}
    assert {int(fn['entry_rva'],0) for fn in functions} == set(expected)
    units, instructions, calls, artifacts = [], [], [], []
    for fn in functions:
        start = int(fn['entry_rva'],0)
        assert fn['complete_reachable_cfg'] and fn['adjacent_function_units']==0
        assert fn['reachable_byte_count']==expected[start] and not fn['skipped_ranges']
        assert int(fn['end_exclusive_rva'],0)==start+expected[start]
        assert im.record(start,expected[start]) == fn['whole_range']
        starts = {int(row['rva'],0) for row in fn['instructions']}
        occupied = set()
        for row in fn['instructions']:
            at = int(row['rva'],0)
            raw = im.read(at,row['size'])
            assert raw.hex()==row['bytes'] and sha(raw)==row['sha256']
            span = set(range(at,at+row['size']))
            assert not occupied.intersection(span)
            occupied.update(span)
            if row['mnemonic'].startswith('j') or row['mnemonic'].startswith('loop'):
                assert helper.direct_target(row) in starts
        assert occupied==set(range(start,start+expected[start]))
        for edge in fn['direct_calls']:
            row = next(row for row in fn['instructions'] if row['rva']==edge['caller_rva'])
            assert helper.direct_target(row)==int(edge['target_rva'],0)
        units.append(dict(**fn['whole_range'],instruction_count=len(fn['instructions']),complete_reachable_cfg=True))
        instructions.extend(fn['instructions'])
        calls.extend(fn['direct_calls'])
        for extension in ('json','txt'):
            path = SCRATCH/f'function-{start:08x}.{extension}'
            artifacts.append(dict(path=path.relative_to(ROOT).as_posix(),sha256=sha(path.read_bytes()),size=path.stat().st_size))
    assert len(instructions)==198 and sum(row['size'] for row in instructions)==554
    by_address = {int(row['rva'],0):row for row in instructions}
    exact = []
    for at,encoded,meaning in (
        (0x26f74,'8bf1','ESI=B, service receiver'),
        (0x26f76,'8b4c241c','ECX=L, original second stack argument; inherited caller supplies O+dc'),
        (0x26f7c,'8b5008','Read listener virtual slot+8; distinct from inherited callback slot+4'),
        (0x26f84,'ffd2','Call listener virtual+8 with no explicit stack arguments; AX used as key component'),
        (0x26f86,'8b4c241c','ECX=e, original first stack argument; inherited caller supplies1f'),
        (0x26f9a,'81e20000ff7f','Pack e low15 bits into key bits16..30'),
        (0x26fa0,'33d0','Key low16 bits are AX from listener virtual+8; bit31 initially clear'),
        (0x26fb2,'8d7c9648','EDI=T=B+48+1c*e; e=1f gives B+3ac'),
        (0x26fe7,'e814d8feff','Lookup14800 receives T, output node slot, and key address'),
        (0x26ff0,'8d4f04','End/sentinel is T+4'),
        (0x26ff5,'7527','Found node takes marked-bit branch; sentinel returns AL=0'),
        (0x27023,'854814','Test node+14 bit31'),
        (0x27026,'740e','Already-marked node returns AL=0; clear bit proceeds'),
        (0x27036,'837e3400','B+34 selects marked/deferred versus immediate removal-wrapper path'),
        (0x2703c,'094814','Nonzero B+34 directly sets node+14 bit31'),
        (0x2703f,'c644242000','Third argument to25400 has low byte0; remaining bytes are not zeroed here'),
        (0x27049,'83c010','Pass address of node payload N+10 to25400'),
        (0x27052,'8d8e481c0000','25400 receiver is B+1c48'),
        (0x27058,'e8a3e3ffff','Opaque helper25400; internal container effects not decoded'),
        (0x27067,'e84486ffff','Zero B+34 passes T and found node to complete removal wrapper1f6b0'),
        (0x2708a,'b001','Both successful bookkeeping paths return AL=1'),
        (0x27090,'c20800','Service removes exactly two stack arguments'),
        (0x14801,'8b410c','Lookup starts at T+0c tree/root pointer'),
        (0x1482d,'3bd7','First lookup key component compares low16(node+14) to low16(key+4)'),
        (0x14836,'3bd6','Second component compares node+10 with key listener pointer'),
        (0x14843,'81e10000ff7f','Third component compares bits16..30; bit31 is excluded'),
        (0x148a4,'8928','Successful lookup outputs matching node'),
        (0x148b6,'8908','Unsuccessful lookup outputs sentinel T+4'),
        (0x1f6b9,'834714ff','Removal wrapper directly decrements dword[T+14] by1'),
        (0x1f6ca,'e801f70100','Opaque3edd0 receives N and T+4; no recursive generic-container exploration'),
        (0x1f6f4,'8918','Removal wrapper writes output value returned by opaque2c300(N)'),
    ):
        assert by_address[at]['bytes']==encoded
        exact.append(dict(**im.record(at,len(bytes.fromhex(encoded))),meaning=meaning))
    inherited_pins = []
    for at,encoded,meaning in (
        (0x228e9,'c70688a24001','Inherited constructor installs B.vtable=VA140a288'),
        (0x486227,'8b4818','Inherited entity effect obtains B=[G+18]'),
        (0x48622c,'8b5010','Inherited entity effect selects B.vtable+10'),
        (0x48622f,'57','Inherited arg2 is L=O+dc'),
        (0x486230,'6a1f','Inherited arg1 is e=1f'),
        (0x486232,'ffd2','Sole admitted virtual edge resolves to26f70 conditional on same B/table'),
        (0x486235,'c6860001000001','Inherited caller sets O+100 after normal return without inspecting AL'),
    ):
        assert im.read(at,len(bytes.fromhex(encoded))).hex()==encoded
        inherited_pins.append(dict(**im.record(at,len(bytes.fromhex(encoded))),meaning=meaning))
    slot = im.record(0x100a298,4)
    assert struct.unpack('<I',bytes.fromhex(slot['bytes']))[0]==im.base+0x26f70
    return dict(
        schema='chat27.cinematic_service_static.v1',
        evidence_level='Offline file-backed static code/data only; no native execution or headset acceptance',
        supplied_head='d1e69f7b67ded2a558b5c27e0d5a30230cd40b74',git_queried=False,
        executable=dict(path=str(EXE),sha256=EXE_SHA,preferred_base=hex(im.base)),
        script_sha256=sha(Path(__file__).read_bytes()),
        helper=dict(path=HELPER.relative_to(ROOT).as_posix(),sha256=HELPER_SHA,
                    usage='Image parsing, instruction-exact disassembly and relative-target decoding only; helper main never executed'),
        inherited=priors,
        inherited_verification='Context32/entity31 exact proof hashes, their complete function byte hashes and all listed saved artifact hashes verified; no inherited function redecoded',
        scope=dict(new_complete_functions=3,new_instructions=198,new_code_bytes=554,data_slot_bytes=4,
                   absolute_function_maximum=3,absolute_code_byte_maximum=65536,
                   adjacent_spill_functions=0,partial_final_functions=0,generic_scans=0,
                   getter_redecoded=False,scheduler_redecoded=False,transform29_reopened=False,
                   scope_frozen=True,method='Exact reachable-instruction CFG; calls never automatically traversed; three separately admitted complete functions'),
        units=units,instructions=instructions,direct_calls=calls,exact_evidence=exact,
        inherited_pins=inherited_pins,service_slot=dict(**slot,slot_offset='0x10',target_rva='0x26f70'),
        admitted_edges=[dict(caller='0x486232',target='0x26f70',kind='conditional constructed service virtual+10'),
                        dict(caller='0x27067',target='0x1f6b0',kind='direct; inspect immediate removal effect'),
                        dict(caller='0x26fe7',target='0x14800',kind='direct; establish exact listener/key identity')],
        operation=dict(
            classification='Listener-record removal/invalidation bookkeeping; no selected-camera state link established',
            abi='ECX=B; arg1=e; arg2=L; ret8; boolean-like AL return, upper EAX bits unspecified',
            key='L and packed K=(AX from L.virtual+8) | ((e & 0x7fff)<<16); K bit31 clear; no explicit arguments to L.virtual+8',
            bucket='T=B+0x48+0x1c*e; inherited e=0x1f gives T=B+0x3ac',
            lookup='14800 finds exact ordered tuple (K low16,L,K bits16..30) using node+14/node+10; bit31 excluded; miss returns sentinel T+4',
            false_paths='Missing record or already-set bit31 at N+14 returns AL=0 after cleanup',
            marked_path='If dword[B+34]!=0: set bit31 at N+14; call opaque25400 with ECX=B+1c48, arg1=&local key storage, arg2=N+10, arg3 having low byte0; return AL=1',
            immediate_path='If dword[B+34]==0: call1f6b0(T,out,N), which decrements dword[T+14] and invokes opaque node/container helpers; return AL=1',
            semantic_precision='Exact matching and invalidation/count-decrement behavior prove removal bookkeeping. Deferred-list insertion, physical unlink/free internals and the meaning of B+34 remain unproved.',
            event_delivery='The only newly decoded indirect call is L.virtual+8 for a key value; no decoded dispatch to inherited listener callback slot+4 or delivery payload',
            inherited_owner_flag='486210 ignores AL and writes byte[O+100]=1 after any normal service return, including false'),
        opaque_calls=[
            dict(site='0x26f84',target='L.virtual+8',limit='AX key use proven; target identity/body/effects not decoded'),
            dict(targets=['0x10ece0','0x108f50','0x10ecc0','0x108f80'],limit='Guard/cleanup-shaped calls opaque; no semantic lock implementation claim'),
            dict(site='0x27058',target='0x25400',limit='Marked path payload/container helper opaque; no further exploration'),
            dict(targets=['0x2c300','0x3edd0','0x1b0d0','0x1af70'],limit='Removal-wrapper node/container/allocator-shaped helpers opaque; no further exploration')],
        conclusion=dict(done=True,branch_closed=True,listener_bookkeeping=True,selected_camera_evidence=False,sourcefix=False,
            decision='Park this generic service branch; no next generic-service edge requested',
            missing_selected_view_evidence='A concrete owner/instance/lifetime join to the active selected-view object, its actual selection/pose authority, and downstream camera/render consumption; this service record removal does not establish any of those',
            runtime_limit='Conditional static target identity is not live B/vtable identity or a scheduler instance join; no entry/skip/exit, gameplay-return, HUD, blend, camera correction or continuous-head-rotation qualification'),
        retained_failures=dict(
            inherited='Context32 backward-tail boundary stops and auxiliary formatting failure, plus entity31 inherited methods17 spill failure, remain unchanged in their frozen receipts',
            service33='No decoder/proof failure. Initial discovery reported absent optional .agents/.codex paths and absent parent-workspace AGENTS.md; local AGENTS.md and CURRENT.md were read successfully'),
        execution=dict(production_writes=False,game=False,process_access=False,input=False,xr=False,git=False,builds=False,tests=False,install=False,subagents=False),
        artifacts=artifacts,
        verification='Executable/prior/helper/artifact hashes, exact instruction bytes, full reachable CFG boundaries, direct targets and constructor slot verified from saved decodes; proof/verify invoke no disassembler')

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--decode',type=lambda value:int(value,0))
    parser.add_argument('--proof',action='store_true')
    parser.add_argument('--verify',action='store_true')
    args = parser.parse_args()
    SCRATCH.mkdir(parents=True,exist_ok=True)
    for name in ('TEMP','TMP','TMPDIR'):
        os.environ[name] = str(SCRATCH)
    im = helper.Image(EXE)
    inherited(im)
    if args.decode is not None:
        decode(im,args.decode)
    elif args.proof:
        value = proof(im)
        destination = HERE/'proof01.json'
        save(destination,value,True)
        print(json.dumps(dict(proof=str(destination),sha256=sha(destination.read_bytes()),scope=value['scope'],conclusion=value['conclusion'])))
    elif args.verify:
        destination = HERE/'proof01.json'
        assert json.loads(destination.read_bytes())==proof(im)
        print(json.dumps(dict(verified=True,proof_sha256=sha(destination.read_bytes()),functions=3,code_bytes=554,instructions=198,redecoded=False)))
    else:
        raise SystemExit('Choose --decode, --proof, or --verify')

if __name__ == '__main__':
    main()
