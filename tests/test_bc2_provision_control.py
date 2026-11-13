"""Saved-wire regressions; constructed fixtures do not establish native success.

Set BC2_PROVISION_RECEIPT_REPLAY to the retained private98/xr-game05/provision-equip
directory for the pinned same-owner capture regression. It never drives BC2.
"""
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import struct
import unittest

MODULE = Path(__file__).resolve().parents[1] / "tools" / "read_bc2_provision_control.py"
spec = importlib.util.spec_from_file_location("read_bc2_provision_control", MODULE)
reader = importlib.util.module_from_spec(spec)
spec.loader.exec_module(reader)


def fixture():
    request, receipt, admission, control = (bytearray(size) for size in (1264, 1272, 1200, 1344))
    for data, magic in ((request, 0x31504342), (receipt, 0x31524342),
                        (admission, 0x31414342), (control, 0x31464342)):
        struct.pack_into("<3I", data, 0, magic, 1, len(data))
    for data in (request, receipt, admission):
        struct.pack_into("<I", data, 16, 123)
        struct.pack_into("<2Q", data, 1096, 1, 77)
    struct.pack_into("<2q", request, 1120, 100, 200)
    struct.pack_into("<I", receipt, 12, 10)
    struct.pack_into("<q", receipt, 1136, 201)
    struct.pack_into("<4q4I", admission, 1112, 110, 100, 200, 201, 10, 1, 1, 0)
    control[16:1216] = admission
    struct.pack_into("<4I", control, 1216, 1, 31, 2, 0)
    struct.pack_into("<qQ10I", control, 1232, 120, 9, 123, 0x6b0999, 2, 3, 1, 1, 0, 0, 0, 0)
    struct.pack_into("<qQ10I", control, 1288, 130, 10, 124, 0x6b0999, 2, 3, 2, 2, 3, 2, 1, 0)
    return control, request, receipt, admission


def successful_receipt(operation="equip"):
    blob = bytearray(1272)
    process = dict(pid=123, creation=456, session=456, executable="G:\\BC2\\BFBC2Game.exe",
                   executable_sha256=bytes(range(32)).hex())
    struct.pack_into("<4I", blob, 0, 0x31524342, 1, 1272, 6 if operation == "equip" else 1)
    struct.pack_into("<IIQQ", blob, 16, process["pid"], 0, process["creation"], process["session"])
    blob[40:72] = bytes.fromhex(process["executable_sha256"])
    path = process["executable"].encode("utf-16-le")
    blob[72:72 + len(path)] = path
    struct.pack_into("<5Q2q", blob, 1096, 2, 77, 8, 0, 0, 200, 0)
    owner = (0x10001, 0x10002, 0x10003, 0x10004, 1, 2, 3)
    struct.pack_into("<4I3Q", blob, 1192, *owner)
    if operation == "equip":
        struct.pack_into("<2Q", blob, 1120, 1, 9)
        struct.pack_into("<q", blob, 1144, 150)
        struct.pack_into("<4I3Q", blob, 1152, *owner)
        struct.pack_into("<10I", blob, 1232, 3, 1, 0x20001, 124, 0, 1, 1, 0, 1, 0)
    return blob, process


class ReceiptReaderTests(unittest.TestCase):
    def test_equipped_accepts_same_owner_and_preserves_named_fields(self):
        blob, process = successful_receipt()
        result = reader.decode_receipt(blob, process, 77, 2, "equip")
        self.assertEqual(result, dict(
            status=6, identity=process, nonce=2, job=77, preflight=8, retirements=1,
            native_calls=1, dispatch_sequence=9, dispatch_ns=150, observed_ns=200,
            previous=[0x10001, 0x10002, 0x10003, 0x10004, 1, 2, 3],
            current=[0x10001, 0x10002, 0x10003, 0x10004, 1, 2, 3],
            returned_item=0x20001, thread=124, phase=3, input_excluded=False,
            old_item_retired=True, server_item_observed=True, client_server_converged=True))

    def test_equipped_also_accepts_changed_owner(self):
        blob, process = successful_receipt()
        struct.pack_into("<Q", blob, 1224, 4)
        result = reader.decode_receipt(blob, process, 77, 2, "equip")
        self.assertNotEqual(result["previous"], result["current"])

    def test_rejects_each_identity_field_and_request_join_mismatch(self):
        for offset in (16, 24, 32, 40, 72, 1096, 1104):
            with self.subTest(offset=offset):
                blob, process = successful_receipt()
                blob[offset] ^= 1
                with self.assertRaises(ValueError):
                    reader.decode_receipt(blob, process, 77, 2, "equip")
        blob, process = successful_receipt()
        for job, nonce, operation in ((0, 2, "equip"), (77, 0, "equip"),
                                      (77, 2, "observe"), (77, 2, "restore")):
            with self.subTest(job=job, nonce=nonce, operation=operation):
                with self.assertRaises(ValueError):
                    reader.decode_receipt(blob, process, job, nonce, operation)

    def test_rejects_invalid_identity_padding_and_unterminated_path(self):
        for offset in (20, 200, 1094):
            with self.subTest(offset=offset):
                blob, process = successful_receipt()
                blob[offset] = 1
                with self.assertRaises(ValueError):
                    reader.decode_receipt(blob, process, 77, 2, "equip")
        blob, process = successful_receipt()
        blob[72:1096] = b"a\0" * 512
        with self.assertRaises(ValueError):
            reader.decode_receipt(blob, process, 77, 2, "equip")

    def test_rejects_header_size_status_phase_counts_and_exclusion(self):
        for offset, value in ((0, 0), (4, 2), (8, 1200), (12, 5), (12, 8), (12, 11),
                              (1232, 2), (1232, 6), (1236, 0), (1236, 2),
                              (1240, 0), (1244, 0), (1248, 1), (1252, 0), (1252, 2),
                              (1256, 0), (1256, 2), (1260, 1), (1264, 0), (1264, 2), (1268, 1)):
            with self.subTest(offset=offset, value=value):
                blob, process = successful_receipt()
                struct.pack_into("<I", blob, offset, value)
                with self.assertRaises(ValueError):
                    reader.decode_receipt(blob, process, 77, 2, "equip")
        blob, process = successful_receipt()
        for resized in (blob[:-1], blob + b"\0"):
            with self.assertRaises(ValueError):
                reader.decode_receipt(resized, process, 77, 2, "equip")

    def test_rejects_incomplete_preflight_retirement_dispatch_or_current_owner(self):
        for offset, value in ((1112, 7), (1120, 0), (1120, 2), (1128, 0), (1136, 0), (1144, 0)):
            with self.subTest(offset=offset, value=value):
                blob, process = successful_receipt()
                struct.pack_into("<Q", blob, offset, value)
                with self.assertRaises(ValueError):
                    reader.decode_receipt(blob, process, 77, 2, "equip")
        for offset, fmt in ((1192, "I"), (1196, "I"), (1200, "I"), (1204, "I"),
                            (1208, "Q"), (1216, "Q"), (1224, "Q")):
            with self.subTest(offset=offset):
                blob, process = successful_receipt()
                struct.pack_into("<" + fmt, blob, offset, 0)
                with self.assertRaises(ValueError):
                    reader.decode_receipt(blob, process, 77, 2, "equip")

    def test_observe_requires_ready_preflight_without_assignment_or_retirement(self):
        blob, process = successful_receipt("observe")
        result = reader.decode_receipt(blob, process, 77, 2, "observe")
        self.assertEqual((result["status"], result["phase"], result["native_calls"], result["retirements"]),
                         (1, 0, 0, 0))
        for offset, value in ((12, 6), (1120, 1), (1232, 3), (1236, 1), (1260, 1), (1264, 1)):
            with self.subTest(offset=offset):
                changed = bytearray(blob)
                struct.pack_into("<I", changed, offset, value)
                with self.assertRaises(ValueError):
                    reader.decode_receipt(changed, process, 77, 2, "observe")

    @unittest.skipUnless(os.environ.get("BC2_PROVISION_RECEIPT_REPLAY"),
                         "pinned local private98/xr-game05 provisioning receipt required")
    def test_retained_equipped_capture_with_unchanged_owner(self):
        root = Path(os.environ["BC2_PROVISION_RECEIPT_REPLAY"])
        blob = (root / "receipt.bin").read_bytes()
        attempt_blob = (root / "attempt.json").read_bytes()
        self.assertEqual(hashlib.sha256(blob).hexdigest(),
                         "92ed96d87dacc3c517892a416a050f851c8720e0a1573a2c5c9dfc5f6ad2958a")
        self.assertEqual(hashlib.sha256(attempt_blob).hexdigest(),
                         "630363ea1ff36a32616140b00bfcfefd7efcc9509e20c823653dfc68f843b9ce")
        attempt = json.loads(attempt_blob)
        result = reader.decode_receipt(blob, attempt["source_identity"], attempt["job"], attempt["nonce"], "equip")
        self.assertEqual((result["status"], result["phase"], result["native_calls"], result["retirements"]),
                         (6, 3, 1, 1))
        self.assertEqual(result["previous"], result["current"])
        self.assertEqual(result["current"], [1254292336, 1254282592, 135563888, 129028160, 2, 3, 2])
        self.assertTrue(result["server_item_observed"] and result["client_server_converged"] and result["old_item_retired"])
        self.assertFalse(result["input_excluded"])


class ControlReaderTests(unittest.TestCase):
    def test_decodes_known_and_unknown_counts(self):
        result = reader.decode(*fixture())
        blocked = result["causes"]["MutexUnavailable"]["first"]
        self.assertFalse(blocked["callback_counts_known"])
        self.assertIsNone(blocked["callbacks"])
        nested = result["causes"]["ActiveCallbacks"]
        self.assertTrue(nested["saturated"])
        self.assertEqual((nested["first"]["update_depth"], nested["first"]["gather_depth"]), (2, 1))

    def test_rejects_each_changed_join(self):
        for index, offset in ((1, 16), (1, 40), (1, 1096), (2, 1104), (1, 1120), (1, 1128), (2, 12), (2, 1136), (3, 1160)):
            with self.subTest(index=index, offset=offset):
                blobs = fixture()
                blobs[index][offset] ^= 1
                with self.assertRaises(ValueError):
                    reader.decode(*blobs)

    def test_refuses_old_admission_as_new_sidecar_and_unknown_version(self):
        blobs = fixture()
        with self.assertRaises(ValueError):
            reader.decode(blobs[3], *blobs[1:])
        struct.pack_into("<I", blobs[0], 4, 2)
        with self.assertRaises(ValueError):
            reader.decode(*blobs)

    def test_rejects_unsafe_counts_and_bad_sample_windows(self):
        for offset, value in ((1224, 0), (1216, 32), (1272, 1), (1328, 4), (1324, 1)):
            with self.subTest(offset=offset):
                blobs = fixture()
                struct.pack_into("<I", blobs[0], offset, value)
                with self.assertRaises(ValueError):
                    reader.decode(*blobs)
        for now in (109, 200):
            blobs = fixture()
            struct.pack_into("<q", blobs[0], 1232, now)
            with self.assertRaises(ValueError):
                reader.decode(*blobs)

    def test_exhausted_scope_has_no_sample(self):
        blobs = fixture()
        struct.pack_into("<I", blobs[3], 1148, 0)
        blobs[0][16:1216] = blobs[3]
        with self.assertRaises(ValueError):
            reader.decode(*blobs)
        blobs[0][1216:] = bytes(128)
        result = reader.decode(*blobs)
        self.assertEqual(result["scope"], 0)
        self.assertIsNone(result["causes"]["ActiveCallbacks"]["first"])


if __name__ == "__main__":
    unittest.main()
