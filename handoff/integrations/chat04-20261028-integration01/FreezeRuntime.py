"""Freeze the exact CHAT04 normal build after an external StorageBudget admission.

No build, native process attachment, game launch, installation or network access.
PE RSDS and local-only direct-PDB DbgHelp reads follow the previously reviewed
performance436/r20-investigation/worker-export/CandidateSymbols.py routine.
The old scenario module is neither imported nor executed.
"""
import argparse
import ctypes as c
from ctypes import wintypes as w
from datetime import datetime, timezone
import hashlib
import importlib.util
import json
import ntpath
import os
from pathlib import Path
import re
import shutil
import struct
import subprocess
import sys
import uuid

sys.dont_write_bytecode = True
LEASE_TOKEN = "f6b94650-377a-4591-a144-176cbed9f353"
ARTIFACTS = (
    ("probe", "x86", "BC2NativeProbe.dll"),
    ("trace", "x86", "BC2NativeTrace.exe"),
    ("receiver", "x64", "BC2NativeIpcProbe.exe"),
    ("host", "x64", "BC2XrHost.exe"),
    ("resolution", "x86", "BC2NativeResolution.exe"),
    ("resolution_bootstrap", "x86", "BC2ResolutionBootstrap.dll"),
)
SOURCE_PATHS = ("include", "src", "profiles", "cmake", "tests", "tools", "launcher",
                "config", "third_party", "licenses", "CMakeLists.txt", "Build.ps1", "Build-Checkpoint.ps1")


def require(ok, message):
    if not ok:
        raise ValueError(message)


def load(path):
    def unique(pairs):
        result = {}
        for key, value in pairs:
            require(key not in result, "Duplicate JSON key: " + key)
            result[key] = value
        return result
    require(path.stat().st_size < 16 * 1024 * 1024, "Oversized JSON: " + str(path))
    return json.loads(path.read_text(encoding="utf-8-sig"), object_pairs_hook=unique)


def stamp(value):
    return datetime.fromisoformat(value.replace("Z", "+00:00")).astimezone(timezone.utc)


def no_links(path):
    for part in (path, *path.parents):
        require(not part.is_symlink(), "Linked path refused: " + str(part))
        if part.exists() and part.is_dir():
            require(not (getattr(part.stat(follow_symlinks=False), "st_file_attributes", 0) & 0x400),
                    "Reparse directory refused: " + str(part))


def pin(path):
    no_links(path)
    require(path.is_file(), "Missing file: " + str(path))
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return {"path": str(path), "bytes": path.stat().st_size, "sha256": digest.hexdigest()}


def check_pin(value):
    actual = pin(Path(value["path"]))
    require(actual["sha256"] == value["sha256"] and
            ("bytes" not in value or actual["bytes"] == value["bytes"]),
            "Pinned file changed: " + value["path"])
    return actual


def git(repo, *args):
    result = subprocess.run(["git", "-C", str(repo), *args], capture_output=True, text=True)
    require(result.returncode == 0, "Git verification failed: " + result.stderr.strip())
    return result.stdout.strip()


def save_new(path, value):
    with path.open("x", encoding="utf-8", newline="\n") as stream:
        json.dump(value, stream, indent=2)
        stream.write("\n")


def pe_identity(path, arch):
    require(path.stat().st_size < 64 * 1024 * 1024, "Oversized runtime image")
    data = path.read_bytes()
    require(len(data) >= 256 and data[:2] == b"MZ", "Invalid DOS image")
    pe = struct.unpack_from("<I", data, 0x3c)[0]
    require(pe + 24 <= len(data) and data[pe:pe + 4] == b"PE\0\0", "Invalid PE header")
    machine, count, timestamp = struct.unpack_from("<HHI", data, pe + 4)
    require(machine == (0x14c if arch == "x86" else 0x8664), "Wrong runtime architecture")
    optional_size = struct.unpack_from("<H", data, pe + 20)[0]
    optional = pe + 24
    require(optional + optional_size <= len(data), "Truncated optional header")
    magic = struct.unpack_from("<H", data, optional)[0]
    require(magic == (0x10b if arch == "x86" else 0x20b), "Wrong PE optional format")
    directories = optional + (96 if arch == "x86" else 112)
    require(directories + 7 * 8 <= optional + optional_size, "Missing debug directory")
    image_size = struct.unpack_from("<I", data, optional + 56)[0]
    require(image_size > 0, "Empty runtime image")
    sections = optional + optional_size
    require(0 < count <= 96 and sections + count * 40 <= len(data), "Invalid section table")

    def offset(rva, size):
        for index in range(count):
            _, address, raw_size, raw = struct.unpack_from("<IIII", data, sections + index * 40 + 8)
            if address <= rva and rva - address + size <= raw_size:
                result = raw + rva - address
                require(result + size <= len(data), "Truncated debug data")
                return result
        raise ValueError("Unbacked debug RVA")

    rva, size = struct.unpack_from("<II", data, directories + 6 * 8)
    require(rva and size and size % 28 == 0 and size <= 4096, "Invalid debug directory")
    records = []
    for index in range(size // 28):
        entry = offset(rva + index * 28, 28)
        kind, length, _, raw = struct.unpack_from("<IIII", data, entry + 12)
        if kind == 2:
            require(24 <= length <= 4096 and raw + length <= len(data), "Invalid CodeView record")
            if data[raw:raw + 4] == b"RSDS":
                name = data[raw + 24:raw + length].split(b"\0", 1)[0].decode("utf-8")
                records.append({"guid": str(uuid.UUID(bytes_le=data[raw + 4:raw + 20])),
                                "age": struct.unpack_from("<I", data, raw + 20)[0],
                                "embedded_pdb_path": name})
    require(len(records) == 1, "Expected exactly one RSDS identity")
    require(ntpath.basename(records[0]["embedded_pdb_path"]).casefold() == path.with_suffix(".pdb").name.casefold(),
            "Unexpected embedded PDB filename")
    return {"architecture": arch, "machine": machine, "image_size": image_size,
            "timestamp": timestamp, **records[0]}


class Module(c.Structure):
    _fields_ = [("SizeOfStruct", w.DWORD), ("BaseOfImage", c.c_ulonglong), ("ImageSize", w.DWORD),
                ("TimeDateStamp", w.DWORD), ("CheckSum", w.DWORD), ("NumSyms", w.DWORD), ("SymType", c.c_int),
                ("ModuleName", c.c_char * 32), ("ImageName", c.c_char * 256), ("LoadedImageName", c.c_char * 256),
                ("LoadedPdbName", c.c_char * 256), ("CVSig", w.DWORD), ("CVData", c.c_char * 780),
                ("PdbSig", w.DWORD), ("PdbSig70", c.c_ubyte * 16), ("PdbAge", w.DWORD),
                ("PdbUnmatched", w.BOOL), ("DbgUnmatched", w.BOOL), ("LineNumbers", w.BOOL),
                ("GlobalSymbols", w.BOOL), ("TypeInfo", w.BOOL), ("SourceIndexed", w.BOOL),
                ("Publics", w.BOOL), ("MachineType", w.DWORD), ("Reserved", w.DWORD)]


def pdb_identity(path, image_size):
    # Direct PDB load provides an independent identity; it does not load the DLL.
    for name in ("_NT_SYMBOL_PATH", "_NT_ALT_SYMBOL_PATH", "_NT_EXECUTABLE_IMAGE_PATH"):
        os.environ.pop(name, None)
    library = c.WinDLL(r"C:\Windows\System32\dbghelp.dll", use_last_error=True)
    kernel = c.WinDLL(r"C:\Windows\System32\kernel32.dll")
    kernel.GetCurrentProcess.restype = w.HANDLE
    handle = kernel.GetCurrentProcess()
    signatures = {
        "SymInitializeW": ([w.HANDLE, w.LPCWSTR, w.BOOL], w.BOOL),
        "SymSetOptions": ([w.DWORD], w.DWORD),
        "SymLoadModuleExW": ([w.HANDLE, w.HANDLE, w.LPCWSTR, w.LPCWSTR, c.c_ulonglong, w.DWORD, c.c_void_p, w.DWORD], c.c_ulonglong),
        "SymGetModuleInfo64": ([w.HANDLE, c.c_ulonglong, c.POINTER(Module)], w.BOOL),
        "SymCleanup": ([w.HANDLE], w.BOOL),
    }
    for name, (arguments, result) in signatures.items():
        getattr(library, name).argtypes = arguments
        getattr(library, name).restype = result
    library.SymSetOptions(0x020E2612)  # Reviewed local-only direct-PDB option mask.
    require(library.SymInitializeW(handle, str(path.parent), False), "Local symbol initialization failed")
    try:
        base = 0x10000000
        require(library.SymLoadModuleExW(handle, None, str(path), None, base, image_size, None, 0) == base,
                "Direct PDB load failed: " + str(path))
        metadata = Module()
        metadata.SizeOfStruct = c.sizeof(Module)
        require(library.SymGetModuleInfo64(handle, base, c.byref(metadata)), "PDB metadata unavailable")
        loaded = metadata.LoadedPdbName.decode("mbcs", errors="strict")
        require(ntpath.normcase(ntpath.normpath(loaded)) == ntpath.normcase(str(path)) and metadata.SymType == 3,
                "DbgHelp selected a different PDB")
        return {"guid": str(uuid.UUID(bytes_le=bytes(metadata.PdbSig70))), "age": metadata.PdbAge,
                "loaded_path": loaded, "symbol_type": metadata.SymType,
                "direct_pdb_unmatched_flag": bool(metadata.PdbUnmatched)}
    finally:
        require(library.SymCleanup(handle), "Local symbol cleanup failed")


def validation(repo, run, arch, attempt, now, built_commit):
    require(re.fullmatch(r"[A-Za-z0-9_-]+", attempt), "Invalid attempt name")
    files = {name: run / f"{arch}-{attempt}-{name}" for name in
             ("storage.json", "build.json", "tests.log", "build.log", "configure.log", "build-console.log", "result.json")}
    pins = {name: pin(path) for name, path in files.items()}
    start = stamp(load(files["storage.json"])["checked_utc"])
    result = load(files["build.json"])
    attempt_result = load(files["result.json"])
    require(attempt_result.get("passed") is True and attempt_result.get("source_commit") == built_commit,
            "Attempt did not pass at the requested built commit: " + arch)
    completed = stamp(result["completed_utc"])
    require(start <= completed <= now, "Stale or future build result: " + arch)
    require(result.get("built") is True and result.get("tests_passed") is True and result["architecture"] == arch,
            "Build/test result rejected: " + arch)
    require(Path(result["build_directory"]).resolve() == (repo / "build" / arch).resolve(), "Wrong shared build directory")
    build_start = stamp(result["storage"]["checked_utc"])
    require(start <= build_start <= completed, "Build did not receive current storage admission")
    tests = files["tests.log"].read_text(encoding="utf-8-sig")
    require(re.search(r"100% tests passed, 0 tests failed out of 305(?:\s|$)", tests) is not None,
            "Expected all 305 normal suites: " + arch)
    require("***Failed" not in tests and "***Timeout" not in tests, "Failed episode in selected test log")
    for name in ("tests.log", "build.log", "configure.log", "build-console.log", "build.json"):
        modified = datetime.fromtimestamp(files[name].stat().st_mtime, timezone.utc)
        require(start <= modified <= now, "Stale attempt evidence: " + str(files[name]))
    require(datetime.fromtimestamp(files["tests.log"].stat().st_mtime, timezone.utc) <= completed,
            "Test evidence postdates success receipt")
    return {"architecture": arch, "attempt": attempt, "cpp_suite_count": 305,
            "completed_utc": result["completed_utc"], "evidence": pins}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo", type=Path, required=True)
    parser.add_argument("--run", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--built-commit", required=True)
    parser.add_argument("--storage-admission", type=Path, required=True)
    parser.add_argument("--x86-attempt", default="01")
    parser.add_argument("--x64-attempt", default="01")
    parser.add_argument("--version", default="unreleased-checkpoint436-chat04-custody01")
    args = parser.parse_args()
    require(os.name == "nt", "Local Windows PDB verification required")
    repo, run, output = (path.absolute() for path in (args.repo, args.run, args.output))
    for path in (repo, run, output):
        require(".." not in path.parts, "Parent traversal refused")
        no_links(path)
    require(run.is_relative_to(repo / "reports") and output.is_relative_to(run), "Output must remain in this integration run")
    require(not output.exists(), "Preserve prior runtime; choose a fresh output directory")
    require(re.fullmatch(r"[0-9a-f]{40}", args.built_commit), "Expected exact built source commit")
    require(re.fullmatch(r"[A-Za-z0-9._-]+", args.version), "Invalid private runtime version")
    lease_path = repo.parent / "bc2vr-task-worktrees" / "INTEGRATION.lock"
    require(load(lease_path).get("owner_token") == LEASE_TOKEN, "Integration lease not owned")
    now = datetime.now(timezone.utc)
    admission = load(args.storage_admission)
    admitted_at = stamp(admission["checked_utc"])
    require(admission.get("schema") == "fvr.storage-admission.v1" and admission.get("operation") == "Build" and
            0 <= (now - admitted_at).total_seconds() <= 600 and admission.get("raw_capture_bytes") == 0,
            "Fresh zero-capture storage admission required")
    admitted_output = Path(admission["output_path"]).absolute()
    require(output == admitted_output or output.is_relative_to(admitted_output), "Runtime outside admitted output root")
    state_path = run / "source-state.json"
    state = load(state_path)
    git(repo, "cat-file", "-e", args.built_commit + "^{commit}")
    git(repo, "merge-base", "--is-ancestor", state["source_commit"], args.built_commit)
    git(repo, "diff", "--exit-code", args.built_commit, "--", *SOURCE_PATHS)
    helper = repo / "tools" / "bc2_native_operation_receipts.py"
    specification = importlib.util.spec_from_file_location("chat04_operation_provenance", helper)
    provenance = importlib.util.module_from_spec(specification)
    specification.loader.exec_module(provenance)
    header, source = Path(state["reviewed_header"]), Path(state["source_receipt"])
    closure = provenance.verify_source(repo, source, state["source_sha256"], header)
    prior = load(Path(state["prior_receipt"]))
    check_pin({"path": state["prior_receipt"], "sha256": state["prior_receipt_sha256"]})
    check_pin(prior["source_freeze"])
    previous = load(Path(prior["source_freeze"]["path"]))
    check_pin(previous["reviewed_header"])
    require(state["normal_flags"] == previous["normal_flags"], "Prepared normal flags differ from baseline")
    rebound, original = header.read_bytes(), Path(previous["reviewed_header"]["path"]).read_bytes()
    digest, old_digest = state["source_sha256"].encode(), prior["source_sha256"].encode()
    require(rebound.count(digest) == 1 and rebound.replace(digest, old_digest) == original,
            "Reviewed operation records changed beyond the source binding")
    validations = [validation(repo, run, arch, getattr(args, arch + "_attempt"), now, args.built_commit) for arch in ("x86", "x64")]
    cache_pins, normal_flags = {}, {}
    for arch in ("x86", "x64"):
        cache = repo / "build" / arch / "CMakeCache.txt"
        values = {match[0]: match[1] for match in re.findall(r"^([^:#\r\n]+):[^=\r\n]+=([^\r\n]*)", cache.read_text(encoding="utf-8"), re.M)}
        expected = {key: entry["value"] for key, entry in state["normal_flags"][arch].items()}
        expected.update(BC2_NATIVE_OPERATION_CAPABILITIES_HEADER=header.as_posix(),
                        BC2_NATIVE_OPERATION_SOURCE_RECEIPT=source.as_posix(),
                        BC2_NATIVE_OPERATION_SOURCE_SHA256=state["source_sha256"])
        normal_flags[arch] = {key: {**entry, "value": expected[key]}
                              for key, entry in state["normal_flags"][arch].items()}
        require(all(values.get(key) == str(value) for key, value in expected.items()), "Normal flags changed: " + arch)
        require({key for key, value in values.items() if key.startswith("BC2_") and value == "ON"} ==
                {key for key, value in expected.items() if key.startswith("BC2_") and value == "ON"}, "Unexpected enabled feature")
        require(values["CMAKE_HOME_DIRECTORY"] == repo.as_posix() and values["CMAKE_BUILD_TYPE"] == "RelWithDebInfo",
                "Wrong canonical build configuration")
        configuration = repo / "build" / arch / "generated" / "bc2-operation" / "Bc2NativeOperationBuild.generated.h"
        provenance.verify_build_config(configuration, header, state["source_sha256"])
        cache_pins[arch] = {"cache": pin(cache), "operation_config": pin(configuration)}
    rows = []
    for key, arch, name in ARTIFACTS:
        binary = repo / "build" / arch / name
        pdb = repo / "build" / arch / "symbols" / (Path(name).stem + ".pdb")
        rows.append({"key": key, "arch": arch, "binary": pin(binary), "pdb": pin(pdb)})
    planned = sum(row[k]["bytes"] for row in rows for k in ("binary", "pdb")) + 4 * 1024 * 1024
    require(admission["planned_bytes"] >= planned, "Storage admission does not cover all six binary/PDB pairs")
    for row in rows:
        identity = pe_identity(Path(row["binary"]["path"]), row["arch"])
        pdb = pdb_identity(Path(row["pdb"]["path"]), identity["image_size"])
        require((identity["guid"], identity["age"]) == (pdb["guid"], pdb["age"]), "PDB identity differs: " + row["key"])
        row.update(pe=identity, pdb_identity=pdb, exact_guid_age_match=True)
        check_pin(row["binary"])
        check_pin(row["pdb"])
    require(load(lease_path).get("owner_token") == LEASE_TOKEN, "Integration lease changed")
    output.mkdir()
    binaries, symbols = {}, {}
    for row in rows:
        for kind, destination in (("binary", binaries), ("pdb", symbols)):
            before = row[kind]
            target = output / Path(before["path"]).name
            shutil.copyfile(before["path"], target)
            frozen = pin(target)
            check_pin(before)
            require(frozen["sha256"] == before["sha256"] and frozen["bytes"] == before["bytes"], "Runtime copy differs")
            destination[row["key"]] = frozen
            row["frozen_" + kind] = frozen
    provenance.verify_source(repo, source, state["source_sha256"], header)
    git(repo, "diff", "--exit-code", args.built_commit, "--", *SOURCE_PATHS)
    audit_path = output / "symbol-audit.json"
    save_new(audit_path, {"schema": "fvr.chat04.runtime-symbol-audit.v1", "passed": True, "pairs": rows,
                          "local_only": True, "network_requested": False, "process_attached": False})
    freeze_path = output / "source-freeze.json"
    save_new(freeze_path, {"schema": "fvr.chat04.normal-source-freeze.v1", "source_commit": args.built_commit,
                           "source_sha256": closure["source_sha256"], "reviewed_header": pin(header),
                           "source_receipt": pin(source), "normal_flags": normal_flags,
                           "preparation": pin(state_path), "operation_records_changed": False,
                           "native_provenance_reused": True, "runner": pin(Path(__file__).absolute())})
    receipt = {"schema": "fvr.checkpoint436.normal-build.v1", "checkpoint": 436, "version": args.version,
               "built_source_commit": args.built_commit, "prepared_source_commit": state["source_commit"],
               "source_sha256": closure["source_sha256"], "source_receipt": str(source),
               "source_receipt_sha256": pin(source)["sha256"], "reviewed_header": pin(header),
                "source_freeze": pin(freeze_path), "normal_cache": cache_pins, "binaries": binaries,
               "symbols": symbols["probe"], "symbols_by_binary": symbols, "symbol_audit": pin(audit_path),
               "validation": validations, "runner": pin(Path(__file__).absolute()),
               "storage": pin(args.storage_admission.absolute()), "planned_bytes": planned,
               "normal": True, "full_normal_builds": True, "diagnostic_only": False, "diagnostic_kind": "normal",
               "input_protocol": 4, "operation_records_changed": False,
               "native_provenance_baseline": pin(Path(state["prior_receipt"])),
               "native_admission_granted": False, "native_bc2_verified": False,
               "headset_verified": False, "headset_tested": False, "campaign_verified": False,
               "installed": False, "published": False, "recorded_utc": datetime.now(timezone.utc).isoformat()}
    receipt_path = output / "build-receipt.json"
    save_new(receipt_path, receipt)
    print(json.dumps({"receipt": pin(receipt_path), "pairs": len(rows), "tests": {"x86": 305, "x64": 305},
                      "native_bc2_verified": False, "headset_verified": False}))


if __name__ == "__main__":
    try:
        main()
    except (ValueError, OSError, KeyError, TypeError, UnicodeError, struct.error) as error:
        print("Runtime freeze refused: " + str(error), file=sys.stderr)
        raise SystemExit(1)
