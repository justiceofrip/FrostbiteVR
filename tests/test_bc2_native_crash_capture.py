"""Pure PowerShell identity/exception checks; never attach or start a game."""
import base64
import json
from pathlib import Path
import shutil
import subprocess
import unittest


SCRIPT = Path(__file__).resolve().parents[1] / "tools" / "Capture-Bc2NativeCrash.ps1"
PWSH = shutil.which("pwsh")


@unittest.skipUnless(PWSH, "PowerShell 7 required")
class CaptureSafetyTests(unittest.TestCase):
    def invoke(self, body, payload):
        data = base64.b64encode(json.dumps(payload).encode()).decode()
        path = str(SCRIPT).replace("'", "''")
        # Extract only the four pure functions; do not evaluate script entry.
        code = f"""
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
$tokens=$null;$parseErrors=$null
$ast=[Management.Automation.Language.Parser]::ParseFile('{path}',[ref]$tokens,[ref]$parseErrors)
if($parseErrors.Count){{throw ($parseErrors|Out-String)}}
$names=@('Assert-Capture','Assert-CaptureIdentity','Convert-CaptureException')
foreach($fn in $ast.FindAll({{param($n) $n -is [Management.Automation.Language.FunctionDefinitionAst]}},$false)){{
    if($fn.Name -in $names){{Invoke-Expression $fn.Extent.Text}}
}}
$v=[Text.Encoding]::UTF8.GetString([Convert]::FromBase64String('{data}'))|ConvertFrom-Json -AsHashtable -DateKind String
{body}
"""
        encoded = base64.b64encode(code.encode("utf-16le")).decode()
        result = subprocess.run(
            [PWSH, "-NoLogo", "-NoProfile", "-NonInteractive", "-EncodedCommand", encoded],
            capture_output=True, text=True, timeout=15,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        return json.loads(result.stdout)

    def identity(self, changes, expected_success):
        expected = {"pid": 1234, "start_utc": "2026-10-31T18:48:52.2407864Z",
                    "path": r"G:\Games\BC2\BFBC2Game.exe"}
        actual = dict(expected, **changes)
        result = self.invoke(
            "try {Assert-CaptureIdentity $v.actual $v.expected; $true|ConvertTo-Json} catch {$false|ConvertTo-Json}",
            {"actual": actual, "expected": expected},
        )
        self.assertIs(result, expected_success)

    def test_same_process_accepts_case_insensitive_windows_path(self):
        self.identity({"path": r"g:\games\bc2\bfbc2game.exe"}, True)

    def test_recycled_pid_rejects_one_tick_creation_difference(self):
        self.identity({"start_utc": "2026-10-31T18:48:52.2407865Z"}, False)

    def test_other_installation_with_same_name_is_rejected(self):
        self.identity({"path": r"G:\Other\BFBC2Game.exe"}, False)

    def test_other_pid_with_same_creation_and_path_is_rejected(self):
        self.identity({"pid": 1235}, False)

    @staticmethod
    def record():
        return {"pid": 1234, "thread": 5678, "first_chance": 1, "code": 0xC0000005,
                "address": 0, "parameters": [8, 0], "context_valid": 1,
                "registers": {"esp": 8192}, "memory": [{"address": 8192, "hex": "136b0053"}],
                "modules": [{"name": "driver.dll", "base": 0x53000000, "size": 4096}],
                "modules_truncated": 0, "json_only": True, "minidump_written": False}

    def test_null_execution_fault_retains_raw_stack_without_attributing_module(self):
        result = self.invoke("Convert-CaptureException $v 1234|ConvertTo-Json -Depth 8", self.record())
        self.assertEqual(result["address"], "0x00000000")
        self.assertEqual(result["code"], "0xc0000005")
        self.assertEqual(result["parameters"], [8, 0])
        self.assertTrue(result["raw_stack_present"])
        self.assertFalse(result["unwound_stack"])
        self.assertIsNone(result["module"])

    def test_exception_for_another_process_is_rejected(self):
        result = self.invoke(
            "try {Convert-CaptureException $v 9999|Out-Null; $false|ConvertTo-Json} catch {$true|ConvertTo-Json}",
            self.record(),
        )
        self.assertTrue(result)

    def test_module_end_address_is_not_attributed_to_preceding_module(self):
        record = self.record()
        record["address"] = 0x53001000
        record["first_chance"] = 0
        result = self.invoke("Convert-CaptureException $v 1234|ConvertTo-Json -Depth 8", record)
        self.assertIsNone(result["module"])
        self.assertEqual(result["first_chance"], 0)


if __name__ == "__main__":
    unittest.main()
