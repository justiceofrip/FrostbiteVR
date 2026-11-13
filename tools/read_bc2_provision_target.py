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
COMPLETENESS = ("two bounded observation sweeps of the declared domain, with recorded whole-region "
                "MEM_FREE retirements excluded from unread coverage; exact selected typed sets agree; "
                "no atomic or temporal global-uniqueness guarantee")


class Incomplete(ValueError):
    pass


def digest(blob):
    return hashlib.sha256(blob).hexdigest()


def load_pins(manifest_path):
    """Verify the caller's trusted manifest without executing reader modules."""
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
    if not {"variants", "selectors"} <= blobs.keys():
        raise ValueError("Pinned production variants and authored selectors required")
    return manifest, blobs


def load_dependencies(manifest_path):
    manifest, blobs = load_pins(manifest_path)
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
    if type(variant) is not int or not 0 <= variant <= 0xffffffff:
        raise ValueError("Unsigned production variant required")
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


def bound_target(snapshot, fresh_source, pid, variant=0, *, pins=None):
    """Consumer check immediately BEFORE asking the existing command to equip.

    Does not grant or extend the native source lease. Command/native SeatEntry
    still validate their own fresh source, target relationship and original arm.
    The caller supplies the same reviewed pins used for scanning; snapshot paths
    are never used as authority for loading a selector or production table.
    """
    pins = Path(pins) if pins is not None else Path(__file__).with_name("source-pins.json")
    manifest, blobs = load_pins(pins)
    selected = exact_variant(blobs, variant)
    if (snapshot.get("schema") != SCHEMA or snapshot.get("accepted") is not True
            or snapshot.get("complete") is not True or snapshot.get("variant") != variant
            or type(snapshot.get("variant")) is not int or fresh_source["identity"]["pid"] != pid
            or snapshot["source_after"]["identity"] != fresh_source["identity"]
            or snapshot["source_before"]["identity"] != fresh_source["identity"]
            or fresh_source["source_sequence"] < snapshot["source_after"]["source_sequence"]
            or fresh_source["source_observed_ns"] < snapshot["source_after"]["source_observed_ns"]):
        raise ValueError("Target snapshot is not bound to this retained native source")
    target = snapshot["target"]
    expected = snapshot.get("pinned_expected_metadata", {})
    if (expected.get("name") != selected["name"] or expected.get("path") != selected["path"]
            or expected.get("authored_dbx_resource_sha256") != selected["authored"]["configuration"]["resource_sha256"]
            or expected.get("dependency_manifest_sha256") != digest(pins.read_bytes())
            or expected.get("files") != manifest["files"]
            or target.get("name") != selected["name"]
            or target.get("path", "").lower() != selected["path"].lower()
            or target["asset"].get("path", "").lower() != selected["path"].lower()):
        raise ValueError("Target snapshot differs from the pinned production selector")
    observed = target.get("observed_native", {})
    if observed.get("words") != [dict(primary_offset=offset, bytes=size, value=value)
                                 for offset, size, value in selected["words"]]:
        raise ValueError("Target snapshot differs from the pinned production words")
    logic, ammo = bytes.fromhex(observed.get("fire_logic_hex", "")), bytes.fromhex(observed.get("ammo_hex", ""))
    if len(logic) != 0x8c or len(ammo) != 0x24:
        raise ValueError("Target snapshot native configuration bounds")
    for offset, size, value in selected["words"]:
        block, origin = (logic, 0xc) if offset < 0x170 else (ammo, 0x170)
        if int.from_bytes(block[offset - origin:offset - origin + size], "little") != value:
            raise ValueError("Target snapshot native bytes differ from the pinned production words")
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
        self.read_stage, self.read_context = "reader_initialization", {}
        self.read_failure = None
        self.candidate_reads = None
        self.selected_reads = set()
        self.retired_regions = []

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
            raise self.failed_read(address, size, "Unreadable scanned/typed region", error) from error
        if len(data) != size:
            raise self.failed_read(address, size, "Partial scanned/typed region read",
                                   ValueError("Expected " + str(size) + " bytes, received " + str(len(data))),
                                   received_bytes=len(data))
        self.check()
        if self.candidate_reads is not None:
            self.candidate_reads.add((address, address + size))
        return data

    def failed_read(self, address, size, reason, error, **details):
        # Preserve the failed request before any cleanup can lose its context.
        # A layout is an earlier observation, not a guarantee that an unrelated
        # allocation remains readable. Unread bytes are never counted as read;
        # scan_pass alone may qualify a whole MEM_FREE retirement, without retry.
        self.read_failure = {**self.read_context, "stage": self.read_stage,
                             "address": address, "requested_bytes": size,
                             # The pinned Process.read discards its native
                             # transfer count on exceptions; do not invent it.
                             "received_bytes": None,
                             "error_type": type(error).__name__, "error": str(error)[:512],
                             "errno": getattr(error, "errno", None),
                             "winerror": getattr(error, "winerror", None), **details}
        return Incomplete(reason + " at " + hex(address) + " size=" + str(size) +
                          " (" + self.read_stage + "): " + type(error).__name__ + ": " + str(error)[:160])

    def u32(self, address):
        return struct.unpack("<I", self.read(address, 4))[0]

    def region(self, address):
        self.check()
        if not BEGIN <= address < END:
            raise Incomplete("Typed query bounds")
        if self.queries >= self.max_regions:
            raise Incomplete("Region-query cap reached")
        self.queries += 1
        region = self.query(address)
        base, size, state, _, _ = region
        if size <= 0 or not 0 <= base <= address < base + size <= END:
            raise Incomplete("Malformed region extent")
        if state not in (0x1000, 0x2000, 0x10000):
            raise Incomplete("Unknown memory region state")
        self.check()
        return region

    def layout(self):
        address, result = BEGIN, []
        while address < END:
            base, size, state, kind, protect = self.region(address)
            # Preserve the existing verified scanner's mounted-resource domain.
            if state == 0x1000 and kind in (0x20000, 0x40000) and protect & 0xff in (2, 4, 8) and not protect & 0x100:
                begin = max(BEGIN, base)
                if begin % 4 or (base + size) % 4:
                    raise Incomplete("Unaligned committed region")
                result.append((begin, base + size, kind, protect))
            address = base + size
        return tuple(result)

    def confirm_free(self, region):
        begin, end, _, _ = region
        if any(start < end and begin < stop for start, stop in self.selected_reads):
            raise Incomplete("Selected typed-chain region cannot retire during observation")
        # The first fresh query must cover the ENTIRE old region. An endpoint
        # alone, a free prefix, reserve, changed protection or failed query is
        # insufficient. Query the last byte again; Windows may round its base
        # to a different page, so compare containment/state rather than tuples.
        first = self.region(begin)
        if first[2] != 0x10000 or first[0] + first[1] < end:
            raise Incomplete("Disappeared scan region is not wholly MEM_FREE")
        last = self.region(end - 1)
        if last[2] != 0x10000:
            raise Incomplete("Disappeared scan region changed during MEM_FREE confirmation")
        return [dict(address=address, base=value[0], size=value[1], state=value[2],
                     type=value[3], protect=value[4])
                for address, value in ((begin, first), (end - 1, last))]

    def retire(self, region, phase, sweep, failure=None):
        if len(self.retired_regions) >= 64:
            raise Incomplete("Retired-region record cap reached")
        evidence = self.confirm_free(region)
        self.retired_regions.append({"region": region, "phase": phase, "sweep": sweep,
                                     "free_queries": evidence, "read_failure": failure})

    def reconcile_layout(self, previous, current, sweep):
        # Allow only removal of whole prior regions with fresh positive evidence.
        # Added regions, resized/split survivors and protection/type changes
        # invalidate the observation instead of starting another pass.
        previous_set, current_set = set(previous), set(current)
        if current_set - previous_set:
            raise Incomplete("Mounted-resource region map added or changed a mapping")
        retired = {tuple(row["region"]) for row in self.retired_regions}
        for region in previous:
            if region not in current_set and region not in retired:
                self.retire(region, "layout_boundary", sweep)
        for row in self.retired_regions:
            region = tuple(row["region"])
            if region in current_set:
                raise Incomplete("Retired scan region reappeared in the eligible domain")
            row["latest_free_queries"] = self.confirm_free(region)
            row["last_confirmed_boundary"] = sweep


class ExactReader:
    def __init__(self, selected_reader, variant):
        self.reader, self.p, self.variant = selected_reader, selected_reader.p, variant
        self.stage = "asset_vtable"

    @property
    def stage(self):
        return self._stage

    @stage.setter
    def stage(self, value):
        self._stage = self.p.read_stage = value

    def unallocated_initial_name(self, asset, table, address):
        # A raw vtable word can be an instruction immediate in a code copy.
        # Only positive MEM_FREE evidence excludes before selector consumption;
        # committed unreadability, unknown state and failed queries never do.
        region = self.p.region(address)
        if region[2] != 0x10000:
            return False
        if self.p.u32(asset) != table or self.p.u32(asset + 0xc) != address:
            raise Incomplete("Candidate header changed during Name qualification")
        if self.p.region(address) != region:
            raise Incomplete("Name region changed during candidate qualification")
        if self.p.u32(asset) != table or self.p.u32(asset + 0xc) != address:
            raise Incomplete("Candidate header changed during Name qualification")
        return True

    def matches_asset_path(self, address):
        # A readable unequal byte proves this is not the exact selector. Do not
        # parse unrelated suffixes as metadata, or turn unreadable bytes into a
        # negative match. Include NUL so a longer sibling path cannot select.
        expected = self.variant["path"].encode("ascii").lower() + b"\0"
        for offset, wanted in enumerate(expected):
            observed = self.p.read(address + offset, 1)[0]
            if 65 <= observed <= 90:
                observed += 32
            if observed != wanted:
                return False
        return True

    def once(self, asset, table):
        # The retained reader may return None for a selected Asset whose Object
        # points at a different Data selector. Preserve that distinction before
        # delegation: malformed selected candidates make this sweep incomplete.
        self.stage = "asset_vtable"
        if self.p.u32(asset) != table:
            raise Incomplete("Candidate table changed")
        self.stage = "asset_reflection"
        self.reader.inspector.types = {}
        info = self.reader.inspector.require_type(asset, "SoldierWeaponAsset")
        name = self.reader.field(info, "Name")
        if name["offset"] != 0xc:
            raise Incomplete("Reflected Asset name offset differs from native selector")
        self.stage = "asset_name_allocation"
        name_address = self.p.u32(asset + name["offset"])
        if self.unallocated_initial_name(asset, table, name_address):
            return None
        self.stage = "asset_selector"
        if not self.matches_asset_path(name_address):
            return None
        self.stage = "selected_typed_chain"
        row = self.reader.once(asset, table)
        if row is None:
            raise Incomplete("Selected Asset has an invalid Data selector/path")
        self.stage = "selected_relationship"
        if not row["asset"] or row["asset"]["address"] != asset or row["asset"]["object"] != row["data"]:
            raise Incomplete("Asset to Data relationship invalid")
        if self.p.u32(row["data"]) != 0x145f098:
            raise Incomplete("Data native vtable differs")
        # Existing reflection must agree with the offsets used by production
        # SelectExactProvisionResource, not just produce similarly named fields.
        self.stage = "selected_configuration"
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
        self.stage = "exact_variant"
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
        self.p.read_context["candidate_address"] = asset
        self.p.candidate_reads = set()
        try:
            first, second = self.once(asset, table), self.once(asset, table)
            if first is not None or second is not None:
                self.p.selected_reads.update(self.p.candidate_reads)
        finally:
            self.p.candidate_reads = None
        if first != second:
            self.stage = "stable_recheck"
            raise Incomplete("Typed resource changed between reads")
        return first


def scan_pass(reader, layout, table, sweep):
    selected = []
    read_bytes = retired_unread_bytes = 0
    needle = struct.pack("<I", table)
    for begin, end, kind, protect in layout:
        for address in range(begin, end, 65536):
            reader.p.read_stage = "scan_chunk"
            reader.p.read_context = {"phase": "scan", "sweep": sweep,
                                     "layout_region": {"begin": begin, "end": end,
                                                       "state": 0x1000, "type": kind, "protect": protect}}
            size = min(65536, end - address)
            prior_failure = reader.p.read_failure
            try:
                blob = reader.p.read(address, size)
            except Incomplete:
                failure = reader.p.read_failure
                # Only the observed whole-chunk ERROR_PARTIAL_COPY path can
                # qualify retirement. Typed reads and budget/time failures
                # never enter this branch, and no process read is retried.
                if (failure is prior_failure or failure is None or
                        failure["error_type"] != "OSError" or
                        299 not in (failure["errno"], failure["winerror"])):
                    raise
                reader.p.retire((begin, end, kind, protect), "scan_chunk", sweep, dict(failure))
                failure["retirement_confirmed"] = True
                retired_unread_bytes += end - address
                break
            read_bytes += len(blob)
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
                    raise Incomplete("Typed candidate validation failed at " + hex(address + pos) +
                                     " (" + reader.stage + "): " + str(error)[:160]) from error
                if candidate is not None:
                    selected.append(candidate)
                    if len(selected) > 1:
                        raise Incomplete("Multiple exact typed Asset to Data matches")
    return selected, {"sweep": sweep, "layout_regions": len(layout),
                      "layout_eligible_bytes": sum(end - begin for begin, end, _, _ in layout),
                      "read_bytes": read_bytes, "retired_unread_bytes": retired_unread_bytes,
                      "layout_sha256": digest(json.dumps(layout, separators=(",", ":")).encode())}


def discover(reader, table):
    # Two bounded sweeps with complete eligible coverage or recorded positive
    # whole-region retirement. Selected typed results must still agree exactly.
    reader.p.read_context = {"phase": "before_scan"}
    reader.p.read_stage = "resource_verify"
    reader.reader.verify()
    first_layout = reader.p.layout()
    first, first_coverage = scan_pass(reader, first_layout, table, 1)
    second_layout = reader.p.layout()
    reader.p.reconcile_layout(first_layout, second_layout, 2)
    second, second_coverage = scan_pass(reader, second_layout, table, 2)
    final_layout = reader.p.layout()
    reader.p.reconcile_layout(second_layout, final_layout, 3)
    if first != second:
        raise Incomplete("Resource set changed across full scans")
    reader.p.read_context = {"phase": "after_scan"}
    reader.p.read_stage = "resource_verify"
    reader.reader.verify()
    if len(first) != 1:
        raise Incomplete("No exact typed Asset to Data match")
    if reader.stable(first[0]["asset"]["address"], table) != first[0]:
        raise Incomplete("Selected pair/config changed after full scans")
    reader.p.check()
    return first[0], {"passes": 2, "observations": [first_coverage, second_coverage],
                      "retired_regions": reader.p.retired_regions,
                      "final_eligible_regions": len(final_layout),
                      "final_eligible_bytes": sum(end - begin for begin, end, _, _ in final_layout)}


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
    parser.add_argument("--variant", type=int, default=0,
                        help="Exact production variant also present in the reviewed authored selectors (default: 0)")
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--pins", type=Path, default=Path(__file__).with_name("source-pins.json"))
    parser.add_argument("--max-read-mib", type=int, default=4096)
    parser.add_argument("--max-seconds", type=int, default=120)
    args = parser.parse_args(argv)
    if not 0 < args.pid <= 0xffffffff or not 1 <= args.max_read_mib <= 8192 or not 1 <= args.max_seconds <= 180:
        parser.error("Bounded PID/scan arguments required")
    if not 0 <= args.variant <= 0xffffffff:
        parser.error("Unsigned production variant required")
    no_link_ancestors(args.output)
    if args.output.exists() or args.output.with_name(args.output.name + ".pending").exists():
        parser.error("Output/pending file exists; preserve the earlier observation")
    manifest, blobs, modules = load_dependencies(args.pins)
    variant = exact_variant(blobs, args.variant)
    report = {"schema": SCHEMA, "read_only": True, "process_access": 0x410,
              "accepted": False, "complete": False, "scan_domain": DOMAIN,
              "completeness_scope": COMPLETENESS,
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
                         "max_candidates": 4096, "max_region_queries": 65536,
                         "max_retired_regions": 64, "max_snapshot_bytes": 131072}}
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
        bounded.read_context = {"phase": "before_publication"}
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
            if bounded.read_failure is not None:
                report["read_failure"] = bounded.read_failure
            if bounded.retired_regions and "coverage" not in report:
                report["retired_regions"] = bounded.retired_regions
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
