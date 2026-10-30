"""Bounded file-only follow-up to work14's direct service helper."""
from __future__ import annotations
import argparse
import ast
import hashlib
import json
from pathlib import Path
import re
import struct
import subprocess

ROOT = Path(__file__).resolve().parents[4]
SCRATCH = ROOT / "reports/coordinator-20261030-implementation17/cinematic"
EXE_SHA = "3911fcc8914b0158c434cee36f19295316e715d4c9a0e70c857e8c39e3d54258"
PRIOR_SHA = "b5db47e429185f0a5ebe9d37986fe7ec4c4cb40de746bcd9f974f482147101d3"
READER_SHA = "94db343a4d568c5c6c784aa70b74a508ab936187f63eb95c778359a15294ca88"
DECODE_RANGES = {(0x826900, 0x8B), (0x3FF720, 0x2B), (0x823ED0, 0x107)}

def sha(data):
    return hashlib.sha256(data).hexdigest()

def require(ok, message):
    if not ok:
        raise ValueError(message)

def load(exe):
    prior_raw = (ROOT / "handoff/tasks/CHAT27/20261030-work14/proof01.json").read_bytes()
    require(sha(prior_raw) == PRIOR_SHA, "Prior proof changed")
    reader = ROOT / "tools/capture_reload_state.py"
    raw = reader.read_bytes()
    require(sha(raw) == READER_SHA, "File reader changed")
    node = next(n for n in ast.parse(raw).body if isinstance(n, ast.ClassDef) and n.name == "Image")
    scope = dict(Path=Path, re=re, struct=struct)
    exec(compile(ast.Module(body=[node], type_ignores=[]), str(reader), "exec"), scope)
    im = scope["Image"](exe)
    require(sha(im.data) == EXE_SHA, "Executable changed")
    return im

def refs(im, rva):
    require(rva in (0x826900, 0x826990, 0x3FF720), "Exact reference target not in bounded cone")
    result = dict(absolute=[], direct_branch_byte_candidates=[])
    pattern = struct.pack("<I", im.base+rva)
    for start, _, raw, size, flags in im.sections:
        data = im.data[raw:raw+size]
        at = data.find(pattern)
        while at >= 0:
            result["absolute"].append(dict(rva=hex(start+at), executable=bool(flags & 0x20000000)))
            at = data.find(pattern, at+1)
        if flags & 0x20000000:
            for match in re.finditer(b"[\xe8\xe9]", data):
                at = match.start()
                if at+5 <= len(data) and start+at+5+struct.unpack_from("<i", data, at+1)[0] == rva:
                    result["direct_branch_byte_candidates"].append(dict(rva=hex(start+at), opcode=hex(data[at])))
    require(sum(len(v) for v in result.values()) <= 128, "Reference cap")
    return result

def exact(im, rva, expected):
    raw = bytes.fromhex(expected)
    require(im.read(rva, len(raw)) == raw, "Instruction mismatch at " + hex(rva))

def call(im, rva, target):
    raw = im.read(rva, 5)
    require(raw[0] == 0xE8 and rva+5+struct.unpack_from("<i", raw, 1)[0] == target,
        "Direct call mismatch at " + hex(rva))

def record(im, rva, size, reason):
    flags = im.section(rva, size)[4]
    require(flags & 0x20000000 and not flags & 0x80000000, "Expected readonly executable bytes")
    return dict(rva=hex(rva), size=size, sha256=sha(im.read(rva, size)), reason=reason)

def proof(im):
    # Revalidate only the NEW cone; the eight work14 units are inherited by hash.
    exact(im, 0x826900, "668B41208B542404566689420C8B4108BE0100000066017120")
    exact(im, 0x82691D, "8950088B4108894204EB03895104")
    exact(im, 0x82692B, "6683792000895108751F8B410485C07418578D4900")
    exact(im, 0x826940, "668B79206689780C660171208B400885C075ED5F")
    exact(im, 0x826954, "8B41102B410C83C10CC1F8026689420E8B41043B41087310")
    exact(im, 0x82696C, "85C08D7004897104741189105EC20400")
    exact(im, 0x82697C, "8D5424085250")
    call(im, 0x826982, 0x823ED0)
    exact(im, 0x826987, "5EC20400")
    exact(im, 0x826990, "8B44240450")
    call(im, 0x826995, 0x826900)
    exact(im, 0x82699A, "C20800")
    exact(im, 0x3FF720, "85C95674138D714C")
    call(im, 0x3FF728, 0x3FF060)
    exact(im, 0x3FF72D, "8B484056")
    call(im, 0x3FF731, 0x826900)
    exact(im, 0x3FF736, "5EC333F6")
    call(im, 0x3FF73A, 0x3FF060)
    exact(im, 0x3FF73F, "8B484056")
    call(im, 0x3FF743, 0x826900)
    exact(im, 0x3FF748, "5EC3")
    exact(im, 0x823ED0, "5153568BF18B46043B460857744A")
    exact(im, 0x823F16, "8B1383C40C8917834604045F5E5B59C20800")
    exact(im, 0x823F8E, "8B4C241C8B118910")
    exact(im, 0x823FBF, "8B442410892E8D4C85005D897E045F894E085E5B59C20800")
    helper_refs = refs(im, 0x826900)
    require(not helper_refs["absolute"] and len(helper_refs["direct_branch_byte_candidates"]) == 36,
        "Helper reference set changed")
    wrapper_refs = refs(im, 0x826990)
    require(not any(wrapper_refs.values()), "Wrapper reference set changed")
    caller_refs = refs(im, 0x3FF720)
    require(caller_refs == dict(absolute=[], direct_branch_byte_candidates=[dict(rva="0x15d23d", opcode="0xe8")]),
        "Immediate caller reference set changed")
    decoded = []
    for rva, size in sorted(DECODE_RANGES):
        path = SCRATCH / f"decode-{rva:08x}-{size:x}.txt"
        raw = path.read_bytes()
        require(b"File Type: EXECUTABLE IMAGE" in raw, "Missing saved file-only decode")
        decoded.append(dict(path=str(path.relative_to(ROOT)), size=len(raw), sha256=sha(raw)))
    return dict(schema="chat27.cinematic_service_static_code.v1", evidence_level="offline file-backed code",
        executable_sha256=EXE_SHA, preferred_base=hex(im.base), prior_work14_proof_sha256=PRIOR_SHA,
        reader_sha256=READER_SHA, script_sha256=sha(Path(__file__).read_bytes()),
        scope=dict(primary_helper="0x826900", admitted_immediate_units=3, additional_boundary_spill_units=1,
            total_new_function_units_observed=5, prior_eight_units_reexecuted=False, tables_walked=0,
            note="Initial 0x130-byte decode crossed the helper's return and exposed wrapper0x826990 and adjacent0x8269a0. Wrapper is an exact caller; adjacent function is counted as observed but is not admitted ownership evidence. Subsequent decode ranges end at the known function bounds."),
        units=[record(im, 0x826900, 0x8B, "Assigned exact helper called from work14 entity setup."),
            record(im, 0x826990, 0xD, "Immediate exact caller at0x826995; no absolute or direct branch references to wrapper found."),
            record(im, 0x3FF720, 0x2A, "Immediate exact caller sites0x3ff731/0x3ff743; same admitted context+0x40 receiver."),
            record(im, 0x823ED0, 0x107, "Only direct callee of primary helper, at0x826982; pointer-array insertion/growth."),
            record(im, 0x8269A0, 0x90, "Initial boundary spill only. Not used to infer selection, lifetime, or owner type.")],
        helper_references=helper_refs, wrapper_references=wrapper_refs, caller_references=caller_refs,
        reference_limits="Only decoded call instructions are verified callers. Other byte candidates remain unwalked; absolute/direct scan absence does not exclude all indirect references.",
        static_edges=[
            "Let S be receiver=[context+0x40] and P be work14's entity+0x98. Helper copies word[S+0x20] to word[P+0x0c], then increments the 16-bit service value.",
            "For nonzero prior tail[S+8], helper writes [tail+8]=P and [P+4]=tail; otherwise [S+4]=P. It then sets [S+8]=P. On serial wrap to zero it walks [S+4] via node+8 and rewrites node+0x0c serials.",
            "Helper writes word[P+0x0e]=([S+0x10]-[S+0x0c])/4. If end[S+0x10] is below capacity[S+0x14], it advances end by4 and writes P at the old end. Otherwise it calls0x823ed0 with receiver S+0x0c, insertion position=end, and address of the stack P argument.",
            "Callee0x823ed0 inserts a4-byte value, optionally allocates/copies storage, then publishes begin/end/capacity. It does not resolve a player, view, camera, or selected owner.",
            "Exact immediate caller0x3ff720 passes incoming this+0x4c to the same context+0x40 helper on its nonnull branch. This offset differs from work14's entity+0x98; no type for the incoming this is asserted.",
            "Wrapper0x826990 forwards its first stack argument to0x826900 and returns popping8 bytes. It establishes no selection relationship."
        ],
        safe_inference="The helper establishes registration/list and pointer-array membership. Its serial and array-index writes are not evidence of a selected camera, semantic generation token, local-player ownership, or view authority.",
        source_implementation=dict(supported_correction=False, files_modified=[], runtime_admission_enabled=False,
            reason="No admitted local-player/view -> selected cinematic owner edge. The shared registry cannot support an unconditional node-minus0x98 owner conversion; a second exact caller supplies this+0x4c."),
        precise_missing_edge="From the mixed-offset registry at [context+0x40]+0x0c, prove which node is a currently selected cinematic instance, its verified owner/subobject conversion, and the authoritative join to the admitted local player or active view/request. Neither registration membership nor 16-bit serial/index chooses that node.",
        proof_limits="Static file evidence only. No full service type, deregistration/lifetime, synchronization, local-player scope, blend authority, current camera, or entry/exit/skip behavior is established. No runtime read chain/signature prepared.",
        saved_decodes=decoded,
        failed_attempt_retained="Initial decoder invocation omitted0x in /RANGE values and returned1147 before saving output; corrected invocation succeeded. No game execution occurred.",
        game_process_access=False, desktop_checks=False, builds=False, tests_executed=False,
        package_profile_install_changes=False, archive_survey=False,
        stop_reason="Primary helper and three immediate related functions resolve to shared node registration/container insertion; one initial boundary spill is disclosed and counted. No source correction is provable within this cone. Stopped without spending the remaining function allowance.")

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--exe", type=Path, required=True)
    ap.add_argument("--decoder", type=Path)
    ap.add_argument("--decode", type=lambda v: int(v, 0), nargs=2)
    ap.add_argument("--refs", type=lambda v: int(v, 0))
    ap.add_argument("--caller-boundary", type=lambda v: int(v, 0))
    ap.add_argument("--end-boundary", type=lambda v: int(v, 0))
    ap.add_argument("--output", type=Path)
    args = ap.parse_args()
    im = load(args.exe)
    if args.decode:
        rva, size = args.decode
        require((rva, size) in DECODE_RANGES, "Decode range outside bounded cone")
        require(args.decoder is not None, "Existing decoder required")
        im.read(rva, size)
        command = [str(args.decoder), "/NOLOGO", "/DISASM", f"/RANGE:0x{im.base+rva:X},0x{im.base+rva+size-1:X}", str(args.exe)]
        result = subprocess.run(command, capture_output=True, check=True)
        SCRATCH.mkdir(parents=True, exist_ok=True)
        dest = SCRATCH / f"decode-{rva:08x}-{size:x}.txt"
        with dest.open("xb") as out:
            out.write(result.stdout)
        print(result.stdout.decode("utf-8", errors="replace"))
    if args.refs is not None:
        print(json.dumps(refs(im, args.refs), indent=2))
    if args.caller_boundary is not None:
        at = args.caller_boundary
        require(at == 0x3FF731, "Caller boundary outside bounded cone")
        raw = im.read(at-2048, 2048)
        padding = list(re.finditer(b"\xcc{2,}", raw))
        require(bool(padding), "No bounded preceding padding")
        start = at-2048+padding[-1].end()
        print(json.dumps(dict(candidate_start=hex(start), call=hex(at), prefix=im.read(start, 16).hex(),
            note="Padding boundary candidate only; instruction decode must establish one function.")))
    if args.end_boundary is not None:
        start = args.end_boundary
        require(start == 0x823ED0, "End boundary outside bounded cone")
        match = re.search(b"\xcc{2,}", im.read(start, 2048))
        require(match is not None, "No bounded following padding")
        print(json.dumps(dict(start=hex(start), candidate_size=hex(match.start()),
            note="Padding boundary candidate only; instruction decode must establish exits.")))
    if args.output:
        require(not any((args.decode, args.refs, args.caller_boundary, args.end_boundary)), "Proof output is separate")
        destination = args.output.resolve()
        require(destination.parent == Path(__file__).resolve().parent, "Proof must stay beside script")
        raw = (json.dumps(proof(im), indent=2)+"\n").encode("utf-8")
        require(len(raw) <= 24576, "Compact proof limit")
        with destination.open("xb") as out:
            out.write(raw)
        print(json.dumps(dict(proof=str(destination), size=len(raw), sha256=sha(raw))))

if __name__ == "__main__":
    main()
