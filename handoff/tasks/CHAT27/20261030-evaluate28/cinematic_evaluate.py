"""Bounded, exact-edge, file-only evaluation of queue25's record consumer."""
from __future__ import annotations
import argparse
import importlib.util
import json
import os
from pathlib import Path
import re
import struct
import sys
sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[4]
SCRATCH = ROOT / 'reports/coordinator-20261030-cinematic-evaluate28'
INHERITED = ROOT / 'handoff/tasks/CHAT27/20261030-queue25/proof01.json'
SOURCE = ROOT / 'handoff/tasks/CHAT27/20261030-manager18/cinematic_manager.py'
spec = importlib.util.spec_from_file_location('evaluate28_file_only_helper', SOURCE)
helper = importlib.util.module_from_spec(spec)
spec.loader.exec_module(helper)
assert helper.digest(SOURCE.read_bytes()) == '1a6506e11576f290e70f847a65f307cf1cebffab928360120d4fee5e587857fb'
assert helper.digest(INHERITED.read_bytes()) == '09a31eea86970d877e406ae258273f1188910571f494e6539e8c6c701b2772ab'
UNITS = {
    0xb6b9c0: (0xb6bb00, 'Exact direct consumer from inherited call0xb6bc20.'),
    0xb628e0: (0xb62b70, 'Exact record/workspace call at0xb6ba6b.'),
    0xb6b050: (0xb6b520, 'Exact local-result collection call at0xb6ba81.'),
    0xb6b520: (0xb6b670, 'Exact final local-result processing call at0xb6ba9d.'),
}

def save(path, value):
    with path.open('xb') as stream:
        stream.write((json.dumps(value, indent=2)+'\n').encode('utf-8'))

def decode(im, unit):
    assert unit in UNITS and len(UNITS) <= 8
    if unit != 0xb6b9c0:
        admitted = json.loads((SCRATCH/'decode-00b6b9c0.json').read_text(encoding='utf-8'))
        assert any(row.get('target_rva') == hex(unit) and row['asm'].startswith('call') for row in admitted['instructions'])
    ceiling, reason = UNITS[unit]
    pending, rows = [unit], {}
    while pending:
        at = pending.pop()
        while at not in rows:
            assert unit <= at < ceiling, hex(at)
            raw, asm, line = helper.instruction(im, at)
            assert at+len(raw) <= ceiling, (hex(at), asm)
            assert not asm.startswith(('int', 'db')), (hex(at), asm)
            row = dict(**im.record(at,len(raw)), bytes=raw.hex(), asm=asm, listing=line)
            rows[at] = row
            mnemonic = asm.split()[0]
            branch = re.fullmatch(r'(?:j\w+|call)\s+([0-9A-Fa-f]+)', asm)
            if branch:
                target = int(branch[1],16)-im.base
                row['target_rva'] = hex(target)
                if mnemonic != 'call' and unit <= target < ceiling:
                    pending.append(target)
                elif mnemonic != 'call':
                    assert mnemonic == 'jmp', (hex(at), asm)
                    row['tail_call'] = True
            if mnemonic.startswith('ret') or mnemonic == 'jmp':
                break
            at += len(raw)
    ordered = [rows[at] for at in sorted(rows)]
    used = {at+i for at,row in rows.items() for i in range(row['size'])}
    assert len(used) == sum(row['size'] for row in rows.values())
    assert len(used) <= 65536
    listing = ('\n'.join(row['listing'] for row in ordered)+'\n').encode('utf-8')
    path = SCRATCH / f'decode-{unit:08x}.txt'
    with path.open('xb') as stream:
        stream.write(listing)
    result = dict(unit=hex(unit), reason=reason, complete_reachable_cfg=True,
        size=len(used), extent_end_rva=hex(max(used)+1),
        listing_sha256=helper.digest(listing), listing_path=str(path.relative_to(ROOT)),
        instructions=ordered)
    save(path.with_suffix('.json'), result)
    print(listing.decode('utf-8'))
    print(json.dumps({k:v for k,v in result.items() if k != 'instructions'}))

def proof(im):
    inherited = json.loads(INHERITED.read_text(encoding='utf-8'))
    reused = [row for row in inherited['saved_decodes'] if row['unit'] == '0xb6bb00']
    assert len(reused) == 1
    for row in reused:
        assert im.record(int(row['rva'],0),row['size'])['sha256'] == row['sha256']
        assert helper.digest((ROOT/row['path']).read_bytes()) == row['decode_sha256']
    decodes, instructions, covered = [], {}, set()
    for unit in UNITS:
        path = SCRATCH / f'decode-{unit:08x}.json'
        saved = json.loads(path.read_text(encoding='utf-8'))
        assert saved['complete_reachable_cfg'] and saved['unit'] == hex(unit)
        listing = (ROOT/saved['listing_path']).read_bytes()
        assert helper.digest(listing) == saved['listing_sha256']
        rows = {int(row['rva'],0):row for row in saved['instructions']}
        reachable, pending = set(), [unit]
        while pending:
            at = pending.pop()
            if at in reachable:
                continue
            assert at in rows, hex(at)
            row = rows[at]
            raw = bytes.fromhex(row['bytes'])
            assert im.read(at,len(raw)) == raw and im.record(at,len(raw))['sha256'] == row['sha256']
            assert unit <= at and at+len(raw) <= UNITS[unit][0]
            reachable.add(at)
            mnemonic = row['asm'].split()[0]
            if mnemonic.startswith('j'):
                assert 'target_rva' in row, row
                target = int(row['target_rva'],0)
                if not row.get('tail_call'):
                    pending.append(target)
            if mnemonic != 'jmp' and not mnemonic.startswith('ret'):
                pending.append(at+len(raw))
        assert reachable == set(rows)
        used = {at+i for at,row in rows.items() for i in range(row['size'])}
        assert used == set(range(unit,int(saved['extent_end_rva'],0)))
        assert len(used) == saved['size'] and not (used & covered)
        covered.update(used)
        instructions.update(rows)
        decodes.append(dict(**{k:v for k,v in saved.items() if k not in ('instructions','size')},
            metadata_path=str(path.relative_to(ROOT)), metadata_sha256=helper.digest(path.read_bytes()),
            **im.record(unit,len(used)), bytes=im.read(unit,len(used)).hex()))
    assert len(UNITS) == 4 and len(covered) == 1579 and len(covered) <= 65536
    def pin(at,encoded,meaning):
        raw = bytes.fromhex(encoded)
        assert at in instructions and im.read(at,len(raw)) == raw, hex(at)
        return dict(**im.record(at,len(raw)),bytes=encoded,meaning=meaning)
    pins = [pin(*row) for row in (
        (0xb6ba55,'8b46343b4638','Only a nonempty S+34/S+38 pointer span admits record initialization and the following W-only call.'),
        (0xb6ba60,'8b4d148b55105751528bce','Forward unchanged F,R,W as arguments(R,F,W), with ECX=S.'),
        (0xb6baf2,'c21400','Wrapper consumes exactly five stack words(T,A,R,F,W).'),
        (0xb628ea,'8b5d08','Record R is first argument in EBX.'),
        (0xb628ee,'8b7510','Workspace W is third argument in ESI.'),
        (0xb628f4,'8b4f2c85c9','Optional helper receiver Q=[S+2c].'),
        (0xb62995,'d99e80000000','Fallback W+80 gets R+30; following instructions copy R+34/+38 to W+84/+88.'),
        (0xb629ad,'f30f118698000000f30f118e94000000f30f118690000000','Fallback W+90/+98 become zero; W+94 uses a branch-specific constant.'),
        (0xb629c5,'8b078986a0000000','State payload S+0..+c is copied as four dwords into W+a0..+ac.'),
        (0xb629e8,'d903d95e10','First of twelve scalar copies: R+0 -> W+10. Padding is not copied.'),
        (0xb62a29,'d94338d95e48','Last of twelve scalar copies: R+38 -> W+48.'),
        (0xb62a2f,'8b47308b4810890e','W+0 gets [[S+30]+10]; W+4 gets [[S+30]+14].'),
        (0xb62a43,'8986d8020000','W+2d8 points to W+c0.'),
        (0xb62b00,'8b96d0020000','Per-child workspace U_i starts at [W+2d0]+i*bb0.'),
        (0xb62b41,'0f95c00a450c','U_i+34 receives low-byte F OR membership in S+44/S+48 pointer span.'),
        (0xb62b47,'81442418b00b0000884134','Advance U_i by bb0 and store its flag at+34.'),
        (0xb6b080,'83c04050','First collection pass forwards U_i+40 to b503e0 with child receiver.'),
        (0xb6b0da,'d94429308d0429525183c040d91c2450','Second collection pass forwards arguments(U_i+40,float[U_i+30],L).'),
        (0xb6b1c9,'d99e00010000','Direct state output S+100=abs(float[W+2c4]).'),
        (0xb6b1e6,'8b56445f895648','Direct state output S+48=S+44.'),
        (0xb6b5c8,'d9442408518b4c2414d91c24518bce','Final helper b4fb50 receives ECX=S and arguments(L,T).'),
        (0xb6b5e4,'c20c00','Final-stage wrapper consumes three arguments(T,A,L); A has no direct read in this body.'),
    )]
    calls = []
    for at,row in sorted(instructions.items()):
        if not row['asm'].startswith('call'):
            continue
        raw = bytes.fromhex(row['bytes'])
        value = dict(**im.record(at,len(raw)),bytes=row['bytes'],assembly=row['asm'])
        if 'target_rva' in row:
            target = int(row['target_rva'],0)
            assert raw[0] == 0xe8 and at+5+struct.unpack_from('<i',raw,1)[0] == target
            value.update(target_rva=hex(target),new_target_decoded=target in UNITS)
        else:
            value['target_status'] = 'Indirect target unresolved; no dynamic object identity inferred.'
        calls.append(value)
    inherited_call = next(row for row in inherited['direct_calls'] if row['rva'] == '0xb6bc20')
    assert im.read(0xb6bc20,5).hex() == inherited_call['bytes']
    constants = []
    for at,size,meaning in (
        (0x1009d00,4,'Optional helper direction x/z.'),
        (0x10d9420,4,'Optional helper direction y.'),
        (0x10d9488,8,'Optional helper input y displacement; x/z displacement is zero times this finite constant.'),
        (0x10d94f0,4,'Fallback W+94 when Q exists but helper returns AL=0; also state scalar clamp ceiling.'),
        (0x1055d9c,4,'Fallback W+94 when Q is null.'),
    ):
        raw = im.read(at,size)
        constants.append(dict(**im.record(at,size),bytes=raw.hex(),value=struct.unpack('<f' if size==4 else '<d',raw)[0],meaning=meaning))
    return dict(schema='chat27.cinematic_evaluate_static_code.v1',
        evidence_level='Offline file-backed static code; no deterministic execution test, actual-game check, or headset acceptance.',
        executable_sha256=helper.SHA, preferred_base=hex(im.base),
        supplied_worktree_head='6c42c3fd6d39715d47a07584061697b2be8a678e; supplied by parent, no Git queried',
        script_sha256=helper.digest(Path(__file__).read_bytes()),
        helper=dict(path=str(SOURCE.relative_to(ROOT)),sha256=helper.digest(SOURCE.read_bytes())),
        inherited=dict(path=str(INHERITED.relative_to(ROOT)),sha256=helper.digest(INHERITED.read_bytes())),
        scope=dict(new_complete_functions=4,new_unique_decoded_bytes=len(covered),partial_functions=0,
            preferred_function_limit=4,absolute_function_limit=8,maximum_decoded_bytes=65536,
            adjacent_spill_functions=0,generic_scans=0,reused_bytes=480,
            strategy='Exact inherited call then three exact calls within that complete wrapper. Decode single instructions, follow reachable intrafunction branches, stop at returns; byte/hash and CFG closure verified.'),
        units=decodes,reused_decodes=reused,exact_instructions=pins,direct_calls=calls,
        inherited_entry_call=inherited_call,constants=constants,
        notation=dict(S='[R+40]',A='float[R+44]',F='zero-extended byte[R+4d]',
            W='Caller-supplied workspace pointer, fifth argument of b6b9c0.',
            L='Wrapper stack-local result container at stable post-prologue ESP+10.',
            C_i='Pointer entry i in S+34/S+38 span.',U_i='[W+2d0]+i*bb0'),
        abi=dict(
            wrapper='b6b9c0: ECX=S, stack(T,A,R,F,W), ret14. Calls b57550(S), b62b70(T,A) with ECX left by the previous call (not assumed), then conditionally b628e0(S,R,F,W) and cdecl b5d3f0(W), then b6b050(S,L,W), b6b520(S,T,A,L), and local-container cleanup calls.',
            initialize='b628e0: ECX=S, stack(R,F,W), ret0c. Direct twelve-float copy and workspace setup, then child b57f90(C_i,U_i).',
            collect='b6b050: ECX=S, stack(L,W), ret8. Calls b503e0(C_i,U_i+40), then b6a3c0(C_i,U_i+40,float[U_i+30],L); optional b461c0([S+e0],float[S+c],L,W+c0). Direct state side effects are listed separately.',
            finish='b6b520: ECX=S, stack(T,A,L), ret0c. Direct second argument A is unused; forwards L,T to b4fb50 and subsequently calls b6b400(S).'),
        findings=dict(
            exact_transform_copy=[dict(source_offset=hex(offset),destination_offset=hex(offset+0x10)) for offset in (0,4,8,0x10,0x14,0x18,0x20,0x24,0x28,0x30,0x34,0x38)],
            transform_copy_limit='b628e0 copies R float payload to W+10..48 without matrix arithmetic. It skips padding. This establishes initialization only; following opaque b5d3f0 may change W before collection. No final composed transform is claimed.',
            fallback='If Q=[S+2c] is null, or b3a9d0 returns AL=0, copy R+30/+34/+38 into W+80/+84/+88 and write W+90/+98=0. W+94 uses the exact branch constants above. A nonzero helper result skips these fallback stores; exact helper effects are unresolved.',
            state_payload='Copy S+0,+4,+8,+c as dwords to W+a0,+a4,+a8,+ac; W+0/+4 get [[S+30]+10/+14], and W+2d8=W+c0.',
            child_records='Each C_i is passed with U_i to b57f90; U_i+34 becomes low-byte F bitwise-OR 1 if C_i occurs in S+44/S+48, otherwise OR 0. Workspace record stride is bb0.',
            optional_points='When [[S+28]]+54 is nonzero, wrapper/helper and virtual calls produce an unresolved collection. Its virtual slot0 return goes to W+70. Per-result position+30/+34/+38 is copied to W+50+10*j / W+54+10*j / W+58+10*j. No identity or coordinate-space claim.',
            direct_state_outputs='b6b050 conditionally updates S+fc using W+2c0 with a clamp; writes S+100=abs(float[W+2c4]); if signed S+108 >= 0, adds W+2c4 to [ [S+84] + index*4 ] pointee+10; copies S+44 to S+48. b6b520 conditionally copies each S+64/S+68 child+1c to+18 and adds each S+94/S+98 child+5c to its nonnull child+74 pointee+10. Other callees can add effects.',
            composition_boundary='At b6ba70, W is pushed immediately after b628e0 returns; b6ba71 calls b5d3f0 and b6ba76 reclaims four bytes. This is the next exact target receiving the initialized transform before collection.',
            ownership_limit='Inherited 424370 proves this layout for a separate stack-local queue. No evidence here joins that queue to the original cinematic B or selected local-player view; the other agent owns original-B provenance.',
            acceptance_limit='No selected camera, active pose authority, HUD, cutscene entry/skip/exit, gameplay return, continuous head rotation, production correction, or API proposal.'),
        next_unresolved_edge=dict(callsite_rva='0xb6ba71',target_rva='0xb5d3f0',
            arguments='One cdecl stack argument W after b628e0 copied R into W+10..48; branch requires S+34 != S+38.',
            required='Decode this exact target to establish what it does to W and whether/how it composes the copied transform before b6b050. Do not assume transform composition from its position in the call chain.'),
        verification=dict(executable_hash=True,inherited_hash=True,saved_decode_bytes_and_hashes=True,
            complete_reachable_cfg=True,no_overlap=True,exact_rel32_targets=True,
            attempts='All four single-instruction decodes succeeded. First proof assembly rejected a duplicate size keyword before writing proof01.json; assembly was corrected and final validation succeeded. No rejected decode and no neighboring function decoded.'),
        execution=dict(git=False,builds=False,tests=False,game=False,game_process_access=False,input=False,xr=False,
            profiles=False,installs=False,packages=False,subagents=False,production_writes=False),frozen=True)

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--exe',type=Path,required=True)
    parser.add_argument('--decode',type=lambda x:int(x,0))
    parser.add_argument('--output',type=Path)
    args = parser.parse_args()
    SCRATCH.mkdir(parents=True,exist_ok=True)
    for key in ('TEMP','TMP','TMPDIR'):
        os.environ[key] = str(SCRATCH)
    im = helper.Image(args.exe)
    if args.output is not None:
        assert args.output.resolve() == Path(__file__).resolve().parent/'proof01.json'
        result = proof(im)
        save(args.output,result)
        print(json.dumps(dict(path=str(args.output.resolve()),sha256=helper.digest(args.output.read_bytes()),scope=result['scope'],constants=result['constants'])))
    elif args.decode is not None:
        decode(im,args.decode)

if __name__ == '__main__':
    main()
