"""Exact-edge, file-only CHAT27 workspace-transform investigation."""
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
SCRATCH = ROOT / 'reports/coordinator-20261030-cinematic-transform29'
INHERITED = ROOT / 'handoff/tasks/CHAT27/20261030-evaluate28/proof01.json'
SOURCE = ROOT / 'handoff/tasks/CHAT27/20261030-manager18/cinematic_manager.py'
spec = importlib.util.spec_from_file_location('transform29_file_only_helper', SOURCE)
helper = importlib.util.module_from_spec(spec)
spec.loader.exec_module(helper)
assert helper.digest(SOURCE.read_bytes()) == '1a6506e11576f290e70f847a65f307cf1cebffab928360120d4fee5e587857fb'
assert helper.digest(INHERITED.read_bytes()) == 'fff1adb965c7d90422cc84bfd0bedf0c1667a0d8de2ae461a29e8cb006f8138e'
UNITS = {
    0xb5d3f0: (0xb613f0, 'Exact inherited call b6ba71 with cdecl stack argument W.'),
    0xb5d310: (0xb5d3f0, 'Exact b5d473 call with (W+10,W+c0,U_i,K).'),
    0xb59460: (0xb5d310, 'Exact b5d485 call with ECX=[[W+b0]+4], stack(W+c0).'),
    0xb5a380: (0xb5d310, 'Exact b5d3bb call receiving W+10 and W+c0 plus child scalar and locals.'),
}

def save(path, value):
    with path.open('xb') as stream:
        stream.write((json.dumps(value, indent=2)+'\n').encode('utf-8'))

def inherited(im):
    prior = json.loads(INHERITED.read_text(encoding='utf-8'))
    for unit in prior['units']:
        assert im.record(int(unit['rva'],0),unit['size'])['sha256'] == unit['sha256']
        assert helper.digest((ROOT/unit['metadata_path']).read_bytes()) == unit['metadata_sha256']
        assert helper.digest((ROOT/unit['listing_path']).read_bytes()) == unit['listing_sha256']
    return prior

def decode(im, unit):
    prior = inherited(im)
    assert unit in UNITS and len(UNITS) <= 8
    admitted = [row for row in prior['direct_calls'] if row.get('target_rva') == hex(unit)]
    if unit != 0xb5d3f0:
        for path in SCRATCH.glob('decode-*.json'):
            admitted.extend(row for row in json.loads(path.read_text(encoding='utf-8'))['instructions'] if row.get('target_rva') == hex(unit))
    assert admitted, 'No exact reachable admission edge'
    ceiling, reason = UNITS[unit]
    pending, rows = [unit], {}
    while pending:
        at = pending.pop()
        while at not in rows:
            assert unit <= at < ceiling, hex(at)
            raw, asm, line = helper.instruction(im, at)
            assert at+len(raw) <= ceiling, (hex(at),asm)
            assert not asm.startswith(('int','db')), (hex(at),asm)
            row = dict(**im.record(at,len(raw)),bytes=raw.hex(),asm=asm,listing=line)
            rows[at] = row
            mnemonic = asm.split()[0]
            branch = re.fullmatch(r'(?:j\w+|call)\s+([0-9A-Fa-f]+)',asm)
            if branch:
                target = int(branch[1],16)-im.base
                row['target_rva'] = hex(target)
                if mnemonic != 'call' and unit <= target < ceiling:
                    pending.append(target)
                elif mnemonic != 'call':
                    assert mnemonic == 'jmp', (hex(at),asm)
                    row['tail_call'] = True
            if mnemonic.startswith('ret') or mnemonic == 'jmp':
                break
            at += len(raw)
    ordered = [rows[at] for at in sorted(rows)]
    used = {at+i for at,row in rows.items() for i in range(row['size'])}
    assert len(used) == sum(row['size'] for row in rows.values())
    previous_bytes = sum(json.loads(path.read_text(encoding='utf-8'))['size'] for path in SCRATCH.glob('decode-*.json'))
    assert previous_bytes+len(used) <= 65536
    listing = ('\n'.join(row['listing'] for row in ordered)+'\n').encode('utf-8')
    path = SCRATCH/f'decode-{unit:08x}.txt'
    with path.open('xb') as stream:
        stream.write(listing)
    result = dict(unit=hex(unit),reason=reason,complete_reachable_cfg=True,size=len(used),
        extent_end_rva=hex(max(used)+1),listing_sha256=helper.digest(listing),
        listing_path=str(path.relative_to(ROOT)),instructions=ordered)
    save(path.with_suffix('.json'),result)
    print(listing.decode('utf-8'))
    print(json.dumps({k:v for k,v in result.items() if k != 'instructions'}))

def validated_decodes(im):
    units, instructions, covered = [], {}, set()
    for unit in UNITS:
        path = SCRATCH/f'decode-{unit:08x}.json'
        saved = json.loads(path.read_text(encoding='utf-8'))
        assert saved['complete_reachable_cfg'] and saved['unit'] == hex(unit)
        assert helper.digest((ROOT/saved['listing_path']).read_bytes()) == saved['listing_sha256']
        rows = {int(row['rva'],0):row for row in saved['instructions']}
        reachable, pending = set(), [unit]
        while pending:
            at = pending.pop()
            if at in reachable:
                continue
            assert at in rows, hex(at)
            row = rows[at]
            raw = bytes.fromhex(row['bytes'])
            assert im.read(at,len(raw)) == raw
            assert im.record(at,len(raw))['sha256'] == row['sha256']
            assert unit <= at and at+len(raw) <= UNITS[unit][0]
            reachable.add(at)
            mnemonic = row['asm'].split()[0]
            if mnemonic.startswith('j') and not row.get('tail_call'):
                pending.append(int(row['target_rva'],0))
            if mnemonic != 'jmp' and not mnemonic.startswith('ret'):
                pending.append(at+len(raw))
        assert reachable == set(rows)
        used = {at+i for at,row in rows.items() for i in range(row['size'])}
        assert len(used) == saved['size'] == sum(row['size'] for row in rows.values())
        assert not used & covered
        covered.update(used)
        instructions.update(rows)
        spans = []
        for at in sorted(used):
            if spans and at == spans[-1][1]:
                spans[-1][1] += 1
            else:
                spans.append([at,at+1])
        units.append(dict(**{k:v for k,v in saved.items() if k != 'instructions'},
            metadata_path=str(path.relative_to(ROOT)),metadata_sha256=helper.digest(path.read_bytes()),
            byte_spans=[dict(**im.record(start,end-start),bytes=im.read(start,end-start).hex()) for start,end in spans]))
    assert len(units) <= 8 and len(covered) <= 65536
    return units,instructions,covered

def proof(im):
    prior = inherited(im)
    units,instructions,covered = validated_decodes(im)
    assert len(units) == 4 and len(covered) == 1326
    def pin(at,meaning):
        row = instructions[at]
        return dict(**im.record(at,row['size']),bytes=row['bytes'],assembly=row['asm'],meaning=meaning)
    pins = [pin(*row) for row in (
        (0xb5d3f8,'ESI receives the sole cdecl argument W.'),
        (0xb5d414,'H=[W]; the scalar accumulator count is unsigned [H+14].'),
        (0xb5d436,'Initialize every accumulator float to -1 using the pinned constant.'),
        (0xb5d449,'Unsigned [W+2d4] controls the per-child loop.'),
        (0xb5d460,'U_i=[W+2d0]+i*bb0.'),
        (0xb5d46e,'Pass M=W+10 unchanged to the child helper.'),
        (0xb5d47e,'Post-evaluation receiver J=[[W+b0]+4]; b5d310 sets W+b0=U_i+20 before b5a380.'),
        (0xb5d4b9,'COMISS/JB skips negative or unordered (NaN) accumulator values.'),
        (0xb5d4c1,'Publish ordered values >=0 to [H+10]+j*4.'),
        (0xb5d344,'U_i+b90 receives D_i=[U_i+8].'),
        (0xb5d34a,'U_i+20 receives the shared stack-local accumulator descriptor K.'),
        (0xb5d384,'K+4 receives [U_i+10].'),
        (0xb5d38b,'M+a0=W+b0 receives U_i+20.'),
        (0xb5d398,'O+208=W+2c8 receives U_i+40.'),
        (0xb5d3b6,'N_i=[[D_i]] becomes ECX for b5a380; D_i=[U_i+8].'),
        (0xb5a3a6,'Multiply caller scalar U_i+18 by float[N_i+4], after opaque b48c80(N_i+4,float[U_i+2c]).'),
        (0xb5a3d1,'Use signed mapped byte indexed by the nonnegative signed16 [C_i+18], with C_i=[N_i+14].'),
        (0xb5a3f8,'Write existing accumulator if existing>scaled weight, otherwise scaled weight; unordered comparison selects scaled weight.'),
        (0xb5a443,'Nonzero byte[C_i+1a] admits virtual dispatch receiving M and O.'),
        (0xb5a45d,'Push the same M=W+10 for the virtual call.'),
        (0xb5a464,'Unresolved virtual slot+8 call with ECX=N_i and stack(weight,M,O,descriptor,zero-vector).'),
        (0xb595fa,'Write byte[V+b54]=1 when [V+770]>=32 or the helper-produced local count>=32; V=[O+208].'),
        (0xb5966e,'For a nonnegative signed16 index, store a clamped scalar to O+108+index*90.'),
        (0xb596af,'For a negative signed16 index, store first clamped scalar to O+108.'),
        (0xb596e9,'The negative-index branch also stores a clamped scalar to O+198.'),
    )]
    calls = []
    for at,row in sorted(instructions.items()):
        if not row['asm'].startswith('call'):
            continue
        value = dict(**im.record(at,row['size']),bytes=row['bytes'],assembly=row['asm'])
        if 'target_rva' in row:
            target = int(row['target_rva'],0)
            raw = bytes.fromhex(row['bytes'])
            assert raw[0] == 0xe8 and at+5+struct.unpack_from('<i',raw,1)[0] == target
            value.update(target_rva=hex(target),new_target_decoded=target in UNITS)
        else:
            value['target_status'] = 'Virtual slot+8 of N_i, dynamic receiver/vtable identity not established.'
        calls.append(value)
    entry = next(row for row in prior['direct_calls'] if row['rva'] == '0xb6ba71')
    assert im.read(0xb6ba71,5).hex() == entry['bytes'] and entry['target_rva'] == '0xb5d3f0'
    constants = []
    for at,meaning in (
        (0x10d9420,'Accumulator initialization -1.'),
        (0x10ab500,'Threshold gating b5a380 child-record processing.'),
        (0x10d94f0,'Scalar upper-clamp value 1.'),
    ):
        raw = im.read(at,4)
        constants.append(dict(**im.record(at,4),bytes=raw.hex(),value=struct.unpack('<f',raw)[0],meaning=meaning))
    return dict(schema='chat27.cinematic_transform_static_code.v1',
        evidence_level='Offline file-backed static code. No deterministic execution test, actual-game check, or headset acceptance.',
        executable_sha256=helper.SHA,preferred_base=hex(im.base),
        supplied_worktree_head='6c42c3fd6d39715d47a07584061697b2be8a678e; supplied by parent, no Git queried',
        script_sha256=helper.digest(Path(__file__).read_bytes()),
        helper=dict(path=str(SOURCE.relative_to(ROOT)),sha256=helper.digest(SOURCE.read_bytes())),
        inherited=dict(path=str(INHERITED.relative_to(ROOT)),sha256=helper.digest(INHERITED.read_bytes())),
        inherited_entry_call=entry,reused_decodes=[{k:v for k,v in row.items() if k != 'bytes'} for row in prior['units']],
        scope=dict(new_complete_functions=4,new_unique_decoded_bytes=len(covered),partial_functions=0,
            preferred_function_limit=4,absolute_function_limit=8,maximum_decoded_bytes=65536,
            adjacent_spill_functions=0,generic_scans=0,reused_bytes=1579,
            strategy='Exact inherited b6ba71 target, its two direct calls, and exact b5d3bb transform-forwarding target. Single-instruction decode with reachable CFG closure; six skipped alignment bytes in b5d3f0 were not decoded.'),
        units=units,exact_instructions=pins,direct_calls=calls,constants=constants,
        notation=dict(W='Original caller workspace.',M='W+10: copied twelve-float payload starts here; padded slots are inherited unchanged.',
            O='W+c0',H='[W]',U_i='[W+2d0]+i*bb0',
            K='Stack descriptor in b5d3f0: K+0=H, K+4=per-child [U_i+10], K+8=address of stack scalar array, K+c=initial [H+14].',
            D_i='[U_i+8]',N_i='[[D_i]]',C_i='[N_i+14]',V='[O+208], set to U_i+40 before b5a380',
            J='[[W+b0]+4], equal to [U_i+24] if nested evaluation preserves the pointers'),
        abi=dict(
            driver='b5d3f0(W), cdecl, plain ret. Caller b6ba76 reclaims 4 bytes.',
            prepare='b5d310(M,O,U_i,K), cdecl, plain ret. Caller b5d481 reclaims 10h bytes.',
            evaluate='b5a380: ECX=N_i, stack(float[U_i+18],M,O,stack descriptor,stack zero-vector), ret14.',
            postprocess='b59460: ECX=J, stack(O), ret4.',
            virtual='b5a464: ECX=N_i; target=[[N_i]+8]; stack(scaled weight,M,O,same descriptor,same zero-vector).'),
        findings=dict(
            transform='No decoded body directly multiplies or overwrites the twelve copied transform floats at M+{0,4,8,10,14,18,20,24,28,30,34,38}. M is forwarded to conditional unresolved virtual+8 at b5a464, and opaque callees/pointer aliases prevent a whole-call preservation or final-composition claim.',
            driver='Allocate one local scalar array initialized to -1 for unsigned [H+14] elements. For each i<[W+2d4], call b5d310(M,O,U_i,K), then b59460([[W+b0]+4],O). After all children, publish only ordered scalar values >=0 into the float array [H+10]. No selected-view identity follows.',
            preparation='Direct stores: U_i+b90=[U_i+8]; U_i+20=K; K+4=[U_i+10]; W+b0=U_i+20; W+2c8=U_i+40. K and the two descriptor/vector arguments are local stack objects; lifetime beyond the calls is not established.',
            scalar_accumulation='b5a380 first calls opaque b48c80 with receiver N_i+4 and float[U_i+2c], then computes w=float[N_i+4]*float[U_i+18]. If signed16[C_i+18]>=0, use signed8[ [[K+4]+10] + index ] as accumulator index. Store old if ordered old>w, else w. Mapping range and object identity are not proved.',
            child_output='If w is ordered greater than the pinned threshold, b5a380 forwards each 8-byte record in [C_i+4], count[C_i+8], plus w to b50c60 with ECX=[O+208]. If byte[C_i+1a]!=0, dispatch virtual+8 with M and O. These targets remain opaque.',
            postprocess='b59460 iterates records at V+110 in strides10, count[V+4d0]. Opaque b50720/b534c0/b53580 receive child-derived inputs. Explicit output pointers include V+570 and O+1f8/+1fc/+200 (W+2b8/+2bc/+2c0), plus a stack list; callee effects are not inferred. Set byte[V+b54]=1 when [V+770]>=32 or local count>=32. For each local record, finite-value arithmetic uses delta=(1-a)*b: if signed16 index>=0, O+108+index*90=clamp(previous-delta,0,1); otherwise apply the same subtraction/clamp separately to O+108 and O+198. Exact NaN behavior follows the pinned branches, not generic clamp semantics.',
            direct_output='At b5d4c1, K scalar[j] is copied to [H+10]+4*j only when COMISS against zero is ordered >=0; negative and NaN values are skipped. This is a directly reachable scalar-output route, not a proven selected camera transform.',
            conditional_zero_child_case='If [W+2d4]==0, b5d3f0 invokes no child helpers, leaves the local array at -1, and publishes none of those scalars.',
            ownership_limit='Parent reports original B route separately established through 3b5a00 conditional record virtual+14 ->3b23c0/39cd30, B=P+8e0, then b6bb00. That route is not re-decoded here; scheduler-instance equivalence and selected-view ownership remain unproved.',
            acceptance_limit='No production fix, hook, API proposal, selected local-player view/camera, head-rotation authority, HUD, cutscene entry/skip/exit, gameplay return, actual game, or headset claim.'),
        next_unresolved_edge=dict(callsite_rva='0xb5a464',target='[[N_i]+8]',
            receiver='N_i=[[D_i]], D_i=[U_i+8], U_i=[W+2d0]+i*bb0.',
            gate='byte[[N_i+14]+1a]!=0',
            arguments='(w,M=W+10,O=W+c0,stack descriptor,stack zero-vector)',
            required='Establish this exact per-child receiver/vtable identity and slot+8 target, then decode its effects on the forwarded M/O. Do not choose an adjacent or guessed implementation.'),
        verification=dict(executable_hash=True,inherited_hash=True,reused_saved_decode_bytes_and_hashes=True,
            new_saved_decode_bytes_and_hashes=True,complete_reachable_cfg=True,no_overlap=True,exact_rel32_targets=True,
            attempts='All four exact-target decodes succeeded; no rejected decode, adjacent spill, or generic scan.'),
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
    else:
        decode(im,args.decode)

if __name__ == '__main__':
    main()
