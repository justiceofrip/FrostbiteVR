"""File-only, bounded scheduler receiver identity investigation."""
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
SCRATCH = ROOT / 'reports/coordinator-20261030-cinematic-scheduler30'
SOURCE = ROOT / 'handoff/tasks/CHAT27/20261030-manager18/cinematic_manager.py'
assert hashlib.sha256(SOURCE.read_bytes()).hexdigest() == '1a6506e11576f290e70f847a65f307cf1cebffab928360120d4fee5e587857fb'
spec = importlib.util.spec_from_file_location('scheduler30_file_only_helper', SOURCE)
helper = importlib.util.module_from_spec(spec)
spec.loader.exec_module(helper)
UNITS = {
    0x435c60: (0x16c, 'Complete caller of candidate435cc1; incoming container+0x88 supplies3b5a00 receiver.'),
    0x16e3c0: (0x845, 'Complete exact caller16eb6c of435c60; container comes from incoming parent+0x34.'),
    0x188af0: (0x13, 'Exact TLS-index writer reference; forward caller context to KERNEL32!TlsSetValue.'),
    0x16f580: (0xd63, 'Complete outer caller16fa98; installs context from3ff060 before invoking16e3c0.'),
    0x3ff060: (0x79, 'Exact context supplier16fa20; returns static context RVA11713d8.'),
    0x429b00: (0xdb8, 'Exact context+14 writer429cd7; assigns the incoming container+88 manager to global context+14.'),
    0x141230: (0x131, 'Exact caller141304 initializes a new container, then stores it in parent+68.'),
    0x141380: (0xa2, 'Exact parent field transfer1413f0/1413f3 publishes pending+68 to scheduled+34.'),
}

def save(path, value):
    raw = (json.dumps(value, indent=2) + '\n').encode('utf-8')
    with path.open('xb') as stream:
        stream.write(raw)
    return helper.digest(raw)

def complete(im, start, size):
    assert start in UNITS and size == UNITS[start][0]
    assert len(UNITS) <= 8 and sum(x[0] for x in UNITS.values()) <= 65536
    end = start + size
    islands=[]
    if start in (0x429b00,0x141230,0x141380):
        body=im.read(start,size)
        marker=b'\xeb\x0eMa0WyG1km'
        pos=body.find(marker)
        while pos>=0:
            islands.append((start+pos+2,start+pos+16))
            pos=body.find(marker,pos+1)
    segments=[]
    cursor=start
    for low,high in islands:
        segments.append((cursor,low))
        cursor=high
    segments.append((cursor,end))
    source=[]
    for low,high in segments:
        result = subprocess.run([str(helper.DUMPBIN), '/NOLOGO', '/DISASM:BYTES',
            f'/RANGE:0x{im.base+low:x},0x{im.base+high-1:x}', str(im.path)], capture_output=True, check=True)
        source.extend(result.stdout.decode('utf-8', errors='replace').splitlines())
    pattern = re.compile(r'^\s*([0-9A-Fa-f]{8}):\s+((?:[0-9A-Fa-f]{2} )+)(.*)$')
    rows, lines = [], []
    for line in source:
        m = pattern.match(line)
        if m and m[3].strip():
            rows.append(dict(rva=int(m[1], 16)-im.base, raw=bytes.fromhex(m[2]), asm=m[3].strip()))
            lines.append(line)
        elif rows and re.fullmatch(r'\s{10,}(?:[0-9A-Fa-f]{2}\s*)+', line):
            rows[-1]['raw'] += bytes.fromhex(line.strip())
            lines.append(line)
    cursor = start
    for row in rows:
        if any(cursor==low for low,high in islands):
            cursor=next(high for low,high in islands if cursor==low)
        assert row['rva'] == cursor
        assert im.read(cursor, len(row['raw'])) == row['raw']
        cursor += len(row['raw'])
    assert cursor == end and rows[-1]['asm'].startswith('ret'), (hex(cursor), hex(end), rows[-1])
    raw = ('\n'.join(lines)+'\n').encode('utf-8')
    dest = SCRATCH / f'decode-{start:08x}-{size:x}.txt'
    with dest.open('xb') as stream:
        stream.write(raw)
    row = dict(**im.record(start, size), bytes=im.read(start, size).hex(),
        decode_sha256=helper.digest(raw), instruction_count=len(rows), unit=hex(start),
        path=str(dest.relative_to(ROOT)), inline_data=[dict(**im.record(low,high-low),bytes=im.read(low,high-low).hex()) for low,high in islands])
    save(dest.with_suffix('.json'), row)
    if size<512:
        print(raw.decode('utf-8'))
    print(json.dumps({k:v for k,v in row.items() if k != 'bytes'}))

def references(im, target):
    calls, pointers = [], []
    needle = struct.pack('<I', im.base + target)
    for start, _, offset, length, flags in im.sections:
        raw = im.data[offset:offset+length]
        if flags & 0x20000000:
            for pos in range(length-4):
                if raw[pos] in (0xe8, 0xe9) and start+pos+5+struct.unpack_from('<i',raw,pos+1)[0] == target:
                    calls.append(dict(**im.record(start+pos, 5), bytes=raw[pos:pos+5].hex()))
        pos = raw.find(needle)
        while pos >= 0:
            pointers.append(dict(**im.record(start+pos,4), executable=bool(flags & 0x20000000)))
            pos = raw.find(needle, pos+1)
    row = dict(target=hex(target), kind='Raw exact-target candidates; no instruction/owner claim until complete enclosing function admission.', calls=calls, pointers=pointers)
    save(SCRATCH / f'raw-references-{target:08x}.json', row)
    print(json.dumps(row if len(calls)+len(pointers)<=32 else dict(target=hex(target),call_count=len(calls),pointer_count=len(pointers))))

def context_writes(im):
    prior=json.loads((SCRATCH/'raw-references-003ff060.json').read_bytes())
    hits=[]
    for row in prior['calls']:
        at=int(row['rva'],0)
        raw=im.read(at+5,48)
        if any(bytes((0x89,0x40+(r<<3),0x14)) in raw for r in range(8)):
            hits.append(dict(call_rva=hex(at),**im.record(at-24,77),bytes=im.read(at-24,77).hex()))
    result=dict(target='0x3ff060',kind='Raw exact-context-supplier references filtered for possible [eax+0x14] stores within following48 bytes; not admitted instructions.',hits=hits)
    save(SCRATCH/'raw-context14-write-candidates.json',result)
    print(json.dumps(result))

def tls_candidates(im):
    hits = []
    total = 0
    for start, _, offset, length, flags in im.sections:
        if not flags & 0x20000000:
            continue
        raw = im.data[offset:offset+length]
        for pos in range(length-4):
            if raw[pos] == 0xe8 and start+pos+5+struct.unpack_from('<i',raw,pos+1)[0] == 0x188b10:
                total += 1
                low, high = max(0, pos-0x80), min(length,pos+0x80)
                window = raw[low:high]
                if b'\x88\x00\x00\x00' in window:
                    hits.append(dict(call_rva=hex(start+pos), **im.record(start+low,len(window)), bytes=window.hex()))
    row = dict(target='0x188b10', total_exact_rel32_candidates=total,
        filter='Raw +/-0x80-byte window contains encoded0x88; candidate receiver field relation only, no instruction claim.', hits=hits)
    save(SCRATCH / 'raw-tls-receiver-candidates.json', row)
    print(json.dumps(row))

def proof(im):
    prior={}
    inherited=[]
    for name,digest in (
        ('owner27','431f6a4cf5abbc28cae8029ad6bc3a506baeafeac55d46d00a22cd8e9f47b37c'),
        ('manager18','4ff54dcefc7901926869f376322a023b438a8f1ab1f3477afdd421579936a432')):
        path=ROOT/f'handoff/tasks/CHAT27/20261030-{name}/proof01.json'
        assert helper.digest(path.read_bytes())==digest
        prior[name]=json.loads(path.read_bytes())
        inherited.append(dict(path=str(path.relative_to(ROOT)),sha256=digest))
    reused=list(prior['owner27']['saved_decodes'])+list(prior['owner27']['reused_decodes'])
    reused.extend(r for r in prior['manager18']['saved_decodes'] if int(r['rva'],0)==0x188b10)
    starts=set()
    for row in reused:
        at=int(row['rva'],0)
        assert im.record(at,row['size'])['sha256']==row['sha256']
        raw=(ROOT/row['path']).read_bytes()
        assert helper.digest(raw)==row['decode_sha256']
        starts.update(int(m,16)-im.base for m in re.findall(r'^\s*([0-9A-Fa-f]{8}):',raw.decode('utf-8'),re.M))
    decodes=[]
    code_covered=set()
    data_covered=set()
    branch_count=0
    pattern=re.compile(r'^\s*([0-9A-Fa-f]{8}):\s+((?:[0-9A-Fa-f]{2} )+)(.*)$')
    for at,(size,meaning) in UNITS.items():
        path=SCRATCH/f'decode-{at:08x}-{size:x}.json'
        row=json.loads(path.read_bytes())
        assert row['size']==size and im.record(at,size)['sha256']==row['sha256']
        assert im.read(at,size).hex()==row['bytes']
        raw=(ROOT/row['path']).read_bytes()
        assert helper.digest(raw)==row['decode_sha256']
        instructions=[]
        for line in raw.decode('utf-8').splitlines():
            m=pattern.match(line)
            if m:
                instructions.append(dict(rva=int(m[1],16)-im.base,raw=bytes.fromhex(m[2]),asm=m[3].strip()))
            elif re.fullmatch(r'\s{10,}(?:[0-9A-Fa-f]{2}\s*)+',line):
                instructions[-1]['raw']+=bytes.fromhex(line.strip())
        local_starts={r['rva'] for r in instructions}
        assert len(instructions)==row['instruction_count']
        starts.update(local_starts)
        local=set()
        for inst in instructions:
            ia,encoded=inst['rva'],inst['raw']
            assert im.read(ia,len(encoded))==encoded
            assert at<=ia and ia+len(encoded)<=at+size
            local.update(range(ia,ia+len(encoded)))
            if inst['asm'].startswith('j'):
                match=re.fullmatch(r'j\w+\s+([0-9A-Fa-f]{8})',inst['asm'])
                assert match,inst
                target=int(match[1],16)-im.base
                assert target in local_starts,(hex(ia),hex(target))
                branch_count+=1
        code_covered.update(local)
        for item in row.get('inline_data',[]):
            ia=int(item['rva'],0)
            assert im.read(ia,item['size']).hex()==item['bytes']
            assert helper.digest(im.read(ia,item['size']))==item['sha256']
            assert ia-2 in local_starts and im.read(ia-2,2)==b'\xeb\x0e'
            data_covered.update(range(ia,ia+item['size']))
            local.update(range(ia,ia+item['size']))
        assert local==set(range(at,at+size))
        assert instructions[0]['rva']==at and instructions[-1]['asm'].startswith('ret')
        decodes.append(dict(**row,complete=True,meaning=meaning))
    expected={at+i for at,(size,_) in UNITS.items() for i in range(size)}
    assert len(UNITS)==8 and len(expected)==10027
    assert code_covered.isdisjoint(data_covered)
    assert code_covered|data_covered==expected and len(data_covered)==84
    def exact(at,encoded,meaning):
        raw=bytes.fromhex(encoded)
        assert at in starts and im.read(at,len(raw))==raw,hex(at)
        return dict(**im.record(at,len(raw)),bytes=encoded,meaning=meaning)
    pins=[exact(*entry) for entry in (
        (0x435c6d,'8bf1','C is incoming435c60 receiver preserved in ESI.'),
        (0x435caa,'8b8e88000000','Scheduled manager M is [C+0x88].'),
        (0x435cb3,'d95c2408d94508d9542404d91c24','3b5a00 gets arguments(arg1,arg1,arg4) from435c60.'),
        (0x16e3d2,'8bf1','P is incoming16e3c0 receiver preserved in ESI.'),
        (0x16eb2b,'8b7e34','C loaded from [P+0x34].'),
        (0x16eb49,'8bcf','Pass that same C as435c60 receiver.'),
        (0x16f596,'8bf1','Q is incoming16f580 receiver.'),
        (0x16fa25,'50','Pass3ff060() context G to TLS setter.'),
        (0x16fa74,'8b8eb4000000','Pass P=[Q+0xb4] to16e3c0.'),
        (0x188af4,'8b0da8715601','Setter uses the identical global TLS index as inherited188b10.'),
        (0x188afc,'ff1558534001','Exact imported TLS setter call.'),
        (0x3ff0d3,'b8d8135701','3ff060 returns G=preferredVA15713d8, RVA11713d8.'),
        (0x429b0f,'8bf1','Initializer C is incoming429b00 receiver.'),
        (0x429cb4,'898688000000','Store constructed manager result into C+0x88.'),
        (0x429ccc,'8bbe88000000','Reload exactly C+0x88 into EDI.'),
        (0x429cd7,'897814','After context getter, write that exact manager to G+0x14.'),
        (0x141236,'8bf1','Preparation P is incoming141230 receiver.'),
        (0x141302,'8bcf','Initialize retained C via429b00 receiver EDI.'),
        (0x14133d,'897e68','Retain exactly C at P+0x68.'),
        (0x141382,'8bf1','Publication P is incoming141380 receiver.'),
        (0x1413f0,'8b5668','Load retained C from P+0x68.'),
        (0x1413f3,'895634','Publish exactly C into P+0x34, the later scheduler source.'),
        (0x1413fe,'ffd2','Old nonnull P+0x34 is released through virtual slot0 after new publication.'),
        (0x141400,'8b4e34','Post-publication helper receives new C=[P+0x34].'),
        (0x141403,'c7466800000000','Clear retained pending P+0x68.'),
        (0x3b2385,'8d8e80000000','Inherited registration interface remains companion+0x80.'),
        (0x3b238c,'8b4814','Inherited registration receiver is [TlsGetValue(index)+0x14].'),
        (0x3b1829,'895c2438','Inherited manager registration retains interface pointer as record first dword.'),
    )]
    calls=[]
    for at,target,meaning in (
        (0x435cc1,0x3b5a00,'Sole original raw target now admitted as a complete-caller instruction.'),
        (0x16eb6c,0x435c60,'Forward C=[P+0x34].'),
        (0x16fa20,0x3ff060,'Get static context immediately before TLS install.'),
        (0x16fa26,0x188af0,'Install G in current thread TLS; return status not tested.'),
        (0x16fa98,0x16e3c0,'Invoke parent scheduler after TLS install.'),
        (0x429ca3,0x3b1770,'Manager constructor result is stored; body outside this investigation.'),
        (0x429cd2,0x3ff060,'Get exact context for manager assignment.'),
        (0x141304,0x429b00,'Initialize C before retaining it in parent+0x68.'),
        (0x14140a,0x3ff110,'Unresolved immediate post-publication helper; exact next edge.'),
        (0x3b2380,0x188b10,'Inherited TLS getter before first-use registration.'),
        (0x3b238f,0x3b1800,'Inherited manager registration.'),
    ):
        assert at in starts
        raw=im.read(at,5)
        assert raw[0]==0xe8 and at+5+struct.unpack_from('<i',raw,1)[0]==target
        calls.append(dict(**im.record(at,5),bytes=raw.hex(),target_rva=hex(target),meaning=meaning))
    imports=[im.import_at(0x1005354),im.import_at(0x1005358)]
    assert [(x['dll'].lower(),x['name']) for x in imports]==[('kernel32.dll','TlsGetValue'),('kernel32.dll','TlsSetValue')]
    scans=[]
    for path in sorted(SCRATCH.glob('raw-*.json')):
        row=json.loads(path.read_bytes())
        for item in row.get('calls',[])+row.get('pointers',[])+row.get('hits',[]):
            assert im.record(int(item['rva'],0),item['size'])['sha256']==item['sha256']
        scans.append(dict(path=str(path.relative_to(ROOT)),sha256=helper.digest(path.read_bytes()),status='Raw reference candidates only; only admitted complete-function instructions support claims.'))
    return dict(schema='chat27.cinematic_scheduler_receiver_static.v1',
        evidence_level='Offline file-backed static dataflow; no native execution or headset acceptance.',
        supplied_head='6c42c3fd6d39715d47a07584061697b2be8a678e',git_queried=False,
        executable_sha256=helper.SHA,preferred_base=hex(im.base),
        script_sha256=helper.digest(Path(__file__).read_bytes()),
        helper=dict(path=str(SOURCE.relative_to(ROOT)),sha256=helper.digest(SOURCE.read_bytes())),
        inherited=inherited,reused_decodes=reused,
        scope=dict(preferred_complete_functions=4,maximum_new_complete_functions=8,new_complete_functions=8,partial_functions=0,
            function_range_bytes=10027,new_unique_decoded_bytes=len(code_covered),inline_data_bytes=84,
            maximum_decoded_bytes=65536,adjacent_spill_functions=0,branch_targets_validated=branch_count,ceiling_met=True,
            expansion_reason='Exact receiver provenance required TLS setter, outer scheduler/context getter, initializer and parent retention/publication paths. No descendant evaluators or generic camera search.'),
        saved_decodes=decodes,exact_instructions=pins,direct_calls=calls,tls_imports=imports,raw_reference_searches=scans,
        findings=dict(
            original_candidate_admitted=True,
            exact_scheduled_receiver='At435cc1, M=[C+0x88]. At16eb6c, C=[P+0x34]. On admitted16fa98 route, P=[Q+0xb4];16fa20/16fa26 install G=RVA11713d8 into the inherited TLS index before16e3c0.',
            initialization_assignment_join=True,
            assignment='429b00 stores its manager result at C+0x88, reloads the same value, calls3ff060 and writes it to G+0x14 at429cd7.141230 passes its retained C into429b00 and stores C at P+0x68.141380 moves that same P+0x68 to P+0x34, which16e3c0 later supplies to435c60.',
            registration_join='Inherited companion registration uses188b10()/TlsGetValue(same index), reads context+0x14, and passes companion+0x80 to3b1800. Thus the decoded initialization assignment is exactly the field source selected by the decoded scheduler, conditional on the same published C/P lifetime and the context retaining that assignment.',
            scheduler_instance_join_complete=False,
            reason='Publication writes new P+0x34, invokes old-object virtual slot0 if nonnull, then calls3ff110(new C). That helper is not decoded. The creation-time G+0x14 assignment alone cannot prove its value after publication/replacement or at registration/update. TLS setter success is not checked by caller. No execution proves matching thread, object generation, registration record or route.',
            inherited_original_queue='Owner27 already proves conditional retained-record route3b61fa -> interface+14 ->3b23c0/39cd30 with B=Pqueue+0x8e0, then3b620d ->b6bb00. Reused without new decoding; its branch predicates remain applicable.',
            limits='No selected local-player view, matrix composition, HUD, entry/skip/exit, gameplay-return, continuous head rotation or source correction claim. Transform29 evaluator functions and descendants were not decoded.'),
        next_unresolved_edge=dict(callsite_rva='0x14140a',target_rva='0x3ff110',receiver='new C=[publication P+0x34]',
            required='Admit complete3ff110 and determine its exact G+0x14 and C+0x88 effects after old-object release, closing or rejecting the creation/publication-to-scheduling manager identity.'),
        verification='Eight complete ranges verified against the pinned executable; six explicitly jumped inline data islands excluded from instruction decoding. All internal direct branch targets land on admitted instruction starts. Inherited/new byte hashes, saved decode hashes, exact calls and both TLS imports verified. Initial proof assembly rejected a one-byte-mislocated instruction pin16fa73; corrected to saved instruction16fa74 without any additional decode.',
        execution=dict(production_writes=False,git=False,builds=False,tests=False,game=False,process_access=False,input=False,xr=False,profiles=False,installs=False,packages=False,subagents=False),frozen=True)

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--exe', required=True, type=Path)
    parser.add_argument('--data', type=lambda x:int(x,0))
    parser.add_argument('--size', type=lambda x:int(x,0), default=0x200)
    parser.add_argument('--complete', type=lambda x:int(x,0))
    parser.add_argument('--references', type=lambda x:int(x,0))
    parser.add_argument('--tls-candidates', action='store_true')
    parser.add_argument('--context-writes', action='store_true')
    parser.add_argument('--output',type=Path)
    args = parser.parse_args()
    SCRATCH.mkdir(parents=True, exist_ok=True)
    for key in ('TEMP', 'TMP', 'TMPDIR'):
        os.environ[key] = str(SCRATCH)
    im = helper.Image(args.exe)
    if args.output is not None:
        assert args.output.resolve()==Path(__file__).resolve().parent/'proof01.json'
        result=proof(im)
        digest=save(args.output,result)
        print(json.dumps(dict(path=str(args.output.resolve()),sha256=digest,scope=result['scope'],findings=result['findings'],next=result['next_unresolved_edge'])))
    elif args.data is not None:
        assert 0 < args.size <= 0x1000
        raw = im.read(args.data, args.size)
        for i in range(0, len(raw), 16):
            print(f'{args.data+i:08x} {raw[i:i+16].hex(" ")}')
    elif args.complete is not None:
        complete(im, args.complete, UNITS[args.complete][0])
    elif args.references is not None:
        references(im, args.references)
    elif args.tls_candidates:
        tls_candidates(im)
    elif args.context_writes:
        context_writes(im)

if __name__ == '__main__':
    main()
