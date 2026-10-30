"""File-only, bounded two-family cinematic code proof. Never opens a process."""
from __future__ import annotations
import argparse
import ast
import hashlib
import json
from pathlib import Path
import re
import struct

ROOT = Path(__file__).resolve().parents[4]
PRIOR = ROOT / "handoff/tasks/BUG017-02-04/20261029-work13/preparation01.json"
SHA = "3911fcc8914b0158c434cee36f19295316e715d4c9a0e70c857e8c39e3d54258"
NAMES = ("ClientCutsceneCameraEntity", "ClientCutsceneEntity")
PRIOR_SHA = "5e077a868ad1eb55d7c3a35d2110a9b936f036b3564d27f47c39e821ca54eaef"
# Two getters, two table prefixes, four table-writing functions: eight units.
# No table-slot targets, direct callees, adjacent functions, or callers are walked.
FAMILIES = (
    dict(name=NAMES[0], getter=0x477E80, table=0x1036550,
         setup=(0x4B4340, 0x118), cleanup=(0x4A32E0, 0x8A),
         writes=(0x4B437B, 0x4A32F1), context_calls=(0x4A3325,)),
    dict(name=NAMES[1], getter=0x477EF0, table=0x10366C8,
         setup=(0x4AF7D0, 0x18A), cleanup=(0x4A3400, 0x137),
         writes=(0x4AF853, 0x4A3416), context_calls=(0x4AF8F9, 0x4A3440, 0x4A3452)),
)


def sha(data):
    return hashlib.sha256(data).hexdigest()


def require(ok, message):
    if not ok:
        raise ValueError(message)


def load(exe):
    prior_raw = PRIOR.read_bytes()
    require(sha(prior_raw) == PRIOR_SHA, "Prior proof changed")
    prior = json.loads(prior_raw)
    reader = ROOT / "tools/capture_reload_state.py"
    raw = reader.read_bytes()
    require(sha(raw) == prior["reader_source_sha256"]["tools\\capture_reload_state.py"], "Reader changed")
    # Reuse only the file-backed Image class: no Process, Inspector, or archive import.
    node = next(n for n in ast.parse(raw).body if isinstance(n, ast.ClassDef) and n.name == "Image")
    scope = dict(Path=Path, struct=struct, re=re)
    exec(compile(ast.Module(body=[node], type_ignores=[]), str(reader), "exec"), scope)
    image = scope["Image"](exe)
    require(sha(image.data) == SHA == prior["executable_sha256"], "Executable changed")
    return image, prior


def refs(im, target):
    absolute, relative = [], []
    pattern = struct.pack("<I", target)
    for rva, _, raw, size, flags in im.sections:
        data = im.data[raw:raw+size]
        at = data.find(pattern)
        while at >= 0:
            absolute.append(dict(rva=rva+at, executable=bool(flags & 0x20000000), aligned=(rva+at) % 4 == 0))
            require(len(absolute) <= 128, "Reference cap")
            at = data.find(pattern, at+1)
        if flags & 0x20000000:
            for match in re.finditer(b"[\xe8\xe9]", data):
                at = match.start()
                if at+5 <= len(data) and im.base+rva+at+5+struct.unpack_from("<i", data, at+1)[0] == target:
                    relative.append(dict(rva=rva+at, opcode=hex(data[at]), candidate_only=True))
                    require(len(relative) <= 128, "Relative reference cap")
    return dict(absolute=absolute, relative_byte_candidates=relative)


def inspect(im, prior, targets):
    out = {}
    if not targets:
        targets = [rva+im.base for row in prior["reflected_classes"] if row["name"] in NAMES for rva in row["getter_rvas"]]
    require(len(targets) <= 8, "At most eight exact reference targets")
    for va in targets:
        out[hex(va-im.base)] = refs(im, va)
    return out


def record(im, rva, size):
    flags = im.section(rva, size)[4]
    return dict(rva=hex(rva), size=size, sha256=sha(im.read(rva, size)),
                executable=bool(flags & 0x20000000), writable=bool(flags & 0x80000000))


def exact(im, rva, pattern):
    require(im.read(rva, len(bytes.fromhex(pattern))) == bytes.fromhex(pattern),
            "Instruction mismatch at " + hex(rva))


def call_target(im, rva):
    raw = im.read(rva, 5)
    require(raw[0] == 0xE8, "Expected direct call at " + hex(rva))
    return rva+5+struct.unpack_from("<i", raw, 1)[0]


def proof(im, prior):
    profile = ROOT / "src/games/bc2/Bc2Profile.cpp"
    source = profile.read_text(encoding="utf-8")
    start = source.index("std::optional<GameplayCandidates> DiscoverGameplay(")
    section = source[start:source.index("std::optional<InputBindingCandidates> DiscoverInputBinding(", start)]
    pattern = re.search(r'const auto context=Find\(b,pe,"([0-9A-F? ]+)"\)', section).group(1)
    context = im.find(pattern)
    require(context == 0x3FF060, "Existing context signature changed")
    context_va = struct.unpack("<I", im.read(context+21, 4))[0]
    require(im.read(context+0x73, 6) == b"\xb8"+struct.pack("<I", context_va)+b"\xc3",
            "Existing context getter return changed")
    exact(im, 0x4B4340, "8B442404535556578B7C2418")
    exact(im, 0x4B4455, "C20800")
    exact(im, 0x4AF7D0, "8B44240453558B6C24105657")
    exact(im, 0x4AF957, "C20800")
    exact(im, 0x4A32E0, "53568BF1578D9ED00000008DBEDC000000")
    exact(im, 0x4A3400, "515355568BF1578D6E2C8D9E980000008DBEDC000000")
    for at, target in ((0x4A3365, 0x49E020), (0x4A3532, 0x210A40)):
        raw = im.read(at, 5)
        require(raw[0] == 0xE9 and at+5+struct.unpack_from("<i", raw, 1)[0] == target,
                "Cleanup tail changed")
    families = []
    for f in FAMILIES:
        saved = next(row for row in prior["reflected_classes"] if row["name"] == f["name"])
        getter = im.read(f["getter"], 6)
        require(saved["getter_rvas"] == [f["getter"]] and sha(getter) == saved["getter_sha256"][0],
                "Saved getter mismatch")
        require(getter == b"\xb8"+struct.pack("<I", im.base+saved["type_info_storage_rva"])+b"\xc3",
                "Getter no longer returns saved TypeInfo")
        getter_refs = refs(im, im.base+f["getter"])
        require(getter_refs["absolute"] == [dict(rva=f["table"]+8, executable=False, aligned=True)] and
                not getter_refs["relative_byte_candidates"], "Getter references changed")
        table = struct.unpack("<4I", im.read(f["table"], 16))
        require(table[2] == im.base+f["getter"] and all(im.section(v-im.base)[4] & 0x20000000 for v in table),
                "Four-slot table prefix changed")
        table_refs = refs(im, im.base+f["table"])
        require(sorted(r["rva"] for r in table_refs["absolute"]) == sorted(at+2 for at in f["writes"]) and
                all(r["executable"] for r in table_refs["absolute"]) and
                not table_refs["relative_byte_candidates"], "Table references changed")
        for at in f["writes"]:
            require(im.read(at, 6) == b"\xc7\x06"+struct.pack("<I", im.base+f["table"]), "Table assignment changed")
        for at in f["context_calls"]:
            require(call_target(im, at) == context, "Context call changed")
        families.append(dict(name=f["name"], type_info_storage_rva=hex(saved["type_info_storage_rva"]),
            getter=record(im, f["getter"], 6), table_prefix=record(im, f["table"], 16),
            table_prefix_function_rvas=[hex(v-im.base) for v in table],
            exact_getter_reference_rva=hex(f["table"]+8), direct_getter_call_candidates=0,
            setup=record(im, *f["setup"]), cleanup=record(im, *f["cleanup"]),
            primary_table_write_instruction_rvas=[hex(at) for at in f["writes"]],
            exact_context_call_instruction_rvas=[hex(at) for at in f["context_calls"]]))
    # Check only relationships inside the four selected functions. Neither
    # indirect dispatch nor a helper's name is assigned ownership semantics.
    exact(im, 0x4AF8FE, "8B484057")  # context+0x40 receiver; EDI=this+0x98
    require(call_target(im, 0x4AF902) == 0x826900, "Opaque helper target changed")
    exact(im, 0x4A332A, "8B48188B018B5010576A1FFFD2")
    exact(im, 0x4A3445, "8B48188B018B5010576A1FFFD2")
    exact(im, 0x4A3457, "8B48188B018B5010576A12FFD2")
    exact(im, 0x4B43C6, "8BBFE0000000F30F108F94000000")
    exact(im, 0x4B4407, "F30F108F90000000")
    exact(im, 0x4AF881, "8B8D98000000898EE0000000")
    exact(im, 0x4A348D, "8B8EE800000085C9741E8B118B4204FFD08B8EE80000008B118B4208FFD0C786E800000000000000")
    sources = (profile, ROOT/"src/games/bc2/Bc2Profile.h", ROOT/"src/games/bc2/Bc2Gameplay.cpp")
    return dict(schema="chat27.cinematic_owner_static_code.v1", evidence_level="offline file-backed code",
        executable_sha256=sha(im.data), preferred_base=hex(im.base), prior_proof_sha256=PRIOR_SHA,
        script_sha256=sha(Path(__file__).read_bytes()),
        source_sha256={str(p.relative_to(ROOT)): sha(p.read_bytes()) for p in sources},
        selected_cone_units=8, families=families,
        existing_context_anchor=dict(source="Bc2Profile.cpp DiscoverGameplay existing unique signature and return check",
            getter_rva=hex(context), storage_rva=hex(context_va-im.base),
            note="Existing admitted source anchor revalidated; no new callee-body walk."),
        exact_static_edges=[
            "Both table-writing functions in each family assign this[0] to that family's table; table+8 is the saved TypeInfo getter. This is code-backed type association, not a selected-instance proof.",
            "Entity setup RVA0x4af7d0: ECX entry is this; at0x4af7fc EDI=this+0x98; call0x4af8f9 returns existing context; 0x4af8fe loads receiver=[context+0x40]; 0x4af901 pushes EDI; 0x4af902 directly calls RVA0x826900. Callee semantics and reverse registry lookup are unproved.",
            "Camera cleanup RVA0x4a32e0 and entity cleanup RVA0x4a3400 form EDI=this+0xdc; call existing context getter; load receiver=[context+0x18]; call [receiver.vtable+0x10] with stack arguments (0x1f,this+0xdc). Entity cleanup repeats with (0x12,this+0xdc). Dispatch target/meaning is unproved.",
            "Camera setup consumes original second stack argument P: at0x4b43c6 Q=[P+0xe0], then reads float[Q+0x94] and float[Q+0x90]. These numerically match prior Camera/PeekAround metadata, but caller/resource type and current selection are not established by this cone.",
            "Entity setup copies [second argument+0x98] into [this+0xe0] and zeros [this+0xe8]; cleanup0x4a348d tests [this+0xe8], dispatches its vtable+4 and +8, then clears it. This demonstrates teardown behavior of an unidentified member; no camera identity, live selection, or complete lifetime is asserted."
        ],
        admitted_roots=dict(gameplay="Bc2Gameplay.cpp Resolve: context+8 -> manager -> localPlayerOffset0xb4; validates player, soldier ownership, controlled entry and input cache.",
            view="Bc2Profile.cpp DiscoverViewLayout: view object+0x70 request owner; four RenderView blocks and active byte. The selected functions do not establish a static chain to those admitted roots."),
        read_chain_status="No runtime read chain or new admission signature prepared. The exact context service argument chains above start from an already-supplied entity this; they do not discover a currently selected entity from an admitted player/view root.",
        resource_selected_lifetime=dict(resource="Previous authored fields retained as metadata only; constructor argument shape is consistency evidence, not type/selection proof.",
            selected_instance="Unproved: no local-player or active view/request -> exact live cinematic entity -> current camera/sequence join.",
            lifetime_authority="Partial native initialization/cleanup instructions only; live generation, local-player scope, blend authority, entry/exit/skip and synchronization are unproved."),
        stop_reason="Eight-unit cone reached. Missing edge is admitted local player or active view/request -> exact currently selected cinematic instance. Shared context services do not establish that edge. Opaque callee0x826900, service dispatches, table slots and external callers remain unwalked.",
        process_access=False, archives_read=0, source_modified=False, runtime_admission_enabled=False,
        verification="All exact hashes, reference uniqueness, table assignments, call destinations and listed instruction relationships checked against the required executable. No build/game/XR/headset check.")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--exe", type=Path, required=True)
    ap.add_argument("--refs", type=lambda x: int(x, 0), nargs="*")
    ap.add_argument("--output", type=Path)
    args = ap.parse_args()
    image, prior = load(args.exe)
    if args.output:
        require(args.refs is None, "Proof output and exploratory refs are separate")
        destination = args.output.resolve()
        require(destination.parent == Path(__file__).resolve().parent, "Proof must stay beside script")
        result = proof(image, prior)
        raw = (json.dumps(result, indent=2)+"\n").encode("utf-8")
        require(len(raw) <= 24576, "Compact proof limit")
        with destination.open("xb") as stream:
            stream.write(raw)
        print(json.dumps(dict(proof=str(destination), size=len(raw), sha256=sha(raw), selected_cone_units=8)))
    else:
        print(json.dumps(inspect(image, prior, args.refs), indent=2))


if __name__ == "__main__":
    main()
