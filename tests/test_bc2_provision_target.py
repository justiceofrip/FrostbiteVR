"""Selected-asset refusal regression; no process or historical files required."""
import importlib.util
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("provision_target", ROOT / "tools/read_bc2_provision_target.py")
tool = importlib.util.module_from_spec(spec)
spec.loader.exec_module(tool)


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
        return 0x20000

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

    def test_selector_change_during_read_refuses(self):
        reader = tool.ExactReader(Reader("Objects/Weapons/Other", {}), {"path": self.target})
        with self.assertRaisesRegex(tool.Incomplete, "selector changed"):
            reader.once(0x11000, 0x1474554)

    def test_unverified_asset_name_offset_refuses(self):
        reader = tool.ExactReader(Reader(self.target, name_offset=16), {"path": self.target})
        with self.assertRaisesRegex(tool.Incomplete, "name offset"):
            reader.once(0x11000, 0x1474554)


if __name__ == "__main__":
    unittest.main()
