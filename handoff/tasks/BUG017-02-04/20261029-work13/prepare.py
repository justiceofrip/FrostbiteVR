"""Bounded file-only cinematic metadata proof. No process/archive access or observer."""
from __future__ import annotations
import argparse
import ast
import hashlib
import json
from pathlib import Path
import struct
import sys

ROOT = Path(__file__).resolve().parents[4]
READER = ROOT / "handoff/tasks/CHAT16/20261029-work12/pistol_readiness_probe.py"
sys.path.insert(0, str(READER.parent))
from pistol_readiness_probe import image_for, locations, require

NAMES = ("ClientCutsceneCameraEntity", "ClientCutsceneEntity",
         "ServerCutsceneCameraEntity", "CutsceneCameraEntityData",
         "CutsceneEntityData", "CutsceneEntityCameraData", "CutsceneCameraData")
LABELS = ("CameraController", "CutsceneCamera", "PlayingCutscene", "IsCutscene")


def sha(data):
    return hashlib.sha256(data).hexdigest()


def text_at(image, address):
    out = bytearray()
    for k in range(192):
        c = image.read(address - image.base + k, 1)[0]
        if not c:
            require(bool(out), "Empty reflected name")
            return out.decode("ascii")
        require(32 <= c < 127, "Non-ASCII reflected name")
        out.append(c)
    raise ValueError("Unterminated reflected name")


def metadata(image, name):
    strings = locations(image, name.encode("ascii") + b"\0")
    require(len(strings) == 1, "Missing/ambiguous type name: " + name)
    rows = []
    for va in locations(image, struct.pack("<I", strings[0])):
        try:
            raw = image.read(va - image.base, 24)
            flags, size = struct.unpack_from("<HH", raw, 4)
            if va % 4 == 0 and flags == 0x35 and 0 < size <= 4096 and raw[13] <= 64:
                rows.append(dict(name=name, string_rva=strings[0] - image.base,
                                 metadata_rva=va-image.base, metadata_sha256=sha(raw),
                                 flags=flags, size=size, own_field_count=raw[13]))
        except (ValueError, struct.error):
            continue
    require(len(rows) == 1, "Missing/ambiguous class metadata: " + name)
    return rows[0]


def initializer(image, row, constructor):
    result = []
    metadata_va = image.base + row["metadata_rva"]
    for ref_va in locations(image, struct.pack("<I", metadata_va)):
        ref = ref_va - image.base
        if not image.section(ref)[4] & 0x20000000:
            continue
        # Four immediate arguments, then mov ecx, ClassInfo storage; call.
        for start in range(ref - 17, ref - 6):
            at, args = start, []
            for _ in range(4):
                code = image.read(at, 5)
                if code[0] == 0x68:
                    args.append(struct.unpack_from("<I", code, 1)[0]); at += 5
                elif code[0] == 0x6a:
                    args.append(struct.unpack_from("<b", code, 1)[0]); at += 2
                else:
                    break
            if len(args) != 4 or at != ref + 4 or args[-1] != metadata_va:
                continue
            code = image.read(at, 10)
            if code[0] != 0xb9 or code[5] != 0xe8:
                continue
            info = struct.unpack_from("<I", code, 1)[0]
            callee = at + 10 + struct.unpack_from("<i", code, 6)[0]
            wrapper = None
            if callee != constructor:
                # Exact decoded forwarding wrapper: original arguments A,B,C,D
                # are passed unchanged to ClassInfo(A,B,C,D); own vtable follows.
                body = image.read(callee, 42)
                prefix = bytes.fromhex("8B4424108B54240856508B44240C8BF18B4C24145152508BCEE8")
                require(body[:26] == prefix, "Unsupported ClassInfo wrapper")
                require(callee + 30 + struct.unpack_from("<i", body, 26)[0] == constructor,
                        "ClassInfo wrapper call does not match verified constructor")
                require(body[30:32] == bytes((0xc7, 0x06)) and
                        body[36:] == bytes.fromhex("8BC65EC21000"), "Wrapper tail differs")
                wrapper = dict(rva=callee, size=42, sha256=sha(body),
                               forwards_arguments_unchanged=True)
            require(image.section(info-image.base, 40)[4] & 0x80000000,
                    "ClassInfo storage is not writable image storage")
            records = image.read(args[0]-image.base, row["own_field_count"]*24) if row["own_field_count"] else b""
            fields = []
            for n in range(row["own_field_count"]):
                label, _, _, _, offset, _ = struct.unpack_from("<6I", records, n*24)
                require(offset < row["size"], "Field outside declared type")
                fields.append(dict(name=text_at(image, label), type_relative_offset=offset))
            require(len({f["name"] for f in fields}) == len(fields), "Duplicate field names")
            getters = locations(image, bytes((0xb8,)) + struct.pack("<I", info) + bytes((0xc3,)))
            getters = [a-image.base for a in getters if image.section(a-image.base, 6)[4] & 0x20000000]
            require(1 <= len(getters) <= 8, "Missing/ambiguous bounded reflection getters")
            result.append(dict(initializer_rva=start, initializer_size=at+10-start,
                               initializer_sha256=sha(image.read(start, at+10-start)),
                               type_info_storage_rva=info-image.base,
                               parent_type_info_storage_rva=args[2]-image.base,
                               wrapper=wrapper, fields=fields,
                               table_rva=args[0]-image.base if records else None,
                               table_sha256=sha(records) if records else None,
                               getter_rvas=getters,
                               getter_sha256=[sha(image.read(r, 6)) for r in getters]))
    require(len(result) == 1, "Missing/ambiguous constructor linkage: " + row["name"])
    row.update(result[0])
    return row


def prepare(executable):
    image = image_for(executable)  # Existing reader requires the exact reviewed SHA256.
    reader_path = ROOT / "tools/capture_reload_state.py"
    tree = ast.parse(reader_path.read_text(encoding="utf-8"))
    fn = next(n for n in ast.walk(tree) if isinstance(n, ast.FunctionDef) and n.name == "inherited_field")
    patterns = [n.args[0].value for n in ast.walk(fn) if isinstance(n, ast.Call) and
                isinstance(n.func, ast.Attribute) and n.func.attr == "find" and n.args and
                isinstance(n.args[0], ast.Constant) and isinstance(n.args[0].value, str)]
    require(len(patterns) == 1, "Existing ClassInfo signature changed")
    constructor = image.find(patterns[0])
    types = [initializer(image, metadata(image, name), constructor) for name in NAMES]
    require(sum(t["own_field_count"] for t in types) == 31, "Reviewed field inventory changed")
    source_files = (READER, reader_path, ROOT/"tools/read_bc2.py",
                    ROOT/"src/games/bc2/Bc2Profile.h", ROOT/"src/games/bc2/Bc2Profile.cpp",
                    ROOT/"src/games/bc2/Bc2Gameplay.cpp")
    return dict(schema="bug017.cinematic_static_preparation.v1", evidence_level="offline static file examination",
                executable_sha256=sha(image.data), preferred_base=image.base,
                reader_source_sha256={str(p.relative_to(ROOT)): sha(p.read_bytes()) for p in source_files},
                script_sha256=sha(Path(__file__).read_bytes()),
                constructor=dict(rva=constructor, verified_existing_signature_unique=True,
                                 size=62, sha256=sha(image.read(constructor, 62)),
                                 parent_type_info_offset=20, fields_type_info_offset=36),
                reflected_classes=types,
                labels_only=[dict(name=n, string_rvas=[a-image.base for a in locations(image,n.encode()+b"\0")]) for n in LABELS],
                static_relationship="CutsceneEntityCameraData parent TypeInfo storage equals CutsceneCameraData TypeInfo storage; no runtime instance asserted.",
                existing_root_paths=["Bc2Profile.cpp:281 DiscoverGameplay: context -> manager -> local player / soldier weak; input cache",
                                     "Bc2Profile.cpp:315 DiscoverInputBinding: controlled getter, input cache, router",
                                     "Bc2Gameplay.cpp:576 Resolve: verifies local player, soldier owner, controlled entry and cache",
                                     "Bc2Profile.h:39 ViewLayoutCandidates: view request and RenderView copies; no cinematic owner field"],
                missing_dynamic_join="No verified existing relationship from the admitted local player or active render-view/request to a live ClientCutsceneCameraEntity/ClientCutsceneEntity and its currently selected camera/sequence. Type metadata and class hierarchy do not establish active ownership, blending, local-player scope or lifetime.",
                field_limits="Offsets are declared-type relative metadata, not candidate runtime owner offsets. ActiveFromStart is authored configuration; PeekAround fields and Camera names do not prove live selection. Zero own fields does not exclude inherited or unreflected state.",
                observer_prepared=False, stop_reason="No verified existing object relationship reaches an active cinematic owner; stop before inventing offsets or an ownership classifier.",
                process_opened=False, archives_read=0, native_capability_changed=False)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    require(args.output.resolve().parent == Path(__file__).resolve().parent, "Output must stay beside this preparation")
    report = prepare(args.exe)
    names = {r["name"]: r for r in report["reflected_classes"]}
    require(names["CutsceneEntityCameraData"]["parent_type_info_storage_rva"] ==
            names["CutsceneCameraData"]["type_info_storage_rva"], "Declared camera inheritance differs")
    payload = json.dumps(report, indent=2) + "\n"
    require(len(payload.encode("utf-8")) <= 32768, "Compact proof bound exceeded")
    with args.output.open("x", encoding="utf-8", newline="\n") as stream:
        stream.write(payload)
    print(json.dumps(dict(status="static-proof-complete", classes=len(names), fields=31,
                         bytes=len(payload.encode("utf-8")), sha256=sha(payload.encode("utf-8")),
                         observer_prepared=False)))
