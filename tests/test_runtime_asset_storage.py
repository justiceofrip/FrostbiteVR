import importlib.util
import ntpath
from pathlib import Path, PureWindowsPath
import sys
from types import SimpleNamespace
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
spec = importlib.util.spec_from_file_location('runtime_asset_storage', ROOT / 'launcher/runtime/prepare_runtime_assets.py')
runtime = importlib.util.module_from_spec(spec)
spec.loader.exec_module(runtime)


class FakePath(PureWindowsPath):
    links = set()

    def is_symlink(self):
        return str(self) in self.links

    def is_junction(self):
        return False

    def resolve(self):
        return self.__class__(ntpath.normpath(str(self)))


class AssetStorageTests(unittest.TestCase):
    def setUp(self):
        FakePath.links = set()

    def test_system_drive_and_other_fixed_drive_are_allowed_without_disk_access(self):
        with patch.object(runtime, 'drive_type', return_value=3) as disk:
            for drive in ('C:', 'D:'):
                runtime.validate_cache_path(FakePath(drive + '/BC2VR/data/assets'), FakePath(drive + '/BC2VR/versions/0.1.6'))
            self.assertEqual([x.args[0] for x in disk.call_args_list], ['C:\\', 'D:\\'])

    def test_relative_unc_and_mapped_network_cache_reject(self):
        with patch.object(runtime, 'drive_type', return_value=4):
            for path in ('relative/cache', 'C:relative', '//server/share/cache', 'Z:/mapped/cache'):
                with self.subTest(path=path), self.assertRaises(ValueError):
                    runtime.validate_cache_path(FakePath(path), FakePath('D:/BC2VR/versions/0.1.6'))

    def test_versioned_package_cannot_become_a_cache(self):
        package = FakePath('C:/BC2VR/versions/0.1.6')
        with patch.object(runtime, 'drive_type', return_value=3):
            for path in (package, package / 'runtime/cache'):
                with self.assertRaisesRegex(ValueError, 'outside the versioned'):
                    runtime.validate_cache_path(path, package)

    def test_parent_links_are_rejected_before_resolving(self):
        FakePath.links = {str(FakePath('C:/BC2VR/data'))}
        with patch.object(runtime, 'drive_type', return_value=3), self.assertRaisesRegex(ValueError, 'Linked cache'):
            runtime.validate_cache_path(FakePath('C:/BC2VR/data/cache'), FakePath('C:/BC2VR/versions/0.1.6'))

    def test_main_rejects_package_alias_after_collapsing_parent_segments(self):
        alias = FakePath(str(ROOT.parent)) / 'outside' / '..' / ROOT.name / 'runtime-cache'
        with patch.object(runtime, 'drive_type', return_value=3), patch.object(
            runtime.argparse.ArgumentParser, 'parse_args', return_value=SimpleNamespace(output=alias)
        ), self.assertRaisesRegex(ValueError, 'outside the versioned'):
            runtime.main()


if __name__ == '__main__':
    unittest.main()
