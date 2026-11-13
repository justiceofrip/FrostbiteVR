"""Execute the runner's real continuation/backup/restore blocks with inert owners.

No runner entry, launcher, game or input is invoked. These are deterministic
provenance and lifecycle checks, not evidence of campaign checkpoint progress.
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
NAMES = ("GameSettings.bin", "GameSettings.ini", "settings.ini")
HARNESS = r"""
param([string]$Root,[string]$Fixture,[string]$Case)
Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'
$tokens=$null;$errors=$null
$ast=[Management.Automation.Language.Parser]::ParseFile((Join-Path $Root 'tools/Invoke-Bc2XrGameSimulation.ps1'),[ref]$tokens,[ref]$errors)
if($errors.Count){throw ($errors|Out-String)}
foreach($name in 'Require','Full','Pin','Json','Backup','SameCampaignPin','GetCampaignContinuation','PrepareCampaignContinuation'){
    $fn=$ast.Find({param($n) $n -is [Management.Automation.Language.FunctionDefinitionAst] -and $n.Name -ceq $name}.GetNewClosure(),$true)
    if(-not $fn){throw "Missing production function $name"}
    . ([scriptblock]::Create($fn.Extent.Text))
}
$core=[Management.Automation.Language.Parser]::ParseFile((Join-Path $Root 'launcher/Launcher.Core.ps1'),[ref]$tokens,[ref]$errors)
$fn=$core.Find({param($n) $n -is [Management.Automation.Language.FunctionDefinitionAst] -and $n.Name -ceq 'Assert-FvrNoLinks'},$true)
. ([scriptblock]::Create($fn.Extent.Text))
$main=@($ast.EndBlock.Statements|Where-Object {$_ -is [Management.Automation.Language.TryStatementAst]})
if($main.Count -ne 1){throw 'Expected one runner lifecycle'}
$steps=@($main[0].Body.Statements)
$indices=@(for($i=0;$i -lt $steps.Count;$i++){if($steps[$i].Extent.Text.StartsWith('if($ContinueCampaignFromRun){')){$i}})
if($indices.Count -ne 2){throw 'Expected admission and staging in the production lifecycle'}
$prepare=[scriptblock]::Create(($steps[$indices[0]..$indices[1]].Extent.Text -join "`n"))
$launchGuard=@($steps|Where-Object {$_.Extent.Text.StartsWith('foreach($row in $profiles+$launcherFiles){')})
if($launchGuard.Count -ne 1){throw 'Missing prepared/original launch guard'}
$launchGuard=[scriptblock]::Create($launchGuard[0].Extent.Text)
$cleanup=$main[0].Finally.Find({param($n) $n -is [Management.Automation.Language.TryStatementAst] -and
    $n.Body.Statements[0].Extent.Text.StartsWith('if($session -and $session.ContainsKey(''performance_mode''')},$true)
if(-not $cleanup){throw 'Missing production profile cleanup'}
$source=$ast.Extent.Text
if(-not ($source.IndexOf("Save 'originals.json'") -lt $steps[$indices[1]].Extent.StartOffset -and
    $steps[$indices[1]].Extent.EndOffset -lt $source.IndexOf('$entry.Start()') -and
    $source.IndexOf('$entry.Start()') -lt $cleanup.Extent.StartOffset -and
    $cleanup.Extent.Text.IndexOf("Save 'profile-after.json'") -lt $cleanup.Extent.Text.IndexOf('[IO.File]::WriteAllBytes'))){throw 'Backup/stage/start/retain/restore order changed'}
$cleanup=[scriptblock]::Create($cleanup.Extent.Text)
$events=[Collections.Generic.List[string]]::new()
function NoOtherProcesses {param([switch]$BeforeLaunch) $events.Add('no_other_processes')}
function Find-Bc2OwnedProcess($Identity){
    if($Case -ceq 'live_owner' -and $Identity.pid -eq 101){
        $p=[pscustomobject]@{};$p|Add-Member ScriptMethod Dispose {} ;return $p
    }
    return $null
}
function Save([string]$Name,$Value){
    $events.Add($Name)
    [IO.File]::WriteAllText((Join-Path $OutputDir $Name),($Value|ConvertTo-Json -Depth 40),[Text.UTF8Encoding]::new($false))
    if($Name -ceq 'campaign-continuation.json' -and $Value.status -ceq 'admitted'){
        if($Case -ceq 'changed_after_admission'){[IO.File]::AppendAllText($Value.source.path,'changed')}
        if($Case -ceq 'proof_changed_after_admission'){[IO.File]::AppendAllText($Value.proof['result.json'].path,' ')}
    }
}
$RepositoryRoot=$Fixture;$OutputDir=Join-Path $Fixture 'reports/new-run'
$ProfileDirectory=Join-Path $Fixture 'profiles';$InstallRoot=Join-Path $Fixture 'install'
$ContinueCampaignFromRun=Join-Path $Fixture 'reports/prior-run'
if($Case -ceq 'default'){$ContinueCampaignFromRun=''}
if($Case -ceq 'foreign_directory'){$ContinueCampaignFromRun=Join-Path $Fixture 'foreign-run'}
$preAttachSpec=if($Case -ceq 'conflicting_seed'){@{campaign_seed=@{}}}else{$null}
$gamePin=Pin (Join-Path $Fixture 'game/BFBC2Game.exe')
$profiles=@();$launcherFiles=@();$preparedProfileHashes=@{};$receipt=@{}
$failures=[Collections.Generic.List[string]]::new();$owned=@{};$session=$null;$restored=$false
$caught=$null;$staged=@{}
try{
    . $prepare
    . $launchGuard
    foreach($name in 'GameSettings.bin','GameSettings.ini','settings.ini'){$staged[$name]=(Pin (Join-Path $ProfileDirectory $name)).sha256}
    if($Case -ceq 'later_failure'){throw 'Deliberate post-staging failure'}
}catch{$caught=$_.Exception.Message}finally{. $cleanup}
@{error=$caught;staged=$staged;restored=$restored;failures=@($failures);events=@($events);receipt=$receipt;prepared=$preparedProfileHashes}|ConvertTo-Json -Depth 40 -Compress
"""


def pin(path):
    data = path.read_bytes()
    return dict(path=str(path), bytes=len(data), sha256=hashlib.sha256(data).hexdigest())


def write_json(path, value):
    path.write_text(json.dumps(value), encoding="utf-8")


@unittest.skipUnless(os.name == "nt" and PWSH, "requires Windows PowerShell 7.5+")
class CampaignContinuationTests(unittest.TestCase):
    def replay(self, case="valid", mutate=None):
        with tempfile.TemporaryDirectory(prefix="campaign-continuation-", dir=ROOT / "reports") as temp:
            base = Path(temp)
            prior = base / "reports/prior-run"
            current = base / "reports/new-run"
            for folder in (prior / "profile-before", prior / "profile-after", current,
                           base / "profiles", base / "install/data", base / "game"):
                folder.mkdir(parents=True, exist_ok=True)
            game = base / "game/BFBC2Game.exe"
            game.write_bytes(b"inert game identity fixture")
            originals, after, expected = [], [], {}
            for name in NAMES:
                profile = base / "profiles" / name
                profile.write_bytes(("current user " + name).encode())
                expected[name] = profile.read_bytes()
                old = prior / "profile-before" / name
                old.write_bytes(("prior user " + name).encode())
                saved = prior / "profile-after" / name
                saved.write_bytes(("retained session " + name).encode())
                originals.append(dict(original=dict(pin(old), path=str(profile)), backup=pin(old)))
                after.append(dict(original=dict(pin(saved), path=str(profile)), backup=pin(saved)))
            for name in ("active.json", "data/settings.json"):
                (base / "install" / name).write_bytes(b"unchanged launcher fixture")
            processes = [dict(name=name, exited=True, exit_code=0,
                              identity=dict(pid=101 + i, start_utc="2026-11-12T12:00:00Z",
                                            path=str(game if name == "game" else base / f"{name}.exe")))
                         for i, name in enumerate(("entry", "supervisor", "game", "host", "trace"))]
            result = dict(schema="fvr.xr-game-simulation.v1", status="observed_and_closed", failures=[],
                          forced_termination_by_wrapper=False, processes=processes)
            result.update({key: True for key in ("actual_game_started", "native_running_observed",
                          "simulator_host_observed", "native_retirement_proven", "exact_three_profiles_restored",
                          "normal_game_close", "launcher_active_settings_preserved")})
            files = {
                "run-manifest.json": dict(schema="fvr.xr-game-run.v1", output_directory=str(prior),
                                          artifact_binding_path=str(prior / "artifact-binding.json")),
                "result.json": result,
                "artifact-binding.json": dict(game=pin(game)),
                "native-retirement.json": dict(hooks_disabled=True, reload_flow_drained=True,
                    native_retirement_proven=True, pre_activation_refusal=False,
                    bootstrap=dict(pid=103, probe_exit=0, remote_thread_exit_observed=True)),
                "originals.json": dict(profiles=originals),
                "profile-after.json": after,
            }
            if mutate:
                mutate(files, prior, base)
            for name, value in files.items():
                write_json(prior / name, value)
            source_hash = pin(prior / "profile-after/GameSettings.bin")["sha256"]
            if case == "tampered_binary":
                (prior / "profile-after/GameSettings.bin").write_bytes(b"tampered")
            if case == "missing_proof":
                (prior / "result.json").unlink()
            if case == "foreign_directory":
                shutil.copytree(prior, base / "foreign-run")
            linked = None
            if case == "reparse_run":
                linked = base / "retained-run"
                prior.rename(linked)
                # Junction creation does not require symlink privileges.
                made = subprocess.run(["cmd", "/c", "mklink", "/J", str(prior), str(linked)],
                                      capture_output=True, text=True)
                self.assertEqual(made.returncode, 0, made.stderr)
            harness = base / "harness.ps1"
            harness.write_text(HARNESS, encoding="utf-8")
            try:
                run = subprocess.run([PWSH, "-NoProfile", "-NonInteractive", "-File", str(harness),
                                      str(ROOT), str(base), case], capture_output=True, text=True, timeout=30)
            finally:
                if linked is not None:
                    os.rmdir(prior)  # Remove only the junction, never recurse through it.
            self.assertEqual(run.returncode, 0, run.stderr)
            answer = json.loads(run.stdout)
            self.assertEqual(answer["failures"], [])
            for name, data in expected.items():
                self.assertEqual((base / "profiles" / name).read_bytes(), data, name)
            answer["expected"] = {name: hashlib.sha256(data).hexdigest() for name, data in expected.items()}
            answer["source_hash"] = source_hash
            answer["after_binary"] = ((current / "profile-after/GameSettings.bin").read_bytes()
                                      if (current / "profile-after/GameSettings.bin").exists() else None)
            if (current / "campaign-continuation.json").exists():
                answer["continuation"] = json.loads((current / "campaign-continuation.json").read_text())
            return answer

    def test_stage_only_binary_after_backup_and_retain_before_exact_restore(self):
        answer = self.replay()
        self.assertIsNone(answer["error"])
        self.assertTrue(answer["restored"])
        self.assertEqual(answer["staged"][NAMES[0]], answer["source_hash"])
        for name in NAMES[1:]:
            self.assertEqual(answer["staged"][name], answer["expected"][name])
        self.assertEqual(answer["after_binary"], b"retained session GameSettings.bin")
        events = answer["events"]
        self.assertLess(events.index("originals.json"), len(events) - 1 - events[::-1].index("campaign-continuation.json"))
        continuation = answer["continuation"]
        self.assertEqual(continuation, answer["receipt"]["campaign_continuation"])
        self.assertEqual(continuation["source"]["sha256"], continuation["staged"]["sha256"])
        self.assertFalse(continuation["checkpoint_progress_claimed"])
        self.assertEqual(len(continuation["proof"]), 6)

    def test_default_and_failure_preserve_current_user_profiles(self):
        answer = self.replay("default")
        self.assertIsNone(answer["error"])
        self.assertEqual(answer["staged"], answer["expected"])
        self.assertNotIn("continuation", answer)
        failed = self.replay("later_failure")
        self.assertIn("post-staging", failed["error"])
        self.assertTrue(failed["restored"])
        self.assertEqual(failed["after_binary"], b"retained session GameSettings.bin")

    def test_refuse_bad_evidence_without_staging(self):
        mutations = {
            "failed": lambda f, p, b: f["result.json"].update(status="failed"),
            "unclosed": lambda f, p, b: f["result.json"].update(normal_game_close=False),
            "not_started": lambda f, p, b: f["result.json"].update(actual_game_started=False),
            "unrestored": lambda f, p, b: f["result.json"].update(exact_three_profiles_restored=False),
            "unretired": lambda f, p, b: f["native-retirement.json"].update(reload_flow_drained=False),
            "string_bool": lambda f, p, b: f["result.json"].update(normal_game_close="true"),
            "owner_unclosed": lambda f, p, b: f["result.json"]["processes"][0].update(exited=False),
            "cross_game": lambda f, p, b: f["artifact-binding.json"]["game"].update(sha256="0" * 64),
            "cross_game_path": lambda f, p, b: f["artifact-binding.json"]["game"].update(path=str(b / "other.exe")),
            "foreign_manifest": lambda f, p, b: f["run-manifest.json"].update(output_directory=str(b)),
            "foreign_source": lambda f, p, b: f["profile-after.json"][0]["backup"].update(path=str(p / "profile-before/GameSettings.bin")),
            "wrong_size": lambda f, p, b: f["profile-after.json"][0]["backup"].update(bytes=1),
        }
        for case in ("tampered_binary", "missing_proof", "foreign_directory", "live_owner", "conflicting_seed",
                     "changed_after_admission", "proof_changed_after_admission", "reparse_run", *mutations):
            with self.subTest(case=case):
                answer = self.replay(case, mutations.get(case))
                self.assertIsNotNone(answer["error"])
                self.assertEqual(answer["prepared"], {})
                self.assertNotEqual(answer.get("continuation", {}).get("status"), "staged")


if __name__ == "__main__":
    unittest.main()
