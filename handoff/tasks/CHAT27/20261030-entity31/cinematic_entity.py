"""Bounded, file-only CHAT27 entity callback inspection. Never opens a process."""
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
HERE = Path(__file__).resolve().parent
SCRATCH = ROOT / "reports/coordinator-20261030-cinematic-entity31"
EXE_SHA = "3911fcc8914b0158c434cee36f19295316e715d4c9a0e70c857e8c39e3d54258"
PRIOR = "handoff/tasks/CHAT27/20261030-methods17/proof01.json"
PRIOR_SHA = "af327be2ac45a67eaa428b99a559c9f9c596caa79922f47ed889da624cc491b8"
WORK14 = "handoff/tasks/CHAT27/20261030-work14/proof01.json"
WORK14_SHA = "b5db47e429185f0a5ebe9d37986fe7ec4c4cb40de746bcd9f974f482147101d3"
DUMPBIN = Path(r"C:\Program Files\Microsoft Visual Studio\18\Community\VC\Tools\MSVC\14.51.36231\bin\HostX64\x64\dumpbin.exe")
FORBIDDEN = {0x435cc1, 0x3b5a00, 0x3ff060, 0xb5d3f0, 0xb5d310, 0xb59460, 0xb5a380}

def sha(raw):
    return hashlib.sha256(raw).hexdigest()

def save(path, value):
    path.write_text(json.dumps(value, indent=2) + "\n", encoding="utf-8")

class Image:
    def __init__(self, path):
        self.path = path.resolve()
        self.data = self.path.read_bytes()
        assert sha(self.data) == EXE_SHA, "Executable identity mismatch"
        pe = struct.unpack_from("<I", self.data, 0x3c)[0]
        assert self.data[pe:pe+4] == b"PE\0\0"
        count = struct.unpack_from("<H", self.data, pe+6)[0]
        opts = struct.unpack_from("<H", self.data, pe+20)[0]
        optional = pe+24
        assert struct.unpack_from("<H", self.data, optional)[0] == 0x10b
        self.base = struct.unpack_from("<I", self.data, optional+28)[0]
        assert self.base == 0x400000
        self.sections = []
        for index in range(count):
            at = optional + opts + index*40
            virtual, rva, rawsize, raw = struct.unpack_from("<4I", self.data, at+8)
            flags = struct.unpack_from("<I", self.data, at+36)[0]
            self.sections.append((rva, rawsize, raw, flags))
    def read(self, rva, size):
        for start, count, raw, flags in self.sections:
            if start <= rva and rva+size <= start+count:
                return self.data[raw+rva-start:raw+rva-start+size]
        raise ValueError(hex(rva))
    def executable(self, rva):
        return any(start <= rva < start+count and flags & 0x20000000 for start,count,raw,flags in self.sections)
    def record(self, rva, size):
        raw = self.read(rva,size)
        return dict(rva=hex(rva),size=size,bytes=raw.hex(),sha256=sha(raw))

def inherited(im):
    raw = (ROOT/PRIOR).read_bytes()
    assert sha(raw) == PRIOR_SHA
    prior = json.loads(raw)
    raw = (ROOT/WORK14).read_bytes()
    assert sha(raw) == WORK14_SHA
    work = json.loads(raw)
    entity = next(row for row in work["families"] if row["name"] == "ClientCutsceneEntity")
    for key in ("setup", "cleanup"):
        row=entity[key]
        assert sha(im.read(int(row["rva"],0),row["size"])) == row["sha256"]
    assert im.read(0x4af877,10).hex() == "c786dc00000050664301"
    table = next(row for row in prior["tables"] if row["rva"] == "0x1036650")
    assert sha(im.read(0x1036650,table["size"])) == table["sha256"]
    assert struct.unpack("<I", im.read(0x1036654,4))[0] == im.base+0x495a90
    return prior, entity

def decode_instruction(im, rva):
    """Increase the exact range until one instruction completes; never decode its successor."""
    assert im.executable(rva)
    for size in range(1,16):
        result = subprocess.run([str(DUMPBIN),"/NOLOGO","/DISASM:BYTES",f"/RANGE:0x{im.base+rva:x},0x{im.base+rva+size-1:x}",str(im.path)],capture_output=True,check=True)
        output = result.stdout.decode("utf-8",errors="replace")
        line = next((line for line in output.splitlines() if re.match(rf"\s*{im.base+rva:08X}:",line)),None)
        if line is None:
            continue
        match = re.match(r"\s*[0-9A-F]+:\s+((?:[0-9A-F]{2}(?:\s+|$))+)(.*)",line)
        if match and match.group(2).strip():
            raw = im.read(rva,size)
            encoded = bytes.fromhex(match.group(1))
            # dumpbin wraps instruction byte columns after seven bytes.
            if len(encoded) < size:
                remaining = size-len(encoded)
                tail = next((row.strip() for row in output.splitlines() if re.fullmatch(r"\s*(?:[0-9A-F]{2}\s*)+",row)),"")
                encoded += bytes.fromhex(tail)
            assert encoded == raw, (hex(rva),size,line)
            text = match.group(2).strip()
            mnemonic = text.split()[0]
            assert mnemonic not in ("???", "db"), (hex(rva),text)
            return dict(**im.record(rva,size),text=text,mnemonic=mnemonic)
    raise AssertionError(f"No complete instruction at {rva:x}")

def direct_target(row):
    raw=bytes.fromhex(row["bytes"])
    at=int(row["rva"],0)
    if raw[0] in (0xe8,0xe9):
        return at+len(raw)+struct.unpack_from("<i",raw,1)[0]
    if raw[0] == 0xeb or 0x70 <= raw[0] <= 0x7f or raw[0] in (0xe0,0xe1,0xe2,0xe3):
        return at+len(raw)+struct.unpack_from("<b",raw,1)[0]
    if raw[:1] == b"\x0f" and 0x80 <= raw[1] <= 0x8f:
        return at+len(raw)+struct.unpack_from("<i",raw,2)[0]
    return None

def decode_function(im, start):
    prior, entity = inherited(im)
    assert start not in FORBIDDEN
    prior_starts = {int(row["rva"],0) for row in prior["units"]}
    assert start not in prior_starts, "Do not repeat methods17 decodes"
    existing = sorted(SCRATCH.glob("function-*.json"))
    assert len(existing) < 4, "Preferred four-function bound reached"
    if start != 0x495a90:
        known = {int(edge["target_rva"],0) for path in existing for edge in json.loads(path.read_text())["direct_calls"]}
        assert start in known, "Only a proven direct edge may extend this chain"
    destination = SCRATCH/f"function-{start:08x}.json"
    assert not destination.exists(), "Never overwrite a saved decode"
    pending=[start]
    rows={}
    calls=[]
    stops=[]
    while pending:
        at=pending.pop()
        if at in rows:
            continue
        assert start <= at < start+0x1000, "Branch escaped the bounded function; manual review required"
        assert at not in FORBIDDEN and at not in prior_starts
        row=decode_instruction(im,at)
        rows[at]=row
        save(SCRATCH/f"progress-{start:08x}.json",dict(entry_rva=hex(start),instructions=[rows[key] for key in sorted(rows)],pending_rvas=[hex(item) for item in pending],last_rva=hex(at)))
        if len(rows) % 20 == 0:
            print(f"Progress {start:#x}: {len(rows)} reachable instructions, latest {at:#x}",flush=True)
        assert sum(item["size"] for item in rows.values()) <= 0x1000
        mnemonic=row["mnemonic"]
        target=direct_target(row)
        after=at+row["size"]
        if mnemonic == "call" and target is not None:
            calls.append(dict(caller_rva=hex(at),target_rva=hex(target),bytes=row["bytes"]))
        if mnemonic.startswith("ret") or mnemonic in ("int", "int3", "ud2"):
            stops.append(dict(rva=hex(at),kind=mnemonic))
            continue
        if mnemonic.startswith("j") or mnemonic.startswith("loop"):
            assert target is not None, "Indirect branch requires a separate bounded review"
            pending.append(target)
            if mnemonic == "jmp":
                continue
        pending.append(after)
    instructions=[rows[at] for at in sorted(rows)]
    occupied=set()
    for row in instructions:
        span=set(range(int(row["rva"],0),int(row["rva"],0)+row["size"]))
        assert not occupied.intersection(span), "Overlapping decode"
        occupied.update(span)
    result=dict(entry_rva=hex(start),instruction_count=len(instructions),reachable_byte_count=len(occupied),start_rva=hex(min(occupied)),end_exclusive_rva=hex(max(occupied)+1),instructions=instructions,direct_calls=sorted(calls,key=lambda row:int(row["caller_rva"],0)),terminal_instructions=stops,complete_reachable_cfg=True,adjacent_function_units=0)
    save(destination,result)
    text="\n".join(f"{row['rva']:>10}: {row['bytes']:<30} {row['text']}" for row in instructions)+"\n"
    (SCRATCH/f"function-{start:08x}.txt").write_text(text,encoding="utf-8")
    print(text)
    print(json.dumps({key: value for key,value in result.items() if key != "instructions"}))

def proof(im):
    prior, entity = inherited(im)
    shared_path=ROOT/"reports/coordinator-20261030-cinematic-scheduler30/decode-003ff060-79.json"
    shared_raw=shared_path.read_bytes()
    shared=json.loads(shared_raw)
    assert shared["rva"] == "0x3ff060" and shared["size"] == 0x79
    assert shared["sha256"] == "89422b84c64640424675adfa046919caa1ee166f691478a4e5bb00ff27fc3e57"
    assert sha(im.read(0x3ff060,0x79)) == shared["sha256"]
    assert im.read(0x3ff060,0x79).hex() == shared["bytes"]
    shared_text_path=shared_path.with_suffix(".txt")
    shared_text=shared_text_path.read_bytes()
    assert sha(shared_text) == "6680d43785c778a6266ed20cdc1b5b034d13bbb278ad47973239ddd9260ccc0d"
    assert im.read(0x3ff0d3,6).hex() == "b8d8135701c3"
    units=[]
    all_instructions=[]
    all_calls=[]
    artifacts=[]
    for start,size in ((0x495a90,121),(0x3b8cc0,6),(0x486210,46)):
        path=SCRATCH/f"function-{start:08x}.json"
        raw=path.read_bytes()
        saved=json.loads(raw)
        assert saved["entry_rva"] == hex(start)
        assert saved["reachable_byte_count"] == size
        assert saved["complete_reachable_cfg"] and saved["adjacent_function_units"] == 0
        assert int(saved["end_exclusive_rva"],0) == start+size
        occupied=set()
        for row in saved["instructions"]:
            at=int(row["rva"],0)
            encoded=im.read(at,row["size"])
            assert encoded.hex() == row["bytes"] and sha(encoded) == row["sha256"]
            occupied.update(range(at,at+row["size"]))
        assert occupied == set(range(start,start+size))
        for call in saved["direct_calls"]:
            instruction=next(row for row in saved["instructions"] if row["rva"] == call["caller_rva"])
            assert direct_target(instruction) == int(call["target_rva"],0)
        units.append(dict(**im.record(start,size),instruction_count=saved["instruction_count"],complete_reachable_cfg=True,adjacent_function_units=0))
        all_instructions.extend(saved["instructions"])
        all_calls.extend(saved["direct_calls"])
        artifacts.append(dict(path=path.relative_to(ROOT).as_posix(),size=len(raw),sha256=sha(raw)))
        text_path=path.with_suffix(".txt")
        text_raw=text_path.read_bytes()
        artifacts.append(dict(path=text_path.relative_to(ROOT).as_posix(),size=len(text_raw),sha256=sha(text_raw)))
    assert len(all_instructions) == 60 and sum(row["size"] for row in units) == 173
    # Independent byte assertions pin every material ABI/filter/owner/effect claim.
    checks=(
        (0x495a90,"8b442404"),(0x495a94,"83f81f"),(0x495a9c,"837c240c2f"),
        (0x495ab1,"807e2400"),(0x495ab7,"85c0"),(0x495ac2,"6a39"),
        (0x495ac6,"84c0"),(0x495aca,"8b8e30ffffff"),(0x495ad0,"80b9cd00000000"),
        (0x495ad9,"83f812"),(0x495ade,"837c240c1b"),(0x495ae5,"807e2400"),
        (0x495aeb,"8b9630ffffff"),(0x495af1,"80bacd00000000"),
        (0x495afa,"8d8e24ffffff"),(0x495b06,"c21000"),
        (0x3b8cc0,"a1900e5701"),(0x486218,"8dbedc000000"),(0x486227,"8b4818"),
        (0x48622c,"8b5010"),(0x48622f,"57"),(0x486230,"6a1f"),(0x486232,"ffd2"),
        (0x486235,"c6860001000001"),(0x48623d,"c3"))
    for at,encoded in checks:
        assert im.read(at,len(bytes.fromhex(encoded))).hex() == encoded
    assert 0xdc+0x24 == 0x100 and 0xdc-0xd0 == 0xc and 0xdc-0xdc == 0
    return dict(
        schema="chat27.cinematic_entity_callback_static.v1",
        evidence_level="offline file-backed code only",
        executable=dict(path=str(im.path),sha256=EXE_SHA,preferred_base=hex(im.base)),
        script_sha256=sha(Path(__file__).read_bytes()),
        supplied_worktree_head="6c42c3fd6d39715d47a07584061697b2be8a678e",
        git_invoked=False,
        inherited=dict(
            methods17=dict(path=PRIOR,sha256=PRIOR_SHA,scope_failure_preserved=prior["scope"],verifier_failure_preserved=prior["verifier_failure_retained"]),
            work14=dict(path=WORK14,sha256=WORK14_SHA,entity_setup=entity["setup"],entity_cleanup=entity["cleanup"]),
            scheduler30_getter=dict(receipt_path=shared_path.relative_to(ROOT).as_posix(),receipt_sha256=sha(shared_raw),decode_path=shared_text_path.relative_to(ROOT).as_posix(),decode_sha256=sha(shared_text),function=im.record(0x3ff060,0x79),decoded_here=False)),
        scope=dict(new_complete_functions=3,new_instruction_count=60,new_reachable_code_bytes=173,preferred_maximum_functions=4,maximum_functions=8,maximum_code_bytes=65536,adjacent_spill_functions=0,ceiling_met=True,methods17_functions_redecoded=0,scope_frozen=True,
            decode_method="Dumpbin instruction-exact ranges increased only until one instruction completed; follow reachable intra-function branches and stop at returns; never decode call targets implicitly or use adjacent table entries as boundaries.",
            initial_boundary_probe="Before the verifier run, exact RVA0x495a90 was shown as one incomplete byte and then its complete four-byte entry instruction; no second function exposed. Subsequent EXE/function/instruction hash verification covers those bytes."),
        ownership=dict(family="ClientCutsceneEntity (inherited work14 identification)",setup_assignment=im.record(0x4af877,10),assigned_interface_offset="0xdc",table=im.record(0x1036650,12),slot_offset="0x4",slot=im.record(0x1036654,4),callback_rva="0x495a90",derivation="Exact setup assignment and hash-pinned table word; no primary-table adjacency inference."),
        units=units,
        instructions=all_instructions,
        direct_calls=all_calls,
        callback_abi=dict(entry="0x495a90",incoming_ecx="S=whole owner O+0xdc",stack="Four 32-bit stack slots are removed by ret0x10; only original slots1 and2 are read. No stack slot is forwarded to0x486210.",convention="x86 ECX receiver plus callee-cleaned16 stack bytes; thiscall-compatible, exact source types unproved",owner_conversion=dict(instruction_rva="0x495afa",operation="ECX=S-0xdc=O",direct_call_rva="0x495b00",target_rva="0x486210"),return_value="No normalized return value; do not declare bool semantics."),
        callback_paths=[
            dict(discriminators=["0x1f","0x2f"],operations_in_order=[
                "0x3b8cc0 returns P=dword[VA0x1570e90 / RVA0x1170e90].",
                "Call P.vtable+0x38 with ECX=P and no stack arguments, yielding Q; P is dereferenced without a local null check.",
                "Require byte[O+0x100]==0 and Q!=0.",
                "Call Q.vtable+0x14 with ECX=Q and stack argument0x39; require AL!=0. Caller assumes callee cleanup of four bytes.",
                "Read R=dword[O+0xc]; require byte[R+0xcd]!=0. R has no local null check.",
                "Call0x486210 with ECX=O, no explicit stack arguments."]),
            dict(discriminators=["0x12","0x1b"],operations_in_order=[
                "Require byte[O+0x100]==0.",
                "Read R=dword[O+0xc]; require byte[R+0xcd]!=0.",
                "Call0x486210 with ECX=O, no explicit stack arguments."]),
        ],
        effect_target=dict(entry="0x486210",abi="ECX=O, no explicit stack arguments, plain ret; preserves ESI/EDI",operations_in_order=[
            "EDI=O+0xdc when O!=0, otherwise EDI=0; this is not overall null safety because O+0x100 is later written.",
            "Call0x3ff060, then load B=dword[EAX+0x18]. Reused scheduler30 evidence proves EAX=preferred VA0x15713d8, hence B=dword[preferred VA0x15713f0 / RVA0x11713f0].",
            "Call B.vtable+0x10 with ECX=B, arg1=0x1f, arg2=EDI; caller assumes callee cleanup of eight bytes.",
            "After the virtual call returns normally, write byte[O+0x100]=1 at0x486235, then return."],
            direct_nonstack_store="Only byte[O+0x100]=1 in the three newly decoded functions; opaque virtual callees may have additional effects.",semantic_limit="The byte is an eligibility gate set after manager dispatch; no authored name, registration/removal role, camera-selection or lifecycle meaning is assigned."),
        getter_3ff060=dict(decoded_here=False,evidence_status="Reused complete exact saved scheduler30 getter; all bytes and decode/receipt hashes verified without disassembly.",returns="Both paths return preferred VA0x15713d8 / RVA0x11713d8.",lazy_path="If flag bit0 at preferred VA0x1571434 is clear, set it, call0x188a50 with ECX=0x15713d8 and stack argument1, install vtableVA0x1431530 at the singleton, zero offsets0x28/0x2c/0x34..0x58, and call0x852315 with stack value0x1382ed0. These targets were not decoded here.",field_limit="The getter does not establish which concrete object backs singleton+0x18; service+0x18 is not selected-camera authority."),
        indirect_calls=[
            dict(rva="0x495aaf",receiver="P=dword[VA0x1570e90]",vtable_offset="0x38",stack_arguments=[],return_use="Q subsequently null-checked",target_resolved=False),
            dict(rva="0x495ac4",receiver="Q=previous virtual return",vtable_offset="0x14",stack_arguments=["0x39"],return_use="AL must be nonzero",target_resolved=False),
            dict(rva="0x486232",receiver="B=dword[return(0x3ff060)+0x18]",vtable_offset="0x10",stack_arguments=["0x1f","O+0xdc"],return_use="Not inspected before owner flag store",target_resolved=False),
        ],
        next_exact_edge=dict(callsite_rva="0x3ff079",target_rva="0x188a50",receiver="ECX=preferred VA0x15713d8",arguments=["1"],question="Does this singleton initializer establish field+0x18 and its owned vtable, resolving virtual+0x10 at0x486232? No decode performed here.",motivating_unresolved_call=dict(callsite_rva="0x486232",expression="dword[dword[dword[preferred VA0x15713f0]]+0x10]",arguments=["0x1f","O+0xdc"])),
        preserved_failures=dict(methods17="Original ten-unit/four-selected/six-spill scope failure and earlier transcription assertion remain unchanged; this receipt does not replace them.",entity31="No failed decode or proof verification. One auxiliary rg lookup used an unsupported Windows glob and failed before reading files; no evidence conclusion depended on it."),
        limitations=["Discriminators0x1f/0x2f/0x12/0x1b and selector0x39 have no established semantic names.","Owner+0x100 and resource+0xcd have no admitted metadata-field names.","Global getter3b8cc0 does not by itself prove local-player or active-view identity.","Opaque virtual calls are unresolved; no selected-view/HUD/camera correction claim, runtime field access, source adapter, live ownership/generation, scheduling, blend, or headset qualification."],
        execution=dict(production_writes=False,game=False,process_access=False,input=False,xr=False,git=False,builds=False,tests=False,install=False,subagents=False),
        saved_decodes=artifacts,
        verification="File-only executable/prior/setup/cleanup/table/complete-function/instruction hash and exact direct-target verification; no decoder invocation during proof generation.")

def main():
    parser=argparse.ArgumentParser()
    parser.add_argument("--exe",type=Path,required=True)
    parser.add_argument("--decode",type=lambda value:int(value,0))
    parser.add_argument("--proof",action="store_true")
    args=parser.parse_args()
    SCRATCH.mkdir(parents=True,exist_ok=True)
    for key in ("TEMP","TMP","TMPDIR"):
        os.environ[key]=str(SCRATCH)
    im=Image(args.exe)
    inherited(im)
    if args.decode is not None:
        decode_function(im,args.decode)
    if args.proof:
        destination=HERE/"proof01.json"
        assert not destination.exists(), "Never overwrite a finalized proof"
        result=proof(im)
        save(destination,result)
        raw=destination.read_bytes()
        print(json.dumps(dict(path=str(destination),size=len(raw),sha256=sha(raw),new_functions=3,new_bytes=173,adjacent_spills=0)))

if __name__ == "__main__":
    main()
