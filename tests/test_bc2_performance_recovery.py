"""Real lease-lock contention and complete Entry restart/Stop control flow.

All files, settings and child temporary paths are isolated under test-temp.
Process observations and ordinary game-close methods are fixtures; no game,
watcher, headset, player profile or native helper is launched.
"""
import base64
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
PWSH = shutil.which("pwsh")
ORIGINAL = b"[Graphics]\r\nMSAA=3\r\nQuality=high\r\n"
PREPARED = ORIGINAL.replace(b"MSAA=3", b"MSAA=0")


@unittest.skipUnless(PWSH and os.name == "nt", "Windows PowerShell 7.5 required")
class PerformanceRecoveryTests(unittest.TestCase):
    def setUp(self):
        scratch = ROOT / "test-temp"
        scratch.mkdir(exist_ok=True)
        self.temporary = tempfile.TemporaryDirectory(prefix="performance-recovery-", dir=scratch)
        self.addCleanup(self.temporary.cleanup)
        self.folder = Path(self.temporary.name)
        self.environment = dict(os.environ, TEMP=str(self.folder), TMP=str(self.folder),
                                TMPDIR=str(self.folder), PYTHONDONTWRITEBYTECODE="1",
                                FVR_PRIVATE_HOST_COPY_SERVICE_MS="0")

    def invoke(self, action, *, entry=False):
        folder = Path(tempfile.mkdtemp(prefix="case-", dir=self.folder))
        (folder / "settings.ini").write_bytes(ORIGINAL)
        if entry:
            runtime = folder / "entry/runtime"
            app = folder / "entry/app"
            runtime.mkdir(parents=True)
            app.mkdir()
            shutil.copyfile(ROOT / "launcher/runtime/Entry.ps1", runtime / "Entry.ps1")
            for name in ("Runtime.Core.ps1", "Dependencies.ps1", "Startup.ps1",
                         "Resolution.ps1", "GraphicsSettings.ps1"):
                (runtime / name).write_text("# Fixture dependencies are loaded by check.ps1.\n", encoding="utf-8")
            (app / "Launcher.Core.ps1").write_text("# Fixture dependencies are already loaded.\n", encoding="utf-8")
        payload = base64.b64encode(json.dumps({"repo": str(ROOT), "folder": str(folder)}).encode()).decode()
        script = folder / "check.ps1"
        script.write_text(r"""
$ErrorActionPreference='Stop'
Set-StrictMode -Version 3
$v=[Text.Encoding]::UTF8.GetString([Convert]::FromBase64String('PAYLOAD'))|ConvertFrom-Json -AsHashtable
. (Join-Path $v.repo 'launcher/Launcher.Core.ps1')
. (Join-Path $v.repo 'launcher/runtime/Runtime.Core.ps1')
. (Join-Path $v.repo 'launcher/runtime/GraphicsSettings.ps1')
$stateRoot=Join-Path $v.folder 'install/data/runtime'
$gamePath=Join-Path $v.folder 'BFBC2Game.exe'
$global:fixture=@{settings=(Join-Path $v.folder 'settings.ini');alive=$false;unknown=$false;multiple=$false;
    closeAccepted=$true;exitAccepted=$true;closeBytes=$null;preflightBytes=$null;events=[Collections.Generic.List[string]]::new();
    supervisor=@{pid=$PID;path=(Join-Path $PSHOME 'pwsh.exe');start_utc='2026-11-12T12:00:00.1234567Z'};
    identity=@{pid=999991;path=$gamePath;start_utc='2026-11-12T12:00:01.1234567Z'}}
$global:fixture.game=[pscustomobject]@{Id=999991}
$global:fixture.game|Add-Member ScriptMethod CloseMainWindow {
    $global:fixture.events.Add('close')
    $global:fixture.closeBytes=[Convert]::ToBase64String([IO.File]::ReadAllBytes($global:fixture.settings))
    return $global:fixture.closeAccepted
}
$global:fixture.game|Add-Member ScriptMethod WaitForExit {
    param($Milliseconds)
    $global:fixture.events.Add('wait:'+$Milliseconds)
    if($global:fixture.exitAccepted){$global:fixture.alive=$false}
    return $global:fixture.exitAccepted
}
$global:fixture.game|Add-Member ScriptMethod Dispose {}
function Get-Bc2GraphicsSettingsPath {return $global:fixture.settings}
function Get-Bc2ProcessIdentity($Process,[bool]$RequireLive=$false){
    if($Process.Id -eq 999991){return $global:fixture.identity.Clone()}
    if($Process.Id -eq $PID){return $global:fixture.supervisor.Clone()}
    throw 'Unexpected fixture identity lookup'
}
function Get-Bc2GraphicsProcessState($Identity){
    if($Identity.pid -eq $PID){return 'alive'}
    if($Identity.pid -ne 999991){throw 'Unexpected fixture graphics identity'}
    if($global:fixture.unknown){return 'unknown'}
    if($global:fixture.alive){return 'alive'}
    return 'exited'
}
function Get-Bc2GraphicsLiveGames {
    if($global:fixture.alive){return $global:fixture.identity}
}
function Get-Bc2GraphicsParentPid([int]$ProcessId){return $PID}
function Get-Process {
    param([string[]]$Name,[int]$Id)
    if($Id -eq $PID){return Microsoft.PowerShell.Management\Get-Process -Id $PID}
    if($Name -contains 'BFBC2Game'){
        $global:fixture.events.Add('select')
        if($global:fixture.alive){
            $global:fixture.game
            if($global:fixture.multiple){$global:fixture.game}
        }
        return
    }
    if(($Name -join ',') -ceq 'BC2XrHost,BC2NativeTrace,BC2NativeIpcProbe'){return}
    throw 'Unexpected fixture process enumeration'
}
function Prepare {Start-Bc2GraphicsSettingsLease $stateRoot $gamePath -Enabled $true}
function Bind {
    $global:fixture.alive=$true
    Set-Bc2GraphicsSettingsGame $stateRoot $global:fixture.identity
}
function Journal {Read-FvrJson (Join-Path $stateRoot 'graphics-settings/lease.json')}
function SettingsBytes {[Convert]::ToBase64String([IO.File]::ReadAllBytes($global:fixture.settings))}
ACTION
""".replace("PAYLOAD", payload).replace("ACTION", action), encoding="utf-8")
        result = subprocess.run(
            [PWSH, "-NoLogo", "-NoProfile", "-NonInteractive", "-ExecutionPolicy", "Bypass",
             "-File", str(script)],
            capture_output=True, text=True, timeout=30, env=self.environment,
            creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0),
        )
        self.assertEqual(result.returncode, 0, result.stderr + result.stdout)
        return json.loads(result.stdout)

    def test_bind_waits_for_actual_exclusive_lock_then_rechecks_settings(self):
        for edit in (False, True):
            with self.subTest(edit_while_waiting=edit):
                result = self.invoke(r"""
$null=Prepare;$paths=Get-Bc2GraphicsLeasePaths $stateRoot
$ready=[Threading.ManualResetEventSlim]::new($false)
$release=[Threading.ManualResetEventSlim]::new($false)
$holder=[PowerShell]::Create()
$null=$holder.AddScript({
    param($Path,$Ready,$Release,$Settings,$Edit)
    $lock=[IO.File]::Open($Path,[IO.FileMode]::OpenOrCreate,[IO.FileAccess]::ReadWrite,[IO.FileShare]::None)
    try {
        $Ready.Set()
        if(-not $Release.Wait(10000)){throw 'Fixture release signal timed out'}
        Start-Sleep -Milliseconds 300
        if($Edit){[IO.File]::WriteAllText($Settings,"[Graphics]`r`nMSAA=2`r`nQuality=high`r`n")}
    } finally {$lock.Dispose()}
}).AddArgument($paths.lock).AddArgument($ready).AddArgument($release).AddArgument($global:fixture.settings).AddArgument(EDIT)
$pending=$holder.BeginInvoke()
try {
    if(-not $ready.Wait(10000)){throw 'Fixture lock did not become ready'}
    $sharing=0
    try {$unexpected=Enter-Bc2GraphicsLease $paths;$unexpected.Dispose()}
    catch {$sharing=$_.Exception.GetBaseException().HResult}
    $release.Set();$watch=[Diagnostics.Stopwatch]::StartNew();$errorText=$null;$bound=$null
    try {$bound=Bind} catch {$errorText=$_.Exception.Message}
    $watch.Stop();$null=$holder.EndInvoke($pending)
    if($holder.HadErrors){throw ($holder.Streams.Error -join '; ')}
    @{sharing=$sharing;elapsed=$watch.ElapsedMilliseconds;bound=$bound;error=$errorText;state=(Journal).state;bytes=(SettingsBytes)}|ConvertTo-Json -Depth 6
} finally {$release.Set();$holder.Dispose();$ready.Dispose();$release.Dispose()}
""".replace("EDIT", "$true" if edit else "$false"))
                self.assertEqual(result["sharing"], -2147024864)
                self.assertGreaterEqual(result["elapsed"], 200)
                self.assertLess(result["elapsed"], 5000)
                self.assertEqual(result["state"], "prepared" if edit else "bound")
                if edit:
                    self.assertIsNone(result["bound"])
                    self.assertIn("changed before game binding", result["error"])
                else:
                    self.assertEqual(result["bound"]["status"], "bound")
                    self.assertEqual(base64.b64decode(result["bytes"]), PREPARED)

    def test_continuous_lock_contention_expires_without_binding_or_mutation(self):
        result = self.invoke(r"""
$null=Prepare;$paths=Get-Bc2GraphicsLeasePaths $stateRoot;$lock=Enter-Bc2GraphicsLease $paths
$watch=[Diagnostics.Stopwatch]::StartNew();$code=0
try {try {$null=Bind} catch {$code=$_.Exception.GetBaseException().HResult}}
finally {$watch.Stop();$lock.Dispose()}
@{hresult=$code;elapsed=$watch.ElapsedMilliseconds;state=(Journal).state;bytes=(SettingsBytes)}|ConvertTo-Json
""")
        self.assertEqual(result["hresult"], -2147024864)
        self.assertGreaterEqual(result["elapsed"], 4800)
        self.assertLess(result["elapsed"], 6500)
        self.assertEqual(result["state"], "prepared")
        self.assertEqual(base64.b64decode(result["bytes"]), PREPARED)

    def test_nonsharing_lock_error_is_not_retried(self):
        result = self.invoke(r"""
$null=Prepare;$paths=Get-Bc2GraphicsLeasePaths $stateRoot
[IO.File]::Delete($paths.lock);$null=[IO.Directory]::CreateDirectory($paths.lock)
$watch=[Diagnostics.Stopwatch]::StartNew();$errorType=$null
try {$null=Bind} catch {$errorType=$_.Exception.GetBaseException().GetType().Name}
$watch.Stop()
@{error=$errorType;elapsed=$watch.ElapsedMilliseconds;state=(Journal).state;bytes=(SettingsBytes)}|ConvertTo-Json
""")
        self.assertEqual(result["error"], "UnauthorizedAccessException")
        self.assertLess(result["elapsed"], 1500)
        self.assertEqual(result["state"], "prepared")
        self.assertEqual(base64.b64decode(result["bytes"]), PREPARED)

    def invoke_entry(self, *, fresh=True, performance=False, action="Launch", mutation=""):
        # Run the complete, unchanged Entry script with fixture dependencies.
        # Production graphics transactions and game selection remain loaded;
        # preflight stops the run before any headset or asset preparation.
        return self.invoke(r"""
$null=Prepare;$null=Bind
$global:fixture.restore=${function:Restore-Bc2GraphicsSettingsLease}
function Restore-Bc2GraphicsSettingsLease([string]$StateRoot){
    $result=& $global:fixture.restore $StateRoot
    $global:fixture.events.Add('recovery:'+$result.status+':'+$result.reason)
    return $result
}
function Set-FvrLauncherStorage($Root) {return $Root}
function Get-Bc2Session($StateRoot) {return $null}
function Get-Bc2RuntimeConfig {return @{version='fixture';source_sha256=('a'*64);asset_recipe_sha256=('b'*64)}}
function Test-Bc2GameInstallation($Settings,$Config,$StateRoot) {return @{exe=$gamePath;sha256=('c'*64)}}
function Get-FvrLauncherGamePlacement($Root,$Game) {return @{allowed=$true}}
function Get-Bc2OpenXrSelection {return @{label='fixture';manifest='fixture'}}
function Resolve-Bc2Tool {return 'Invoke-FixturePython'}
function Invoke-FixturePython {$global:LASTEXITCODE=0}
function Test-Bc2AssetCache {return $true}
function Assert-Bc2RuntimeSpace {}
function Get-Bc2PreparationFfmpeg {throw 'Unexpected asset preparation before fixture preflight'}
function Invoke-Bc2OpenXrPreflight {
    $global:fixture.events.Add('preflight')
    $global:fixture.preflightBytes=SettingsBytes
    throw 'fixture_preflight_reached'
}
$settingsPath=Join-Path $v.folder 'launcher-settings.json'
Write-FvrJson $settingsPath @{game_path=$v.folder}
MUTATION
$arguments=@{Action='ACTION';SettingsPath=$settingsPath;StateRoot=$stateRoot;Fresh=FRESH;PerformanceMode=PERFORMANCE}
$message=$null;$output=$null
try {$output=& (Join-Path $v.folder 'entry/runtime/Entry.ps1') @arguments}
catch {$message=$_.Exception.Message}
$j=Journal
@{error=$message;output=$output;events=@($global:fixture.events);bytes=(SettingsBytes);state=$j.state;
  alive=$global:fixture.alive;backup=[IO.File]::Exists((Join-Path $stateRoot ('graphics-settings/'+$j.backup)));
  close_bytes=$global:fixture.closeBytes;preflight_bytes=$global:fixture.preflightBytes}|ConvertTo-Json -Depth 8
""".replace("MUTATION", mutation).replace("'ACTION'", repr(action))
            .replace("FRESH", "$true" if fresh else "$false")
            .replace("PERFORMANCE", "$true" if performance else "$false"), entry=True)

    def test_fresh_restart_closes_exact_game_then_restores_before_preflight_in_either_mode(self):
        for performance in (False, True):
            with self.subTest(performance=performance):
                result = self.invoke_entry(performance=performance)
                self.assertEqual(result["error"], "fixture_preflight_reached")
                self.assertEqual(result["events"], ["recovery:pending:game_alive", "select", "close",
                                                    "wait:8000", "recovery:restored:", "select", "preflight"])
                self.assertEqual(base64.b64decode(result["close_bytes"]), PREPARED)
                self.assertEqual(base64.b64decode(result["preflight_bytes"]), ORIGINAL)
                self.assertEqual(result["state"], "restored")
                self.assertFalse(result["alive"])
                self.assertTrue(result["backup"])

    def test_restart_preserves_unrelated_settings_edits(self):
        result = self.invoke_entry(mutation=r"""
[IO.File]::WriteAllText($global:fixture.settings,"[Graphics]`r`nMSAA=0`r`nQuality=low`r`n")
""")
        self.assertEqual(result["error"], "fixture_preflight_reached")
        self.assertEqual(base64.b64decode(result["bytes"]), ORIGINAL.replace(b"high", b"low"))

    def test_conflicted_or_ambiguous_leases_do_not_close_a_game(self):
        cases = {
            "edited_msaa": "[IO.File]::WriteAllText($global:fixture.settings,\"[Graphics]`r`nMSAA=2`r`n\")",
            "ambiguous_ini": "[IO.File]::WriteAllText($global:fixture.settings,\"[Graphics]`r`nMSAA=0`r`nMSAA=0`r`n\")",
            "multiple_games": "$global:fixture.multiple=$true",
            "unknown_identity": "$global:fixture.unknown=$true",
            "changed_birth": "$global:fixture.identity.start_utc='2026-11-12T12:00:01.1234568Z'",
            "other_installation": "$global:fixture.identity.path=Join-Path $v.folder 'other/BFBC2Game.exe'",
            "invalid_lease": "$j=Journal;$j.prepared_sha256='0'*64;Write-Bc2GraphicsJournal (Join-Path $stateRoot 'graphics-settings/lease.json') $j",
        }
        for name, mutation in cases.items():
            with self.subTest(case=name):
                result = self.invoke_entry(mutation=mutation)
                self.assertIsNotNone(result["error"])
                self.assertNotIn("close", result["events"])
                self.assertNotIn("preflight", result["events"])
                self.assertEqual(result["state"], "bound")
                self.assertTrue(result["alive"])
                self.assertTrue(result["backup"])

    def test_stop_and_launch_without_fresh_leave_live_game_and_lease_untouched(self):
        for action, fresh in (("Launch", False), ("Stop", False), ("Stop", True)):
            with self.subTest(action=action, fresh=fresh):
                result = self.invoke_entry(action=action, fresh=fresh)
                self.assertNotIn("close", result["events"])
                self.assertNotIn("preflight", result["events"])
                self.assertEqual(result["state"], "bound")
                self.assertTrue(result["alive"])
                self.assertEqual(base64.b64decode(result["bytes"]), PREPARED)
                if action == "Stop":
                    self.assertEqual(result["output"]["outcome"], "no_owned_session")

    def test_refused_or_timed_out_normal_close_does_not_restore_or_prepare(self):
        for mutation, expected_wait in (("$global:fixture.closeAccepted=$false", False),
                                         ("$global:fixture.exitAccepted=$false", True)):
            with self.subTest(mutation=mutation):
                result = self.invoke_entry(mutation=mutation)
                self.assertIn("did not exit normally", result["error"])
                self.assertIn("close", result["events"])
                self.assertEqual("wait:8000" in result["events"], expected_wait)
                self.assertNotIn("preflight", result["events"])
                self.assertEqual(result["state"], "bound")
                self.assertTrue(result["alive"])
                self.assertEqual(base64.b64decode(result["bytes"]), PREPARED)


if __name__ == "__main__":
    unittest.main()
