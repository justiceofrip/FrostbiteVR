"""Execute production runner fragments with inert Entry/process boundaries.

No runner launch, game, real profile, install or durable watcher is started.
The existing private/default runner fixtures remain the regression baseline.
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


@unittest.skipUnless(PWSH, "PowerShell 7.5 required")
class SimulationPerformanceModeTests(unittest.TestCase):
    def invoke(self, action):
        scratch = ROOT / "test-temp"
        scratch.mkdir(exist_ok=True)
        with tempfile.TemporaryDirectory(prefix="simulation-performance-", dir=scratch) as temporary:
            payload = base64.b64encode(json.dumps({"root": str(ROOT), "folder": temporary}).encode()).decode()
            code = r"""
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
$v=[Text.Encoding]::UTF8.GetString([Convert]::FromBase64String('PAYLOAD'))|ConvertFrom-Json -AsHashtable
$runner=Join-Path $v.root 'tools/Invoke-Bc2XrGameSimulation.ps1'
$tokens=$null;$errors=$null
$ast=[Management.Automation.Language.Parser]::ParseFile($runner,[ref]$tokens,[ref]$errors)
if($errors.Count){throw ($errors|Out-String)}
function Assignment([string]$Name){
    $rows=@($ast.FindAll({param($n) $n -is [Management.Automation.Language.AssignmentStatementAst] -and $n.Left.Extent.Text -ceq $Name}.GetNewClosure(),$true))
    if($rows.Count -ne 1){throw "Expected one production assignment $Name"}
    return [scriptblock]::Create($rows[0].Extent.Text)
}
foreach($name in 'Require','SameIdentity','AssertPerformanceOptions','ObservePerformanceSession','AssertPerformanceRestoration','CompletePerformanceRestoration'){
    $fn=$ast.Find({param($n) $n -is [Management.Automation.Language.FunctionDefinitionAst] -and $n.Name -ceq $name}.GetNewClosure(),$true)
    if(-not $fn){throw "Missing production function $name"}
    . ([scriptblock]::Create($fn.Extent.Text))
}
function Reject([scriptblock]$Body){try{& $Body|Out-Null;return $false}catch{return $true}}
$PerformanceMode=[switch]$true;$PrivateSourcePipeline=[switch]$false
. (Assignment '$performanceSelection')
. (Assignment '$sourcePipeline')
$gameExe=Join-Path $v.folder 'BFBC2Game.exe'
$supervisor=@{pid=901;path='fixture-pwsh';start_utc='2026-11-12T12:00:00.0000001Z'}
$game=@{pid=902;path=$gameExe;start_utc='2026-11-12T12:00:01.0000001Z'}
$watcherIdentity=@{pid=903;path='fixture-pwsh';start_utc='2026-11-12T12:00:00.5000001Z'}
$owned=@{supervisor=@{identity=$supervisor;process=[pscustomobject]@{HasExited=$true}};game=@{identity=$game;process=[pscustomobject]@{HasExited=$true}}}
$settingsRow=@{original=@{path=(Join-Path $v.folder 'settings.ini');sha256=('a'*64)}}
$journal=@{schema='bc2vr.graphics-settings-lease.v1';state='restored';supervisor=$supervisor.Clone();game=$game.Clone();game_path=$gameExe;settings_path=$settingsRow.original.path;original_sha256=('a'*64);original_msaa='3'}
$record=@{state='running';performance_mode=@{requested=$true;enabled=$true;native_samples_verified=$false;lease=@{status='bound'};watcher=$watcherIdentity};private_source_pipeline=@{selection='performance_mode';requested=$true;applied=$true}}
ACTION
""".replace("PAYLOAD", payload).replace("ACTION", action)
            encoded = base64.b64encode(code.encode("utf-16le")).decode()
            environment = dict(os.environ, TEMP=temporary, TMP=temporary, TMPDIR=temporary)
            result = subprocess.run(
                [PWSH, "-NoLogo", "-NoProfile", "-NonInteractive", "-EncodedCommand", encoded],
                capture_output=True, text=True, timeout=20, env=environment,
                creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0),
            )
            self.assertEqual(result.returncode, 0, result.stderr)
            return json.loads(result.stdout)

    def test_conflict_is_first_executable_statement_and_default_is_off(self):
        result = self.invoke(r"""
$parameter=@($ast.ParamBlock.Parameters|Where-Object {$_.Name.VariablePath.UserPath -ceq 'PerformanceMode'})
$guard=$ast.EndBlock.Statements[0]
if($guard -isnot [Management.Automation.Language.IfStatementAst] -or -not $guard.Extent.Text.StartsWith('if($PerformanceMode -and $PrivateSourcePipeline)')){throw 'Conflict must precede all initialization, mutation and process work'}
$guardBody=[scriptblock]::Create($guard.Extent.Text+'; $script:laterWork=$true')
$cases=@()
foreach($pair in @(@($false,$false),@($true,$false),@($false,$true),@($true,$true))){
    $PerformanceMode=[switch]$pair[0];$PrivateSourcePipeline=[switch]$pair[1];$script:laterWork=$false
    $rejected=Reject {& $guardBody}
    $cases+=@{performance=$pair[0];private=$pair[1];rejected=$rejected;later_work=$script:laterWork}
}
@{count=$parameter.Count;is_switch=($parameter[0].StaticType -eq [switch]);default=(& ([scriptblock]::Create($parameter[0].DefaultValue.Extent.Text)));cases=$cases}|ConvertTo-Json -Depth 8
""")
        self.assertEqual(result["count"], 1)
        self.assertTrue(result["is_switch"])
        self.assertFalse(result["default"])
        for case in result["cases"]:
            conflict = case["performance"] and case["private"]
            self.assertEqual(case["rejected"], conflict)
            self.assertEqual(case["later_work"], not conflict)

    def test_production_argument_list_and_generated_wrapper_forward_independent_switches(self):
        result = self.invoke(r"""
$write=$ast.Find({param($n) $n -is [Management.Automation.Language.InvokeMemberExpressionAst] -and $n.Extent.Text.StartsWith('[IO.File]::WriteAllText($launchScript,')},$true)
$loop=$ast.Find({param($n) $n -is [Management.Automation.Language.ForEachStatementAst] -and $n.Extent.Text.Contains('$psi.ArgumentList.Add($arg)')},$true)
if(-not $write -or -not $loop){throw 'Missing production launch assembly'}
. ([scriptblock]::Create('$launchText='+$write.Arguments[1].Extent.Text))
function Invoke-FixtureEntry {
    param($Action,$SettingsPath,$StateRoot,$StartAt,[switch]$PerformanceMode,[switch]$PrivateSourcePipeline)
    $script:forwarded=@{action=$Action;start_at=$StartAt;settings=$SettingsPath;state=$StateRoot;performance=[bool]$PerformanceMode;private=[bool]$PrivateSourcePipeline;performance_bound=$PSBoundParameters.ContainsKey('PerformanceMode')}
}
$rows=@()
foreach($pair in @(@($false,$false),@($true,$false),@($false,$true))){
    $PerformanceMode=[switch]$pair[0];$PrivateSourcePipeline=[switch]$pair[1]
    $psi=[Diagnostics.ProcessStartInfo]::new();$OutputDir=$v.folder;$runtimeRoot=$v.folder
    $launchScript=Join-Path $v.folder 'launch.ps1';$settingsPath='literal settings with spaces';$stateRoot='literal state with spaces'
    $PreAttachSetupSpec='';$PreAttachSetupSpecSha256='';$PrivateSessionBinding='';$PrivateSessionBindingSha256=''
    . ([scriptblock]::Create($loop.Extent.Text))
    $assembled=@($psi.ArgumentList);$argsForWrapper=$assembled[6..($assembled.Count-1)]
    if($argsForWrapper[0] -cne (Join-Path $runtimeRoot 'Entry.ps1')){throw 'Wrong Entry argument'}
    $argsForWrapper[0]='Invoke-FixtureEntry';$script:forwarded=$null
    & ([scriptblock]::Create($launchText)) @argsForWrapper
    $rows+=$script:forwarded
}
ConvertTo-Json -InputObject $rows -Depth 6
""")
        self.assertEqual([(r["performance"], r["private"]) for r in result], [(False, False), (True, False), (False, True)])
        for row in result:
            self.assertEqual((row["action"], row["start_at"]), ("Launch", "MainMenu"))
            self.assertEqual(row["settings"], "literal settings with spaces")
            self.assertEqual(row["state"], "literal state with spaces")
            self.assertEqual(row["performance_bound"], row["performance"])

    def test_session_options_require_exact_boolean_and_preserve_older_default(self):
        result = self.invoke(r"""
$rows=@()
foreach($requested in @($false,$true)){
    foreach($value in @('missing',$false,$true,'true','false',0,1,$null)){
        $PerformanceMode=[switch]$requested;. (Assignment '$performanceSelection')
        $options=@{};if($value -isnot [string] -or $value -cne 'missing'){$options.performance_mode=$value}
        $rejected=Reject {AssertPerformanceOptions $options}
        $rows+=@{requested=$requested;value=$value;rejected=$rejected;verified=$performanceSelection.session_option_verified}
    }
}
ConvertTo-Json -InputObject $rows -Depth 6
""")
        for row in result:
            value = row["value"]
            accepted = (value == "missing" and not row["requested"]) or (type(value) is bool and value == row["requested"])
            self.assertEqual(row["rejected"], not accepted, row)
            self.assertEqual(row["verified"], accepted, row)

    def test_session_enablement_is_observed_without_native_or_restore_claim(self):
        result = self.invoke(r"""
AssertPerformanceOptions @{performance_mode=$true}
$before=$performanceSelection|ConvertTo-Json -Depth 8|ConvertFrom-Json -AsHashtable
$record.graphics_settings_restoration=@{status='pending';reason='game_alive'}
$record.graphics_settings_restore_pending=$true
ObservePerformanceSession $record
@{before=$before;after=$performanceSelection;private=$sourcePipeline}|ConvertTo-Json -Depth 12
""")
        self.assertTrue(result["before"]["requested"])
        self.assertFalse(result["before"]["session_enabled_observed"])
        self.assertTrue(result["after"]["session_enabled_observed"])
        self.assertFalse(result["after"]["native_samples_verified"])
        self.assertFalse(result["after"]["restoration_verified"])
        self.assertEqual(result["after"]["graphics_settings_restoration"]["status"], "pending")
        self.assertFalse(result["private"]["requested"])
        self.assertNotIn("session", result["private"])

    def test_real_manifest_launch_and_result_separate_request_from_observation(self):
        result = self.invoke(r"""
$Seconds=300;$TimeoutSeconds=420;$MaxGeneratedBytes=40GB;$GeneratedBudgetReason=''
$plannedBytes=1;$rawBytes=1;$rawLimitBytes=1;$CaptureCount=4;$CaptureWidth=1024;$CaptureHeight=768
$capturePlan=@{maximum_compositions=4};$mechanismMarker=@{requested=$false};$privatePresentation=@{requested=$false};$attributionProfile=@{requested=$false}
$failures=[Collections.Generic.List[string]]::new();$OutputDir=$v.folder;$Version='fixture-version';$ExpectedSourceSha256=('a'*64)
$pwsh='fixture-pwsh';$runtimeRoot=$v.folder;$stateRoot=$v.folder;$MechanismSnapshots=$false;$environment=@{}
. (Assignment '$receipt')
$script:saved=@{}
function Save([string]$Name,$Value){$script:saved[$Name]=$Value|ConvertTo-Json -Depth 16|ConvertFrom-Json -AsHashtable}
foreach($name in 'run-manifest.json','launch.json'){
    $command=$ast.Find({param($n) $n -is [Management.Automation.Language.CommandAst] -and $n.GetCommandName() -ceq 'Save' -and $n.Extent.Text.StartsWith("Save '$name' @{")}.GetNewClosure(),$true)
    if(-not $command){throw "Missing real receipt save $name"}
    . ([scriptblock]::Create($command.Extent.Text))
}
AssertPerformanceOptions @{performance_mode=$true}
ObservePerformanceSession $record
AssertPerformanceRestoration $journal $settingsRow '3'
@{manifest=$saved['run-manifest.json'].performance_mode;launch=$saved['launch.json'].performance_mode;result=$receipt.performance_mode}|ConvertTo-Json -Depth 16
""")
        for early in (result["manifest"], result["launch"]):
            self.assertTrue(early["requested"])
            self.assertFalse(early["session_enabled_observed"])
            self.assertFalse(early["restoration_verified"])
        self.assertTrue(result["result"]["session_option_verified"])
        self.assertTrue(result["result"]["session_enabled_observed"])
        self.assertTrue(result["result"]["restoration_verified"])
        self.assertFalse(result["result"]["native_samples_verified"])

    def test_running_or_unrequested_receipt_contract_failures_are_rejected(self):
        changes = [
            "$record.Remove('performance_mode')",
            "$record.performance_mode.requested=$false",
            "$record.performance_mode.requested='true'",
            "$record.performance_mode.enabled='true'",
            "$record.performance_mode.enabled=$false",
            "$record.performance_mode.lease.status='prepared'",
            "$record.performance_mode.Remove('watcher')",
            "$record.Remove('private_source_pipeline')",
            "$record.private_source_pipeline.selection='private_override'",
            "$record.private_source_pipeline.requested='true'",
            "$PerformanceMode=[switch]$false",
        ]
        for change in changes:
            with self.subTest(change=change):
                result = self.invoke(change + ";@{rejected=(Reject {ObservePerformanceSession $record});native_claim=$performanceSelection.native_samples_verified;restored=$performanceSelection.restoration_verified}|ConvertTo-Json")
                self.assertTrue(result["rejected"])
                self.assertFalse(result["native_claim"])
                self.assertFalse(result["restored"])

    def test_restore_receipt_matches_original_backup_exact_game_and_supervisor(self):
        changes = ["", "$journal.state='bound'", "$journal.state='restoring'",
                   "$journal.supervisor.start_utc='2026-11-12T12:00:00.0000002Z'",
                   "$journal.game.pid=999", "$journal.original_sha256=('b'*64)",
                   "$journal.settings_path='other-settings.ini'", "$journal.game_path='other-game.exe'",
                   "$currentMsaa='0'"]
        for change in changes:
            with self.subTest(change=change):
                result = self.invoke("$currentMsaa='3';" + change + ";$bad=Reject {AssertPerformanceRestoration $journal $settingsRow $currentMsaa};@{rejected=$bad;restored=$performanceSelection.restoration_verified;status=$performanceSelection.restoration_status}|ConvertTo-Json")
                self.assertEqual(result["rejected"], bool(change))
                self.assertEqual(result["restored"], not bool(change))

    def test_cleanup_waits_for_exact_watcher_within_original_deadline(self):
        for case in ("success", "live_game", "watcher_missing", "watcher_live", "watcher_failed", "pending_lease"):
            with self.subTest(case=case):
                result = self.invoke(r"""
$session=$record;$performanceSelection.session_enabled_observed=$true
$profiles=@($settingsRow);$runtimeRoot=$v.folder;$stateRoot=$v.folder;$TimeoutSeconds=420
$clock=[pscustomobject]@{Elapsed=[pscustomobject]@{TotalSeconds=419.5}}
$script:waitCalls=0;$script:waitBudget=$null;$script:leaseReads=0;$script:saved=@{}
$watcher=[pscustomobject]@{HasExited=$false;ExitCode=0;Completes=$true}
$watcher|Add-Member ScriptMethod WaitForExit {param($Milliseconds) $script:waitCalls++;$script:waitBudget=$Milliseconds;$this.HasExited=$this.Completes;return $this.Completes}
$owned.graphics_watcher=@{identity=$watcherIdentity;process=$watcher}
function NoOtherProcesses {}
function Save([string]$Name,$Value){$script:saved[$Name]=$Value}
function Json([string]$Path,[long]$Maximum){return $journal}
[IO.File]::WriteAllText($settingsRow.original.path,'fixture profile bytes')
$helper=@'
function Get-Bc2GraphicsLeasePaths($StateRoot){return @{journal=(Join-Path $StateRoot 'missing-journal.json')}}
function Read-Bc2GraphicsLease($Paths){
    if(-not $owned.graphics_watcher.process.HasExited){throw 'Read overlapped live watcher'}
    $script:leaseReads++;return $journal
}
function Convert-Bc2GraphicsMsaa([byte[]]$Bytes){return @{value='3'}}
'@
[IO.File]::WriteAllText((Join-Path $runtimeRoot 'GraphicsSettings.ps1'),$helper)
switch('CASE'){
    live_game {$owned.game.process.HasExited=$false}
    watcher_missing {$owned.Remove('graphics_watcher')}
    watcher_live {$watcher.Completes=$false}
    watcher_failed {$watcher.ExitCode=2}
    pending_lease {$journal.state='bound'}
}
$rejected=Reject {CompletePerformanceRestoration}
@{rejected=$rejected;wait_calls=$script:waitCalls;wait_budget=$script:waitBudget;lease_reads=$script:leaseReads;restored=$performanceSelection.restoration_verified;profile=[IO.File]::ReadAllText($settingsRow.original.path)}|ConvertTo-Json
""".replace("CASE", case))
                self.assertEqual(result["rejected"], case != "success")
                self.assertEqual(result["restored"], case == "success")
                self.assertEqual(result["profile"], "fixture profile bytes")
                if case in ("live_game", "watcher_missing"):
                    self.assertEqual(result["wait_calls"], 0)
                else:
                    self.assertEqual(result["wait_budget"], 500)
                self.assertEqual(result["lease_reads"], int(case in ("success", "pending_lease")))


if __name__ == "__main__":
    unittest.main()
