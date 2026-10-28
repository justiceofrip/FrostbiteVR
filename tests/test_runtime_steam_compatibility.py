"""Synthetic executable/CLI checks; no game, process handles, input or disk writes.

Set FVR_RUNTIME_TEST_ROOT to an unpacked payload's runtime directory to exercise
the exact packaged helper. Patching the candidate digest enrolls only these
synthetic bytes in a test; it does not validate the unavailable Steam executable.
"""
import contextlib
import hashlib
import importlib.util
import io
import json
import os
from pathlib import Path
import struct
import sys
import types
import unittest
from unittest import mock


sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[1]
RUNTIME = Path(os.environ.get('FVR_RUNTIME_TEST_ROOT', ROOT / 'launcher/runtime'))
HELPER = RUNTIME / 'wait_main_menu.py'
if not HELPER.is_file():
    HELPER = RUNTIME / 'tools/wait_main_menu.py'
SPEC = importlib.util.spec_from_file_location('steam_menu_under_test', HELPER)
menu = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(menu)

NT = 0x80
OPTIONAL = NT + 24
SECTION = OPTIONAL + 224
RENDERER_GLOBAL = 0x1901000
GAME_GLOBAL = 0x1901004


def renderer_signatures():
    return (bytes.fromhex('55 8b ec 83 e4 f0 81 ec 44 01 00 00 53 56 8b 35')
            + struct.pack('<I', RENDERER_GLOBAL)
            + bytes.fromhex('8b 06 8b 50 18 57 8b f9 8b ce ff d2 00 56 8b 35')
            + struct.pack('<I', GAME_GLOBAL)
            + bytes.fromhex('8b 8e d8 09 00 00 e8 00 00 00 00 8b ce e8 00 00 00 00 8b 8e d8 09 00 00 5e e9'))


def synthetic_pe():
    raw = bytearray(0x400)
    raw[:2] = b'MZ'
    struct.pack_into('<I', raw, 0x3c, NT)
    raw[NT:NT + 4] = b'PE\0\0'
    struct.pack_into('<HHI', raw, NT + 4, 0x14c, 1, 1314210909)
    struct.pack_into('<H', raw, NT + 20, 224)
    struct.pack_into('<H', raw, OPTIONAL, 0x10b)
    struct.pack_into('<I', raw, OPTIONAL + 28, 0x400000)
    struct.pack_into('<I', raw, OPTIONAL + 56, 0x1913000)
    raw[SECTION:SECTION + 8] = b'.text\0\0\0'
    struct.pack_into('<IIII', raw, SECTION + 8, 0x200, 0x1000, 0x200, 0x200)
    struct.pack_into('<I', raw, SECTION + 36, 0x60000020)
    signatures = renderer_signatures()
    raw[0x200:0x200 + len(signatures)] = signatures
    return bytes(raw)


def changed(raw, offset, fmt, value):
    result = bytearray(raw)
    struct.pack_into(fmt, result, offset, value)
    return bytes(result)


def digest(raw):
    return hashlib.sha256(raw).hexdigest()


class ExecutableAdmissionTests(unittest.TestCase):
    def test_known_exact_image_keeps_existing_non_candidate_path(self):
        # Existing renderer tests use this signature-only image. The known
        # fingerprint remains trusted without imposing the candidate PE gate.
        raw = renderer_signatures()
        with mock.patch.object(menu, 'SUPPORTED_GAME_SHA', digest(raw)):
            menu.validate_executable(raw, digest(raw))
            menu.validate_executable(raw, digest(raw), experimental_steam=True)

    def test_candidate_requires_explicit_opt_in(self):
        raw = synthetic_pe()
        with mock.patch.object(menu, 'STEAM_GAME_SHA', digest(raw)):
            with self.assertRaises(ValueError):
                menu.validate_executable(raw, digest(raw))
            with self.assertRaises(ValueError):
                menu.validate_executable(raw, digest(raw), experimental_steam=False)
            menu.validate_executable(raw, digest(raw), experimental_steam=True)

    def test_unrelated_image_is_not_admitted_by_opt_in(self):
        raw = synthetic_pe()
        # Same structural metadata is insufficient: this hash was not enrolled.
        with mock.patch.object(menu, 'STEAM_GAME_SHA', '0' * 64), \
                mock.patch.object(menu, 'SUPPORTED_GAME_SHA', '1' * 64), \
                self.assertRaises(ValueError):
            menu.validate_executable(raw, digest(raw), experimental_steam=True)

    def test_real_hash_recheck_rejects_changed_bytes_for_both_policies(self):
        raw = synthetic_pe()
        altered = changed(raw, len(raw) - 1, '<B', 1)
        for constant, opted in (('SUPPORTED_GAME_SHA', False), ('STEAM_GAME_SHA', True)):
            with self.subTest(policy=constant), mock.patch.object(menu, constant, digest(raw)), \
                    self.assertRaises(ValueError):
                menu.validate_executable(altered, digest(raw), experimental_steam=opted)

    def test_candidate_identity_fields_are_checked_after_hash_match(self):
        raw = synthetic_pe()
        cases = {
            'DOS signature': changed(raw, 0, '<H', 0),
            'PE signature': changed(raw, NT, '<I', 0),
            'machine': changed(raw, NT + 4, '<H', 0x8664),
            'timestamp': changed(raw, NT + 8, '<I', 1314210910),
            'optional magic': changed(raw, OPTIONAL, '<H', 0x20b),
            'image base': changed(raw, OPTIONAL + 28, '<I', 0x500000),
            'image size': changed(raw, OPTIONAL + 56, '<I', 0x1914000),
        }
        for name, candidate in cases.items():
            with self.subTest(field=name), mock.patch.object(menu, 'STEAM_GAME_SHA', digest(candidate)), \
                    self.assertRaises(ValueError):
                menu.validate_executable(candidate, digest(candidate), experimental_steam=True)

    def test_candidate_header_bounds_fail_as_validation_errors(self):
        raw = synthetic_pe()
        cases = {
            'short DOS': raw[:63],
            'NT inside DOS': changed(raw, 0x3c, '<I', 63),
            'NT outside file': changed(raw, 0x3c, '<I', 0xffffffff),
            'truncated COFF': raw[:NT + 23],
            'no sections': changed(raw, NT + 6, '<H', 0),
            'too many sections': changed(raw, NT + 6, '<H', 97),
            'short optional size': changed(raw, NT + 20, '<H', 223),
            'truncated optional header': raw[:OPTIONAL + 223],
            'truncated section table': raw[:SECTION + 39],
        }
        for name, candidate in cases.items():
            with self.subTest(bound=name), mock.patch.object(menu, 'STEAM_GAME_SHA', digest(candidate)), \
                    self.assertRaises(ValueError):
                menu.validate_executable(candidate, digest(candidate), experimental_steam=True)

    def test_candidate_section_extents_are_bounded(self):
        raw = synthetic_pe()
        cases = {
            'raw pointer': changed(raw, SECTION + 20, '<I', 0xffffff00),
            'raw size': changed(raw, SECTION + 16, '<I', 0xffffffff),
            'virtual address': changed(raw, SECTION + 12, '<I', 0x1913000),
            'virtual size': changed(raw, SECTION + 8, '<I', 0xffffffff),
        }
        for name, candidate in cases.items():
            with self.subTest(bound=name), mock.patch.object(menu, 'STEAM_GAME_SHA', digest(candidate)), \
                    self.assertRaises(ValueError):
                menu.validate_executable(candidate, digest(candidate), experimental_steam=True)


class MenuCliAdmissionTests(unittest.TestCase):
    def run_cli(self, raw, opted, expected_hash=None, known=False):
        """Run actual argparse/admission/stability flow with only fake readers."""
        expected_hash = expected_hash or digest(raw)
        args = ['wait_main_menu.py', '--pid', '1', '--creation-filetime', '2',
                '--exe', 'synthetic-bc2.exe', '--expected-sha256', expected_hash,
                '--output', 'synthetic-menu-ready.json']
        if opted:
            args.append('--experimental-steam')
        process = types.SimpleNamespace(close=mock.Mock())
        construct = mock.Mock(return_value=process)
        row = dict(manager=1, owner=2, renderer=3, game_renderer=4,
                   graphics_api='D3D11', width=1920, height=1080)
        error = code = None
        with mock.patch.object(menu, 'SUPPORTED_GAME_SHA' if known else 'STEAM_GAME_SHA', expected_hash), \
                mock.patch.object(sys, 'argv', args), \
                mock.patch.dict(sys.modules, {
                    'read_bc2': types.SimpleNamespace(Process=construct),
                    'runtime_process': types.SimpleNamespace(creation_filetime=lambda _p: 2)}), \
                mock.patch.object(Path, 'exists', return_value=False), \
                mock.patch.object(Path, 'read_bytes', return_value=raw), \
                mock.patch.object(Path, 'write_text') as output, \
                mock.patch.object(menu.time, 'monotonic', side_effect=range(20)), \
                mock.patch.object(menu.time, 'sleep'), \
                mock.patch.object(menu, 'sample_menu_renderer', return_value=row) as sample, \
                contextlib.redirect_stdout(io.StringIO()):
            try:
                code = menu.main()
            except ValueError as caught:
                error = caught
        result = json.loads(output.call_args.args[0]) if output.called else None
        return code, error, result, construct, process.close, sample

    def test_cli_opt_in_reaches_three_stable_reads_and_retains_dimensions(self):
        code, error, result, construct, close, sample = self.run_cli(synthetic_pe(), True)
        self.assertIsNone(error)
        self.assertEqual(code, 0)
        self.assertTrue(result['passed'])
        self.assertEqual((result['width'], result['height'], result['stable_samples']), (1920, 1080, 3))
        self.assertEqual((construct.call_count, close.call_count, sample.call_count), (3, 3, 3))
        self.assertTrue(result['read_only'])
        self.assertFalse(result['sent_input'])
        self.assertFalse(result['injected_module'])

    def test_cli_known_baseline_still_works_without_candidate_flag(self):
        code, error, result, construct, close, _ = self.run_cli(renderer_signatures(), False, known=True)
        self.assertIsNone(error)
        self.assertEqual(code, 0)
        self.assertTrue(result['passed'])
        self.assertEqual(construct.call_count, close.call_count)

    def test_rejected_candidates_never_open_a_process_or_report_ready(self):
        good = synthetic_pe()
        bad_base = changed(good, OPTIONAL + 28, '<I', 0x500000)
        for name, raw, opted, expected in (
                ('opt in absent', good, False, digest(good)),
                ('file changed', good + b'changed', True, digest(good)),
                ('wrong base', bad_base, True, digest(bad_base))):
            with self.subTest(case=name):
                code, error, result, construct, close, sample = self.run_cli(raw, opted, expected)
                self.assertIsNone(code)
                self.assertIsInstance(error, ValueError)
                self.assertFalse(result and result.get('passed'))
                construct.assert_not_called()
                close.assert_not_called()
                sample.assert_not_called()


if __name__ == '__main__':
    unittest.main()
