"""Offline launcher settings, real WPF callbacks and Worker argument checks.

Fixtures stay under test-temp with child TEMP/TMP/TMPDIR redirected there.
The packaged runtime entry is a harmless argument recorder: no BC2, SteamVR,
native helper, real profile, update server or visible window is accessed.
"""
import base64
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
PWSH = shutil.which("pwsh")
APP_FILES = (
    "Launcher.ps1", "Worker.ps1", "Launcher.Core.ps1",
    "Launcher.Settings.ps1", "Launcher.Storage.ps1", "Launcher.Feedback.ps1",
)


@unittest.skipUnless(PWSH and os.name == "nt", "Windows PowerShell 7.5 required")
class PerformanceSettingsTests(unittest.TestCase):
    def setUp(self):
        scratch = ROOT / "test-temp"
        scratch.mkdir(exist_ok=True)
        self.temporary = tempfile.TemporaryDirectory(prefix="performance-settings-", dir=scratch)
        self.addCleanup(self.temporary.cleanup)
        self.folder = Path(self.temporary.name)
        self.environment = dict(os.environ, TEMP=str(self.folder), TMP=str(self.folder),
                                TMPDIR=str(self.folder), PYTHONDONTWRITEBYTECODE="1")

    def powershell(self, code):
        script = self.folder / "check.ps1"
        script.write_text(code, encoding="utf-8")
        result = subprocess.run(
            [PWSH, "-NoLogo", "-NoProfile", "-NonInteractive", "-ExecutionPolicy", "Bypass",
             "-File", str(script)],
            capture_output=True, text=True, timeout=30, env=self.environment,
            creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0),
        )
        self.assertEqual(result.returncode, 0, result.stderr + result.stdout)
        return json.loads(result.stdout)

    def invoke(self, action):
        payload = base64.b64encode(json.dumps({"repo": str(ROOT), "root": str(self.folder)}).encode()).decode()
        return self.powershell(r"""
$ErrorActionPreference='Stop'
$v=[Text.Encoding]::UTF8.GetString([Convert]::FromBase64String('PAYLOAD'))|ConvertFrom-Json -AsHashtable
. (Join-Path $v.repo 'launcher/Launcher.Core.ps1')
. (Join-Path $v.repo 'launcher/Launcher.Settings.ps1')
$InstallRoot=$v.root
$null=Set-FvrLauncherStorage $InstallRoot
$path=Join-Path $InstallRoot 'data/settings.json'
function Reject([scriptblock]$Body){try{& $Body|Out-Null;return $false}catch{return $true}}
function Bytes {return [Convert]::ToBase64String([IO.File]::ReadAllBytes($path))}
ACTION
""".replace("PAYLOAD", payload).replace("ACTION", action))

    def test_missing_and_legacy_settings_default_off_without_rewriting(self):
        result = self.invoke(r"""
$missing=Read-FvrLauncherSettings $InstallRoot
$created=Test-Path -LiteralPath $path
[IO.File]::WriteAllText($path,'{"game_path":"fixture","steam_compatibility":true,"custom":{"number":7}}')
$before=Bytes;$legacy=Read-FvrLauncherSettings $InstallRoot
@{missing=$missing.settings;created=$created;legacy=$legacy.settings;unchanged=((Bytes) -ceq $before)}|ConvertTo-Json -Depth 10
""")
        self.assertIs(result["missing"]["vr_performance_mode"], False)
        self.assertIs(result["legacy"]["vr_performance_mode"], False)
        self.assertIs(result["legacy"]["steam_compatibility"], True)
        self.assertFalse(result["created"])
        self.assertTrue(result["unchanged"])

    def test_non_booleans_never_enable_or_overwrite_settings(self):
        result = self.invoke(r"""
$rows=@(foreach($bad in @('"true"','"false"','1','0','null','[]','{}')){
    [IO.File]::WriteAllText($path,('{"vr_performance_mode":'+$bad+',"custom":"keep"}'))
    $before=Bytes;$state=Read-FvrLauncherSettings $InstallRoot
    @{value=$bad;reset=$state.needs_reset;enabled=$state.settings.vr_performance_mode;
      refused=(Reject {Save-FvrPerformanceMode $InstallRoot $true});unchanged=((Bytes) -ceq $before)}
})
ConvertTo-Json -InputObject $rows
""")
        for row in result:
            with self.subTest(value=row["value"]):
                self.assertTrue(row["reset"])
                self.assertIs(row["enabled"], False)
                self.assertTrue(row["refused"])
                self.assertTrue(row["unchanged"])

    def test_save_rereads_latest_settings_and_other_saves_preserve_mode(self):
        result = self.invoke(r"""
$game=Join-Path $InstallRoot 'Game';$null=[IO.Directory]::CreateDirectory($game)
[IO.File]::WriteAllText((Join-Path $game 'BFBC2Game.exe'),'fixture, never executed')
$original=@{game_path=$game;steam_compatibility=$true;local_backend=@{label='old';pins=@('a','b')};custom=@{number=7;enabled=$false}}
Write-FvrLauncherSettings $path $original
$opened=Read-FvrLauncherSettings $InstallRoot
$original.local_backend.label='updated';$original.custom.number=9;Write-FvrLauncherSettings $path $original
$saved=Save-FvrPerformanceMode $InstallRoot $true
$null=Save-FvrSteamCompatibility $InstallRoot $false
$null=Save-FvrGameFolder $InstallRoot $game
$reopened=(Read-FvrLauncherSettings $InstallRoot).settings
$null=Save-FvrPerformanceMode $InstallRoot $false
@{saved=$saved.settings;reopened=$reopened;off=(Read-FvrLauncherSettings $InstallRoot).settings}|ConvertTo-Json -Depth 10
""")
        self.assertIs(result["saved"]["vr_performance_mode"], True)
        self.assertIs(result["saved"]["steam_compatibility"], True)
        self.assertEqual(result["saved"]["local_backend"], {"label": "updated", "pins": ["a", "b"]})
        self.assertEqual(result["saved"]["custom"], {"number": 9, "enabled": False})
        self.assertIs(result["reopened"]["vr_performance_mode"], True)
        self.assertIs(result["reopened"]["steam_compatibility"], False)
        self.assertIs(result["off"]["vr_performance_mode"], False)

    def test_locked_and_malformed_settings_are_left_exact(self):
        result = self.invoke(r"""
Write-FvrLauncherSettings $path (New-FvrLauncherSettings)
$before=Bytes;$lock=Enter-FvrLock $InstallRoot
try{$locked=Reject {Save-FvrPerformanceMode $InstallRoot $true};$same=((Bytes) -ceq $before)}finally{$lock.Dispose()}
$rows=@(foreach($invalid in @('{"vr_performance_mode":','{"game_path":"G:\Broken","vr_performance_mode":false}')){
    [IO.File]::WriteAllText($path,$invalid);$before=Bytes
    @{refused=(Reject {Save-FvrPerformanceMode $InstallRoot $true});unchanged=((Bytes) -ceq $before)}
})
@{locked=$locked;same=$same;invalid=$rows}|ConvertTo-Json -Depth 5
""")
        self.assertTrue(result["locked"])
        self.assertTrue(result["same"])
        self.assertTrue(all(row["refused"] and row["unchanged"] for row in result["invalid"]))

    def test_actual_checkbox_click_persists_and_respects_busy_recovery_and_local(self):
        result = self.invoke(r"""
Add-Type -AssemblyName PresentationFramework,PresentationCore,WindowsBase
$source=[IO.File]::ReadAllText((Join-Path $v.repo 'launcher/Launcher.ps1'))
$tokens=$null;$errors=$null;$ast=[Management.Automation.Language.Parser]::ParseInput($source,[ref]$tokens,[ref]$errors)
if($errors.Count){throw ($errors -join '; ')}
foreach($name in @('Show-Settings','Set-Busy')){
    $definition=$ast.Find({param($n) $n -is [Management.Automation.Language.FunctionDefinitionAst] -and $n.Name -ceq $name}.GetNewClosure(),$true)
    . ([scriptblock]::Create($definition.Extent.Text))
}
$registration=@($ast.FindAll({param($n) $n -is [Management.Automation.Language.InvokeMemberExpressionAst] -and $n.Member.Value -ceq 'Add_Click' -and $n.Expression.Extent.Text -ceq '$controls.PerformanceMode'},$true))
if($registration.Count -ne 1){throw 'Expected one performance click handler'}
$block=[regex]::Match($source,'(?s)\[xml\]\$markup=@''\r?\n(.*?)\r?\n''@')
[xml]$markup=$block.Groups[1].Value
$window=[Windows.Markup.XamlReader]::Load([Xml.XmlNodeReader]::new($markup))
try{
    $controls=@{};foreach($node in $markup.SelectNodes('//*[@*[local-name()="Name"]]')){
        $name=$node.GetAttribute('Name','http://schemas.microsoft.com/winfx/2006/xaml');$controls[$name]=$window.FindName($name)
    }
    $actions=@('Launch','Stop','Validate','Check','Update','Rollback','Feedback')
    $package=@{runtime_entry='runtime/Entry.ps1'};$packageState=@{verified=$true;error=$null};$active=@{current='fixture'}
    $script:job=$null;$script:reopenRequired=$false
    $game=Join-Path $InstallRoot 'Game';$null=[IO.Directory]::CreateDirectory($game)
    [IO.File]::WriteAllText((Join-Path $game 'BFBC2Game.exe'),'fixture, never executed')
    $script:settingsState=Save-FvrGameFolder $InstallRoot $game
    . ([scriptblock]::Create($registration[0].Extent.Text))
    Show-Settings;Set-Busy $false
    $defaultOff=(-not $controls.PerformanceMode.IsChecked -and $controls.PerformanceMode.IsEnabled)
    $before=Bytes;$controls.PerformanceMode.IsChecked=$true;$programmaticUnchanged=((Bytes) -ceq $before)
    $controls.PerformanceMode.RaiseEvent([Windows.RoutedEventArgs]::new([Windows.Controls.Primitives.ButtonBase]::ClickEvent))
    $saved=(Read-FvrLauncherSettings $InstallRoot).settings.vr_performance_mode
    $script:settingsState=Read-FvrLauncherSettings $InstallRoot;Show-Settings
    $reopened=[bool]$controls.PerformanceMode.IsChecked
    $before=Bytes;Set-Busy $true;$controls.PerformanceMode.IsChecked=$false
    $controls.PerformanceMode.RaiseEvent([Windows.RoutedEventArgs]::new([Windows.Controls.Primitives.ButtonBase]::ClickEvent))
    $busySafe=(-not $controls.PerformanceMode.IsEnabled -and (Bytes) -ceq $before)
    Set-Busy $false;Show-Settings;$controls.Local.IsChecked=$true;Set-Busy $false
    $localDisabled=-not $controls.PerformanceMode.IsEnabled
    $controls.Local.IsChecked=$false;Set-Busy $false
    $controls.PerformanceMode.IsChecked=$false
    $controls.PerformanceMode.RaiseEvent([Windows.RoutedEventArgs]::new([Windows.Controls.Primitives.ButtonBase]::ClickEvent))
    $off=(Read-FvrLauncherSettings $InstallRoot).settings.vr_performance_mode
    $script:reopenRequired=$true;Set-Busy $false;$reopenDisabled=-not $controls.PerformanceMode.IsEnabled
    $script:reopenRequired=$false;Set-Busy $false
    [IO.File]::WriteAllText($path,'{"vr_performance_mode":"true"}');$before=Bytes
    $controls.PerformanceMode.IsChecked=$true
    $controls.PerformanceMode.RaiseEvent([Windows.RoutedEventArgs]::new([Windows.Controls.Primitives.ButtonBase]::ClickEvent))
    @{default_off=$defaultOff;programmatic_unchanged=$programmaticUnchanged;saved=$saved;reopened=$reopened;
      busy_safe=$busySafe;local_disabled=$localDisabled;off=$off;reopen_disabled=$reopenDisabled;
      invalid_safe=((Bytes) -ceq $before -and -not $controls.PerformanceMode.IsEnabled -and -not $controls.PerformanceMode.IsChecked);
      label=[string]$controls.PerformanceMode.Content;markup=$block.Groups[1].Value}|ConvertTo-Json -Depth 5
}finally{$window.Close()}
""")
        for key in ("default_off", "programmatic_unchanged", "saved", "reopened", "busy_safe",
                    "local_disabled", "reopen_disabled", "invalid_safe"):
            self.assertTrue(result[key], key)
        self.assertIs(result["off"], False)
        self.assertEqual(result["label"], "VR performance mode")
        self.assertIn("Temporarily disables MSAA", result["markup"])
        self.assertIn("edges may look less smooth", result["markup"])
        self.assertNotIn("FPS", result["markup"])

    def test_actual_launcher_argument_assembly_forwards_mode_only_for_launch(self):
        result = self.invoke(r"""
$source=[IO.File]::ReadAllText((Join-Path $v.repo 'launcher/Launcher.ps1'))
$tokens=$null;$errors=$null;$ast=[Management.Automation.Language.Parser]::ParseInput($source,[ref]$tokens,[ref]$errors)
if($errors.Count){throw ($errors -join '; ')}
$function=$ast.Find({param($n) $n -is [Management.Automation.Language.FunctionDefinitionAst] -and $n.Name -ceq 'Begin-Action'},$true)
$assignments=@($function.FindAll({param($n) $n -is [Management.Automation.Language.AssignmentStatementAst]},$true))
$begin=@($assignments|Where-Object {$_.Left.Extent.Text -ceq '$start'})[0]
$end=@($assignments|Where-Object {$_.Left.Extent.Text -ceq '$script:jobStarted'})[0]
$assembly=$source.Substring($begin.Extent.StartOffset,$end.Extent.StartOffset-$begin.Extent.StartOffset)
# Preserve the file-backed PSScriptRoot used to locate Worker.ps1.
$assemblyPath=Join-Path $InstallRoot 'argument-assembly.ps1'
[IO.File]::WriteAllText($assemblyPath,$assembly,[Text.UTF8Encoding]::new($false))
$request=@{command_id='fixture'}
$rows=@(foreach($Action in @('Launch','Stop','Validate','Check','Update','Rollback','Feedback')){
    foreach($enabled in @($false,$true)){
        $controls=@{PerformanceMode=@{IsChecked=$enabled};Local=@{IsChecked=$false};Fresh=@{IsChecked=$true};Prerelease=@{IsChecked=$true}}
        . $assemblyPath
        @{action=$Action;enabled=$enabled;args=@($start.ArgumentList)}
    }
})
ConvertTo-Json -InputObject $rows -Depth 5
""")
        for row in result:
            with self.subTest(action=row["action"], enabled=row["enabled"]):
                self.assertEqual("-PerformanceMode" in row["args"],
                                 row["action"] == "Launch" and row["enabled"])
                self.assertEqual("-ActivateGameWindow" in row["args"], row["action"] == "Launch")
                self.assertIn("-Fresh", row["args"])
                self.assertIn("-Prerelease", row["args"])

    def fixture_install(self, settings):
        install = self.folder / "install"
        version = install / "versions/0.0.1"
        app = version / "app"
        runtime = version / "runtime"
        app.mkdir(parents=True)
        runtime.mkdir()
        for name in APP_FILES:
            shutil.copyfile(ROOT / "launcher" / name, app / name)
        (runtime / "Entry.ps1").write_text(r"""
[CmdletBinding()]
param([string]$Action,[string]$SettingsPath,[string]$StateRoot,[switch]$Fresh,
      [string]$StartAt,[switch]$ActivateGameWindow,[switch]$QualifyChestKnife,[switch]$PerformanceMode)
$null=[IO.Directory]::CreateDirectory($StateRoot)
$record=@{action=$Action;keys=@($PSBoundParameters.Keys);fresh=[bool]$Fresh;start_at=$StartAt;
          activate=[bool]$ActivateGameWindow;performance=[bool]$PerformanceMode;qualify=[bool]$QualifyChestKnife}
[IO.File]::WriteAllText((Join-Path $StateRoot 'arguments.json'),($record|ConvertTo-Json))
if($Action -eq 'Stop'){
    [pscustomobject]@{schema='bc2vr.stop-result.v1';outcome='requested';message='VR stop requested. BC2 and SteamVR remain open; headset shutdown is not yet confirmed.'}
}
""", encoding="utf-8")
        manifest = {"schema": "fvr.desktop.package.v1", "version": "0.0.1",
                    "runtime_entry": "runtime/Entry.ps1", "files": [
                        {"path": path.relative_to(version).as_posix(),
                         "sha256": hashlib.sha256(path.read_bytes()).hexdigest()}
                        for path in version.rglob("*") if path.is_file()]}
        (version / "package.json").write_text(json.dumps(manifest), encoding="utf-8")
        (install / "active.json").write_text('{"current":"0.0.1"}', encoding="utf-8")
        (install / "data").mkdir()
        settings_path = install / "data/settings.json"
        settings_path.write_text(json.dumps(settings), encoding="utf-8")
        return install, app / "Worker.ps1", settings_path

    def run_worker(self, saved, action="Launch", extra=()):
        settings = {"game_path": "fixture, never launched", "steam_compatibility": False,
                    "local_backend": None, "custom": {"keep": "exact"}}
        if saved is not None:
            settings["vr_performance_mode"] = saved
        install, worker, settings_path = self.fixture_install(settings)
        original = settings_path.read_bytes()
        result = subprocess.run(
            [PWSH, "-NoLogo", "-NoProfile", "-NonInteractive", "-ExecutionPolicy", "Bypass",
             "-File", str(worker), "-InstallRoot", str(install), "-Action", action, *extra],
            capture_output=True, text=True, timeout=30, env=self.environment,
            creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0),
        )
        self.assertEqual(result.returncode, 0, result.stderr + result.stdout)
        self.assertEqual(settings_path.read_bytes(), original)
        return (json.loads((install / "data/runtime/arguments.json").read_text()),
                json.loads((install / "data/operation.json").read_text()))

    def test_worker_default_launch_preserves_arguments_and_settings_bytes(self):
        args, operation = self.run_worker(None)
        self.assertEqual(set(args["keys"]), {"Action", "SettingsPath", "StateRoot", "Fresh", "StartAt", "ActivateGameWindow"})
        self.assertFalse(args["performance"])
        self.assertFalse(args["fresh"])
        self.assertFalse(args["activate"])
        self.assertEqual(args["start_at"], "MainMenu")
        self.assertEqual(operation["state"], "started")

    def test_worker_saved_mode_preserves_restart_campaign_and_activation(self):
        args, _ = self.run_worker(True, extra=("-Fresh", "-StartAt", "Campaign", "-ActivateGameWindow"))
        self.assertTrue(args["performance"])
        self.assertIn("PerformanceMode", args["keys"])
        self.assertTrue(args["fresh"])
        self.assertTrue(args["activate"])
        self.assertEqual(args["start_at"], "Campaign")

    def test_worker_explicit_switch_enables_without_changing_saved_choice(self):
        args, _ = self.run_worker(False, extra=("-PerformanceMode",))
        self.assertTrue(args["performance"])

    def test_worker_explicit_false_overrides_saved_mode_for_one_launch(self):
        args, _ = self.run_worker(True, extra=("-PerformanceMode:$false",))
        self.assertFalse(args["performance"])
        self.assertNotIn("PerformanceMode", args["keys"])

    def test_worker_stop_ignores_mode_and_preserves_game_open_notice(self):
        args, operation = self.run_worker(True, "Stop", ("-PerformanceMode",))
        self.assertNotIn("PerformanceMode", args["keys"])
        self.assertFalse(args["activate"])
        self.assertEqual(operation["stop_outcome"], "requested")
        self.assertIn("BC2 and SteamVR remain open", operation["message"])

    def test_worker_validate_ignores_saved_and_explicit_mode(self):
        args, operation = self.run_worker(True, "Validate", ("-PerformanceMode",))
        self.assertNotIn("PerformanceMode", args["keys"])
        self.assertFalse(args["activate"])
        self.assertEqual(operation["state"], "done")


if __name__ == "__main__":
    unittest.main()
