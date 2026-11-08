"""Saved-wire shape/join regressions; fixtures do not establish native success."""
import importlib.util
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
