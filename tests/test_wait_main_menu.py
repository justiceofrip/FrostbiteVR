"""Pure reader regression: no native process, headset, game file or input."""
import importlib.util
import contextlib
import io
import json
from pathlib import Path
import struct
import sys
import types
import unittest
from unittest import mock

SPEC = importlib.util.spec_from_file_location('menu_startup_under_test',
    Path(__file__).resolve().parents[1] / 'launcher/runtime/wait_main_menu.py')
menu = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(menu)


class Memory:
    def __init__(self, width=2560, height=1440, delta=0):
        self.base = 0x400000 + delta
        self.renderer_global, self.game_global = 0x1901000, 0x1901004
        self.manager, self.owner, self.renderer, self.game = 0x2000000, 0x2100000, 0x2200000, 0x2300000
        self.values = {0x19a4ca8 + delta: self.manager, self.manager + 0x82b24: self.owner,
                       self.renderer_global + delta: self.renderer, self.game_global + delta: self.game,
                       self.renderer: menu.RENDERER_VTABLE + delta}
        for offset, value in zip((0, 0x2c, 0x90), (0x142b4e8, 0x142b4d4, 0x142b4c8)):
            self.values[self.owner + offset] = value + delta
        for offset, value in zip((20, 40, 44), (menu.RENDERER_PRESENT, menu.WIDTH_GETTER, menu.HEIGHT_GETTER)):
            self.values[menu.RENDERER_VTABLE + delta + offset] = value + delta
        self.bytes = {self.renderer + 0x18: struct.pack('<II', width, height),
                      menu.WIDTH_GETTER + delta: b'\x8b\x41\x18\xc3',
                      menu.HEIGHT_GETTER + delta: b'\x8b\x41\x1c\xc3'}
        self.reads = {}
        self.second = {}

    def value(self, address, default):
        self.reads[address] = self.reads.get(address, 0) + 1
        return self.second.get(address, default) if self.reads[address] >= 2 else default

    def u32(self, address):
        return self.value(address, self.values[address])

    def read(self, address, size):
        value = self.value(address, self.bytes[address])
        assert len(value) == size
        return value

    def sample(self):
        return menu.sample_menu_renderer(self, self.renderer_global, self.game_global)


class MenuRendererTests(unittest.TestCase):
    def run_main_fixture(self, ready_after):
        clock = types.SimpleNamespace(value=0.0)
        reader = menu.sample_menu_renderer
        def sleep(seconds):
            clock.value += seconds
        def sample(*_args):
            if clock.value < ready_after:
                raise ValueError('Native main-menu owner is not ready')
            memory = Memory()
            return reader(memory, memory.renderer_global, memory.game_global)
        process = types.SimpleNamespace(close=mock.Mock())
        raw = (bytes.fromhex('55 8b ec 83 e4 f0 81 ec 44 01 00 00 53 56 8b 35')
               + struct.pack('<I', 0x1901000)
               + bytes.fromhex('8b 06 8b 50 18 57 8b f9 8b ce ff d2 00 56 8b 35')
               + struct.pack('<I', 0x1901004)
               + bytes.fromhex('8b 8e d8 09 00 00 e8 00 00 00 00 8b ce e8 00 00 00 00 8b 8e d8 09 00 00 5e e9'))
        args = ['wait_main_menu.py', '--pid', '1', '--creation-filetime', '2', '--exe', 'synthetic-menu-image.bin',
                '--expected-sha256', menu.SUPPORTED_GAME_SHA, '--output', 'synthetic-menu-ready.json']
        with mock.patch.object(sys, 'argv', args), mock.patch.dict(sys.modules, {
                'read_bc2': types.SimpleNamespace(Process=lambda *_args, **_kwargs: process),
                'runtime_process': types.SimpleNamespace(creation_filetime=lambda _process: 2)}), \
                mock.patch.object(Path, 'exists', return_value=False), \
                mock.patch.object(Path, 'read_bytes', return_value=raw), \
                mock.patch.object(Path, 'write_text') as output, \
                mock.patch.object(menu.hashlib, 'sha256', return_value=types.SimpleNamespace(hexdigest=lambda: menu.SUPPORTED_GAME_SHA)), \
                mock.patch.object(menu.time, 'monotonic', side_effect=lambda: clock.value), \
                mock.patch.object(menu.time, 'sleep', side_effect=sleep), \
                mock.patch.object(menu, 'sample_menu_renderer', side_effect=sample), contextlib.redirect_stdout(io.StringIO()):
            code = menu.main()
        output.assert_called_once()
        return code, json.loads(output.call_args.args[0]), clock.value

    def test_startup_exits_early_and_accepts_slow_menu_without_real_wait(self):
        code, result, elapsed = self.run_main_fixture(0)
        self.assertEqual(code, 0)
        self.assertLess(elapsed, 1)
        self.assertEqual((result['width'], result['height'], result['stable_samples']), (2560, 1440, 3))
        code, result, elapsed = self.run_main_fixture(40)
        self.assertEqual(code, 0)
        self.assertTrue(result['passed'])
        self.assertGreater(elapsed, 40)
        self.assertLess(elapsed, 41)

    def test_timeout_never_reports_dimensions_or_success(self):
        code, result, elapsed = self.run_main_fixture(1000)
        self.assertEqual(code, 1)
        self.assertFalse(result['passed'])
        self.assertIsNone(result['width'])
        self.assertIsNone(result['height'])
        self.assertLessEqual(len(result['last_observations']), 20)
        self.assertGreaterEqual(elapsed, 90)
        self.assertLess(elapsed, 91)

    def test_real_dimensions_and_relocated_profile(self):
        for delta in (0, 0x1000000):
            process = Memory(delta=delta)
            row = process.sample()
            self.assertEqual((row['width'], row['height'], row['graphics_api']), (2560, 1440, 'D3D11'))
            self.assertEqual(process.reads[process.renderer + 0x18], 2)

    def test_dimension_limits(self):
        for dimensions in ((0, 1080), (1920, 0), (16385, 1), (1, 16385), (8192, 4096)):
            with self.subTest(dimensions=dimensions), self.assertRaisesRegex(ValueError, 'render size'):
                Memory(*dimensions).sample()
        self.assertEqual(Memory(8000, 4000).sample()['width'], 8000)

    def test_wrong_renderer_fails_before_offset_read(self):
        process = Memory()
        process.values[process.renderer] += 4
        with self.assertRaisesRegex(ValueError, 'DxVersion=11'):
            process.sample()
        self.assertNotIn(process.renderer + 0x18, process.reads)

    def test_layout_code_mismatch_rejected(self):
        for address in (menu.RENDERER_VTABLE + 20, menu.RENDERER_VTABLE + 40, menu.RENDERER_VTABLE + 44):
            process = Memory()
            process.values[address] += 4
            with self.subTest(address=address), self.assertRaisesRegex(ValueError, 'code differs'):
                process.sample()
        process = Memory()
        process.bytes[menu.WIDTH_GETTER] = b'\x8b\x41\x20\xc3'
        with self.assertRaisesRegex(ValueError, 'code differs'):
            process.sample()

    def test_resize_and_owner_replacements_never_publish_dimensions(self):
        process = Memory()
        process.second[process.renderer + 0x18] = struct.pack('<II', 1920, 1080)
        with self.assertRaisesRegex(ValueError, 'changed during'):
            process.sample()
        for field in (0x19a4ca8, process.manager + 0x82b24, process.owner,
                      process.renderer_global, process.game_global, process.renderer,
                      menu.RENDERER_VTABLE + 20):
            changed = Memory()
            changed.second[field] = changed.values[field] + 4
            with self.subTest(field=field), self.assertRaisesRegex(ValueError, 'changed during'):
                changed.sample()

    def test_not_in_menu_rejected(self):
        process = Memory()
        process.values[process.owner] = 0
        with self.assertRaisesRegex(ValueError, 'main-menu owner'):
            process.sample()


if __name__ == '__main__':
    unittest.main()
