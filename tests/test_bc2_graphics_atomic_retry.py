"""Real scratch files and the production atomic writer; no game/profile access.

Only the File.Replace boundary is substituted for deterministic failure cases.
The ordinary creation/replacement case uses the actual OS primitive and ACLs.
"""
import base64
import os
import unittest

import test_bc2_graphics_settings as lease_fixture


INJECT_REPLACE = r"""
$script:replaceCalls=0;$script:waits=0;$script:waitMilliseconds=0
$script:lastTemporary='';$script:observations=[Collections.Generic.List[object]]::new()
$originalHash=Get-Bc2GraphicsHash ([IO.File]::ReadAllBytes($script:fixtureSettings))
$preparedBytes=(Convert-Bc2GraphicsMsaa ([IO.File]::ReadAllBytes($script:fixtureSettings))).bytes
$script:replaceAction={param($Temporary,$Target) [IO.File]::Replace($Temporary,$Target,[NullString]::Value)}
function Invoke-FixtureReplace([string]$Temporary,[string]$Target){
    $script:replaceCalls++;$script:lastTemporary=$Temporary
    $script:observations.Add(@{temporary=$Temporary;target=$Target;
        original=(Get-Bc2GraphicsHash ([IO.File]::ReadAllBytes($Target)));
        prepared=(Get-Bc2GraphicsHash ([IO.File]::ReadAllBytes($Temporary)))})
    & $script:replaceAction $Temporary $Target
}
function Start-Sleep([int]$Milliseconds){
    $script:waits++;$script:waitMilliseconds+=$Milliseconds
    Microsoft.PowerShell.Utility\Start-Sleep -Milliseconds $Milliseconds
}
$tokens=$null;$errors=$null
$ast=[Management.Automation.Language.Parser]::ParseFile((Join-Path $v.root 'launcher/runtime/GraphicsSettings.ps1'),[ref]$tokens,[ref]$errors)
if($errors.Count){throw 'Atomic fixture source did not parse.'}
$definition=$ast.FindAll({param($n) $n -is [Management.Automation.Language.FunctionDefinitionAst] -and $n.Name -ceq 'Write-Bc2GraphicsAtomic'},$false)[0].Extent.Text
$native='[IO.File]::Replace($temporary,$Path,[NullString]::Value)'
if(-not $definition.Contains($native)){throw 'Atomic fixture replacement boundary not found.'}
Invoke-Expression ($definition.Replace($native,'Invoke-FixtureReplace $temporary $Path'))
function Get-FixtureResult($Failure){
    $data=@{};$hresult=$null
    if($Failure){
        $hresult=$Failure.Exception.GetBaseException().HResult
        foreach($key in $Failure.Exception.Data.Keys){$data[[string]$key]=$Failure.Exception.Data[$key]}
    }
    return @{rejected=($null -ne $Failure);hresult=$hresult;data=$data;calls=$script:replaceCalls;
        waits=$script:waits;wait_ms=$script:waitMilliseconds;bytes=(Read-Settings);
        observations=@($script:observations.ToArray());temps=@(Get-ChildItem -LiteralPath $v.folder -Filter '*.tmp').Count}
}
function Invoke-FixtureWrite([string]$ExpectedHash=$originalHash){
    $failure=$null;$clock=[Diagnostics.Stopwatch]::StartNew()
    try {Write-Bc2GraphicsAtomic $script:fixtureSettings $preparedBytes $ExpectedHash}
    catch {$failure=$_}
    $result=Get-FixtureResult $failure;$result.elapsed_ms=$clock.Elapsed.TotalMilliseconds
    return $result
}
"""


@unittest.skipUnless(lease_fixture.PWSH, "PowerShell 7.5 required")
class GraphicsAtomicRetryTests(unittest.TestCase):
    invoke = lease_fixture.GraphicsSettingsLeaseTests.invoke

    @unittest.skipUnless(os.name == "nt", "Windows replacement metadata contract")
    def test_actual_atomic_creation_and_replacement_preserve_bytes_and_target_acl(self):
        result = self.invoke(r"""
$acl=Get-Acl -LiteralPath $script:fixtureSettings
$acl.SetAccessRuleProtection($true,$true)
Set-Acl -LiteralPath $script:fixtureSettings -AclObject $acl
$before=(Get-Acl -LiteralPath $script:fixtureSettings).Sddl
$hash=Get-Bc2GraphicsHash ([IO.File]::ReadAllBytes($script:fixtureSettings))
$bytes=[byte[]](0,255,254,13,10,128,65)
Write-Bc2GraphicsAtomic $script:fixtureSettings $bytes $hash
$created=Join-Path $v.folder 'new-journal.json'
Write-Bc2GraphicsAtomic $created $bytes
@{bytes=(Read-Settings);created=[Convert]::ToBase64String([IO.File]::ReadAllBytes($created));
    before=$before;after=(Get-Acl -LiteralPath $script:fixtureSettings).Sddl;
    temps=@(Get-ChildItem -LiteralPath $v.folder -Filter '*.tmp').Count}|ConvertTo-Json
""")
        expected = bytes((0, 255, 254, 13, 10, 128, 65))
        self.assertEqual(base64.b64decode(result["bytes"]), expected)
        self.assertEqual(base64.b64decode(result["created"]), expected)
        self.assertEqual(result["before"], result["after"])
        self.assertEqual(result["temps"], 0)

    @unittest.skipUnless(os.name == "nt", "Windows FileShare replacement contract")
    def test_actual_sharing_failure_retries_after_readable_target_handle_is_released(self):
        result = self.invoke(r"""
$bytes=(Convert-Bc2GraphicsMsaa ([IO.File]::ReadAllBytes($script:fixtureSettings))).bytes
$hash=Get-Bc2GraphicsHash ([IO.File]::ReadAllBytes($script:fixtureSettings))
$script:waits=0
$script:held=[IO.File]::Open($script:fixtureSettings,[IO.FileMode]::Open,[IO.FileAccess]::Read,[IO.FileShare]::Read)
function Start-Sleep([int]$Milliseconds){
    $script:waits++
    if($script:held){$script:held.Dispose();$script:held=$null}
    Microsoft.PowerShell.Utility\Start-Sleep -Milliseconds $Milliseconds
}
try {Write-Bc2GraphicsAtomic $script:fixtureSettings $bytes $hash}
finally {if($script:held){$script:held.Dispose()}}
@{bytes=(Read-Settings);waits=$script:waits;temps=@(Get-ChildItem -LiteralPath $v.folder -Filter '*.tmp').Count}|ConvertTo-Json
""")
        self.assertGreaterEqual(result["waits"], 1)
        self.assertEqual(base64.b64decode(result["bytes"]), lease_fixture.PREPARED)
        self.assertEqual(result["temps"], 0)

    def test_specific_transient_errors_retry_same_intact_files_then_replace(self):
        for hresult in (-2147023721, -2147024864, -2147024863):
            with self.subTest(hresult=hresult):
                result = self.invoke(INJECT_REPLACE + r"""
$script:replaceAction={param($Temporary,$Target)
    if($script:replaceCalls -eq 1){throw [IO.IOException]::new('fixture transient',HRESULT)}
    [IO.File]::Replace($Temporary,$Target,[NullString]::Value)
}
Invoke-FixtureWrite|ConvertTo-Json -Depth 8
""".replace("HRESULT", str(hresult)))
                self.assertFalse(result["rejected"], result)
                self.assertGreaterEqual(result["calls"], 2)
                self.assertEqual(result["waits"], result["calls"] - 1)
                self.assertEqual(base64.b64decode(result["bytes"]), lease_fixture.PREPARED)
                self.assertEqual(result["temps"], 0)
                for key in ("temporary", "original", "prepared"):
                    self.assertEqual(len({item[key] for item in result["observations"]}), 1)

    def test_repeated_eligible_failure_exhausts_one_deadline_with_original_untouched(self):
        result = self.invoke(INJECT_REPLACE + r"""
$script:replaceAction={throw [IO.IOException]::new('fixture persistent',-2147023721)}
Invoke-FixtureWrite|ConvertTo-Json -Depth 8
""")
        self.assertTrue(result["rejected"])
        self.assertGreater(result["calls"], 1)
        self.assertGreaterEqual(result["elapsed_ms"], 950)
        self.assertLess(result["elapsed_ms"], 5000)
        self.assertLessEqual(result["wait_ms"], 1000 + result["waits"])
        self.assertEqual(base64.b64decode(result["bytes"]), lease_fixture.ORIGINAL)
        self.assertEqual(result["hresult"], -2147023721)
        self.assertEqual(result["data"]["graphics_attempts"], result["calls"])
        self.assertEqual(result["data"]["graphics_original_sha256"], result["data"]["graphics_current_sha256"])
        self.assertEqual(result["data"]["graphics_prepared_sha256"], result["data"]["graphics_temporary_sha256"])

    def test_retry_deadline_cannot_expire_during_wait_then_allow_another_attempt(self):
        result = self.invoke(INJECT_REPLACE + r"""
$script:replaceAction={throw [IO.IOException]::new('fixture transient',-2147023721)}
function Start-Sleep([int]$Milliseconds){
    $script:waits++
    Microsoft.PowerShell.Utility\Start-Sleep -Milliseconds 1100
}
Invoke-FixtureWrite|ConvertTo-Json -Depth 8
""")
        self.assertTrue(result["rejected"])
        self.assertEqual(result["calls"], 1)
        self.assertEqual(result["waits"], 1)
        self.assertEqual(base64.b64decode(result["bytes"]), lease_fixture.ORIGINAL)

    def test_permissions_and_other_failures_are_never_retried(self):
        failures = (
            "[UnauthorizedAccessException]::new('fixture denied')",
            "[IO.IOException]::new('fixture access denied',-2147024891)",
            "[IO.IOException]::new('fixture cannot move replacement',-2147023720)",
            "[IO.IOException]::new('fixture missing',-2147024894)",
            "[InvalidOperationException]::new('fixture unrelated failure')",
        )
        for failure in failures:
            with self.subTest(failure=failure):
                result = self.invoke(INJECT_REPLACE + "$script:replaceAction={throw " + failure + "}\nInvoke-FixtureWrite|ConvertTo-Json -Depth 8")
                self.assertTrue(result["rejected"])
                self.assertEqual((result["calls"], result["waits"]), (1, 0))
                self.assertEqual(base64.b64decode(result["bytes"]), lease_fixture.ORIGINAL)

    def test_expected_hash_conflict_never_attempts_replacement(self):
        result = self.invoke(INJECT_REPLACE + "Invoke-FixtureWrite ('0'*64)|ConvertTo-Json -Depth 8")
        self.assertTrue(result["rejected"])
        self.assertEqual((result["calls"], result["waits"]), (0, 0))
        self.assertEqual(base64.b64decode(result["bytes"]), lease_fixture.ORIGINAL)

    def test_changed_or_consumed_files_after_failed_operation_are_never_retried(self):
        mutations = (
            ("[IO.File]::AppendAllText($Target,'; user edit')", lease_fixture.ORIGINAL + b"; user edit"),
            ("[IO.File]::AppendAllText($Temporary,'; tampered')", lease_fixture.ORIGINAL),
            ("[IO.File]::Delete($Temporary)", lease_fixture.ORIGINAL),
            # Model an operation that consumed its input and changed the target;
            # the writer must not treat a reported error as an untouched pair.
            ("[IO.File]::WriteAllBytes($Target,[IO.File]::ReadAllBytes($Temporary));[IO.File]::Delete($Temporary)", lease_fixture.PREPARED),
        )
        for mutation, expected in mutations:
            with self.subTest(mutation=mutation):
                result = self.invoke(INJECT_REPLACE + r"""
$script:replaceAction={param($Temporary,$Target)
    MUTATION
    throw [IO.IOException]::new('fixture uncertain replacement',-2147023721)
}
Invoke-FixtureWrite|ConvertTo-Json -Depth 8
""".replace("MUTATION", mutation))
                self.assertTrue(result["rejected"])
                self.assertEqual((result["calls"], result["waits"]), (1, 0))
                self.assertEqual(base64.b64decode(result["bytes"]), expected)
                self.assertIn("graphics_retry_refusal", result["data"])

    def test_target_and_temp_changes_during_retry_wait_refuse_second_attempt(self):
        for expected_hash in ("$originalHash", "''"):
            for path, expected in (("$script:fixtureSettings", lease_fixture.ORIGINAL + b"; racing edit"),
                                   ("$script:lastTemporary", lease_fixture.ORIGINAL)):
                with self.subTest(expected_hash=expected_hash, path=path):
                    result = self.invoke(INJECT_REPLACE + r"""
$script:replaceAction={throw [IO.IOException]::new('fixture transient',-2147023721)}
function Start-Sleep([int]$Milliseconds){
    $script:waits++
    [IO.File]::AppendAllText(EDIT_PATH,'; racing edit')
}
Invoke-FixtureWrite EXPECTED_HASH|ConvertTo-Json -Depth 8
""".replace("EDIT_PATH", path).replace("EXPECTED_HASH", expected_hash))
                    self.assertTrue(result["rejected"])
                    self.assertEqual((result["calls"], result["waits"]), (1, 1))
                    self.assertEqual(base64.b64decode(result["bytes"]), expected)

    def test_link_guard_is_rechecked_for_both_paths_after_retry_wait(self):
        for guarded_path in ("$script:fixtureSettings", "$script:lastTemporary"):
            with self.subTest(path=guarded_path):
                result = self.invoke(INJECT_REPLACE + r"""
$script:noLinks=(Get-Item Function:Assert-FvrNoLinks).ScriptBlock
function Assert-FvrNoLinks([string]$Path){
    if($script:waits -gt 0 -and $Path -ceq GUARDED_PATH){throw 'fixture changed link guard'}
    & $script:noLinks $Path
}
$script:replaceAction={throw [IO.IOException]::new('fixture transient',-2147023721)}
function Start-Sleep([int]$Milliseconds){$script:waits++}
Invoke-FixtureWrite|ConvertTo-Json -Depth 8
""".replace("GUARDED_PATH", guarded_path))
                self.assertTrue(result["rejected"])
                self.assertEqual((result["calls"], result["waits"]), (1, 1))
                self.assertEqual(base64.b64decode(result["bytes"]), lease_fixture.ORIGINAL)

    def test_unreadable_target_after_failure_does_not_authorize_retry(self):
        result = self.invoke(INJECT_REPLACE + r"""
$script:held=$null
$script:replaceAction={param($Temporary,$Target)
    $script:held=[IO.File]::Open($Target,[IO.FileMode]::Open,[IO.FileAccess]::Read,[IO.FileShare]::None)
    throw [IO.IOException]::new('fixture transient with unavailable verification',-2147023721)
}
$failure=$null
try {Write-Bc2GraphicsAtomic $script:fixtureSettings $preparedBytes $originalHash}
catch {$failure=$_}
finally {if($script:held){$script:held.Dispose()}}
Get-FixtureResult $failure|ConvertTo-Json -Depth 8
""")
        self.assertTrue(result["rejected"])
        self.assertEqual((result["calls"], result["waits"]), (1, 0))
        self.assertEqual(result["data"]["graphics_current_sha256"], "unavailable")
        self.assertEqual(base64.b64decode(result["bytes"]), lease_fixture.ORIGINAL)

    def test_failed_restore_keeps_backup_journal_and_concurrent_user_edit(self):
        result = self.invoke("$null=Prepare\n" + INJECT_REPLACE + r"""
$script:replaceAction={param($Temporary,$Target)
    if($Target -ceq $script:fixtureSettings){
        [IO.File]::AppendAllText($Target,'; user edit')
        throw [IO.IOException]::new('fixture restore conflict',-2147023721)
    }
    [IO.File]::Replace($Temporary,$Target,[NullString]::Value)
}
$restored=Restore-Bc2GraphicsSettingsLease $stateRoot;$journal=Read-Journal
@{status=$restored.status;state=$journal.state;bytes=(Read-Settings);
    backup=[Convert]::ToBase64String([IO.File]::ReadAllBytes((Join-Path $stateRoot ('graphics-settings/'+$journal.backup))));
    settings_attempts=@($script:observations|Where-Object {$_.target -ceq $script:fixtureSettings}).Count}|ConvertTo-Json
""")
        self.assertEqual(result["status"], "pending")
        self.assertEqual(result["state"], "restoring")
        self.assertEqual(result["settings_attempts"], 1)
        self.assertEqual(base64.b64decode(result["backup"]), lease_fixture.ORIGINAL)
        self.assertEqual(base64.b64decode(result["bytes"]), lease_fixture.PREPARED + b"; user edit")


if __name__ == "__main__":
    unittest.main()
