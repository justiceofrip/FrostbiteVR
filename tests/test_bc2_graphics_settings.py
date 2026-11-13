"""Real lease-file transactions with mocked process observations; no game/profile access.

All fixtures and child TEMP/TMP/TMPDIR stay under the repository's test-temp.
The suite does not launch the detached watcher or touch Documents/BFBC2.
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
ORIGINAL = b"; fixture\r\n[Graphics]\r\n  MSAA = 3 ; original\r\nQuality=high\r\n[WindowSettings]\r\nWidth=1024\r\n"
PREPARED = ORIGINAL.replace(b"MSAA = 3", b"MSAA = 0")


@unittest.skipUnless(PWSH, "PowerShell 7.5 required")
class GraphicsSettingsLeaseTests(unittest.TestCase):
    def invoke(self, action, original=ORIGINAL):
        scratch = ROOT / "test-temp"
        scratch.mkdir(exist_ok=True)
        with tempfile.TemporaryDirectory(prefix="graphics-lease-", dir=scratch) as temporary:
            folder = Path(temporary)
            (folder / "settings.ini").write_bytes(original)
            payload = base64.b64encode(json.dumps({"root": str(ROOT), "folder": str(folder)}).encode()).decode()
            code = r"""
$ErrorActionPreference='Stop'
Set-StrictMode -Version 3
$v=[Text.Encoding]::UTF8.GetString([Convert]::FromBase64String('PAYLOAD'))|ConvertFrom-Json -AsHashtable
. (Join-Path $v.root 'launcher/Launcher.Core.ps1')
. (Join-Path $v.root 'launcher/runtime/Runtime.Core.ps1')
. (Join-Path $v.root 'launcher/runtime/GraphicsSettings.ps1')
$script:fixtureSettings=Join-Path $v.folder 'settings.ini'
$stateRoot=Join-Path $v.folder 'runtime'
$gamePath=Join-Path $v.folder 'BFBC2Game.exe'
$script:supervisor=@{pid=$PID;path=(Join-Path $PSHOME 'pwsh.exe');start_utc='2026-11-12T12:00:00.1234567Z'}
$gameIdentity=@{pid=999991;path=$gamePath;start_utc='2026-11-12T12:00:01.1234567Z'}
$script:processStates=@{};$script:processStates[[string]$PID]='alive'
$script:games=@();$script:parentPid=$PID
function Get-Bc2GraphicsSettingsPath {return $script:fixtureSettings}
function Get-Bc2ProcessIdentity($Process,[bool]$RequireLive=$false){return $script:supervisor.Clone()}
function Get-Bc2GraphicsProcessState($Identity){
    if($script:processStates.ContainsKey([string]$Identity.pid)){return $script:processStates[[string]$Identity.pid]}
    return 'exited'
}
function Get-Bc2GraphicsLiveGames {return $script:games}
function Get-Bc2GraphicsParentPid([int]$ProcessId){return $script:parentPid}
function Prepare {Start-Bc2GraphicsSettingsLease $stateRoot $gamePath -Enabled $true}
function Bind {
    $script:processStates[[string]$gameIdentity.pid]='alive';$script:games=@($gameIdentity)
    Set-Bc2GraphicsSettingsGame $stateRoot $gameIdentity
}
function Read-Journal {Read-FvrJson (Join-Path $stateRoot 'graphics-settings/lease.json')}
function Save-Journal($Journal){Write-Bc2GraphicsJournal (Join-Path $stateRoot 'graphics-settings/lease.json') $Journal}
function Read-Settings {[Convert]::ToBase64String([IO.File]::ReadAllBytes($script:fixtureSettings))}
function Reject([scriptblock]$Body){try{& $Body|Out-Null;return $false}catch{return $true}}
try {
ACTION
} catch {
    $failure=$_;$details=@{}
    for($cause=$failure.Exception;$null -ne $cause;$cause=$cause.InnerException){
        foreach($key in $cause.Data.Keys){$details[[string]$key]=$cause.Data[$key]}
    }
    [Console]::Error.WriteLine((@{message=$failure.Exception.Message;data=$details}|ConvertTo-Json -Depth 8 -Compress))
    throw
}
""".replace("PAYLOAD", payload).replace("ACTION", action)
            encoded = base64.b64encode(code.encode("utf-16le")).decode()
            environment = dict(os.environ, TEMP=str(folder), TMP=str(folder), TMPDIR=str(folder))
            result = subprocess.run(
                [PWSH, "-NoLogo", "-NoProfile", "-NonInteractive", "-EncodedCommand", encoded],
                capture_output=True, text=True, timeout=20, env=environment,
                creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0),
            )
            self.assertEqual(result.returncode, 0, result.stderr)
            return json.loads(result.stdout)

    def test_default_off_has_no_profile_or_state_effects(self):
        result = self.invoke("""
function Get-Bc2GraphicsSettingsPath {throw 'Off must not inspect settings'}
$r=Start-Bc2GraphicsSettingsLease 'not absolute' 'not a game'
@{result=$r;exists=[IO.Directory]::Exists($stateRoot);bytes=(Read-Settings)}|ConvertTo-Json
""")
        self.assertEqual(result["result"]["status"], "disabled")
        self.assertFalse(result["exists"])
        self.assertEqual(base64.b64decode(result["bytes"]), ORIGINAL)

    def test_exact_msaa_only_roundtrip_for_supported_encodings(self):
        text = ORIGINAL.decode().replace("; fixture", "; café")
        for codec, bom in (("utf-8", b""), ("utf-8", b"\xef\xbb\xbf"),
                           ("utf-16le", b"\xff\xfe"), ("utf-16be", b"\xfe\xff"),
                           ("cp1252", b"")):
            with self.subTest(codec=codec, bom=bom):
                original = bom + text.encode(codec)
                result = self.invoke("""
$lease=Prepare;$prepared=Read-Settings;$j=Read-Journal
$backup=[Convert]::ToBase64String([IO.File]::ReadAllBytes((Join-Path $stateRoot ('graphics-settings/'+$j.backup))))
$restore=Restore-Bc2GraphicsSettingsLease $stateRoot
@{lease=$lease.status;prepared=$prepared;original=$backup;restored=(Read-Settings);restore=$restore.status}|ConvertTo-Json
""", original)
                self.assertEqual(result["lease"], "prepared")
                self.assertEqual(base64.b64decode(result["prepared"]), bom + text.replace("MSAA = 3", "MSAA = 0").encode(codec))
                self.assertEqual(base64.b64decode(result["original"]), original)
                self.assertEqual(base64.b64decode(result["restored"]), original)
                self.assertEqual(result["restore"], "restored")

    def test_all_four_msaa_values_and_mixed_line_endings(self):
        for value in range(4):
            with self.subTest(value=value):
                raw = f"[gRaPhIcS] # graphics\r\tmsaa\t=\t{value}# keep\nOther=1".encode()
                result = self.invoke("$null=Prepare;$r=Restore-Bc2GraphicsSettingsLease $stateRoot;@{status=$r.status;bytes=(Read-Settings)}|ConvertTo-Json", raw)
                self.assertEqual(result["status"], "restored")
                self.assertEqual(base64.b64decode(result["bytes"]), raw)

    def test_unsupported_and_ambiguous_ini_refused_before_journal(self):
        cases = [b"[Graphics]\nMSAA=4\n", b"[Graphics]\nMSAA=1\nMSAA=2\n",
                 b"[Graphics]\nMSAA=3\n[Graphics]\n", b"[Graphics]\nQuality=1\n",
                 b"[Graphics]\nMSAA=3 garbage\n", b"[Graphics]\nMSAA=03\n",
                 b"[Graphics]\n[Malformed\nMSAA=3\n", b"[Other]\nMSAA=3\n",
                 b"\xff\xfe[\x00G", b"[Graphics]\nMSAA=3\x00\n"]
        for raw in cases:
            with self.subTest(raw=raw):
                result = self.invoke("@{rejected=(Reject {Prepare});journal=[IO.File]::Exists((Join-Path $stateRoot 'graphics-settings/lease.json'));bytes=(Read-Settings)}|ConvertTo-Json", raw)
                self.assertTrue(result["rejected"])
                self.assertFalse(result["journal"])
                self.assertEqual(base64.b64decode(result["bytes"]), raw)

    def test_running_or_unqueryable_game_prevents_preparation(self):
        result = self.invoke("""
$script:games=@(@{pid=333;unknown=$true})
@{rejected=(Reject {Prepare});bytes=(Read-Settings)}|ConvertTo-Json
""")
        self.assertTrue(result["rejected"])
        self.assertEqual(base64.b64decode(result["bytes"]), ORIGINAL)

    def test_bound_game_outlives_stop_until_exact_exit(self):
        result = self.invoke("""
$null=Prepare;$bound=Bind;$pending=Restore-Bc2GraphicsSettingsLease $stateRoot;$during=Read-Settings
$script:processStates[[string]$gameIdentity.pid]='exited';$script:games=@()
$done=Restore-Bc2GraphicsSettingsLease $stateRoot
@{bound=$bound.status;pending=$pending;during=$during;done=$done.status;bytes=(Read-Settings)}|ConvertTo-Json
""")
        self.assertEqual(result["bound"], "bound")
        self.assertEqual(result["pending"]["reason"], "game_alive")
        self.assertEqual(base64.b64decode(result["during"]), PREPARED)
        self.assertEqual(result["done"], "restored")
        self.assertEqual(base64.b64decode(result["bytes"]), ORIGINAL)

    def test_bind_requires_exact_path_live_birth_and_owned_parent(self):
        for mutation in ("$gameIdentity.path=Join-Path $v.folder 'other/BFBC2Game.exe'",
                         "$script:processStates[[string]$gameIdentity.pid]='different'",
                         "$script:processStates[[string]$gameIdentity.pid]='unknown'",
                         "$script:parentPid=777777"):
            with self.subTest(mutation=mutation):
                result = self.invoke("$null=Prepare;$script:processStates[[string]$gameIdentity.pid]='alive';" + mutation + ";@{rejected=(Reject {Set-Bc2GraphicsSettingsGame $stateRoot $gameIdentity});state=(Read-Journal).state}|ConvertTo-Json")
                self.assertTrue(result["rejected"])
                self.assertEqual(result["state"], "prepared")

    def test_bound_identity_checks_one_tick_birth_and_pid_reuse(self):
        # Exercise the actual identity classifier with a fake retained process;
        # only its OS acquisition/metadata boundary is replaced by AST substitution.
        result = self.invoke("""
$source=(Get-Item Function:Get-Bc2GraphicsProcessState).ScriptBlock
$tokens=$null;$errors=$null
$ast=[Management.Automation.Language.Parser]::ParseFile((Join-Path $v.root 'launcher/runtime/GraphicsSettings.ps1'),[ref]$tokens,[ref]$errors)
$fn=$ast.FindAll({param($n) $n -is [Management.Automation.Language.FunctionDefinitionAst] -and $n.Name -ceq 'Get-Bc2GraphicsProcessState'},$false)[0].Extent.Text
$fn=$fn.Replace('[Diagnostics.Process]::GetProcessById([int]$Identity.pid)','$script:fakeProcess')
Invoke-Expression $fn
$script:fakeProcess=[pscustomobject]@{HasExited=$false}
$script:fakeProcess|Add-Member ScriptMethod Dispose {}
$script:supervisor=$gameIdentity.Clone();$same=Get-Bc2GraphicsProcessState $gameIdentity
$script:supervisor.start_utc='2026-11-12T12:00:01.1234568Z';$reused=Get-Bc2GraphicsProcessState $gameIdentity
function Get-Bc2ProcessIdentity {throw 'query denied'}
$unknown=Get-Bc2GraphicsProcessState $gameIdentity
@{same=$same;reused=$reused;unknown=$unknown;parse_errors=$errors.Count}|ConvertTo-Json
""")
        self.assertEqual(result, {"same": "alive", "reused": "different", "unknown": "unknown", "parse_errors": 0})

    def test_unbound_crash_recovery_after_supervisor_exit(self):
        for original_on_disk in (False, True):
            with self.subTest(crash_before_mutation=original_on_disk):
                action = """
$null=Prepare;$j=Read-Journal;$j.state='preparing';$j.supervisor.pid=777777;Save-Journal $j
"""
                if original_on_disk:
                    action += "[IO.File]::WriteAllBytes($script:fixtureSettings,[IO.File]::ReadAllBytes((Join-Path $stateRoot ('graphics-settings/'+$j.backup))))\n"
                result = self.invoke(action + "$r=Restore-Bc2GraphicsSettingsLease $stateRoot;@{status=$r.status;bytes=(Read-Settings)}|ConvertTo-Json")
                self.assertEqual(result["status"], "restored")
                self.assertEqual(base64.b64decode(result["bytes"]), ORIGINAL)

    def test_unbound_live_or_unknown_supervisor_blocks_recovery(self):
        for lifetime in ("alive", "unknown"):
            with self.subTest(lifetime=lifetime):
                result = self.invoke("$null=Prepare;$j=Read-Journal;$j.supervisor.pid=777777;Save-Journal $j;$script:processStates['777777']='" + lifetime + "';$r=Restore-Bc2GraphicsSettingsLease $stateRoot;@{result=$r;bytes=(Read-Settings)}|ConvertTo-Json")
                self.assertEqual(result["result"]["status"], "pending")
                self.assertEqual(base64.b64decode(result["bytes"]), PREPARED)

    def test_unknown_owned_or_ambiguous_other_game_retains_lease(self):
        for lifetime, others in (("unknown", "@()"), ("exited", "@(@{pid=222;unknown=$true})"),
                                 ("different", "@(@{pid=999991;unknown=$true})")):
            with self.subTest(lifetime=lifetime, others=others):
                result = self.invoke("$null=Prepare;$null=Bind;$script:processStates[[string]$gameIdentity.pid]='" + lifetime + "';$script:games=" + others + ";$r=Restore-Bc2GraphicsSettingsLease $stateRoot;@{result=$r;bytes=(Read-Settings)}|ConvertTo-Json")
                self.assertEqual(result["result"]["status"], "pending")
                self.assertEqual(base64.b64decode(result["bytes"]), PREPARED)

    def test_restore_preserves_unrelated_user_edits(self):
        result = self.invoke("""
$null=Prepare
$text=[IO.File]::ReadAllText($script:fixtureSettings).Replace('Width=1024','Width=1920').Replace('Quality=high','Quality=low')
[IO.File]::WriteAllText($script:fixtureSettings,$text,[Text.UTF8Encoding]::new($false))
$r=Restore-Bc2GraphicsSettingsLease $stateRoot
@{status=$r.status;bytes=(Read-Settings)}|ConvertTo-Json
""")
        self.assertEqual(result["status"], "restored")
        self.assertEqual(base64.b64decode(result["bytes"]), ORIGINAL.replace(b"Width=1024", b"Width=1920").replace(b"Quality=high", b"Quality=low"))

    def test_conflicting_msaa_or_duplicate_retains_backup_and_blocks_new_lease(self):
        for change in (".Replace('MSAA = 0','MSAA = 2')", "+\"`r`n[Graphics]`r`nMSAA=0\""):
            with self.subTest(change=change):
                result = self.invoke("""
$null=Prepare;$text=([IO.File]::ReadAllText($script:fixtureSettings))CHANGE
[IO.File]::WriteAllText($script:fixtureSettings,$text,[Text.UTF8Encoding]::new($false));$before=Read-Settings
$r=Restore-Bc2GraphicsSettingsLease $stateRoot;$j=Read-Journal
@{status=$r.status;before=$before;bytes=(Read-Settings);backup=[IO.File]::Exists((Join-Path $stateRoot ('graphics-settings/'+$j.backup)));new_refused=(Reject {Prepare})}|ConvertTo-Json
""".replace("CHANGE", change))
                self.assertEqual(result["status"], "pending")
                self.assertEqual(result["bytes"], result["before"])
                self.assertTrue(result["backup"])
                self.assertTrue(result["new_refused"])

    def test_original_zero_lease_never_overwrites_later_user_value(self):
        result = self.invoke("""
$null=Prepare
[IO.File]::WriteAllText($script:fixtureSettings,"[Graphics]`nMSAA=2`nWidth=1234",[Text.UTF8Encoding]::new($false));$before=Read-Settings
$r=Restore-Bc2GraphicsSettingsLease $stateRoot
@{status=$r.status;before=$before;bytes=(Read-Settings)}|ConvertTo-Json
""", PREPARED)
        self.assertEqual(result["status"], "restored")
        self.assertEqual(result["bytes"], result["before"])

    def test_corrupt_backup_or_redirected_journal_is_retained(self):
        for mutation in ("[IO.File]::AppendAllText((Join-Path $stateRoot ('graphics-settings/'+$j.backup)),'corruption')",
                         "$j.settings_path=Join-Path $v.folder 'other.ini';Save-Journal $j",
                         "$j.backup='../settings.ini';Save-Journal $j",
                         "$j.game=@{pid=3;path=$gamePath;start_utc='bad'};Save-Journal $j"):
            with self.subTest(mutation=mutation):
                result = self.invoke("$null=Prepare;$j=Read-Journal;" + mutation + ";$r=Restore-Bc2GraphicsSettingsLease $stateRoot;@{status=$r.status;bytes=(Read-Settings)}|ConvertTo-Json")
                self.assertEqual(result["status"], "pending")
                self.assertEqual(base64.b64decode(result["bytes"]), PREPARED)

    def test_compare_before_restore_write_preserves_racing_edit(self):
        result = self.invoke("""
$null=Prepare
$script:writeOriginal=(Get-Item Function:Write-Bc2GraphicsAtomic).ScriptBlock
function Write-Bc2GraphicsAtomic([string]$Path,[byte[]]$Bytes,[string]$ExpectedHash=''){
    if($Path -ceq $script:fixtureSettings){[IO.File]::AppendAllText($Path,"; concurrent edit`r`n",[Text.UTF8Encoding]::new($false))}
    & $script:writeOriginal $Path $Bytes $ExpectedHash
}
$r=Restore-Bc2GraphicsSettingsLease $stateRoot
@{status=$r.status;bytes=(Read-Settings);state=(Read-Journal).state}|ConvertTo-Json
""")
        self.assertEqual(result["status"], "pending")
        self.assertEqual(base64.b64decode(result["bytes"]), PREPARED + b"; concurrent edit\r\n")
        self.assertEqual(result["state"], "restoring")

    def test_crash_after_restore_write_is_idempotently_recovered(self):
        result = self.invoke("""
$null=Prepare
$script:journalOriginal=(Get-Item Function:Write-Bc2GraphicsJournal).ScriptBlock
function Write-Bc2GraphicsJournal([string]$Path,$Journal){
    if($Journal.state -ceq 'restored'){throw 'fixture crash after settings replacement'}
    & $script:journalOriginal $Path $Journal
}
$first=Restore-Bc2GraphicsSettingsLease $stateRoot;$pendingState=(Read-Journal).state
Set-Item Function:Write-Bc2GraphicsJournal $script:journalOriginal
$second=Restore-Bc2GraphicsSettingsLease $stateRoot
@{first=$first.status;pending_state=$pendingState;second=$second.status;bytes=(Read-Settings)}|ConvertTo-Json
""")
        self.assertEqual(result["first"], "pending")
        self.assertEqual(result["pending_state"], "restoring")
        self.assertEqual(result["second"], "restored")
        self.assertEqual(base64.b64decode(result["bytes"]), ORIGINAL)

    def test_crash_after_prepare_write_keeps_original_journal_recoverable(self):
        result = self.invoke("""
$script:journalOriginal=(Get-Item Function:Write-Bc2GraphicsJournal).ScriptBlock
function Write-Bc2GraphicsJournal([string]$Path,$Journal){
    if($Journal.state -ceq 'prepared'){throw 'fixture crash after preparation replacement'}
    & $script:journalOriginal $Path $Journal
}
$failed=Reject {Prepare};$pendingState=(Read-Journal).state;$during=Read-Settings
Set-Item Function:Write-Bc2GraphicsJournal $script:journalOriginal
$r=Restore-Bc2GraphicsSettingsLease $stateRoot
@{failed=$failed;pending_state=$pendingState;during=$during;status=$r.status;bytes=(Read-Settings)}|ConvertTo-Json
""")
        self.assertTrue(result["failed"])
        self.assertEqual(result["pending_state"], "preparing")
        self.assertEqual(base64.b64decode(result["during"]), PREPARED)
        self.assertEqual(result["status"], "restored")
        self.assertEqual(base64.b64decode(result["bytes"]), ORIGINAL)

    def test_concurrent_recovery_lock_defers_without_changes(self):
        result = self.invoke("""
$null=Prepare;$held=Enter-Bc2GraphicsLease (Get-Bc2GraphicsLeasePaths $stateRoot)
try {$r=Restore-Bc2GraphicsSettingsLease $stateRoot} finally {$held.Dispose()}
@{status=$r.status;bytes=(Read-Settings)}|ConvertTo-Json
""")
        self.assertEqual(result["status"], "pending")
        self.assertEqual(base64.b64decode(result["bytes"]), PREPARED)

    def test_completed_lease_can_be_replaced_without_losing_evidence(self):
        result = self.invoke("""
$first=Prepare;$old=Read-Journal;$null=Restore-Bc2GraphicsSettingsLease $stateRoot;$second=Prepare
@{status=$second.status;archive=[IO.File]::Exists((Join-Path $stateRoot ('graphics-settings/'+$old.id+'.completed.json')));backup=[IO.File]::Exists($first.backup)}|ConvertTo-Json
""")
        self.assertEqual(result["status"], "prepared")
        self.assertTrue(result["archive"])
        self.assertTrue(result["backup"])

    def test_watcher_uses_hidden_child_and_has_no_game_stop_operation(self):
        result = self.invoke("""
$null=Prepare
function Start-Bc2Child([string]$File,[string[]]$Arguments,[string]$Folder,[string]$LogName){return @{file=$File;arguments=$Arguments;folder=$Folder;log=$LogName}}
$child=Start-Bc2GraphicsSettingsWatcher $stateRoot
$tokens=$null;$errors=$null
$null=[Management.Automation.Language.Parser]::ParseFile((Join-Path $v.root 'launcher/runtime/RestoreGraphicsSettings.ps1'),[ref]$tokens,[ref]$errors)
@{child=$child;errors=$errors.Count}|ConvertTo-Json -Depth 8
""")
        self.assertEqual(result["errors"], 0)
        self.assertIn("-NonInteractive", result["child"]["arguments"])
        self.assertTrue(any(str(item).endswith("RestoreGraphicsSettings.ps1") for item in result["child"]["arguments"]))
        core = (ROOT / "launcher/runtime/Runtime.Core.ps1").read_text(encoding="utf-8")
        self.assertIn("WindowStyle='Hidden'", core)
        for name in ("GraphicsSettings.ps1", "RestoreGraphicsSettings.ps1"):
            source = (ROOT / "launcher/runtime" / name).read_text(encoding="utf-8")
            for forbidden in ("Stop-Process", "CloseMainWindow", "taskkill", "Start-Bc2GraphicsGame"):
                self.assertNotIn(forbidden, source)


if __name__ == "__main__":
    unittest.main()
