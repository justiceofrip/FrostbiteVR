"""BPF1 saved-wire regressions; constructed fixtures prove no native behavior.

BC2_PROVISION_PREFLIGHT_REPLAY optionally selects the retained actual104/01
provision-observe directory for the pinned Expired 5/8 receipt regression.
"""
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import struct
import unittest


MODULE = Path(__file__).resolve().parents[1] / "tools" / "read_bc2_provision_preflight.py"
spec = importlib.util.spec_from_file_location("read_bc2_provision_preflight", MODULE)
reader = importlib.util.module_from_spec(spec)
spec.loader.exec_module(reader)


def fixture(status=10, preflight=5):
    control, request, receipt, admission, sidecar = (bytearray(size) for size in (1344, 1264, 1272, 1200, 1496))
    for blob, magic in ((control, 0x31464342), (request, 0x31504342), (receipt, 0x31524342),
                        (admission, 0x31414342), (sidecar, 0x31465042)):
        struct.pack_into("<3I", blob, 0, magic, 1, len(blob))
    identity = bytearray(1080)
    struct.pack_into("<IIQQ", identity, 0, 123, 0, 456, 456)
    identity[24:56] = bytes(range(32))
    path = "G:\\BC2\\BFBC2Game.exe".encode("utf-16-le")
    identity[56:56 + len(path)] = path
    for blob in (request, receipt, admission):
        blob[16:1096] = identity
        struct.pack_into("<2Q", blob, 1096, 2, 77)
    observed = 201 if status == 10 else 190
    struct.pack_into("<2q", request, 1120, 100, 200)
    struct.pack_into("<I", receipt, 12, status)
    struct.pack_into("<Q", receipt, 1112, preflight)
    struct.pack_into("<q", receipt, 1136, observed)
    struct.pack_into("<4q4I", admission, 1112, 110, 100, 200, observed, status, 1, 1, 0)
    control[16:1216] = sidecar[16:1216] = admission
    struct.pack_into("<3Q", sidecar, 1216, 0, 500, 507)
    struct.pack_into("<2Q4I", sidecar, 1240, 501, 507, 7, 4, 1, 2)
    struct.pack_into("<2Q4I", sidecar, 1272, 501, 507, 3, 2, 0, 1)
    struct.pack_into("<I", sidecar, 1304, 4)
    struct.pack_into("<10I", sidecar, 1312, 5, 1, 1, 2, 1, 0, 1, 1, 1, 1)
    struct.pack_into("<13I", sidecar, 1352, 8, 0, 0, 0, 0, 2, 0, 0, 0, 0, 0, 0, 0)
    struct.pack_into("<13I", sidecar, 1404, 7, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0)
    struct.pack_into("<4I", sidecar, 1456, 6, 6, 1, 1)
    struct.pack_into("<6I", sidecar, 1472, 7, 2, 1, 1, 1, 1)
    return control, request, receipt, admission, sidecar


class PreflightReaderTests(unittest.TestCase):
    def test_successfully_joins_and_decodes_named_offsets(self):
        result = reader.inspect(*fixture())
        self.assertEqual(result["state"], "KNOWN")
        self.assertIsNone(result["reason"])
        terminal, sidecar = result["terminal"], result["preflight"]
        self.assertEqual((terminal["status_name"], terminal["preflight"], terminal["preflight_required"]),
                         ("Expired", 5, 8))
        self.assertFalse(terminal["preflight_met"])
        self.assertEqual((sidecar["nonce"], sidecar["job"], sidecar["scope"]), (2, 77, 1))
        self.assertEqual((sidecar["server_sequence_at_submit"], sidecar["server_sequence_latest"]), (500, 507))
        self.assertEqual(sidecar["maximum_native_depth"], 4)
        self.assertEqual(sidecar["outcomes"]["Accepted"], dict(count=5, saturated=False, lower_bound=False))
        self.assertEqual(sidecar["boundary"]["ServerAge"]["count"], 2)
        self.assertEqual(sidecar["revalidated_boundary"]["ServerAge"]["count"], 1)
        self.assertEqual(sidecar["native_depth"][">=3"]["count"], 1)
        self.assertEqual(sidecar["server_events"]["Invalid"]["count"], 2)
        self.assertEqual((sidecar["seats"]["first_sequence"], sidecar["seats"]["last_sequence"]), (501, 507))
        self.assertEqual(sidecar["seats"]["counts"]["Regressed"]["count"], 1)
        self.assertEqual(sidecar["zero_depth_seats"]["counts"]["Advanced"]["count"], 3)

    def test_ready_receipt_preflight_can_exceed_this_arm_accepted_count(self):
        blobs = fixture(status=1, preflight=9)
        struct.pack_into("<I", blobs[4], 1312, 1)
        result = reader.inspect(*blobs)
        self.assertEqual(result["state"], "KNOWN")
        self.assertEqual(result["terminal"]["preflight"], 9)
        self.assertEqual(result["preflight"]["outcomes"]["Accepted"]["count"], 1)

    def test_missing_files_remain_unknown_without_inventing_zero_counts(self):
        for index in range(5):
            with self.subTest(index=index):
                blobs = list(fixture())
                blobs[index] = None
                result = reader.inspect(*blobs)
                self.assertEqual(result["state"], "UNKNOWN")
                self.assertIsNone(result["preflight"])
                self.assertTrue(result["reason"].startswith("missing"))
                self.assertEqual(result["terminal"] is not None, index == 4)

    def test_partial_or_oversized_files_remain_unknown(self):
        for index in range(5):
            for extra in (False, True):
                with self.subTest(index=index, extra=extra):
                    blobs = list(fixture())
                    blobs[index] = blobs[index] + b"\0" if extra else blobs[index][:-1]
                    result = reader.inspect(*blobs)
                    self.assertEqual(result["state"], "UNKNOWN")
                    self.assertIsNone(result["preflight"])
                    self.assertEqual(result["terminal"] is not None, index == 4)

    def test_mismatched_quartet_cannot_supply_a_terminal_or_counter(self):
        for index, offset in ((0, 32), (1, 16), (1, 40), (1, 1096), (2, 1104),
                              (1, 1120), (1, 1128), (1, 12), (2, 12), (2, 1136), (3, 1160)):
            with self.subTest(index=index, offset=offset):
                blobs = fixture()
                blobs[index][offset] ^= 1
                result = reader.inspect(*blobs)
                self.assertEqual(result["state"], "UNKNOWN")
                self.assertIsNone(result["terminal"])
                self.assertIsNone(result["preflight"])

    def test_every_embedded_admission_join_is_exact(self):
        for offset in (0, 4, 8, 12, 16, 40, 1096, 1104, 1112, 1120, 1128, 1136, 1144, 1148, 1152, 1156, 1160, 1199):
            with self.subTest(offset=offset):
                blobs = fixture()
                blobs[4][16 + offset] ^= 1
                result = reader.inspect(*blobs)
                self.assertEqual(result["state"], "UNKNOWN")
                self.assertEqual(result["terminal"]["status_name"], "Expired")
                self.assertIsNone(result["preflight"])
                self.assertIn("embedded admission", result["reason"])

    def test_every_saturation_bit_is_a_lower_bound(self):
        groups = ((96, reader.OUTCOMES, 0, "outcomes"), (136, reader.BOUNDARIES, 10, "boundary"),
                  (188, reader.BOUNDARIES, 23, "revalidated_boundary"),
                  (240, ("0", "1", "2", ">=3"), 36, "native_depth"),
                  (256, reader.SERVER_EVENTS, 40, "server_events"),
                  (40, reader.PROGRESSION, 46, "seats"), (72, reader.PROGRESSION, 50, "zero_depth_seats"))
        for offset, names, first_bit, group in groups:
            for index, name in enumerate(names):
                with self.subTest(bit=first_bit + index):
                    blobs = fixture()
                    struct.pack_into("<I", blobs[4], 1216 + offset + 4 * index, 65535)
                    struct.pack_into("<Q", blobs[4], 1216, 1 << (first_bit + index))
                    result = reader.decode(*blobs)
                    values = result[group]["counts"] if group in ("seats", "zero_depth_seats") else result[group]
                    self.assertEqual(values[name], dict(count=65535, saturated=True, lower_bound=True))

    def test_invalid_schema_reserved_count_or_saturation_is_unknown(self):
        for offset, fmt, value in ((0, "I", 0), (4, "I", 2), (8, "I", 1495), (12, "I", 1),
                                   (1308, "I", 1), (1216, "Q", 1 << 54), (1216, "Q", 1),
                                   (1312, "I", 65535), (1312, "I", 65536)):
            with self.subTest(offset=offset, value=value):
                blobs = fixture()
                struct.pack_into("<" + fmt, blobs[4], offset, value)
                self.assertEqual(reader.inspect(*blobs)["state"], "UNKNOWN")
                with self.assertRaises(ValueError):
                    reader.decode(*blobs)

    def test_failed_receipts_preserve_status_boundary_and_incomplete_preflight(self):
        for status, name in ((9, "Refused"), (10, "Expired"), (11, "Quarantined"), (12, "Stopped")):
            blobs = fixture(status=status)
            struct.pack_into("<I", blobs[2], 1248, 5)
            result = reader.inspect(*blobs)
            self.assertEqual(result["state"], "KNOWN")
            terminal = result["terminal"]
            self.assertEqual((terminal["status"], terminal["status_name"]), (status, name))
            self.assertEqual((terminal["boundary"], terminal["boundary_name"]), (5, "ServerAge"))
            self.assertEqual((terminal["preflight"], terminal["native_calls"], terminal["retirements"]), (5, 0, 0))

    def test_terminal_decoder_validates_binding_without_success_only_validator(self):
        blob = fixture()[2]
        identity = reader._control._identity(blob[16:1096])
        decoded = reader.decode_terminal_receipt(blob, identity, 77, 2, "observe")
        self.assertEqual(decoded["status_name"], "Expired")
        for job, nonce, operation in ((0, 2, "observe"), (77, 0, "observe"), (77, 3, "observe"), (77, 2, "invalid")):
            with self.assertRaises(ValueError):
                reader.decode_terminal_receipt(blob, identity, job, nonce, operation)
        for offset, value in ((12, 3), (1232, 9), (1248, 13), (1252, 2), (1256, 2),
                              (1260, 2), (1264, 2), (1268, 1), (20, 1), (200, 1)):
            changed = bytearray(blob)
            struct.pack_into("<I", changed, offset, value)
            with self.assertRaises(ValueError):
                reader.decode_terminal_receipt(changed, identity, 77, 2, "observe")

    @unittest.skipUnless(os.environ.get("BC2_PROVISION_PREFLIGHT_REPLAY"), "retained actual104/01 quartet required")
    def test_retained_actual104_01_is_expired_five_of_eight_with_unknown_preflight_details(self):
        root = Path(os.environ["BC2_PROVISION_PREFLIGHT_REPLAY"])
        pins = {
            "control.bin": "21867ca3ba9465f9159e5ed2ab22462241e12483e343810b1ceba0ad5e9346e4",
            "request.bin": "398abedd9eb9f865fca3a2a8e22ff92411a7c2dfb0cb1de2023a7a7c57a0bd3d",
            "receipt.bin": "c719a9b68534b69edccb75800e112277e2b0a9759df84c064c9861b99b0a9b8b",
            "admission.bin": "39f2fde5f855cf0b8425af771e6afc3ac7418f61b76114c215cc1f9ac77f8385",
        }
        blobs = []
        for name, expected in pins.items():
            blob = (root / name).read_bytes()
            self.assertEqual(hashlib.sha256(blob).hexdigest(), expected)
            blobs.append(blob)
        result = reader.inspect(*blobs)
        self.assertEqual(result["state"], "UNKNOWN")
        self.assertIsNone(result["preflight"])
        terminal = result["terminal"]
        self.assertEqual((terminal["status"], terminal["status_name"], terminal["preflight"], terminal["preflight_required"]),
                         (10, "Expired", 5, 8))
        self.assertEqual((terminal["nonce"], terminal["job"], terminal["native_calls"]), (1, 1285001, 0))
        self.assertEqual((terminal["boundary"], terminal["boundary_name"], terminal["phase"]), (8, "NativeDepth", 0))
        self.assertEqual(terminal["retirements"], 0)
        self.assertFalse(terminal["input_excluded"])
        attempt = json.loads((root / "attempt.json").read_text(encoding="utf-8"))
        self.assertEqual(terminal["identity"], attempt["source_identity"])


if __name__ == "__main__":
    unittest.main()
