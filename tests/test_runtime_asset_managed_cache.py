"""Real filesystem and Entry -> Python tests; fixtures never execute a game."""
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
from types import SimpleNamespace
import unittest
from unittest.mock import patch
import uuid

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
SPEC = importlib.util.spec_from_file_location('managed_cache_runtime', ROOT / 'launcher/runtime/prepare_runtime_assets.py')
runtime = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(runtime)
RECIPE = 'a' * 64


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


class ManagedCacheFixture(unittest.TestCase):
    def setUp(self):
        self.scratch = ROOT / 'test-temp'
        self.scratch.mkdir(exist_ok=True)
        self.root = self.scratch / ('managed-assets-' + uuid.uuid4().hex)
        self.root.mkdir()
        self.addCleanup(self.cleanup)
        self.game = self.root / 'games/Origin Games/Battlefield Bad Company 2'
        for relative, content in (
                ('BFBC2Game.exe', b'fixture, never executable'),
                ('Dist/Win32/common.fbrb', b'stock archive sentinel'),
                ('Package/package.mft', b'stock package sentinel')):
            path = self.game / relative
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(content)
        self.stock = {path: digest(path) for path in self.game.rglob('*') if path.is_file()}
        self.set_launcher(self.game / 'BC2VR-Launcher')

    def cleanup(self):
        if self.root.exists():
            assert self.root.resolve().parent == self.scratch.resolve()
            # Tests remove their own junction explicitly before this bounded tree.
            shutil.rmtree(self.root)

    def set_launcher(self, path):
        self.launcher = path
        self.package = path / 'versions/0.1.10'
        self.tool = self.package / 'runtime/tools/prepare_runtime_assets.py'
        self.tool.parent.mkdir(parents=True, exist_ok=True)
        self.tool.write_bytes((ROOT / 'launcher/runtime/prepare_runtime_assets.py').read_bytes())
        (self.package / 'package.json').write_text(json.dumps({
            'schema': 'fvr.desktop.package.v1', 'version': '0.1.10',
            'runtime_entry': 'runtime/Entry.ps1'}))
        self.managed = path / 'data/runtime/cache'
        self.output = self.managed / RECIPE

    def validate(self, output=None, managed=None, recipe=RECIPE):
        return runtime.validate_managed_cache(
            output or self.output, managed or self.managed, self.game, self.package, recipe)

    def assert_stock_unchanged(self):
        self.assertEqual(self.stock, {path: digest(path) for path in self.stock})


class ManagedCacheTests(ManagedCacheFixture):
    def test_exact_tester_nested_layout_and_separate_launcher_are_allowed(self):
        for launcher in (self.game / 'BC2VR-Launcher', self.root / 'separate-launcher'):
            with self.subTest(launcher=launcher):
                self.set_launcher(launcher)
                self.assertEqual(self.validate(), self.output)
                self.assertFalse(self.output.exists(), 'validation must not create output')
        self.assert_stock_unchanged()

    def test_main_reproduces_tester_layout_and_reaches_preparation(self):
        self.assertIn(self.game, self.output.parents)
        args = SimpleNamespace(game=self.game, output=self.output, managed_cache_root=self.managed,
                               recipe=RECIPE, ffmpeg=self.root / 'unused', game_sha256='b' * 64)
        with patch.object(runtime, '__file__', str(self.tool)), \
                patch.object(runtime.argparse.ArgumentParser, 'parse_args', return_value=args), \
                patch.object(runtime, 'prepare_assets') as prepare:
            runtime.main()
            prepare.assert_called_once_with(args, self.package / 'runtime')
        self.assertFalse(self.output.exists())

    def test_managed_root_cannot_be_supplied_as_stock_or_another_launcher(self):
        for output, managed in (
                (self.game / 'Dist/Win32' / RECIPE, self.game / 'Dist/Win32'),
                (self.game / 'Package' / RECIPE, self.game / 'Package'),
                (self.game / RECIPE, self.game),
                (self.managed, self.managed),
                (self.output / 'extra', self.managed),
                (self.root / 'other/data/runtime/cache' / RECIPE, self.root / 'other/data/runtime/cache')):
            with self.subTest(output=output), self.assertRaises(ValueError):
                self.validate(output, managed)
        self.assert_stock_unchanged()
        self.assertFalse(self.output.exists())

    def test_installer_layout_inside_game_root_or_stock_tree_is_not_an_exception(self):
        for launcher in (self.game, self.game / 'Dist/Win32/BC2VR-Launcher',
                         self.game / 'Package/BC2VR-Launcher'):
            self.set_launcher(launcher)
            with self.subTest(launcher=launcher), self.assertRaisesRegex(ValueError, 'dedicated BC2VR-Launcher'):
                self.validate()
        self.assert_stock_unchanged()

    def test_recipe_and_traversal_are_checked_before_writes(self):
        for recipe in ('../Dist', 'b' * 63, 'A' * 64):
            with self.subTest(recipe=recipe), self.assertRaises(ValueError):
                self.validate(recipe=recipe)
        with self.assertRaisesRegex(ValueError, 'traversal'):
            self.validate(self.managed / '..' / 'cache' / RECIPE)
        self.assertFalse(self.output.exists())
        self.assert_stock_unchanged()

    def test_missing_or_wrong_package_marker_does_not_authorize_nested_cache(self):
        marker = self.package / 'package.json'
        for content in (None, '{}', '{"schema":"fvr.desktop.package.v1","version":"9.9.9"}'):
            if content is None:
                marker.unlink()
            else:
                marker.write_text(content)
            with self.subTest(content=content), self.assertRaisesRegex(ValueError, 'verified installed'):
                self.validate()
        self.assertFalse(self.output.exists())

    def test_existing_cache_and_stock_files_survive_rejected_path(self):
        self.output.mkdir(parents=True)
        old = self.output / 'assets.json'
        old.write_bytes(b'old cache completion must survive preflight rejection')
        before = digest(old)
        with self.assertRaises(ValueError):
            self.validate(managed=self.game / 'Package')
        self.assertEqual(digest(old), before)
        self.assert_stock_unchanged()

    def test_new_recipe_keeps_previous_assets_and_rejects_file_as_cache_root(self):
        self.output.mkdir(parents=True)
        prior = self.output / 'assets.json'
        prior.write_bytes(b'previous verified cache')
        newer = self.managed / ('b' * 64)
        self.assertEqual(self.validate(newer, recipe='b' * 64), newer)
        self.assertEqual(prior.read_bytes(), b'previous verified cache')
        newer.write_bytes(b'file occupying output')
        with self.assertRaisesRegex(ValueError, 'occupied by a file'):
            self.validate(newer, recipe='b' * 64)
        self.assertEqual(newer.read_bytes(), b'file occupying output')

    def test_hardlinked_output_rejects_before_any_cache_write(self):
        self.output.mkdir(parents=True)
        alias = self.output / 'body-ammo-assets.fvrprop'
        os.link(self.game / 'Dist/Win32/common.fbrb', alias)
        with self.assertRaisesRegex(ValueError, 'linked'):
            self.validate()
        self.assertFalse((self.output / 'preparation.json').exists())
        self.assert_stock_unchanged()

    @unittest.skipUnless(os.name == 'nt', 'Windows junction regression')
    def test_junction_ancestors_and_output_children_are_rejected(self):
        import _winapi
        self.managed.parent.mkdir(parents=True)
        _winapi.CreateJunction(str(self.game / 'Package'), str(self.managed))
        try:
            # Exercise the lstat guard used by supported Python3.11 as well.
            with patch.object(Path, 'is_junction', return_value=False, create=True):
                with self.assertRaisesRegex(ValueError, 'Linked cache'):
                    self.validate()
        finally:
            os.rmdir(self.managed)
        self.output.mkdir(parents=True)
        alias = self.output / 'pump'
        _winapi.CreateJunction(str(self.game / 'Package'), str(alias))
        try:
            with self.assertRaisesRegex(ValueError, 'Linked cache'):
                self.validate()
        finally:
            os.rmdir(alias)
        self.assert_stock_unchanged()

    def test_main_validates_before_generators_and_preserves_existing_receipt(self):
        self.output.mkdir(parents=True)
        prior = self.output / 'assets.json'
        prior.write_bytes(b'prior completion')
        args = SimpleNamespace(game=self.game, output=self.output, managed_cache_root=self.game,
                               recipe=RECIPE, ffmpeg=self.root / 'unused', game_sha256='b' * 64)
        with patch.object(runtime, '__file__', str(self.tool)), \
                patch.object(runtime.argparse.ArgumentParser, 'parse_args', return_value=args), \
                patch.object(runtime, 'prepare_assets') as prepare:
            with self.assertRaises(ValueError):
                runtime.main()
            prepare.assert_not_called()
        self.assertEqual(prior.read_bytes(), b'prior completion')
        self.assert_stock_unchanged()


@unittest.skipUnless(os.name == 'nt' and shutil.which('pwsh'), 'Windows PowerShell7 Entry integration')
class EntryManagedCacheTests(ManagedCacheFixture):
    def test_actual_entry_passes_managed_root_and_cache_hit_skips_preparation(self):
        for nested in (True, False):
            self.set_launcher(self.game / 'BC2VR-Launcher' if nested else self.root / 'standalone launcher')
            version = self.package
            app = version / 'app'
            app.mkdir()
            runtime_dir = version / 'runtime'
            # The Entry is byte-exact. Only its external boundary modules are
            # fixture doubles; Python and the changed asset path policy are real.
            entry_source = ROOT / 'launcher/runtime/Entry.ps1'
            entry = runtime_dir / 'Entry.ps1'
            shutil.copyfile(entry_source, entry)
            shutil.copyfile(ROOT / 'launcher/runtime/Resolution.ps1', runtime_dir / 'Resolution.ps1')
            shutil.copyfile(ROOT / 'launcher/Launcher.Storage.ps1', app / 'Launcher.Storage.ps1')
            self.assertEqual(digest(entry), digest(entry_source))
            (app / 'Launcher.Core.ps1').write_text(r'''
. (Join-Path $PSScriptRoot 'Launcher.Storage.ps1')
function Read-FvrJson($p){Get-Content -LiteralPath $p -Raw|ConvertFrom-Json -AsHashtable}
function Write-FvrJson($p,$v){[IO.File]::WriteAllText($p,($v|ConvertTo-Json -Depth 12))}
function Get-FvrChild($p,$r){Join-Path $p $r}
function Assert-FvrNoLinks($p){}
function Set-FvrLauncherStorage($p){$null=[IO.Directory]::CreateDirectory((Join-Path $p 'data/runtime'))}
function Get-Process {return $null}
''')
            (runtime_dir / 'Runtime.Core.ps1').write_text(r'''
function Get-Bc2Session($p){return $null}
function Get-Bc2RuntimeConfig {return @{version='0.1.10';asset_recipe_sha256=('a'*64);source_sha256=('c'*64)}}
function Resolve-Bc2Tool {return $env:FVR_PYTHON}
function Assert-Bc2RuntimeSpace($p){}
function Get-Bc2ExistingGame {return $null}
function Test-Bc2AssetCache($p,$r,$g) {
    $file=Join-Path $p 'assets.json';if(-not(Test-Path -LiteralPath $file)){return $false}
    $m=Read-FvrJson $file
    if($m.recipe_sha256 -ne $r -or $m.game_sha256 -ne $g -or $m.files.Count -ne 6){return $false}
    foreach($f in $m.files){if((Get-FileHash -LiteralPath (Join-Path $p $f.path)).Hash.ToLowerInvariant() -ne $f.sha256){return $false}}
    return $true
}
function Start-Bc2Child {throw 'FIXTURE_STOP_BEFORE_SUPERVISOR'}
''')
            (runtime_dir / 'Dependencies.ps1').write_text("function Get-Bc2PreparationFfmpeg {return 'unused-fixture-ffmpeg'}")
            (runtime_dir / 'Startup.ps1').write_text(r'''
function Test-Bc2GameInstallation($s,$c,$r){
    $exe=Join-Path $s.game_path 'BFBC2Game.exe';return @{exe=$exe;sha256=(Get-FileHash -LiteralPath $exe).Hash.ToLowerInvariant()}
}
function Get-Bc2OpenXrSelection {return @{label='fixture';manifest='unused'}}
function Invoke-Bc2OpenXrPreflight {return @{instance_created=$true;system_available=$true;
    eye_width=2784;recommended_eye_width=2784;eye_height=3024;recommended_eye_height=3024;
    max_eye_width=8192;max_eye_height=8192}}
function Save-Bc2AssetPreparationResult($cache,$root,$recipe,$hash,$code,$valid){
    if($code -ne 0 -or -not $valid){throw 'FIXTURE_ASSET_PREPARATION_FAILED'}
    return @{state='complete'}
}
''')
            (self.tool.parent / 'bc2_body_ammo_assets.py').write_text(r'''
from pathlib import Path
def main(argv):
    out=Path(argv[argv.index('--output')+1])
    out.write_bytes(b'fixture body')
    (out.parent/'generator-called').write_text('fixture')
''')
            audio = '''from pathlib import Path
def prepare(game, ffmpeg, output):
    output.mkdir()
    for name in {names!r}:
        (output/name).write_bytes(name.encode())
'''
            (self.tool.parent / 'bc2_mechanism_audio_cache.py').write_text(audio.format(
                names=['spas-rear.wav', 'spas-closure.wav', 'mechanism-audio.json']))
            (self.tool.parent / 'bc2_shell_reload_audio_cache.py').write_text(audio.format(
                names=['spas-shell-insert.wav', 'shell-reload-audio.json']))
            state = self.launcher / 'data/runtime'
            state.mkdir(parents=True, exist_ok=True)
            (state / 'launcher-placement.json').write_text(json.dumps({
                'schema': 'bc2vr.launcher-placement.v1', 'allowed': False, 'error': 'previous failure'}))
            settings = self.launcher / 'data/settings.json'
            settings.write_text(json.dumps({'game_path': str(self.game)}))
            env = dict(os.environ, FVR_PYTHON=sys.executable, PYTHONDONTWRITEBYTECODE='1',
                       TEMP=str(self.root), TMP=str(self.root), TMPDIR=str(self.root))
            command = [shutil.which('pwsh'), '-NoProfile', '-NonInteractive', '-File', str(entry),
                       '-Action', 'Launch', '-SettingsPath', str(settings), '-StateRoot', str(state)]
            # Prove changing the Python guard alone is insufficient: the old
            # caller (one argument omitted) must fail before preparing anything.
            current_entry = entry.read_bytes()
            omitted = b' --managed-cache-root $cacheRoot'
            self.assertEqual(current_entry.count(omitted), 1)
            entry.write_bytes(current_entry.replace(omitted, b''))
            missing = subprocess.run(command, env=env, capture_output=True, text=True, timeout=20)
            self.assertIn('--managed-cache-root', missing.stderr)
            self.assertFalse((self.output / 'assets.json').exists())
            entry.write_bytes(current_entry)
            self.assertEqual(digest(entry), digest(entry_source))
            first = subprocess.run(command, env=env, capture_output=True, text=True, timeout=20)
            self.assertIn('FIXTURE_STOP_BEFORE_SUPERVISOR', first.stderr, first.stdout + first.stderr)
            placement = json.loads((state / 'launcher-placement.json').read_text())
            self.assertTrue(placement['allowed'])
            self.assertIsNone(placement['error'])
            self.assertEqual(placement['relationship'], 'game_subfolder' if nested else 'separate')
            self.assertEqual(json.loads((self.output / 'preparation.json').read_text())['state'], 'complete')
            before = {path: digest(path) for path in self.output.rglob('*') if path.is_file()}
            # A verified existing cache must bypass all derivation on retry.
            (self.tool.parent / 'bc2_body_ammo_assets.py').write_text("raise RuntimeError('MUST_NOT_REGENERATE')")
            second = subprocess.run(command, env=env, capture_output=True, text=True, timeout=20)
            self.assertIn('FIXTURE_STOP_BEFORE_SUPERVISOR', second.stderr, second.stdout + second.stderr)
            self.assertEqual(before, {path: digest(path) for path in self.output.rglob('*') if path.is_file()})
            self.assert_stock_unchanged()

    def test_entry_refuses_unsafe_placement_before_xr_tools_or_existing_game(self):
        for launcher, relationship in ((self.game, 'game_root'),
                                       (self.game / 'Dist/Win32/BC2VR-Launcher', 'game_dist'),
                                       (self.game / 'Package/BC2VR-Launcher', 'game_package')):
            with self.subTest(relationship=relationship):
                self.set_launcher(launcher)
                app = self.package / 'app'
                app.mkdir()
                runtime_dir = self.package / 'runtime'
                entry = runtime_dir / 'Entry.ps1'
                shutil.copyfile(ROOT / 'launcher/runtime/Entry.ps1', entry)
                shutil.copyfile(ROOT / 'launcher/Launcher.Storage.ps1', app / 'Launcher.Storage.ps1')
                (app / 'Launcher.Core.ps1').write_text(r'''
. (Join-Path $PSScriptRoot 'Launcher.Storage.ps1')
function Read-FvrJson($p){Get-Content -LiteralPath $p -Raw|ConvertFrom-Json -AsHashtable}
function Write-FvrJson($p,$v){[IO.File]::WriteAllText($p,($v|ConvertTo-Json -Depth 12))}
function Set-FvrLauncherStorage($p){}
''')
                (runtime_dir / 'Runtime.Core.ps1').write_text(r'''
function Get-Bc2Session($p){return $null}
function Get-Bc2RuntimeConfig {return @{version='0.1.15';asset_recipe_sha256=('a'*64);source_sha256=('c'*64)}}
function Resolve-Bc2Tool {throw 'MUST_NOT_RESOLVE_TOOLS'}
function Get-Bc2ExistingGame {throw 'MUST_NOT_TOUCH_GAME'}
function Test-Bc2AssetCache {throw 'MUST_NOT_CHECK_CACHE'}
''')
                (runtime_dir / 'Dependencies.ps1').write_text(
                    "function Get-Bc2PreparationFfmpeg {throw 'MUST_NOT_DOWNLOAD'}")
                (runtime_dir / 'Resolution.ps1').write_text('')
                (runtime_dir / 'Startup.ps1').write_text(r'''
function Test-Bc2GameInstallation($s,$c,$r){return @{exe=(Join-Path $s.game_path 'BFBC2Game.exe');sha256=('b'*64)}}
function Get-Bc2OpenXrSelection {throw 'MUST_NOT_SELECT_XR'}
function Invoke-Bc2OpenXrPreflight {throw 'MUST_NOT_PREFLIGHT_XR'}
''')
                state = self.launcher / 'data/runtime'
                state.mkdir(parents=True, exist_ok=True)
                settings = self.launcher / 'data/settings.json'
                settings.write_text(json.dumps({'game_path': str(self.game)}))
                old_settings = settings.read_bytes()
                prior_cache = self.output / 'assets.json'
                prior_cache.parent.mkdir(parents=True, exist_ok=True)
                prior_cache.write_bytes(b'previous cache must survive refusal')
                env = dict(os.environ, PYTHONDONTWRITEBYTECODE='1',
                           TEMP=str(self.root), TMP=str(self.root), TMPDIR=str(self.root))
                for action in ('Validate', 'Launch'):
                    result = subprocess.run(
                        [shutil.which('pwsh'), '-NoProfile', '-NonInteractive', '-File', str(entry),
                         '-Action', action, '-SettingsPath', str(settings), '-StateRoot', str(state)],
                        env=env, capture_output=True, text=True, timeout=20)
                    self.assertNotEqual(result.returncode, 0)
                    self.assertIn('BC2VR-Launcher folder', result.stderr)
                    self.assertNotIn('MUST_NOT_', result.stderr)
                    report_text = (state / 'launcher-placement.json').read_text()
                    report = json.loads(report_text)
                    self.assertEqual(report['schema'], 'bc2vr.launcher-placement.v1')
                    self.assertEqual(report['relationship'], relationship)
                    self.assertFalse(report['allowed'])
                    self.assertIn('fresh, separate BC2VR-Launcher', report['error'])
                    self.assertNotIn(str(self.root), report_text)
                    self.assertEqual(settings.read_bytes(), old_settings)
                    self.assertEqual(prior_cache.read_bytes(), b'previous cache must survive refusal')
                    self.assertFalse((self.output / 'preparation.json').exists())
                    self.assert_stock_unchanged()


if __name__ == '__main__':
    unittest.main()
