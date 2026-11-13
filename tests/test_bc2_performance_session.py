"""Assembled Entry/Session + real MSAA lease; process boundaries are fixtures.

No game, headset, native helper, watcher process, or player settings are used.
ROOT runs this suite after the coordinated source freeze.
"""
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
KEYS = ("FVR_PRIVATE_TWO_SOURCE_SLOTS", "FVR_PRIVATE_THREE_SOURCE_SLOTS",
        "FVR_PRIVATE_SOURCE_LOOKAHEAD", "FVR_PRIVATE_EXPORT_OVERLAP")
ORIGINAL = b"[Graphics]\r\nMSAA=3\r\nQuality=high\r\n"

CORE = r"""
Set-StrictMode -Version 3
function Read-FvrJson($Path){return [IO.File]::ReadAllText($Path)|ConvertFrom-Json -AsHashtable -DateKind String}
function Write-FvrJson($Path,$Value,[switch]$RetrySessionPublication){[IO.File]::WriteAllText($Path,($Value|ConvertTo-Json -Depth 16))}
function Get-FvrChild($Root,$Child){return [IO.Path]::GetFullPath((Join-Path $Root $Child))}
function Assert-FvrNoLinks($Path){}
function Set-FvrLauncherStorage($Path){}
function Get-FvrLauncherGamePlacement($Root,$Game){return @{allowed=$true;error=$null}}
"""

RUNTIME = r"""
function Get-Bc2RuntimeConfig {return Read-FvrJson (Join-Path $PSScriptRoot 'fixture.json')}
function Event($Name){[IO.File]::AppendAllText((Join-Path $PSScriptRoot 'events.txt'),$Name+"`n")}
function PipelineEnvironment {
    $map=@{};foreach($key in @('FVR_PRIVATE_TWO_SOURCE_SLOTS','FVR_PRIVATE_THREE_SOURCE_SLOTS','FVR_PRIVATE_SOURCE_LOOKAHEAD','FVR_PRIVATE_EXPORT_OVERLAP')){$map[$key]=[Environment]::GetEnvironmentVariable($key,'Process')};return $map
}
function Get-Bc2ProcessIdentity($Process,[bool]$RequireLive=$false){return @{pid=$Process.Id;path=$Process.Path;start_utc='2026-11-12T12:00:00.1234567Z'}}
function Get-Process {
    param($Name,$Id,$ErrorAction)
    if($Id){
        # The lease disposes its supervisor handle after reading the identity.
        $process=[pscustomobject]@{Id=$Id;Path=(Join-Path $PSHOME 'pwsh.exe');Disposed=$false}
        $process|Add-Member -MemberType ScriptMethod -Name Dispose -Value {$this.Disposed=$true}
        return $process
    }
}
function Find-Bc2OwnedProcess($Identity){return $script:fixtureGame}
function Assert-Bc2RuntimeSpace($Root){}
function Test-Bc2AssetCache($Cache,$Recipe,$Hash){return $true}
function Get-Bc2Session($Root){return $null}
function Get-Bc2ExistingGame($Exe,$Fresh){return $null}
function Resolve-Bc2Tool {return 'Fixture-Python'}
function Fixture-Python {
    $i=[Array]::IndexOf($args,'--output')
    if($i -ge 0){Write-FvrJson $args[$i+1] @{passed=$true;width=1920;height=1080}}
    $global:LASTEXITCODE=0
}
function Start-Bc2Child($File,$Arguments,$Folder,$LogName,$Environment=@{}){
    Event $LogName
    throw 'FIXTURE_SUPERVISOR_BOUNDARY'
}
"""

STARTUP = r"""
function Test-Bc2ExecutableAdmission {param($Exe,$Hash,$Config,[switch]$ExperimentalSteam)return @{profile='fixture';experimental_steam=$false}}
function Test-Bc2GameInstallation($Settings,$Config,$StateRoot){return @{exe=(Join-Path $Settings.game_path 'BFBC2Game.exe');sha256=$Config.game_sha256}}
function Get-Bc2OpenXrSelection {return @{label='fixture';manifest='fixture'}}
function Invoke-Bc2OpenXrPreflight($RuntimeRoot,$StateRoot,$Selection){
    Event 'preflight';Write-FvrJson (Join-Path $PSScriptRoot 'probe-environment.json') (PipelineEnvironment)
    $cfg=Get-Bc2RuntimeConfig
    return @{instance_created=$true;system_available=$true;recommended_eye_width=$cfg.width;recommended_eye_height=$cfg.height;
        eye_width=$cfg.width;eye_height=$cfg.height;max_eye_width=16384;max_eye_height=16384}
}
"""

GRAPHICS = r"""
$script:fixtureGame=$null;$script:fixtureResumed=$false
function Start-Bc2GraphicsGame($Exe,$Config){
    Event 'game-created'
    $script:fixtureGame=[pscustomobject]@{Id=12345;Path=$Exe;HasExited=$false;StartTime=[DateTime]'2026-11-12T12:00:00.1234567Z'}
    $script:game=$script:fixtureGame
    $state.game=Get-Bc2ProcessIdentity $script:fixtureGame
    Write-FvrJson (Join-Path $PSScriptRoot 'game-environment.json') (PipelineEnvironment)
    if($options.ContainsKey('performance_mode') -and $options.performance_mode){Invoke-Bc2GraphicsSettingsGameCreated $state.game}
    $script:fixtureResumed=$true;Event 'game-resumed'
    if($Config.scenario -eq 'game_exit'){$script:fixtureGame.HasExited=$true}
    [IO.File]::WriteAllText($stopFile,'Fixture Stop VR')
    return $script:fixtureGame
}
function Complete-Bc2GraphicsCreation {
    if($script:fixtureGame -and -not $script:fixtureResumed){$script:fixtureGame.HasExited=$true;Event 'suspended-game-aborted'}
    Write-FvrJson (Join-Path $PSScriptRoot 'restored-environment.json') (PipelineEnvironment)
}
"""

LEASE_OVERRIDES = r"""
$script:actualPrepare=${function:Start-Bc2GraphicsSettingsLease}
$script:actualRestore=${function:Restore-Bc2GraphicsSettingsLease}
$script:actualBind=${function:Set-Bc2GraphicsSettingsGame}
function Get-Bc2GraphicsSettingsPath {return [IO.Path]::GetFullPath((Join-Path $PSScriptRoot 'settings.ini'))}
function Get-Bc2GraphicsLiveGames {
    if((Get-Variable fixtureGame -Scope Script -ErrorAction SilentlyContinue) -and $script:fixtureGame -and -not $script:fixtureGame.HasExited){return Get-Bc2ProcessIdentity $script:fixtureGame}
}
function Get-Bc2GraphicsProcessState($Identity){
    if($Identity.pid -eq $PID){return 'alive'}
    if($Identity.pid -eq 12345 -and $script:fixtureGame -and -not $script:fixtureGame.HasExited){return 'alive'}
    return 'exited'
}
function Get-Bc2GraphicsParentPid($ProcessId){return $PID}
function Start-Bc2GraphicsSettingsLease($StateRoot,$GamePath,[bool]$Enabled=$false){
    Event 'prepare';$result=& $script:actualPrepare $StateRoot $GamePath $Enabled
    if((Get-Bc2RuntimeConfig).scenario -eq 'prepare_failure'){throw 'Fixture interruption after durable preparation'}
    return $result
}
function Restore-Bc2GraphicsSettingsLease($StateRoot){
    Event 'restore'
    if((Get-Bc2RuntimeConfig).scenario -eq 'stale_pending'){return @{status='pending';reason='game_identity_unknown'}}
    return & $script:actualRestore $StateRoot
}
function Set-Bc2GraphicsSettingsGame($StateRoot,$GameIdentity){
    Event 'bind'
    if((Get-Bc2RuntimeConfig).scenario -eq 'bind_failure'){throw 'Fixture exact identity binding refused'}
    $result=& $script:actualBind $StateRoot $GameIdentity
    Write-FvrJson (Join-Path $PSScriptRoot 'bound-before-resume.json') (Read-FvrJson (Join-Path $StateRoot 'graphics-settings/lease.json'))
    return $result
}
function Start-Bc2GraphicsSettingsWatcher($StateRoot){
    Event 'watcher'
    if((Get-Bc2RuntimeConfig).scenario -eq 'watcher_failure'){throw 'Fixture watcher startup failed'}
    return [pscustomobject]@{Id=23456;Path=(Join-Path $PSHOME 'pwsh.exe');HasExited=$false}
}
"""


@unittest.skipUnless(PWSH, "PowerShell 7.5 required")
class PerformanceSessionTests(unittest.TestCase):
    def invoke(self, *, mode=True, scenario="stop_live", width=2784, height=3024,
               settings=ORIGINAL, entry=False, extra=None):
        scratch = ROOT / "test-temp"
        scratch.mkdir(exist_ok=True)
        with tempfile.TemporaryDirectory(prefix="performance-session-", dir=scratch) as temp:
            base = Path(temp)
            runtime = base / "package/runtime"
            app = base / "package/app"
            state_root = base / "data/runtime"
            session = state_root / "sessions/fixture"
            game = base / "game"
            for folder in (runtime, app, session, game):
                folder.mkdir(parents=True, exist_ok=True)
            exe = game / "BFBC2Game.exe"
            exe.write_bytes(b"fixture executable; never run")
            game_hash = hashlib.sha256(exe.read_bytes()).hexdigest()
            cfg = {"version": "fixture", "source_sha256": "a" * 64,
                   "asset_recipe_sha256": "b" * 64, "game_sha256": game_hash,
                   "scenario": scenario, "width": width, "height": height}
            (runtime / "fixture.json").write_text(json.dumps(cfg))
            (runtime / "settings.ini").write_bytes(settings)
            (app / "Launcher.Core.ps1").write_text(CORE)
            (runtime / "Runtime.Core.ps1").write_text(RUNTIME)
            (runtime / "Startup.ps1").write_text(STARTUP)
            (runtime / "GraphicsCreation.ps1").write_text(GRAPHICS)
            (runtime / "Dependencies.ps1").write_text("function Get-Bc2PreparationFfmpeg {return 'fixture-ffmpeg'}")
            for name in ("Entry.ps1", "Session.ps1", "Resolution.ps1"):
                shutil.copyfile(ROOT / "launcher/runtime" / name, runtime / name)
            (runtime / "GraphicsSettings.ps1").write_text(
                (ROOT / "launcher/runtime/GraphicsSettings.ps1").read_text(encoding="utf-8") + LEASE_OVERRIDES,
                encoding="utf-8")
            options = {"schema": "bc2vr.session-options.v1", "version": "fixture", "source_sha256": "a" * 64,
                       "state_root": str(state_root), "relative_folder": "sessions/fixture", "game_path": str(game),
                       "game_sha256": game_hash, "cache": str(base / "cache"), "python": "Fixture-Python",
                       "activate_window": False, "render_plan": {"schema": "bc2vr.render-plan.v1", "width": width,
                           "height": height, "recommended_width": width, "recommended_height": height,
                           "max_width": 16384, "max_height": 16384, "source": "openxr_recommendation",
                           "native_source_verified": False}}
            if mode is not None:
                options["performance_mode"] = mode
            options.update(extra or {})
            options_path = session / "options.json"
            options_path.write_text(json.dumps(options))
            command = [PWSH, "-NoLogo", "-NoProfile", "-NonInteractive", "-File"]
            if entry:
                player = base / "data/settings.json"
                player.write_text(json.dumps({"game_path": str(game)}))
                command += [str(runtime / "Entry.ps1"), "-Action", "Launch", "-SettingsPath", str(player), "-StateRoot", str(state_root)]
                if mode:
                    command += ["-PerformanceMode"]
            else:
                command += [str(runtime / "Session.ps1"), "-OptionsPath", str(options_path)]
            env = dict(os.environ, TEMP=temp, TMP=temp, TMPDIR=temp, FVR_PRIVATE_HOST_COPY_SERVICE_MS="0",
                       **{key: "1" for key in KEYS})
            result = subprocess.run(command, env=env, capture_output=True, text=True, timeout=20,
                                    creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
            def read_json(path):
                return json.loads(path.read_text(encoding="utf-8-sig")) if path.exists() else None
            events = (runtime / "events.txt").read_text().splitlines() if (runtime / "events.txt").exists() else []
            return {"returncode": result.returncode, "output": result.stdout + result.stderr, "events": events,
                    "state": read_json(session / "session.json"), "settings": (runtime / "settings.ini").read_bytes(),
                    "lease": read_json(state_root / "graphics-settings/lease.json"),
                    "bound": read_json(runtime / "bound-before-resume.json"),
                    "game_env": read_json(runtime / "game-environment.json"),
                    "probe_env": read_json(runtime / "probe-environment.json"),
                    "restored_env": read_json(runtime / "restored-environment.json"),
                    "entry_options": [read_json(p) for p in (state_root / "sessions").glob("*/options.json") if p != options_path]}

    def test_off_and_missing_option_force_legacy_without_settings_mutation(self):
        for mode in (False, None):
            with self.subTest(mode=mode):
                result = self.invoke(mode=mode, settings=b"unsupported settings must remain untouched")
                self.assertEqual(result["returncode"], 0, result["output"])
                self.assertEqual(result["settings"], b"unsupported settings must remain untouched")
                self.assertIsNone(result["lease"])
                self.assertNotIn("prepare", result["events"])
                self.assertEqual(result["game_env"], dict.fromkeys(KEYS, "0"))
                self.assertEqual(result["restored_env"], dict.fromkeys(KEYS, "1"))
                self.assertEqual(result["state"]["source_pipeline"]["selection"], "legacy")

    def test_stop_vr_keeps_game_and_lease_until_exit(self):
        result = self.invoke()
        self.assertEqual(result["returncode"], 0, result["output"])
        self.assertEqual(result["events"], ["restore", "prepare", "watcher", "game-created", "bind", "game-resumed", "restore"])
        self.assertEqual(result["settings"], ORIGINAL.replace(b"MSAA=3", b"MSAA=0"))
        self.assertEqual(result["lease"]["state"], "bound")
        self.assertEqual(result["bound"]["game"], result["state"]["game"])
        self.assertEqual(result["state"]["graphics_settings_restoration"]["reason"], "game_alive")
        self.assertTrue(result["state"]["graphics_settings_restore_pending"])
        self.assertIn("restoration is pending", result["state"]["phase"])
        self.assertEqual(result["game_env"], dict.fromkeys(KEYS, "1"))
        self.assertEqual(result["state"]["render_plan"]["width"], 2784)
        self.assertEqual(result["state"]["performance_mode"]["logical_reservation_bytes"], 2784 * 3024 * 84)
        self.assertFalse(result["state"]["performance_mode"]["native_samples_verified"])

    def test_game_exit_restores_exact_settings(self):
        result = self.invoke(scenario="game_exit")
        self.assertEqual(result["returncode"], 0, result["output"])
        self.assertEqual(result["settings"], ORIGINAL)
        self.assertEqual(result["lease"]["state"], "restored")
        self.assertFalse(result["state"]["graphics_settings_restore_pending"])

    def test_invalid_input_refuses_before_prepare_or_child(self):
        cases = ({"mode": "true"}, {"width": 4000, "height": 4000},
                 {"settings": b"[Graphics]\nMSAA=8\n"}, {"extra": {"private_source_pipeline": True}},
                 {"extra": {"private_source_pipeline": 0}})
        for kwargs in cases:
            with self.subTest(kwargs=kwargs):
                result = self.invoke(**kwargs)
                self.assertNotEqual(result["returncode"], 0, result["output"])
                self.assertNotIn("prepare", result["events"])
                self.assertNotIn("watcher", result["events"])
                self.assertNotIn("game-created", result["events"])
                self.assertIsNone(result["lease"])
                self.assertEqual(result["settings"], kwargs.get("settings", ORIGINAL))

    def test_pending_recovery_blocks_default_launch(self):
        result = self.invoke(mode=False, scenario="stale_pending")
        self.assertNotEqual(result["returncode"], 0)
        self.assertEqual(result["events"], ["restore"])
        self.assertIn("restoration is pending", result["state"]["error"])

    def test_failed_preparation_or_watcher_restores_without_game(self):
        for scenario in ("prepare_failure", "watcher_failure"):
            with self.subTest(scenario=scenario):
                result = self.invoke(scenario=scenario)
                self.assertNotEqual(result["returncode"], 0)
                self.assertNotIn("game-created", result["events"])
                self.assertEqual(result["settings"], ORIGINAL)
                self.assertEqual(result["lease"]["state"], "restored")

    def test_failed_identity_bind_prevents_resume_and_restores_after_abort(self):
        result = self.invoke(scenario="bind_failure")
        self.assertNotEqual(result["returncode"], 0)
        self.assertIn("bind", result["events"])
        self.assertNotIn("game-resumed", result["events"])
        self.assertIn("suspended-game-aborted", result["events"])
        self.assertEqual(result["settings"], ORIGINAL)
        self.assertEqual(result["lease"]["state"], "restored")

    def test_real_creation_seam_is_before_resume_and_inert_when_off(self):
        source = (ROOT / "launcher/runtime/GraphicsCreation.ps1").read_text(encoding="utf-8")
        created = source.index("$state.game=$identity")
        bind = source.index("Invoke-Bc2GraphicsSettingsGameCreated $identity", created)
        resume = source.index("$d.resumed=$bridge.Resume.Invoke", bind)
        self.assertLess(created, bind)
        self.assertLess(bind, resume)
        self.assertIn("$options.performance_mode -is [bool] -and $options.performance_mode", source[created:bind])

    def test_entry_admits_recommended_size_before_supervisor_and_neutralizes_probe(self):
        result = self.invoke(entry=True)
        self.assertIn("FIXTURE_SUPERVISOR_BOUNDARY", result["output"])
        self.assertEqual(result["probe_env"], dict.fromkeys(KEYS, "0"))
        self.assertNotIn("prepare", result["events"])
        self.assertEqual(result["settings"], ORIGINAL)
        self.assertEqual(len(result["entry_options"]), 1)
        options = result["entry_options"][0]
        self.assertIs(options["performance_mode"], True)
        self.assertEqual((options["render_plan"]["width"], options["render_plan"]["height"]), (2784, 3024))

    def test_entry_refuses_budget_and_settings_before_supervisor(self):
        for kwargs in ({"width": 4000, "height": 4000}, {"settings": b"[Graphics]\nMSAA=invalid\n"},
                       {"scenario": "stale_pending", "mode": False}):
            with self.subTest(kwargs=kwargs):
                result = self.invoke(entry=True, **kwargs)
                self.assertNotEqual(result["returncode"], 0)
                self.assertNotIn("supervisor", result["events"])
                self.assertNotIn("prepare", result["events"])
                self.assertIsNone(result["lease"])
                self.assertEqual(result["settings"], kwargs.get("settings", ORIGINAL))


if __name__ == "__main__":
    unittest.main()
