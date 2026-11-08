"""Private, bounded read-only mounted-resource discovery for retained provisioning.

Frozen readers supply reflection/selector contracts. This wrapper supplies complete
coverage, process/session binding, the exact native variant words, and refusal.
It never writes game memory or provisioning IPC, invokes native code, or launches.
"""
from __future__ import annotations

import argparse
import ctypes as c
from ctypes import wintypes as w
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import re
import struct
import sys
import time

SCHEMA = "fvr.bc2.readonly_provision_target.v1"
BEGIN, END = 0x10000, 0x100000000
DOMAIN = "MEM_PRIVATE or MEM_MAPPED; committed; PAGE_READONLY/READWRITE/WRITECOPY; no PAGE_GUARD"


class Incomplete(ValueError):
    pass


def digest(blob):
    return hashlib.sha256(blob).hexdigest()


def load_dependencies(manifest_path):
    manifest = json.loads(Path(manifest_path).read_text(encoding="utf-8"))
    if manifest.get("schema") != "fvr.bc2.provision-reader-pins.v1":
        raise ValueError("Dependency manifest schema")
    blobs = {}
    # Verify every dependency BEFORE executing any imported reader.
    for key, pin in manifest["files"].items():
        blob = Path(pin["path"]).read_bytes()
        if len(blob) != pin["bytes"] or digest(blob) != pin["sha256"]:
            raise ValueError("Pinned dependency changed: " + key)
        blobs[key] = blob
    modules = {}
    for key in ("read_bc2", "capture_reload_state", "read_flynn_catalog",
                "read_stock_sniper_resources", "read_weapon_resources"):
        if key in sys.modules:
            raise ValueError("Reader module already loaded: " + key)
        spec = importlib.util.spec_from_file_location(key, manifest["files"][key]["path"])
        module = importlib.util.module_from_spec(spec)
        # Execute the bytes we verified, not a second unchecked filesystem read.
        sys.modules[key] = module
        exec(compile(blobs[key], spec.origin, "exec"), module.__dict__)
        modules[key] = module
    return manifest, blobs, modules


def exact_variant(blobs, variant):
    # Parse the pinned production initializer; there is no separately hand-written
    # configuration table. Fail if its deliberately narrow representation changes.
    lines = [line for line in blobs["variants"].decode("utf-8").splitlines()
             if line.startswith("{" + str(variant) + ",")]
    if len(lines) != 1:
        raise ValueError("Missing or ambiguous pinned variant")
    line = lines[0]
    selector = re.search(r'\{"([^"\n]+)","([^"\n]+)",', line)
    words = [(int(a), int(b), int(v, 16)) for a, b, v in
             re.findall(r'\{(\d+),(\d+),(0x[0-9a-f]+)u\}', line)]
    count = re.search(r'\}\},(\d+)\},?$', line)
    if not selector or not count or len(words) != int(count[1]) or not 1 <= len(words) <= 16:
        raise ValueError("Pinned variant initializer shape")
    for offset, size, _ in words:
        if size not in (1, 4) or not (0xc <= offset and offset + size <= 0x98 or
                                    0x170 <= offset and offset + size <= 0x194):
            raise ValueError("Pinned variant word bounds")
    authored = json.loads(blobs["selectors"])
    if authored.get("schema") != "fvr.bc2.authored_resource_selectors.v1":
        raise ValueError("Authored selector schema")
    rows = [r for r in authored["selectors"] if r["name"] == selector[1]
            and r["path"].lower() == selector[2].lower()]
    if len(rows) != 1 or rows[0]["configuration"]["missing_fields"]:
        raise ValueError("Exact authored selector absent")
    return {"variant": variant, "name": selector[1], "path": selector[2],
            "words": words, "authored": rows[0]}


def parse_source(blob):
    # Matches pinned735 Request/ProcessIdentity static_asserts, on both MSVC ABIs.
    if len(blob) != 1264 or struct.unpack_from("<4I", blob) != (0x31504342, 1, 1264, 0):
        raise ValueError("Private source wire layout")
    pid, reserved, creation, session = struct.unpack_from("<IIQQ", blob, 16)
    raw_path = blob[72:1096]
    units = struct.unpack("<512H", raw_path)
    try:
        end = units.index(0)
    except ValueError:
        raise ValueError("Unterminated source executable") from None
    if not end or any(units[end:]):
        raise ValueError("Source executable padding")
    executable = raw_path[:end * 2].decode("utf-16-le", errors="strict")
    nonce, job, sequence, issued, deadline, observed = struct.unpack_from("<QQQqqq", blob, 1096)
    if (not pid or reserved or not creation or session != creation or not sequence or observed <= 0
            or nonce or job or issued or deadline or any(blob[1248:])
            or struct.unpack_from("<Q", blob, 1184)[0] != session):
        raise ValueError("Source identity or observation invalid")
    return {"identity": {"pid": pid, "creation": creation, "session": session,
                         "executable": executable, "executable_sha256": blob[40:72].hex()},
            "source_sequence": sequence, "source_observed_ns": observed,
            "source_sha256": digest(blob)}


def no_link_ancestors(path):
    path = Path(path)
    if not path.is_absolute():
        raise ValueError("Absolute path required")
    for item in (path, *path.parents):
        if item.exists() and getattr(item.lstat(), "st_file_attributes", 0) & 0x400:
            raise ValueError("Reparse path refused")


def read_source(directory):
    path = Path(directory) / "campaign-provision-source.bin"
    no_link_ancestors(path)
    kernel = c.WinDLL("kernel32", use_last_error=True)
    kernel.CreateFileW.argtypes = [w.LPCWSTR, w.DWORD, w.DWORD, c.c_void_p, w.DWORD, w.DWORD, w.HANDLE]
    kernel.CreateFileW.restype = w.HANDLE
    kernel.GetFileInformationByHandleEx.argtypes = [w.HANDLE, c.c_int, c.c_void_p, w.DWORD]
    kernel.GetFileSizeEx.argtypes = [w.HANDLE, c.POINTER(c.c_longlong)]
    kernel.ReadFile.argtypes = [w.HANDLE, c.c_void_p, w.DWORD, c.POINTER(w.DWORD), c.c_void_p]
    kernel.CloseHandle.argtypes = [w.HANDLE]
    handle = kernel.CreateFileW(str(path), 0x80000000, 1 | 4, None, 3, 0x00200000, None)
    if handle == c.c_void_p(-1).value:
        raise OSError(c.get_last_error(), "Read source file")
    try:
        attributes = (w.DWORD * 2)()
        size, count, data = c.c_longlong(), w.DWORD(), c.create_string_buffer(1264)
        if (not kernel.GetFileInformationByHandleEx(handle, 9, attributes, c.sizeof(attributes))
                or attributes[0] & (0x10 | 0x400)
                or not kernel.GetFileSizeEx(handle, c.byref(size)) or size.value != 1264
                or not kernel.ReadFile(handle, data, 1264, c.byref(count), None) or count.value != 1264):
            raise ValueError("Incomplete or linked source file")
        return parse_source(data.raw)
    finally:
        kernel.CloseHandle(handle)


def verify_process(process, identity, expected_hash, expected_pid):
    kernel = c.WinDLL("kernel32", use_last_error=True)
    kernel.GetProcessId.argtypes = [w.HANDLE]
    kernel.GetProcessId.restype = w.DWORD
    kernel.GetProcessTimes.argtypes = [w.HANDLE] + [c.POINTER(w.FILETIME)] * 4
    kernel.GetExitCodeProcess.argtypes = [w.HANDLE, c.POINTER(w.DWORD)]
    kernel.QueryFullProcessImageNameW.argtypes = [w.HANDLE, w.DWORD, w.LPWSTR, c.POINTER(w.DWORD)]
    creation, exit_time, cpu, user = (w.FILETIME() for _ in range(4))
    name, length, status = c.create_unicode_buffer(32768), w.DWORD(32768), w.DWORD()
    if (kernel.GetProcessId(process.h) != expected_pid or identity["pid"] != expected_pid
            or identity["executable_sha256"] != expected_hash
            or not kernel.GetProcessTimes(process.h, c.byref(creation), c.byref(exit_time), c.byref(cpu), c.byref(user))
            or ((creation.dwHighDateTime << 32) | creation.dwLowDateTime) != identity["creation"]
            or identity["session"] != identity["creation"]
            or not kernel.QueryFullProcessImageNameW(process.h, 0, name, c.byref(length))
            or os.path.normcase(os.path.abspath(name.value)) != os.path.normcase(os.path.abspath(identity["executable"]))
            or not kernel.GetExitCodeProcess(process.h, c.byref(status)) or status.value != 259):
        raise Incomplete("Exact retained process/session identity changed")


def bound_target(snapshot, fresh_source, pid, variant=0):
    """Consumer check immediately BEFORE asking the existing command to equip.

    Does not grant or extend the native source lease. Command/native SeatEntry
    still validate their own fresh source, target relationship and original arm.
    """
    if (snapshot.get("schema") != SCHEMA or snapshot.get("accepted") is not True
            or snapshot.get("complete") is not True or snapshot.get("variant") != variant
            or variant != 0 or fresh_source["identity"]["pid"] != pid
            or snapshot["source_after"]["identity"] != fresh_source["identity"]
            or snapshot["source_before"]["identity"] != fresh_source["identity"]
            or fresh_source["source_sequence"] < snapshot["source_after"]["source_sequence"]
            or fresh_source["source_observed_ns"] < snapshot["source_after"]["source_observed_ns"]):
        raise ValueError("Target snapshot is not bound to this retained native source")
    target = snapshot["target"]
    asset, data = target["asset"]["address"], target["data"]
    if (not isinstance(asset, int) or not isinstance(data, int) or not BEGIN <= asset < END
            or not BEGIN <= data < END or asset % 4 or data % 4
            or target["asset"]["object"] != data):
        raise ValueError("Target snapshot relationship or address bounds")
    return variant, asset, data


class WinQuery:
    def __init__(self, process, module):
        self.process, self.module = process, module

    def __call__(self, address):
        info = self.module.MBI()
        size = self.module.k.VirtualQueryEx(self.process.h, c.c_void_p(address), c.byref(info), c.sizeof(info))
        if size != c.sizeof(info):
            raise Incomplete("VirtualQueryEx failed before full address-space coverage")
        return (int(info.base or 0), int(info.size), int(info.state), int(info.type), int(info.protect))


class BudgetedProcess:
    def __init__(self, process, query, max_bytes, seconds, max_candidates=4096, max_regions=65536,
                 clock=time.monotonic):
        self.process, self.query, self.base = process, query, process.base
        self.max_bytes, self.max_candidates, self.max_regions = max_bytes, max_candidates, max_regions
        self.clock, self.started = clock, clock()
        self.deadline = self.started + seconds
        self.bytes_read = self.read_calls = self.candidates = self.queries = 0

    def check(self):
        if self.clock() >= self.deadline:
            raise Incomplete("Scan time cap reached")

    def read(self, address, size):
        self.check()
        if not BEGIN <= address < END or not 0 < size <= 65536 or address + size > END:
            raise Incomplete("Typed read bounds")
        if self.bytes_read + size > self.max_bytes:
            raise Incomplete("Scan and typed-read byte cap reached")
        self.read_calls += 1
        self.bytes_read += size
        try:
            data = self.process.read(address, size)
        except (OSError, ValueError) as error:
            raise Incomplete("Unreadable scanned/typed region") from error
        if len(data) != size:
            raise Incomplete("Partial scanned/typed region read")
        self.check()
        return data

    def u32(self, address):
        return struct.unpack("<I", self.read(address, 4))[0]

    def layout(self):
        address, result = BEGIN, []
        while address < END:
            self.check()
            if self.queries >= self.max_regions:
                raise Incomplete("Region-query cap reached")
            self.queries += 1
            base, size, state, kind, protect = self.query(address)
            if size <= 0 or base > address or base + size <= address or base + size > END:
                raise Incomplete("Malformed region extent")
            # Preserve the existing verified scanner's mounted-resource domain.
            if state == 0x1000 and kind in (0x20000, 0x40000) and protect & 0xff in (2, 4, 8) and not protect & 0x100:
                begin = max(BEGIN, base)
                if begin % 4 or (base + size) % 4:
                    raise Incomplete("Unaligned committed region")
                result.append((begin, base + size, kind, protect))
            address = base + size
        return tuple(result)


class ExactReader:
    def __init__(self, selected_reader, variant):
        self.reader, self.p, self.variant = selected_reader, selected_reader.p, variant

    def once(self, asset, table):
        # The retained reader may return None for a selected Asset whose Object
        # points at a different Data selector. Preserve that distinction before
        # delegation: malformed selected candidates make this sweep incomplete.
        self.reader.inspector.types = {}
        info = self.reader.inspector.require_type(asset, "SoldierWeaponAsset")
        name = self.reader.field(info, "Name")
        if name["offset"] != 0xc:
            raise Incomplete("Reflected Asset name offset differs from native selector")
        asset_path = self.reader.inspector.text(self.p.u32(asset + name["offset"]))
        selected_asset = asset_path.lower() == self.variant["path"].lower()
        row = self.reader.once(asset, table)
        if row is None:
            if selected_asset:
                raise Incomplete("Selected Asset has an invalid Data selector/path")
            return None
        if not selected_asset:
            raise Incomplete("Asset selector changed between reads")
        if not row["asset"] or row["asset"]["address"] != asset or row["asset"]["object"] != row["data"]:
            raise Incomplete("Asset to Data relationship invalid")
        if self.p.u32(row["data"]) != 0x145f098:
            raise Incomplete("Data native vtable differs")
        # Existing reflection must agree with the offsets used by production
        # SelectExactProvisionResource, not just produce similarly named fields.
        inspector = self.reader.inspector
        firing = inspector.require_type(row["firing_data"], "WeaponFiringData")
        primary = inspector.require_type(row["primary_fire"], "FiringFunctionData")
        expected_fields = ((firing, "PrimaryFire", 0x40), (primary, "FireLogic", 0xc), (primary, "Ammo", 0x170))
        if any(self.reader.field(info, name)["offset"] != offset for info, name, offset in expected_fields):
            raise Incomplete("Reflected configuration offsets differ from native selector")
        if row["asset"]["name_offset"] != 0xc or row["asset"]["object_offset"] != 0x50:
            raise Incomplete("Reflected Asset offsets differ from native selector")
        logic = self.p.read(row["primary_fire"] + 0xc, 0x8c)
        ammo = self.p.read(row["primary_fire"] + 0x170, 0x24)
        observed = []
        for offset, size, expected in self.variant["words"]:
            block, origin = (logic, 0xc) if offset < 0x170 else (ammo, 0x170)
            value = int.from_bytes(block[offset - origin:offset - origin + size], "little")
            if value != expected:
                raise Incomplete("Exact production variant word mismatch at " + hex(offset))
            observed.append({"primary_offset": offset, "bytes": size, "value": value})
        # The old field is authored metadata, NOT a hash of observed native bytes.
        row.pop("authored_configuration_sha256", None)
        row.pop("measured_family_geometry", None)
        row["observed_native"] = {"fire_logic_hex": logic.hex(), "ammo_hex": ammo.hex(), "words": observed}
        return row

    def stable(self, asset, table):
        first, second = self.once(asset, table), self.once(asset, table)
        if first != second:
            raise Incomplete("Typed resource changed between reads")
        return first


def scan_pass(reader, layout, table):
    selected = []
    needle = struct.pack("<I", table)
    for begin, end, _, _ in layout:
        for address in range(begin, end, 65536):
            blob = reader.p.read(address, min(65536, end - address))
            cursor = 0
            while True:
                pos = blob.find(needle, cursor)
                if pos < 0:
                    break
                cursor = pos + 1
                if (address + pos) % 4:
                    continue
                if reader.p.candidates >= reader.p.max_candidates:
                    raise Incomplete("Resource candidate cap reached")
                reader.p.candidates += 1
                try:
                    candidate = reader.stable(address + pos, table)
                except (OSError, ValueError, KeyError, RecursionError) as error:
                    raise Incomplete("Typed candidate validation failed: " + str(error)[:160]) from error
                if candidate is not None:
                    selected.append(candidate)
                    if len(selected) > 1:
                        raise Incomplete("Multiple exact typed Asset to Data matches")
    return selected


def discover(reader, table):
    # Two complete bounded sweeps; stable mappings and complete typed result must
    # agree. This is still external observation, never an atomic game-tick claim.
    reader.reader.verify()
    first_layout = reader.p.layout()
    first = scan_pass(reader, first_layout, table)
    second_layout = reader.p.layout()
    if first_layout != second_layout:
        raise Incomplete("Mounted-resource region map changed")
    second = scan_pass(reader, second_layout, table)
    if first != second or first_layout != reader.p.layout():
        raise Incomplete("Resource set or region map changed across full scans")
    reader.reader.verify()
    if len(first) != 1:
        raise Incomplete("No exact typed Asset to Data match")
    if reader.stable(first[0]["asset"]["address"], table) != first[0]:
        raise Incomplete("Selected pair/config changed after full scans")
    reader.p.check()
    return first[0], {"passes": 2, "regions_per_pass": len(first_layout),
                      "eligible_bytes_per_pass": sum(end - begin for begin, end, _, _ in first_layout),
                      "layout_sha256": digest(json.dumps(first_layout, separators=(",", ":")).encode())}


def publish(path, report):
    path = Path(path)
    no_link_ancestors(path)
    pending = path.with_name(path.name + ".pending")
    no_link_ancestors(pending)
    if path.exists():
        raise ValueError("Output already exists; preserve earlier observation")
    blob = (json.dumps(report, indent=2, allow_nan=False) + "\n").encode("utf-8")
    if len(blob) > 131072:
        raise ValueError("Snapshot output bound")
    with pending.open("xb") as handle:
        handle.write(blob)
        handle.flush()
        os.fsync(handle.fileno())
    # Windows rename refuses an existing target. A failed pending file is retained.
    pending.rename(path)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--session-dir", type=Path, required=True)
    parser.add_argument("--pid", type=int, required=True)
    parser.add_argument("--variant", type=int, default=0)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--pins", type=Path, default=Path(__file__).with_name("source-pins.json"))
    parser.add_argument("--max-read-mib", type=int, default=4096)
    parser.add_argument("--max-seconds", type=int, default=120)
    args = parser.parse_args(argv)
    if not 0 < args.pid <= 0xffffffff or not 1 <= args.max_read_mib <= 8192 or not 1 <= args.max_seconds <= 180:
        parser.error("Bounded PID/scan arguments required")
    if args.variant != 0:
        parser.error("This retained M95 seam supports only pinned variant 0")
    no_link_ancestors(args.output)
    if args.output.exists() or args.output.with_name(args.output.name + ".pending").exists():
        parser.error("Output/pending file exists; preserve the earlier observation")
    manifest, blobs, modules = load_dependencies(args.pins)
    variant = exact_variant(blobs, args.variant)
    report = {"schema": SCHEMA, "read_only": True, "process_access": 0x410,
              "accepted": False, "complete": False, "scan_domain": DOMAIN,
              "completeness_scope": "two complete observation sweeps of the declared domain; no atomic or temporal global-uniqueness guarantee",
              "external_reads_are_not_atomic": True, "native_admitted": False,
              "native_functions_called": False, "inventory_modified": False,
              "fresh_equip_source_required": True, "source_input_lease_preserved_by_scan": False,
              "variant": args.variant, "session_directory": str(args.session_dir),
              "pinned_expected_metadata": {"name": variant["name"], "path": variant["path"],
                  "authored_dbx_resource_sha256": variant["authored"]["configuration"]["resource_sha256"],
                  "authored_dbx_hash_verified_from_process_memory": False,
                  "dependency_manifest_sha256": digest(Path(args.pins).read_bytes()),
                  "files": manifest["files"]},
              "bounds": {"max_read_bytes": args.max_read_mib * 1024 * 1024, "max_seconds": args.max_seconds,
                         "max_candidates": 4096, "max_region_queries": 65536, "max_snapshot_bytes": 131072}}
    process = bounded = None
    try:
        source = read_source(args.session_dir)
        report["source_before"] = source
        identity = source["identity"]
        image = modules["capture_reload_state"].Image(identity["executable"])
        expected = modules["read_flynn_catalog"].EXPECTED
        if digest(image.data) != expected or identity["executable_sha256"] != expected:
            raise Incomplete("Pinned executable SHA256 differs")
        process = modules["read_bc2"].Process(args.pid, identity["executable"])
        verify_process(process, identity, expected, args.pid)
        bounded = BudgetedProcess(process, WinQuery(process, modules["read_bc2"]),
                                  args.max_read_mib * 1024 * 1024, args.max_seconds)
        selected = modules["read_weapon_resources"].SelectedResourceReader(
            bounded, image, {variant["path"].lower(): variant["authored"]})
        exact_reader = ExactReader(selected, variant)
        asset_table = modules["read_stock_sniper_resources"].ASSET_TABLE
        target, coverage = discover(exact_reader, asset_table)
        after = read_source(args.session_dir)
        report["source_after"] = after
        if (after["identity"] != identity or after["source_sequence"] < source["source_sequence"]
                or after["source_observed_ns"] < source["source_observed_ns"]):
            raise Incomplete("Retained native source identity regressed or changed")
        verify_process(process, identity, expected, args.pid)
        if digest(Path(identity["executable"]).read_bytes()) != expected:
            raise Incomplete("Executable file changed during scan")
        if exact_reader.stable(target["asset"]["address"], asset_table) != target:
            raise Incomplete("Selected pair/config changed before publication")
        verify_process(process, identity, expected, args.pid)
        bounded.check()
        report.update(accepted=True, complete=True, target=target, coverage=coverage,
                      selected_pair_revalidated_before_publication=True)
    except (OSError, ValueError, RuntimeError, KeyError, RecursionError) as error:
        report["refusal"] = str(error)[:256]
    finally:
        if process is not None:
            process.close()
        if bounded is not None:
            report["scan"] = {"bytes_read_including_typed_reads": bounded.bytes_read,
                              "read_calls": bounded.read_calls, "asset_candidates": bounded.candidates,
                              "region_queries": bounded.queries, "elapsed_seconds": bounded.clock() - bounded.started}
    publish(args.output, report)
    print(json.dumps({"accepted": report["accepted"], "output": str(args.output),
                      "refusal": report.get("refusal"), "variant": args.variant,
                      "asset": report.get("target", {}).get("asset", {}).get("address"),
                      "data": report.get("target", {}).get("data")}))
    return 0 if report["accepted"] else 3


if __name__ == "__main__":
    raise SystemExit(main())
