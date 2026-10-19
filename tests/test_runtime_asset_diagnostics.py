"""Preparation failures report the failing stage without publishing a cache."""
import hashlib
import importlib.util
import json
from pathlib import Path
import sys
import shutil
import uuid
from types import SimpleNamespace
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
spec = importlib.util.spec_from_file_location('asset_diagnostics', ROOT / 'launcher/runtime/prepare_runtime_assets.py')
runtime = importlib.util.module_from_spec(spec)
spec.loader.exec_module(runtime)


class PreparationDiagnosticsTests(unittest.TestCase):
    def setUp(self):
        scratch = ROOT / 'test-temp'
        scratch.mkdir(exist_ok=True)
        self.root = scratch / ('asset-diagnostics-' + uuid.uuid4().hex)
        self.root.mkdir()
        self.addCleanup(self.cleanup)
        self.game = self.root / 'game'
        self.game.mkdir()
        (self.game / 'BFBC2Game.exe').write_bytes(b'fixture, never executable')
        self.output = self.root / 'cache'
        self.args = SimpleNamespace(game=self.game, output=self.output, ffmpeg=self.root / 'unused',
                                    recipe='a'*64, game_sha256=hashlib.sha256((self.game / 'BFBC2Game.exe').read_bytes()).hexdigest())

    def report(self):
        return json.loads((self.output / 'preparation.json').read_text())

    def cleanup(self):
        assert self.root.resolve().parent == (ROOT / 'test-temp').resolve()
        shutil.rmtree(self.root)

    def test_body_failure_has_exact_stage_and_prevents_later_stages(self):
        self.output.mkdir()
        (self.output / 'assets.json').write_text('old success marker')
        with patch.object(runtime, 'body_main', side_effect=ValueError('required mesh resource differs')), \
                patch.object(runtime, 'pump_prepare') as pump, patch.object(runtime, 'shell_prepare') as shell:
            with self.assertRaisesRegex(ValueError, 'required mesh'):
                runtime.prepare_assets(self.args, self.root)
            pump.assert_not_called()
            shell.assert_not_called()
        report = self.report()
        self.assertEqual((report['state'], report['stage']), ('failed', 'body equipment'))
        self.assertIn('required mesh resource differs', report['error'])
        self.assertFalse((self.output / 'assets.json').exists())
        self.assertFalse(report['game_started'])

    def test_audio_failure_is_distinguished_and_bounded(self):
        for function, stage in [('pump_prepare', 'pump audio'), ('shell_prepare', 'shell reload audio')]:
            with self.subTest(stage=stage), patch.object(runtime, 'body_main'), patch.object(runtime, 'pump_prepare'), patch.object(runtime, 'shell_prepare'):
                getattr(runtime, function).side_effect = OSError('decoder failed\n' + 'x'*10000)
                with self.assertRaises(OSError):
                    runtime.prepare_assets(self.args, self.root)
                report = self.report()
                self.assertEqual(report['stage'], stage)
                self.assertLessEqual(len(report['error']), 1200)
                self.assertNotIn('\n', report['error'])
                self.assertFalse((self.output / 'assets.json').exists())

    def test_changed_executable_fails_before_extracting_assets(self):
        self.args.game_sha256 = 'b'*64
        with patch.object(runtime, 'body_main') as body, self.assertRaisesRegex(ValueError, 'executable changed'):
            runtime.prepare_assets(self.args, self.root)
        body.assert_not_called()
        self.assertEqual(self.report()['stage'], 'game fingerprint')

    def test_success_hashes_all_outputs_before_publishing_complete(self):
        def body(_):
            (self.output / 'body-ammo-assets.fvrprop').write_bytes(b'fixture body')
        def audio(_, __, path):
            path.mkdir(exist_ok=True)
            names = ('spas-rear.wav', 'spas-closure.wav', 'mechanism-audio.json') if path.name == 'pump' else ('spas-shell-insert.wav', 'shell-reload-audio.json')
            for name in names:
                (path / name).write_bytes(name.encode())
        with patch.object(runtime, 'body_main', side_effect=body), patch.object(runtime, 'pump_prepare', side_effect=audio), patch.object(runtime, 'shell_prepare', side_effect=audio):
            runtime.prepare_assets(self.args, self.root)
        manifest = json.loads((self.output / 'assets.json').read_text())
        self.assertEqual(len(manifest['files']), 6)
        for row in manifest['files']:
            self.assertEqual(row['sha256'], hashlib.sha256((self.output / row['path']).read_bytes()).hexdigest())
        self.assertEqual(self.report()['state'], 'complete')


if __name__ == '__main__':
    unittest.main()
