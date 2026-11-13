"""Pinned selector regressions; no game process required.

Supply --reader-pins PATH to additionally assemble the retained reader against
synthetic memory. Without those external dependencies that check is explicitly
skipped; the portable tests alone do not establish retained-reader acceptance.
"""
import argparse
import copy
import hashlib
import importlib.util
import io
import json
from pathlib import Path
import re
import struct
import sys
import tempfile
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("provision_target", ROOT / "tools/read_bc2_provision_target.py")
tool = importlib.util.module_from_spec(spec)
spec.loader.exec_module(tool)
READER_PINS = None


class ResourceMemory:
    """Small synthetic heap/image implementing the real reader's byte contract.

    Reflection records, executable slices and configuration are fixture data,
    not observations or copied game assets. Reader/reflection methods are real.
    """
    base = 0x400000

    def __init__(self, path, name, observed):
        self.heap = bytearray(0x5000)
        self.blocks = {}
        self.next_metadata = 0x1800000
        self.heap[0x400c:0x4098] = bytes.fromhex(observed["fire_logic_hex"])
        self.heap[0x4170:0x4194] = bytes.fromhex(observed["ammo_hex"])
        for offset, value in ((0x1000, 0x1474554), (0x100c, 0x10100), (0x1050, 0x12000),
                              (0x2000, 0x145f098), (0x200c, 0x10200), (0x2040, 0x10100),
                              (0x2098, 0x13000), (0x3040, 0x14000)):
            struct.pack_into("<I", self.heap, offset, value)
        for offset, value in ((0x100, path), (0x200, name)):
            encoded = value.encode("ascii") + b"\0"
            self.heap[offset:offset + len(encoded)] = encoded

        boolean = self.type_info("Boolean", 0, 1)
        integer = self.type_info("Int32", 0, 4)
        floating = self.type_info("Float32", 0, 4)
        string = self.type_info("String", 0, 4)
        logic_enum = self.type_info("FireLogicType", 0x179, 4,
                                    [("fltAutomaticFire", 0, 2)])
        reload_enum = self.type_info("ReloadType", 0x179, 4,
                                     [("rtMagazine", 0, 1)])
        bolt = self.type_info("BoltActionData", 0x29, 16, [
            ("BoltActionTime", floating, 4), ("BoltActionDelay", floating, 8),
            ("HoldBoltActionUntilZoomRelease", boolean, 12),
            ("HoldBoltActionUntilFireRelease", boolean, 13)])
        logic = self.type_info("FireLogicData", 0x29, 0x8c, [
            ("ReloadType", reload_enum, 20), ("FireLogicType", logic_enum, 24),
            ("BoltAction", bolt, 64)])
        ammo = self.type_info("AmmoData", 0x29, 0x24, [("MagazineCapacity", integer, 20)])
        asset = self.type_info("SoldierWeaponAsset", 0x35, 0x54,
                              [("Name", string, 12), ("Object", integer, 0x50)], at=0x1c06e88)
        data = self.type_info("SoldierWeaponData", 0x35, 0x9c, at=0x1bfca60)
        firing = self.type_info("WeaponFiringData", 0x35, 0x44, [("PrimaryFire", integer, 0x40)])
        primary = self.type_info("FiringFunctionData", 0x35, 0x194,
                                [("FireLogic", logic, 12), ("Ammo", ammo, 0x170)])
        for table, getter, info in ((0x1474554, 0x13d8050, asset),
                                    (0x145f098, 0x139ecd0, data)):
            self.blocks[table] = struct.pack("<3I", 0, 0, getter)
            self.blocks[getter] = b"\xb8" + struct.pack("<I", info) + b"\xc3"
        for offset, info in ((0x3000, firing), (0x4000, primary)):
            getter = self.allocate(b"\xb8" + struct.pack("<I", info) + b"\xc3")
            table = self.allocate(struct.pack("<3I", 0, 0, getter))
            struct.pack_into("<I", self.heap, offset, table)
        self.blocks[0x13a10ca] = bytes.fromhex("c7450098f04501")
        self.blocks[0xb7c2f6] = bytes.fromhex("c70654454701")
        # Canonical Inspector.inherited_field locates and compares this pattern.
        self.blocks[self.base + 0x1000] = bytes.fromhex(
            "8b44240456508bf1e8000000008b4c24108b44240c8b542414894e1033c9"
            "380d00000000c70600000000894614894e1866894e1c66894e1e894e20895624")

    def allocate(self, blob):
        address = self.next_metadata
        self.next_metadata += (len(blob) + 3) & ~3
        self.blocks[address] = bytes(blob)
        return address

    def type_info(self, name, flags, size, fields=(), at=None):
        records = b"".join(struct.pack("<6I", self.allocate(label.encode() + b"\0"),
                                       0, kind, 0, offset, 0)
                           for label, kind, offset in fields)
        field_address = self.allocate(records) if records else 0
        metadata = bytearray(28)
        struct.pack_into("<IHH", metadata, 0, self.allocate(name.encode() + b"\0"), flags, size)
        metadata[13] = len(fields)
        struct.pack_into("<I", metadata, 24, field_address)
        info = bytearray(40)
        struct.pack_into("<I", info, 4, self.allocate(metadata))
        address = self.allocate(info) if at is None else at
        struct.pack_into("<I", info, 20, address)
        struct.pack_into("<I", info, 36, field_address)
        self.blocks[address] = bytes(info)
        return address

    def read(self, address, size):
        if 0x10000 <= address and address + size <= 0x15000:
            return bytes(self.heap[address - 0x10000:address - 0x10000 + size])
        for start, blob in self.blocks.items():
            if start <= address and address + size <= start + len(blob):
                return blob[address - start:address - start + size]
        raise ValueError("Unmapped fixture read at " + hex(address))

    def query(self, address):
        if 0x10000 <= address < 0x15000:
            return (0x10000, len(self.heap), 0x1000, 0x20000, 4)
        if 0x15000 <= address < self.base:
            return (0x15000, self.base - 0x15000, 0x10000, 0, 0)
        if self.base <= address < 0x2000000:
            return (self.base, 0x2000000 - self.base, 0x1000, 0x1000000, 0x20)
        return (0x2000000, tool.END - 0x2000000, 0x10000, 0, 0)


class ResourceImage:
    """Synthetic image boundary; all Inspector parsing remains canonical."""
    base = ResourceMemory.base

    def __init__(self, memory):
        self.memory = memory

    def section(self, rva, size=1):
        if not 0 <= rva < rva + size <= 0x2000000 - self.base:
            raise ValueError("Fixture image bounds")
        return (0, 0x2000000 - self.base, 0, 0x2000000 - self.base, 0x60000020)

    def read(self, rva, size):
        self.section(rva, size)
        return self.memory.read(self.base + rva, size)

    def find(self, pattern):
        regex = b"".join(b"." if byte == "??" else re.escape(bytes([int(byte, 16)]))
                         for byte in pattern.split())
        code = self.memory.blocks[self.base + 0x1000]
        matches = list(re.finditer(regex, code, re.DOTALL))
        if len(matches) != 1:
            raise ValueError("Fixture signature must be unique")
        return 0x1000 + matches[0].start()


def retained_modules(pins):
    """Verify and execute all real reader imports, with native calls forbidden."""
    class NoNativeCalls:
        def __getattr__(self, name):
            def refused(*args, **kwargs):
                raise AssertionError("Native API called by offline fixture: " + name)
            return refused

    keys = ("read_bc2", "capture_reload_state", "read_flynn_catalog",
            "read_stock_sniper_resources", "read_weapon_resources")
    # The historical import inserts a workstation path. Restore import state,
    # and use only modules compiled from bytes checked by load_dependencies.
    with mock.patch.dict(sys.modules), mock.patch.object(sys, "path", list(sys.path)), \
            mock.patch.object(tool.c, "WinDLL", return_value=NoNativeCalls(), create=True):
        for key in keys:
            sys.modules.pop(key, None)
        return tool.load_dependencies(pins)[2]


class Reader:
    """Reproduce the retained reader's None result for an unmatched Data path."""
    def __init__(self, asset_path, result=None, name_offset=12):
        self.p = self.inspector = self
        self.asset_path, self.result, self.name_offset = asset_path, result, name_offset
        self.types = {}

    def require_type(self, address, name):
        return {"name": name}

    def field(self, info, name):
        return {"offset": self.name_offset}

    def text(self, address):
        return self.asset_path

    def u32(self, address):
        return 0x1474554 if address == 0x11000 else 0x20000

    def read(self, address, size):
        path = self.asset_path.encode("ascii") + b"\0"
        return path[address - 0x20000:address - 0x20000 + size]

    def region(self, address):
        return (0x20000, 0x1000, 0x1000, 0x20000, 4)

    def once(self, asset, table):
        return self.result


class SelectedAssetTests(unittest.TestCase):
    target = "Objects/Weapons/Handheld/BU_sni_M95/SP_sni_M95"

    def test_selected_asset_cannot_hide_unmatched_data(self):
        reader = tool.ExactReader(Reader(self.target), {"path": self.target})
        with self.assertRaisesRegex(tool.Incomplete, "invalid Data selector/path"):
            reader.once(0x11000, 0x1474554)

    def test_unrelated_asset_remains_unselected(self):
        reader = tool.ExactReader(Reader("Objects/Weapons/Other"), {"path": self.target})
        self.assertIsNone(reader.once(0x11000, 0x1474554))

    def test_positive_selector_mismatch_does_not_delegate(self):
        selected = Reader("Objects/Weapons/Other")
        reader = tool.ExactReader(selected, {"path": self.target})
        with mock.patch.object(selected, "once", side_effect=AssertionError("Unselected delegate")):
            self.assertIsNone(reader.once(0x11000, 0x1474554))

    def test_unverified_asset_name_offset_refuses(self):
        reader = tool.ExactReader(Reader(self.target, name_offset=16), {"path": self.target})
        with self.assertRaisesRegex(tool.Incomplete, "name offset"):
            reader.once(0x11000, 0x1474554)


class ReadFailureTests(unittest.TestCase):
    def test_read_failure_preserves_request_error_and_caps_without_retry(self):
        for error in (OSError(299, "fixture partial copy"), ValueError("fixture inaccessible")):
            with self.subTest(error=type(error).__name__):
                memory = mock.Mock(base=0x400000)
                memory.read.side_effect = error
                query = mock.Mock(side_effect=AssertionError("Unexpected diagnostic query"))
                bounded = tool.BudgetedProcess(memory, query, 65536, 30)
                bounded.read_stage = "scan_chunk"
                bounded.read_context = {"phase": "scan", "sweep": 2}
                with self.assertRaisesRegex(tool.Incomplete, r"at 0x23000 size=65536 \(scan_chunk\)"):
                    bounded.read(0x23000, 65536)
                self.assertEqual(bounded.read_failure, {
                    "phase": "scan", "sweep": 2, "stage": "scan_chunk", "address": 0x23000,
                    "requested_bytes": 65536, "received_bytes": None,
                    "error_type": type(error).__name__, "error": str(error),
                    "errno": getattr(error, "errno", None), "winerror": getattr(error, "winerror", None)})
                memory.read.assert_called_once_with(0x23000, 65536)
                query.assert_not_called()
                self.assertEqual((bounded.read_calls, bounded.bytes_read, bounded.queries), (1, 65536, 0))
                with self.assertRaisesRegex(tool.Incomplete, "byte cap reached"):
                    bounded.read(0x23000, 1)
                self.assertEqual(memory.read.call_count, 1)

    def test_partial_read_remains_incomplete_with_received_size(self):
        memory = mock.Mock(base=0x400000)
        memory.read.return_value = b"abc"
        bounded = tool.BudgetedProcess(memory, mock.Mock(), 65536, 30)
        bounded.read_stage = "selected_typed_chain"
        with self.assertRaisesRegex(tool.Incomplete, r"Partial.*at 0x11000 size=4.*received 3"):
            bounded.read(0x11000, 4)
        self.assertEqual(bounded.read_failure["received_bytes"], 3)
        self.assertEqual(bounded.read_failure["requested_bytes"], 4)
        self.assertEqual(bounded.read_failure["stage"], "selected_typed_chain")
        memory.read.assert_called_once_with(0x11000, 4)


class PinnedVariantTests(unittest.TestCase):
    """Use the production table, with explicitly authored in-memory test resources.

    The synthetic source/target is only an external observation fixture. It does
    not grant native admission or establish that these resources are mounted.
    """
    production = ROOT / "src/games/bc2/Bc2CampaignProvisioningVariants.h"
    paths = {
        0: ("M95_sp", "Objects/Weapons/Handheld/BU_sni_M95/SP_sni_M95"),
        16: ("AEK971_sp", "Objects/Weapons/Handheld/RU_rgl_AEK971/SP_rgl_AEK971"),
        17: ("AEK971_sp_k", "Objects/Weapons/Handheld/RU_rgl_AEK971/SP_rgl_AEK971_Kobra"),
    }

    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.directory = Path(self.temporary.name)
        self.pins = self.directory / "source-pins.json"
        self.selectors = self.directory / "selectors.json"
        self.authored = {
            "schema": "fvr.bc2.authored_resource_selectors.v1",
            "selectors": [
                {"name": name, "path": path.lower(),
                 "configuration": {
                     "missing_fields": [], "resource_sha256": str(number % 10) * 64,
                     "fields": {key: {"value": value} for key, value in {
                         "Ammo.MagazineCapacity": 5 if number == 0 else 30,
                         "FireLogic.FireLogicType": "fltSingleFireWithBoltAction" if number == 0 else "fltAutomaticFire",
                         "FireLogic.ReloadType": "rtMagazine",
                         "FireLogic.BoltAction.BoltActionDelay": 0.0,
                         "FireLogic.BoltAction.BoltActionTime": struct.unpack("<f", struct.pack("<f", 2.3))[0] if number == 0 else 0.0,
                         "FireLogic.BoltAction.HoldBoltActionUntilFireRelease": number == 0,
                         "FireLogic.BoltAction.HoldBoltActionUntilZoomRelease": False,
                     }.items()}},
                 "measured_family_geometry": {"fixture_only": True}}
                for number, (name, path) in self.paths.items()
            ],
        }
        self.repin()
        self.source = {"identity": {"pid": 42, "creation": 1234, "session": 1234,
                                    "executable": "fixture.exe", "executable_sha256": "a" * 64},
                       "source_sequence": 3, "source_observed_ns": 900}

    def repin(self):
        self.selectors.write_text(json.dumps(self.authored), encoding="utf-8")
        files = {}
        for key, path in (("variants", self.production), ("selectors", self.selectors)):
            blob = path.read_bytes()
            files[key] = {"path": str(path), "bytes": len(blob), "sha256": hashlib.sha256(blob).hexdigest()}
        self.pins.write_text(json.dumps({"schema": "fvr.bc2.provision-reader-pins.v1", "files": files}), encoding="utf-8")
        self.manifest, self.blobs = tool.load_pins(self.pins)

    def snapshot(self, variant=16):
        name, path = self.paths[variant]
        # Independent fixture bytes for the current M95/AEK native configurations.
        values = {12: 0, 16: 0x3f400000, 20: 0, 24: 0x404ccccd, 32: 1,
                  36: 2, 44: 0, 76: 8, 80: 0, 84: 0, 88: 0, 89: 0,
                  140: 29, 384: 4, 388: 30}
        if variant == 0:
            values.update({16: 0x3f2b851f, 24: 0x40dccccd, 36: 1,
                           80: 0x40133333, 89: 1, 384: 5, 388: 5})
            del values[44]
        primary = bytearray(0x194)
        words = []
        for offset, value in values.items():
            size = 1 if offset in (88, 89) else 4
            primary[offset:offset + size] = value.to_bytes(size, "little")
            words.append({"primary_offset": offset, "bytes": size, "value": value})
        return {"schema": tool.SCHEMA, "accepted": True, "complete": True, "variant": variant,
                "source_before": copy.deepcopy(self.source), "source_after": copy.deepcopy(self.source),
                "pinned_expected_metadata": {
                    "name": name, "path": path, "authored_dbx_resource_sha256": str(variant % 10) * 64,
                    "dependency_manifest_sha256": hashlib.sha256(self.pins.read_bytes()).hexdigest(),
                    "files": copy.deepcopy(self.manifest["files"])},
                "target": {"name": name, "path": path, "data": 0x12000,
                           "asset": {"address": 0x11000, "object": 0x12000, "path": path},
                           "observed_native": {"words": words, "fire_logic_hex": primary[12:0x98].hex(),
                                               "ammo_hex": primary[0x170:0x194].hex()}}}

    def test_current_production_aek_and_m95(self):
        aek = tool.exact_variant(self.blobs, 16)
        self.assertEqual((aek["name"], aek["path"]), self.paths[16])
        self.assertIn((36, 4, 2), aek["words"])
        self.assertIn((388, 4, 30), aek["words"])
        self.assertEqual(tool.bound_target(self.snapshot(), self.source, 42, 16, pins=self.pins),
                         (16, 0x11000, 0x12000))
        self.assertEqual(tool.bound_target(self.snapshot(0), self.source, 42, pins=self.pins),
                         (0, 0x11000, 0x12000))

    def test_aek_exact_words_and_both_sweeps_with_reader_standin(self):
        # Portable wrapper coverage only; the explicit assembled case below
        # exercises the actual retained reader, reflection and authored fields.
        snapshot = self.snapshot()
        target = snapshot["target"]
        target.update(firing_data=0x13000, primary_fire=0x14000)
        target["asset"].update(name_offset=12, object_offset=0x50)
        heap = bytearray(0x5000)
        struct.pack_into("<I", heap, 0x1000, 0x1474554)
        struct.pack_into("<I", heap, 0x100c, 0x10100)
        struct.pack_into("<I", heap, 0x2000, 0x145f098)
        path = self.paths[16][1].encode("ascii") + b"\0"
        heap[0x100:0x100 + len(path)] = path
        heap[0x400c:0x4098] = bytes.fromhex(target["observed_native"]["fire_logic_hex"])
        heap[0x4170:0x4194] = bytes.fromhex(target["observed_native"]["ammo_hex"])

        class Memory:
            base = 0x400000

            def read(self, address, size):
                return bytes(heap[address - 0x10000:address - 0x10000 + size])

            def query(self, address):
                if 0x10000 <= address < 0x15000:
                    return (0x10000, len(heap), 0x1000, 0x20000, 4)
                return (0x15000, tool.END - 0x15000, 0x10000, 0, 0)

        class Selected(Reader):
            def field(self, info, name):
                return {"offset": {"Name": 12, "PrimaryFire": 0x40, "FireLogic": 12, "Ammo": 0x170}[name]}

            def once(self, asset, table):
                return copy.deepcopy(target)

            def verify(self):
                pass

        memory = Memory()
        selected = Selected(self.paths[16][1])
        selected.p = tool.BudgetedProcess(memory, memory.query, 1024 * 1024, 30)
        exact = tool.ExactReader(selected, tool.exact_variant(self.blobs, 16))
        result, coverage = tool.discover(exact, 0x1474554)
        self.assertEqual(result["name"], "AEK971_sp")
        self.assertEqual(coverage["passes"], 2)
        self.assertEqual(selected.p.candidates, 2)
        # One changed magazine-capacity byte must invalidate the same selection.
        heap[0x4184] = 5
        with self.assertRaisesRegex(tool.Incomplete, "variant word mismatch"):
            tool.discover(exact, 0x1474554)

    def test_aek_both_sweeps_with_pinned_retained_reader(self):
        if READER_PINS is None:
            self.skipTest("Retained reader NOT exercised: supply --reader-pins PATH to the reviewed dependency manifest")
        modules = retained_modules(READER_PINS)
        variant = tool.exact_variant(self.blobs, 16)
        row = variant["authored"]
        memory = ResourceMemory(variant["path"], variant["name"], self.snapshot()["target"]["observed_native"])
        bounded = tool.BudgetedProcess(memory, memory.query, 1024 * 1024, 30)
        selected = modules["read_weapon_resources"].SelectedResourceReader(
            bounded, ResourceImage(memory), {variant["path"].lower(): row})
        table = modules["read_stock_sniper_resources"].ASSET_TABLE
        # This is the unmodified real once/compare_config path, including actual
        # Inspector parsing of fixture reflection records and native bytes.
        observed = selected.once(0x11000, table)
        self.assertEqual(observed["configuration"]["ammo"]["MagazineCapacity"], 30)
        self.assertEqual(observed["measured_family_geometry"], {"fixture_only": True})
        self.assertEqual(observed["authored_configuration_sha256"], "6" * 64)
        exact = tool.ExactReader(selected, variant)
        result, coverage = tool.discover(exact, table)
        self.assertEqual((result["name"], result["path"]), self.paths[16])
        self.assertEqual(coverage["passes"], 2)
        self.assertEqual(bounded.candidates, 2)
        self.assertEqual(result["observed_native"], self.snapshot()["target"]["observed_native"])

        # Every field consumed by canonical compare_config must actually agree.
        for key, field in row["configuration"]["fields"].items():
            original = field["value"]
            field["value"] = not original if isinstance(original, bool) else (
                original + "_other" if isinstance(original, str) else original + 1)
            try:
                with self.subTest(field=key), self.assertRaisesRegex(
                        ValueError, "Authored/native configuration differs: " + re.escape(key)):
                    selected.once(0x11000, table)
            finally:
                field["value"] = original
        for container, key in ((row["configuration"], "fields"), (row, "measured_family_geometry")):
            original = container.pop(key)
            try:
                with self.subTest(missing=key), self.assertRaises(KeyError):
                    selected.once(0x11000, table)
            finally:
                container[key] = original
        # NumberOfMagazines is outside compare_config; production exact words
        # must still reject its changed byte after the real retained read.
        memory.heap[0x4180] = 5
        with self.assertRaisesRegex(tool.Incomplete, "variant word mismatch"):
            tool.discover(exact, table)

    def assembled_reader(self):
        if READER_PINS is None:
            self.skipTest("Retained reader NOT exercised: supply --reader-pins PATH to the reviewed dependency manifest")
        modules = retained_modules(READER_PINS)
        variant = tool.exact_variant(self.blobs, 16)
        memory = ResourceMemory(variant["path"], variant["name"], self.snapshot()["target"]["observed_native"])
        bounded = tool.BudgetedProcess(memory, memory.query, 1024 * 1024, 30)
        selected = modules["read_weapon_resources"].SelectedResourceReader(
            bounded, ResourceImage(memory), {variant["path"].lower(): variant["authored"]})
        return memory, selected, tool.ExactReader(selected, variant), modules["read_stock_sniper_resources"].ASSET_TABLE

    def assembled_code_candidate(self):
        memory, selected, exact, table = self.assembled_reader()
        # Actual98/02 candidate +0..127 at 0x33f3b02c: a copy of executable
        # code at 0x13d7fec, with initial Name/Object words both 0xcccccccc.
        code = bytes.fromhex(
            "544547018bc65ec20400cccccccccccccccccccc8b442408568bf18b4c240850518b"
            "cee86c720200c706544547018bc65ec20800c70154454701e9a56a0200cccccccccc"
            "8b4154c3cccccccccccccccccccccccc8b442404894154c20400ccccccccccccb888"
            "6ec001c3cccccccccccccccccccc568b74240885f6741f8b4424")
        memory.heap[0x800:0x800 + len(code)] = code
        return memory, selected, exact, table

    def assembled_retiring_region(self, failed_sweep=2, *, state="free", boundary=None,
                                 boundary_change=None):
        memory, selected, exact, table = self.assembled_reader()
        transient, size = 0x3000000, 32768
        observed = {"reads": 0, "retired": False, "layouts": 0, "queries": []}

        def query(address):
            if address == tool.BEGIN:
                observed["layouts"] += 1
                if observed["layouts"] == boundary:
                    observed["retired"] = True
                    if boundary_change is not None:
                        boundary_change(memory)
            if address < 0x2000000:
                return memory.query(address)
            if address < transient:
                return (0x2000000, transient - 0x2000000, 0x10000, 0, 0)
            if address < transient + size:
                if not observed["retired"]:
                    return (transient, size, 0x1000, 0x20000, 4)
                observed["queries"].append(address)
                if state == "query_failure":
                    raise OSError("fixture retirement query failed")
                if state == "wrong_extent":
                    return (address + 1, size, 0x10000, 0, 0)
                if state == "partial_free":
                    if address < transient + size // 2:
                        return (transient, size // 2, 0x10000, 0, 0)
                    return (transient + size // 2, size // 2, 0x1000, 0x20000, 4)
                if state == "endpoint_changed" and address == transient + size - 1:
                    return (transient, size, 0x1000, 0x20000, 4)
                region_state = {"committed": 0x1000, "reserved": 0x2000, "unknown": 0}.get(state, 0x10000)
                if state == "reallocated_ineligible" and len(observed["queries"]) >= 3:
                    region_state = 0x2000
                # Match the actual later VirtualQueryEx evidence: end-byte
                # queries may report the enclosing page as their BaseAddress.
                base = address & ~0xfff
                return (base, transient + size - base, region_state, 0, 1)
            added = transient + size
            if state == "added" and observed["retired"] and address < added + size:
                return (added, size, 0x1000, 0x20000, 4)
            return (address, tool.END - address, 0x10000, 0, 0)

        original_read = memory.read

        def read(address, count):
            if address == transient:
                observed["reads"] += 1
                if boundary is None and observed["reads"] == failed_sweep:
                    observed["retired"] = True
                    raise OSError(299, "fixture allocation retired after layout")
                return bytes(count)
            return original_read(address, count)

        selected.p.query = query
        memory.read = read
        return memory, selected, exact, table, observed

    def test_pinned_reader_retired_scan_region_requires_whole_free_evidence(self):
        for failed_sweep in (1, 2):
            with self.subTest(failed_sweep=failed_sweep):
                memory, selected, exact, table, observed = self.assembled_retiring_region(failed_sweep)
                result, coverage = tool.discover(exact, table)
                self.assertEqual(result["asset"]["address"], 0x11000)
                self.assertEqual(coverage["passes"], 2)
                self.assertEqual(observed["layouts"], 3)
                self.assertEqual(observed["reads"], failed_sweep)  # No read retry.
                self.assertEqual(selected.p.candidates, 2)
                self.assertEqual(coverage["final_eligible_bytes"], len(memory.heap))
                rows = coverage["observations"]
                self.assertEqual(rows[failed_sweep - 1]["retired_unread_bytes"], 32768)
                for row in rows:
                    self.assertEqual(row["read_bytes"] + row["retired_unread_bytes"], row["layout_eligible_bytes"])
                retirement, = coverage["retired_regions"]
                self.assertEqual(retirement["phase"], "scan_chunk")
                self.assertEqual(retirement["sweep"], failed_sweep)
                self.assertEqual(retirement["last_confirmed_boundary"], 3)
                self.assertEqual([q["address"] for q in retirement["free_queries"]], [0x3000000, 0x3007fff])
                self.assertTrue(all(q["state"] == 0x10000 for q in retirement["free_queries"]))
                failure = selected.p.read_failure
                self.assertEqual(failure["stage"], "scan_chunk")
                self.assertEqual(failure["sweep"], failed_sweep)
                self.assertEqual(failure["address"], 0x3000000)
                self.assertEqual(failure["requested_bytes"], 32768)
                self.assertEqual(failure["errno"], 299)
                self.assertEqual(failure["layout_region"], {
                    "begin": 0x3000000, "end": 0x3008000,
                    "state": 0x1000, "type": 0x20000, "protect": 4})
                self.assertNotIn("candidate_address", failure)
                self.assertTrue(failure["retirement_confirmed"])

    def test_pinned_reader_retirement_between_observations_keeps_current_coverage(self):
        for boundary in (2, 3):
            with self.subTest(boundary=boundary):
                memory, selected, exact, table, observed = self.assembled_retiring_region(boundary=boundary)
                result, coverage = tool.discover(exact, table)
                self.assertEqual(result["asset"]["address"], 0x11000)
                self.assertEqual(selected.p.candidates, 2)
                self.assertEqual(observed["reads"], boundary - 1)
                self.assertEqual([row["read_bytes"] for row in coverage["observations"]],
                                 [len(memory.heap) + 32768,
                                  len(memory.heap) + (32768 if boundary == 3 else 0)])
                self.assertTrue(all(row["retired_unread_bytes"] == 0 for row in coverage["observations"]))
                retirement, = coverage["retired_regions"]
                self.assertEqual(retirement["phase"], "layout_boundary")
                self.assertEqual(retirement["sweep"], boundary)

    def test_pinned_reader_uncertain_or_live_failed_region_remains_incomplete(self):
        for state, reason in (("committed", "not wholly MEM_FREE"), ("reserved", "not wholly MEM_FREE"),
                              ("unknown", "Unknown memory region state"),
                              ("partial_free", "not wholly MEM_FREE"),
                              ("query_failure", "fixture retirement query failed"),
                              ("wrong_extent", "Malformed region extent"),
                              ("endpoint_changed", "changed during MEM_FREE confirmation")):
            with self.subTest(state=state):
                _, selected, exact, table, observed = self.assembled_retiring_region(state=state)
                with self.assertRaisesRegex((OSError, tool.Incomplete), reason):
                    tool.discover(exact, table)
                self.assertEqual(observed["reads"], 2)
                self.assertEqual(selected.p.read_failure["errno"], 299)
                self.assertFalse(selected.p.retired_regions)

    def test_pinned_reader_retirement_cannot_hide_added_mapping_or_changed_candidate(self):
        for change in ("added", "duplicate", "configuration", "absent"):
            with self.subTest(change=change):
                def mutate(memory):
                    if change == "duplicate":
                        memory.heap[0x1800:0x1854] = memory.heap[0x1000:0x1054]
                    elif change == "configuration":
                        memory.heap[0x4180] = 5
                    elif change == "absent":
                        struct.pack_into("<I", memory.heap, 0x1000, 0)

                _, _, exact, table, _ = self.assembled_retiring_region(
                    state="added" if change == "added" else "free", boundary=2, boundary_change=mutate)
                reason = {"added": "added or changed a mapping", "duplicate": "Multiple exact typed",
                          "configuration": "variant word mismatch", "absent": "Resource set changed"}[change]
                with self.assertRaisesRegex(tool.Incomplete, reason):
                    tool.discover(exact, table)

    def test_pinned_reader_retired_range_must_stay_free_at_final_boundary(self):
        _, selected, exact, table, observed = self.assembled_retiring_region(state="reallocated_ineligible")
        with self.assertRaisesRegex(tool.Incomplete, "not wholly MEM_FREE"):
            tool.discover(exact, table)
        self.assertEqual(observed["reads"], 2)
        self.assertEqual(len(selected.p.retired_regions), 1)
        self.assertNotIn("last_confirmed_boundary", selected.p.retired_regions[0])

    def test_pinned_reader_retirement_cannot_hide_changed_live_mapping(self):
        _, selected, exact, table, observed = self.assembled_retiring_region(boundary=2)
        original_query = selected.p.query

        def query(address):
            result = original_query(address)
            if address == tool.BEGIN and observed["layouts"] >= 2:
                return (*result[:4], 8)
            return result

        selected.p.query = query
        with self.assertRaisesRegex(tool.Incomplete, "added or changed a mapping"):
            tool.discover(exact, table)

    def test_pinned_reader_selected_region_retirement_is_never_skipped(self):
        for failure_stage in ("scan", "typed"):
            with self.subTest(stage=failure_stage):
                memory, selected, exact, table = self.assembled_reader()
                retired, heap_reads = False, 0
                original_read = memory.read

                def query(address):
                    if retired and 0x10000 <= address < 0x15000:
                        return (0x10000, 0x5000, 0x10000, 0, 1)
                    return memory.query(address)

                def read(address, size):
                    nonlocal retired, heap_reads
                    if address == 0x10000 and size == len(memory.heap):
                        heap_reads += 1
                    if (failure_stage == "scan" and heap_reads == 2 or
                            failure_stage == "typed" and address == 0x12000):
                        retired = True
                        raise OSError(299, "fixture selected allocation retired")
                    return original_read(address, size)

                selected.p.query = query
                with mock.patch.object(memory, "read", side_effect=read):
                    reason = "Selected typed-chain region cannot retire" if failure_stage == "scan" else "Typed candidate validation failed"
                    with self.assertRaisesRegex(tool.Incomplete, reason):
                        tool.discover(exact, table)
                self.assertFalse(selected.p.retired_regions)
                self.assertEqual(selected.p.read_failure["stage"],
                                 "scan_chunk" if failure_stage == "scan" else "selected_typed_chain")

    def test_pinned_reader_retirement_preserves_query_and_time_caps(self):
        for cap in ("queries", "time"):
            with self.subTest(cap=cap):
                _, selected, exact, table, observed = self.assembled_retiring_region()
                original_query = selected.p.query

                def query(address):
                    result = original_query(address)
                    if observed["retired"]:
                        if cap == "queries":
                            selected.p.max_regions = selected.p.queries
                        else:
                            selected.p.deadline = selected.p.clock()
                    return result

                selected.p.query = query
                reason = "Region-query cap reached" if cap == "queries" else "Scan time cap reached"
                with self.assertRaisesRegex(tool.Incomplete, reason):
                    tool.discover(exact, table)
                self.assertEqual(observed["reads"], 2)

    def test_pinned_reader_unallocated_initial_name_preserves_complete_selection(self):
        for targets in (0, 1, 2):
            with self.subTest(exact_targets=targets):
                memory, selected, exact, table = self.assembled_code_candidate()
                if targets == 0:
                    struct.pack_into("<I", memory.heap, 0x1000, 0)
                elif targets == 2:
                    memory.heap[0x1800:0x1854] = memory.heap[0x1000:0x1054]
                with mock.patch.object(memory, "read", wraps=memory.read) as reads, \
                        mock.patch.object(selected.p, "query", wraps=memory.query) as queries:
                    if targets == 1:
                        result, coverage = tool.discover(exact, table)
                        self.assertEqual(result["asset"]["address"], 0x11000)
                        self.assertEqual(coverage["passes"], 2)
                        self.assertEqual(selected.p.candidates, 4)
                    else:
                        reason = "No exact typed Asset" if targets == 0 else "Multiple exact typed Asset"
                        with self.assertRaisesRegex(tool.Incomplete, reason):
                            tool.discover(exact, table)
                    self.assertEqual(selected.p.queries, queries.call_count)
                    self.assertFalse(any(call.args[0] == 0xcccccccc for call in reads.call_args_list))

    def test_pinned_reader_unallocated_name_header_mutation_stays_incomplete(self):
        for offset in (0, 0xc):
            for mutation_query in (1, 2):
                with self.subTest(header_offset=offset, mutation_query=mutation_query):
                    memory, selected, exact, table = self.assembled_code_candidate()
                    count = 0

                    def query(address):
                        nonlocal count
                        region = memory.query(address)
                        if address == 0xcccccccc:
                            count += 1
                            if count == mutation_query:
                                struct.pack_into("<I", memory.heap, 0x800 + offset, 0x10100)
                        return region

                    selected.p.query = query
                    with self.assertRaisesRegex(tool.Incomplete, "Candidate header changed"):
                        tool.discover(exact, table)

    def test_pinned_reader_unknown_name_allocation_stays_incomplete(self):
        for kind in ("query_failure", "wrong_extent", "zero_extent", "overflow_extent",
                     "committed", "reserved", "unknown", "changed_region"):
            with self.subTest(kind=kind):
                memory, selected, exact, table = self.assembled_code_candidate()
                count = 0

                def query(address):
                    nonlocal count
                    region = memory.query(address)
                    if address != 0xcccccccc:
                        return region
                    count += 1
                    if kind == "query_failure":
                        raise OSError("fixture query failed")
                    if kind == "wrong_extent":
                        return (address + 1, 0x1000, 0x10000, 0, 0)
                    if kind == "zero_extent":
                        return (address, 0, 0x10000, 0, 0)
                    if kind == "overflow_extent":
                        return (address, tool.END, 0x10000, 0, 0)
                    state = {"committed": 0x1000, "reserved": 0x2000, "unknown": 0}.get(kind)
                    if state is not None or kind == "changed_region" and count == 2:
                        return (region[0], region[1], state if state is not None else 0x1000, 0x20000, 4)
                    return region

                selected.p.query = query
                reason = ("fixture query failed" if kind == "query_failure" else
                          "Malformed region extent" if kind.endswith("extent") else
                          "Unknown memory region state" if kind == "unknown" else
                          "Name region changed" if kind == "changed_region" else
                          "Unreadable scanned/typed region")
                with self.assertRaisesRegex(tool.Incomplete, reason):
                    tool.discover(exact, table)

    def test_pinned_reader_name_qualification_cannot_bypass_query_budget(self):
        _, selected, exact, table = self.assembled_code_candidate()
        selected.p.max_regions = 1
        with self.assertRaisesRegex(tool.Incomplete, "Region-query cap reached"):
            exact.stable(0x10800, table)
        self.assertEqual(selected.p.queries, 1)

    def test_pinned_reader_excludes_proven_non_target_before_malformed_suffix(self):
        target = self.paths[16][1].encode("ascii")
        for path in (b"Objects/Weapons/Handheld/Other/\xff\0", target + b"_Scoped\xff\0",
                     b"Objects/Weapons/Handheld/\xff\0"):
            with self.subTest(path=path):
                memory, selected, exact, table = self.assembled_reader()
                struct.pack_into("<I", memory.heap, 0x800, table)
                struct.pack_into("<I", memory.heap, 0x80c, 0x10300)
                memory.heap[0x300:0x300 + len(path)] = path
                # Prove the unmodified retained parser would abort on this
                # non-target candidate, before exercising the complete wrapper.
                with self.assertRaisesRegex(ValueError, "Invalid ASCII metadata"):
                    selected.once(0x10800, table)
                mixed_case = target.swapcase() + b"\0"
                memory.heap[0x100:0x100 + len(mixed_case)] = mixed_case
                result, coverage = tool.discover(exact, table)
                self.assertEqual(result["asset"]["address"], 0x11000)
                self.assertEqual(result["path"].lower(), self.paths[16][1].lower())
                self.assertEqual(coverage["passes"], 2)
                self.assertEqual(selected.p.candidates, 4)

    def test_pinned_reader_unknown_exact_prefix_stays_incomplete(self):
        target = self.paths[16][1].encode("ascii")
        # Neither a partially readable prefix nor all path bytes without a
        # readable terminator proves exclusion or an exact selected path.
        for prefix in (target[:16], target):
            with self.subTest(prefix_bytes=len(prefix)):
                memory, selected, exact, table = self.assembled_reader()
                struct.pack_into("<I", memory.heap, 0x800, table)
                struct.pack_into("<I", memory.heap, 0x80c, 0x15000 - len(prefix))
                memory.heap[-len(prefix):] = prefix
                with self.assertRaisesRegex(tool.Incomplete,
                        r"0x10800 \(asset_selector\): Unreadable scanned/typed region"):
                    tool.discover(exact, table)
                self.assertEqual(selected.p.candidates, 1)
                self.assertEqual(selected.p.read_failure["stage"], "asset_selector")
                self.assertEqual(selected.p.read_failure["candidate_address"], 0x10800)
                self.assertEqual(selected.p.read_failure["address"], 0x15000)
                self.assertEqual(selected.p.read_failure["requested_bytes"], 1)
                self.assertEqual(selected.p.read_failure["sweep"], 1)

    def test_pinned_reader_exclusion_preserves_target_absence_and_uniqueness(self):
        for count in (0, 2):
            with self.subTest(exact_targets=count):
                memory, selected, exact, table = self.assembled_reader()
                struct.pack_into("<I", memory.heap, 0x800, table)
                struct.pack_into("<I", memory.heap, 0x80c, 0x10300)
                path = b"Other/\xff\0"
                memory.heap[0x300:0x300 + len(path)] = path
                if count == 0:
                    struct.pack_into("<I", memory.heap, 0x1000, 0)
                    reason = "No exact typed Asset to Data match"
                else:
                    memory.heap[0x1800:0x1854] = memory.heap[0x1000:0x1054]
                    reason = "Multiple exact typed Asset to Data matches"
                with self.assertRaisesRegex(tool.Incomplete, reason):
                    tool.discover(exact, table)
                self.assertEqual(selected.p.candidates, 2 if count == 0 else 3)

    def test_pinned_reader_selected_malformed_or_changed_data_stays_incomplete(self):
        for malformed in (True, False):
            with self.subTest(malformed_name=malformed):
                memory, _, exact, table = self.assembled_reader()
                if malformed:
                    memory.heap[0x200] = 0xff
                    reason = "Invalid ASCII metadata"
                else:
                    # An exact selected Asset cannot hide a changed Data path
                    # behind the retained reader's ordinary unselected None.
                    struct.pack_into("<I", memory.heap, 0x2040, 0x10300)
                    other = b"Objects/Weapons/Other\0"
                    memory.heap[0x300:0x300 + len(other)] = other
                    reason = "Selected Asset has an invalid Data selector/path"
                with self.assertRaisesRegex(tool.Incomplete,
                        r"0x11000 \(selected_typed_chain\): " + reason):
                    tool.discover(exact, table)

    def test_unsupported_integer_or_name_never_selects(self):
        for variant in (-1, 23, 0x100000000, True, "16", "AEK971_sp"):
            with self.subTest(variant=variant), self.assertRaises(ValueError):
                tool.exact_variant(self.blobs, variant)

    def test_production_entry_requires_unique_complete_authored_selector(self):
        for change in ("absent", "name", "path", "incomplete", "ambiguous"):
            authored = copy.deepcopy(self.authored)
            row = authored["selectors"][1]
            if change == "absent":
                authored["selectors"].remove(row)
            elif change in ("name", "path"):
                row[change] += "_other"
            elif change == "incomplete":
                row["configuration"]["missing_fields"] = ["Ammo.MagazineCapacity"]
            else:
                authored["selectors"].append(copy.deepcopy(row))
            with self.subTest(change=change), self.assertRaisesRegex(ValueError, "authored selector absent"):
                tool.exact_variant({**self.blobs, "selectors": json.dumps(authored).encode()}, 16)

    def test_selected_snapshot_cannot_be_relabelled_as_another_variant(self):
        snapshot = self.snapshot()
        with self.assertRaises(ValueError):
            tool.bound_target(snapshot, self.source, 42, 17, pins=self.pins)
        # AEK's Kobra variant has the same native words but a different exact resource.
        snapshot["variant"] = 17
        snapshot["pinned_expected_metadata"] = self.snapshot(17)["pinned_expected_metadata"]
        with self.assertRaisesRegex(ValueError, "production selector"):
            tool.bound_target(snapshot, self.source, 42, 17, pins=self.pins)

    def test_consumer_refuses_mismatched_metadata_resource_or_native_words(self):
        for change in ("metadata", "data_path", "asset_path", "name", "word", "bytes", "byte_bounds", "pins"):
            snapshot = self.snapshot()
            target = snapshot["target"]
            if change == "metadata":
                snapshot["pinned_expected_metadata"]["name"] = "M95_sp"
            elif change == "data_path":
                target["path"] = self.paths[17][1]
            elif change == "asset_path":
                target["asset"]["path"] = self.paths[17][1]
            elif change == "name":
                target["name"] = self.paths[17][0]
            elif change == "word":
                target["observed_native"]["words"][-1]["value"] = 5
            elif change == "bytes":
                target["observed_native"]["ammo_hex"] = "00" * 0x24
            elif change == "byte_bounds":
                target["observed_native"]["ammo_hex"] += "00"
            else:
                snapshot["pinned_expected_metadata"]["dependency_manifest_sha256"] = "0" * 64
            with self.subTest(change=change), self.assertRaises(ValueError):
                tool.bound_target(snapshot, self.source, 42, 16, pins=self.pins)

    def test_changed_pinned_selector_refuses_without_import_or_fallback(self):
        snapshot = self.snapshot()
        self.selectors.write_text("{}", encoding="utf-8")
        with self.assertRaisesRegex(ValueError, "Pinned dependency changed"):
            tool.bound_target(snapshot, self.source, 42, 16, pins=self.pins)

    def test_unverified_production_table_refuses(self):
        replaced = self.directory / "variants.h"
        replaced.write_bytes(self.production.read_bytes().replace(b'"AEK971_sp"', b'"Arbitrary"'))
        self.manifest["files"]["variants"]["path"] = str(replaced)
        self.pins.write_text(json.dumps(self.manifest), encoding="utf-8")
        with self.assertRaisesRegex(ValueError, "Pinned dependency changed: variants"):
            tool.load_pins(self.pins)

    def test_consumer_retains_identity_sequence_completeness_and_address_gates(self):
        for change in ("identity", "sequence", "complete", "accepted", "unaligned", "bounds", "relationship"):
            snapshot, fresh = self.snapshot(), copy.deepcopy(self.source)
            if change == "identity":
                fresh["identity"]["session"] += 1
            elif change == "sequence":
                fresh["source_sequence"] -= 1
            elif change in ("complete", "accepted"):
                snapshot[change] = False
            elif change == "unaligned":
                snapshot["target"]["asset"]["address"] += 1
            elif change == "bounds":
                snapshot["target"]["asset"]["address"] = tool.END
            else:
                snapshot["target"]["asset"]["object"] += 4
            with self.subTest(change=change), self.assertRaises(ValueError):
                tool.bound_target(snapshot, fresh, 42, 16, pins=self.pins)

    def test_cli_default_and_explicit_variant_reach_source_gate(self):
        for variant in (0, 16):
            output = self.directory / (str(variant) + ".json")
            args = ["--session-dir", str(self.directory), "--pid", "42", "--output", str(output),
                    "--pins", str(self.pins)]
            if variant:
                args += ["--variant", str(variant)]
            with self.subTest(variant=variant), \
                    mock.patch.object(tool, "load_dependencies", return_value=(self.manifest, self.blobs, {})), \
                    mock.patch.object(tool, "read_source", side_effect=tool.Incomplete("fixture source unavailable")), \
                    mock.patch("sys.stdout", new_callable=io.StringIO):
                self.assertEqual(tool.main(args), 3)
            report = json.loads(output.read_text(encoding="utf-8"))
            self.assertEqual(report["variant"], variant)
            self.assertEqual(report["pinned_expected_metadata"]["name"], self.paths[variant][0])
            self.assertEqual(report["refusal"], "fixture source unavailable")
            self.assertFalse(report["accepted"])

    def test_cli_publishes_exact_read_failure_and_closes_process(self):
        output = self.directory / "read-failure.json"
        image = mock.Mock(data=b"fixture image")
        expected = tool.digest(image.data)
        source = copy.deepcopy(self.source)
        source["identity"]["executable_sha256"] = expected
        process = mock.Mock(base=0x400000)
        process.read.side_effect = OSError(299, "fixture partial copy")

        def query(address):
            if address < 0x20000:
                return (0x10000, 65536, 0x1000, 0x20000, 4)
            return (0x20000, tool.END - 0x20000, 0x10000, 0, 0)

        def selected_reader(bounded, *_):
            return mock.Mock(p=bounded)

        modules = {
            "capture_reload_state": mock.Mock(Image=mock.Mock(return_value=image)),
            "read_flynn_catalog": mock.Mock(EXPECTED=expected),
            "read_bc2": mock.Mock(Process=mock.Mock(return_value=process)),
            "read_weapon_resources": mock.Mock(SelectedResourceReader=selected_reader),
            "read_stock_sniper_resources": mock.Mock(ASSET_TABLE=0x1474554)}
        args = ["--session-dir", str(self.directory), "--pid", "42", "--output", str(output),
                "--pins", str(self.pins)]
        with mock.patch.object(tool, "load_dependencies", return_value=(self.manifest, self.blobs, modules)), \
                mock.patch.object(tool, "read_source", return_value=source), \
                mock.patch.object(tool, "verify_process"), \
                mock.patch.object(tool, "WinQuery", return_value=query), \
                mock.patch("sys.stdout", new_callable=io.StringIO):
            self.assertEqual(tool.main(args), 3)
        report = json.loads(output.read_text(encoding="utf-8"))
        self.assertFalse(report["accepted"])
        self.assertFalse(report["complete"])
        self.assertNotIn("coverage", report)
        self.assertNotIn("target", report)
        self.assertEqual(report["read_failure"]["stage"], "scan_chunk")
        self.assertEqual(report["read_failure"]["address"], 0x10000)
        self.assertEqual(report["read_failure"]["requested_bytes"], 65536)
        self.assertIsNone(report["read_failure"]["received_bytes"])
        self.assertEqual(report["read_failure"]["errno"], 299)
        self.assertEqual(report["read_failure"]["error"], str(process.read.side_effect))
        process.read.assert_called_once_with(0x10000, 65536)
        process.close.assert_called_once_with()


if __name__ == "__main__":
    parser = argparse.ArgumentParser(add_help=False)
    parser.add_argument("--reader-pins", type=Path)
    options, remaining = parser.parse_known_args()
    READER_PINS = options.reader_pins
    unittest.main(argv=[sys.argv[0], *remaining])
