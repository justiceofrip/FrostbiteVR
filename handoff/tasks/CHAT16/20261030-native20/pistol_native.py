"""File-only, exact-entry x86 trace for CHAT16; no signature or executable sweep."""
from __future__ import annotations
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import struct
import subprocess

ROOT = Path(__file__).resolve().parents[4]
SCRATCH = ROOT / 'reports/coordinator-20261030-pistol-native20'
SHA = '3911fcc8914b0158c434cee36f19295316e715d4c9a0e70c857e8c39e3d54258'
DUMPBIN = Path(r'C:\Program Files\Microsoft Visual Studio\18\Community\VC\Tools\MSVC\14.51.36231\bin\HostX64\x64\dumpbin.exe')
# Existing whole-function proofs: Bc2ReloadFlow.cpp/Bc2ReloadState.cpp.
# Exact locations: docs/archive/BC2-PUMP-RELOAD-20261001.md, lines59-63,257,363.
UNITS = {
    0x2e9000: (397, 0x4a6ecc3d95700d53, 'Original Update, thiscall(context*, DWORD), ret8.'),
    0x2e62d0: (1732, 0x71118b4d657473d3, 'State Step, reached by Update+0x163; sixteen fixed dispatch cases.'),
    0x2e69e0: (772, 0x7344b32b39a9bd85, 'Timed Step, reached by Update+0x172.'),
    0x2dd6a0: (163, 0x3cae7f07de53d0f7, 'Transition Commit, reached by Update+0x151.'),
    0x2d6eb0: (42, 0x86f6fd3bcb5db20e, 'Ready query, Step+0xaa/+0x1aa and Timed+0x163; KnifeNativeCode.h whole proof.'),
    0x2e1fa0: (168, 0x5a79cdc6166428ff, 'Fire entry, Step+0x1dd; Bc2SelectedKnifeCode.h whole proof.'),
    0x2d6d40: (128, 0x32cc32ebc7884376, 'Abort helper, Update+0x88; DiscoverReloadHoldCode complete proof.'),
    0x2cd5e0: (83, 0xd9d078fbe813d855, 'Ready-query direct callee at0x2d6ec0; complete CFG first established in this job.'),
}

def digest(raw):
    return hashlib.sha256(raw).hexdigest()

def fnv(raw):
    value = 14695981039346656037
    for byte in raw:
        value = ((value ^ byte) * 1099511628211) & 0xffffffffffffffff
    return value

class Image:
    def __init__(self, path):
        self.path = path
        self.data = path.read_bytes()
        assert digest(self.data) == SHA, 'Unexpected executable'
        pe = struct.unpack_from('<I', self.data, 0x3c)[0]
        assert self.data[pe:pe+4] == b'PE\0\0'
        assert struct.unpack_from('<H', self.data, pe+4)[0] == 0x14c
        count = struct.unpack_from('<H', self.data, pe+6)[0]
        optional_size = struct.unpack_from('<H', self.data, pe+20)[0]
        optional = pe+24
        assert struct.unpack_from('<H', self.data, optional)[0] == 0x10b
        self.base = struct.unpack_from('<I', self.data, optional+28)[0]
        assert self.base == 0x400000
        self.sections = []
        for i in range(count):
            at = optional+optional_size+i*40
            virtual, rva, rawsize, raw = struct.unpack_from('<4I', self.data, at+8)
            flags = struct.unpack_from('<I', self.data, at+36)[0]
            self.sections.append((rva, virtual, raw, rawsize, flags))
    def read(self, rva, size, executable=True):
        for start, _, raw, length, flags in self.sections:
            if start <= rva and rva+size <= start+length:
                assert not executable or flags & 0x20000000
                return self.data[raw+rva-start:raw+rva-start+size]
        raise ValueError(hex(rva))
    def record(self, rva, size):
        return dict(rva=hex(rva), size=size, sha256=digest(self.read(rva,size)))

def decode(im, at):
    assert at in UNITS and len(UNITS) <= 8
    assert sum(row[0] for row in UNITS.values()) <= 65536
    size, fingerprint, meaning = UNITS[at]
    raw = im.read(at, size)
    assert fnv(raw) == fingerprint, (hex(at), hex(fnv(raw)))
    output = subprocess.run([str(DUMPBIN), '/NOLOGO', '/DISASM:BYTES',
        f'/RANGE:0x{im.base+at:x},0x{im.base+at+size-1:x}', str(im.path)],
        capture_output=True, check=True).stdout.decode('utf-8',errors='strict')
    instructions = []
    for line in output.splitlines():
        match = re.match(r'^\s*([0-9A-Fa-f]{8}):\s+((?:[0-9A-Fa-f]{2} )+)(.*)$',line)
        if match:
            address = int(match[1],16)-im.base
            assert at <= address < at+size
            instructions.append(dict(rva=hex(address), bytes=match[2].strip().replace(' ',''), asm=match[3].strip()))
        elif instructions and re.fullmatch(r'\s{10,}(?:[0-9A-Fa-f]{2}\s*)+',line):
            instructions[-1]['bytes'] += line.strip().replace(' ','')
    cursor = at
    for row in instructions:
        encoded = bytes.fromhex(row['bytes'])
        assert int(row['rva'],0) == cursor
        assert im.read(cursor,len(encoded)) == encoded
        cursor += len(encoded)
    assert cursor == at+size, (hex(cursor),hex(at+size))
    assert any(row['asm'].startswith('ret') for row in instructions)
    text = '\n'.join(f"{row['rva']}: {row['bytes']:<28} {row['asm']}" for row in instructions)+'\n'
    dest = SCRATCH/f'decode-{at:08x}.txt'
    assert not dest.exists(), 'Frozen decode already exists'
    dest.write_text(text,encoding='utf-8')
    record = dict(**im.record(at,size), fnv1a64=hex(fingerprint), meaning=meaning,
        instruction_count=len(instructions), complete=True, decode_sha256=digest(text.encode()),
        instructions=instructions)
    dest.with_suffix('.json').write_text(json.dumps(record,indent=2)+'\n',encoding='utf-8')
    print(json.dumps({k:v for k,v in record.items() if k!='instructions'}))

def proof(im):
    units=[]
    instructions={}
    for at,(size,fingerprint,meaning) in UNITS.items():
        path=SCRATCH/f'decode-{at:08x}.json'
        row=json.loads(path.read_text(encoding='utf-8'))
        assert row['sha256']==digest(im.read(at,size)) and fnv(im.read(at,size))==fingerprint
        assert row['decode_sha256']==digest(path.with_suffix('.txt').read_text(encoding='utf-8').encode())
        cursor=at
        for line in row['instructions']:
            raw=bytes.fromhex(line['bytes'])
            assert int(line['rva'],0)==cursor and im.read(cursor,len(raw))==raw
            instructions[cursor]=line
            cursor+=len(raw)
        assert cursor==at+size and row['complete']
        units.append(dict(**im.record(at,size),fnv1a64=hex(fingerprint),meaning=meaning,complete=True,
            decode=str(path.with_suffix('.txt').relative_to(ROOT)),normalized_decode_sha256=row['decode_sha256'],
            on_disk_decode_sha256=digest(path.with_suffix('.txt').read_bytes())))
    def pin(at,encoded,meaning):
        raw=bytes.fromhex(encoded)
        assert instructions[at]['bytes'].lower()==encoded.lower() and im.read(at,len(raw))==raw
        return dict(**im.record(at,len(raw)),bytes=encoded,meaning=meaning)
    pins=[pin(*row) for row in (
        (0x2e911b,'f6472c01','Original Update checks fire input bit0.'),
        (0x2e9121,'c686a400000000','Fire input release clears firing+0xa4 latch.'),
        (0x2e6364,'e8470bffff','Return state1 queries native ammunition availability.'),
        (0x2e6377,'80bd8800000000','State1 tests FireLogic.WaitForTriggerRelease at FireLogic+0x88.'),
        (0x2e63f4,'897e44','Release-wait completion selects state2, independent of ammo transfer.'),
        (0x2e6402,'f30f114650','Release-wait completion clears phase timer.'),
        (0x2e6451,'f6432c01','State2 requires fire request.'),
        (0x2e645b,'80be8a00000000','State2 rejects nonzero raw firing inhibit byte+0x8a.'),
        (0x2e647a,'e8310affff','State2 then queries native ammo availability.'),
        (0x2d6ec9,'837e7c00','Availability query checks loaded>0, no chamber field.'),
        (0x2e2022,'c7464405000000','Entry selects state5 for type0; alternate type2/3 paths are9/13.'),
        (0x2e65a5,'c7464406000000','State5 selects shot state6.'),
        (0x2e6625,'837d1801','State6 selects bolt state7 only for FireLogicType1.'),
        (0x2e682c,'e80f07ffff','Ordinary reload state12 calls the existing real native ammo transfer.'),
        (0x2e685a,'837d1400','Magazine type1 skips repeat-single-round branch and selects state1.'),
        (0x2e67b0,'c7464401000000','Common return selects state1; not a chamber write.'),
        (0x2e6b28,'837b1400','Timed reload interruption-to-shot branch is gated to reloadType0, excluded for MP443 magazine1.'),
        (0x2d6d4e,'83782c00','Abort helper checks ReloadLogic raw value0 before cancelling reload states10..12.'),
        (0x2d6d65,'c7414401000000','Permitted abort selects state1 without ammo writes.'),
        (0x2dd738,'894640','Commit saves former current into previous.'),
        (0x2dd73b,'894e3c','Commit changes current to selected next state.'),
    )]
    calls=[]
    for at,target,role in (
        (0x2ec6ea,0x2e9000,'Inherited exact thunk->Update edge; no thunk disassembly.'),
        (0x2e9151,0x2dd6a0,'Update->Commit'),(0x2e9163,0x2e62d0,'Update->Step'),
        (0x2e9172,0x2e69e0,'Update->Timed'),(0x2e9088,0x2d6d40,'Update->Abort'),
        (0x2e6364,0x2d6eb0,'State1->availability'),(0x2e647a,0x2d6eb0,'State2->availability'),
        (0x2d6ec0,0x2cd5e0,'Availability->exception predicate'),
        (0x2e64ad,0x2e1fa0,'State2->firing entry'),
        (0x2e65cc,0x2dd960,'State6->shot/listener helper, target intentionally not decoded'),
        (0x2e682c,0x2d6f40,'State12->previously qualified ammunition transfer, not decoded'),
    ):
        raw=im.read(at,5)
        assert raw[0]==0xe8 and at+5+struct.unpack_from('<i',raw,1)[0]==target
        calls.append(dict(**im.record(at,5),target_rva=hex(target),role=role,callee_decoded=target in UNITS))
    tables=[]
    for at,first,wanted in (
        (0x2e6994,0,[0x2e630e,0x2e6322,0x2e640d,0x2e698a,0x2e698a,0x2e658c,0x2e65b5,0x2e672d,0x2e6785,0x2e6654,0x2e67bd,0x2e67fa,0x2e681e,0x2e689b,0x2e68d1,0x2e68fa]),
        (0x2e6ce4,2,[0x2e6a87,0x2e6a97,0x2e6abd,0x2e6a8e,0x2e6a8e,0x2e6a8e,0x2e6a8e,0x2e6c45,0x2e6ae0,0x2e6ae0,0x2e6ae0,0x2e6c90]),
    ):
        raw=im.read(at,len(wanted)*4)
        assert [value-im.base for value in struct.unpack('<'+'I'*len(wanted),raw)]==wanted
        tables.append(dict(**im.record(at,len(raw)),cases={str(first+n):hex(value) for n,value in enumerate(wanted)}))
    assert fnv(im.read(0x2ec6c0,52))==0xdfc4b050cc5dec21
    inherited=ROOT/'handoff/tasks/CHAT16/20261029-work12/preparation02.json'
    assert digest(inherited.read_bytes())=='0ae8203ae1f190ebdc8b0d1bfc293cdd9f8a1e256e89da2f94e6da48ba78ae55'
    sources=[]
    for path,ranges in (
        ('src/games/bc2/Bc2OriginalFiringObserver.h','1-42'),
        ('src/games/bc2/Bc2ReloadFlow.cpp','7-19,64-86,102-116'),
        ('src/games/bc2/Bc2ReloadState.cpp','10-21,85-103'),
        ('src/games/bc2/Bc2ReloadFlowRuntime.cpp','114-128,140-158,265,381-389,961-1000,1183-1260,1699-1745,1870-1920,2640-2662'),
        ('src/games/bc2/Bc2NativeCycleService.cpp','177-180,590-619'),
        ('src/games/bc2/Bc2NativeCycleEvidence.cpp','55-65,152-176'),
        ('src/games/bc2/Bc2NativeCycleInputGuard.h','1-50'),
        ('src/games/bc2/Bc2NativeCycleUpdateRoute.h','1-42'),
        ('src/games/bc2/KnifeNativeCode.h','8-13'),
        ('src/games/bc2/Bc2SelectedKnifeCode.h','9-21'),
        ('src/games/bc2/Bc2MagazineNativeProfile.h','25-34'),
        ('src/games/bc2/Bc2AmmoResourceEvidence.h','9-80'),
        ('src/games/bc2/Bc2AmmoResourceService.h','64-87,266-281'),
        ('src/games/bc2/Bc2AmmoResourceBinding.h','9-25'),
        ('include/fvr/interaction/AmmunitionLedger.h','10-62'),
        ('include/fvr/interaction/WeaponActionGate.h','21-24,40-47,66-79'),
    ):
        sources.append(dict(path=path,sha256=digest((ROOT/path).read_bytes()),read_ranges=ranges))
    return dict(schema='chat16.pistol_native_authority.v1',evidence_level='offline file-backed static native code plus read-only production source',
        executable_sha256=SHA,preferred_base=hex(im.base),helper_sha256=digest(Path(__file__).read_bytes()),
        inherited_preparation=dict(path=str(inherited.relative_to(ROOT)),sha256=digest(inherited.read_bytes()),reflection_repeated=False),
        scope=dict(maximum_new_complete_functions=8,complete_functions_decoded=len(units),new_code_bytes=3485,
            reused_thunk_hash_only_bytes=52,dispatch_table_bytes=112,total_unique_bounded_bytes=3649,maximum_bytes=65536,
            broad_executable_sweep=False,adjacent_function_spill=False,
            first_new_unpinned_entry='0x2cd5e0 was reached by exact ready-query E8 at0x2d6ec0; instruction-by-instruction CFG covered83 contiguous bytes and all returns; final helper freezes size/hash.'),
        units=units,exact_instructions=pins,direct_calls=calls,dispatch_tables=tables,sources=sources,
        findings=dict(
            availability='RVA0x2d6eb0 returns primaryFire!=null AND (exceptionPredicate0x2cd5e0 OR loaded>0). It reads neither current/next/timer nor a chamber field. Predicate uses firing flagsA8, primaryFire+0x1e7 and effective-capacity==-1 conditions; the called numeric conversion is not decoded or newly named.',
            independent_phase='State1 explicitly gates on release latches, selects next2 and zero timer. State2 separately requires Fire bit0, raw+0x8a==0 and availability before firing entry. Thus phase/input readiness is independent of loaded count; it is not an independent chamber occupancy model.',
            single_fire='Type0 entry selects5, Step5 selects6; Step6 selects bolt7 only for type1. MP443 type0 bypasses7/8 and returns1 after its ordinary single shot. No physical slide completion is performed by that path.',
            stock_feed='Magazine reload1: states10->11->12(native transfer)->1(release checks)->2. MP443 post-reload time0 does not create a separate chambering stage; WaitForTriggerRelease=true is read at FireLogic+0x88.',
            manual_feed='TryAmmoResourceHands admits idle2/2/0, executes an exact native ammo operation on server, then receives three own original Updates. Manual feed can remain2->2, so stock12->1->2 must not be required or fabricated.',
            interruption='Timed reload-to-shot transfer branch requires reloadType0 and is skipped for magazine1. Existing Abort helper can select1 and clear timers for permitted ReloadLogic0 without touching ammo; selection/tracking changes are not evidence that this helper ran or that slide debt completed.',
            modeled_chamber='No separate native chamber is established in these eight functions. This is a bounded negative, not proof of global absence. A mod-owned after-feed slide policy is supported as an explicit firing veto composed with real native feed authority and later native phase readiness.',
            current_limit='Existing strict chamber API and NativeAfterShot mode must remain intact; NativeAfterShot handles Pump/Bolt and intentionally adds no empty-feed action.'),
        supported_route=dict(sourcefix_supported=True,implemented=False,proposal='See HANDOFF.txt: separate PistolAfterFeedPolicy and BC2 witness; retain mod-owned debt across interruptions, no native chamber claim.',
            transaction_keys=['AmmoResourceOutcome.request.intent.id','AmmunitionReceipt.command.context.resource(actor,actorGeneration,weapon,weaponGeneration)',
                'command.context.equipGeneration/space','command.id/sourceSequence/operation','receipt.authorityInvocation/beganNs/completedNs',
                'receipt.authorityVerified/copiesVerified','ReloadHoldIdentity.owner/firing[3]/serverPlayer/serverSoldier/serverItem'],
            available_update_keys=['AmmoResourceOwnUpdate.invocation/branch/firing/beforeOwner/afterOwner','beginNs/endNs/depth/finished/identityRetained/neutralContext/animationHold','current/next/timer'],
            publication_gap='Completion internally retains invocations_[3]/seen_[3], but AmmunitionReceipt publishes only authorityInvocation/completedNs. A new immutable sidecar witness or later own-Update cohort is needed; do not invent three IDs from receipt.copiesVerified.',
            new_read_dependencies=['raw firing+0x8a inhibit and +0xa4/+0xa5 release latches within the already-read0xb0 object span',
                'existing exact FireLogic0/reloadType1/config identity plus inherited WaitForTriggerRelease=true; preserve ordinary predicate/input checks',
                'later fresh neutral original Updates from all3 nonaliasing copies, bound to the successful feed command and item lifetime']),
        unresolved_fact='No actual-game evidence yet shows the proposed mod-owned slide debt/fire veto surviving tactical/empty feed and interruption on all three MP443 copies; native or physical-headset acceptance is not granted.',
        retained_failures=['Two initial one-instruction decoder assertions stopped at0x2cd5e1 because dumpbin emitted a trailing partial-decode byte as an instruction. Address-filter/parser corrected; no receipt/output was saved until the complete83-byte CFG verified.',
            'First proof attempt stopped because Windows write_text saved CRLF while the decoder recorded normalized LF text hashes. Proof now verifies both normalized and actual on-disk hashes; retained decode files unchanged.'],
        execution=dict(git=False,builds=False,tests=False,game=False,process_access=False,input=False,xr=False,installs=False,subagents=False,production_writes=False),frozen=True)

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--exe',type=Path,required=True)
    parser.add_argument('--decode',type=lambda value:int(value,0))
    parser.add_argument('--proof',action='store_true')
    args = parser.parse_args()
    SCRATCH.mkdir(parents=True,exist_ok=True)
    for name in ('TEMP','TMP','TMPDIR'):
        os.environ[name] = str(SCRATCH)
    image = Image(args.exe)
    if args.proof:
        assert args.decode is None
        output=Path(__file__).with_name('proof01.json')
        assert not output.exists(), 'Frozen proof already exists'
        result=proof(image)
        output.write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
        print(json.dumps(dict(path=str(output.relative_to(ROOT)),sha256=digest(output.read_bytes()),functions=len(UNITS),code_bytes=3485)))
    else:
        assert args.decode is not None
        decode(image,args.decode)

if __name__ == '__main__':
    main()
