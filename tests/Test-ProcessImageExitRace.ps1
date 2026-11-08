#requires -Version 7.5
[CmdletBinding()]
param([Parameter(Mandatory)][string]$RepositoryRoot)
$ErrorActionPreference='Stop'
Set-StrictMode -Version 3

# No child/game/input/session operations. Only the kernel seams are
# substituted; the actual Resolve and RequireLive bodies come from its AST.
$path=Join-Path $RepositoryRoot 'launcher/runtime/Runtime.Core.ps1'
$tokens=$null;$parseErrors=$null
$ast=[Management.Automation.Language.Parser]::ParseFile($path,[ref]$tokens,[ref]$parseErrors)
if($parseErrors.Count){throw 'Runtime.Core.ps1 parse failed.'}
$strings=@($ast.FindAll({param($node)
    $node -is [Management.Automation.Language.StringConstantExpressionAst] -and
    $node.Value.Contains('public static class ProcessImageIdentity')
},$true))
if($strings.Count -ne 1){throw 'Expected one native process identity definition.'}
$code=$strings[0].Value
$begin=$code.IndexOf('        [DllImport(')
$end=$code.IndexOf('        static void RequireLive(')
if($begin -lt 0 -or $end -le $begin){throw 'Native seam boundaries changed.'}
$seams=@'
        public static uint FixturePid=42;
        public static long FixtureCreation=123456789;
        public static uint[] FixtureWaitStates=new uint[]{258,258};
        public static int FixtureWaitCalls;
        public static int FixtureImageCalls;
        public static bool FixtureImageSucceeds;
        public static int FixtureImageError=5;
        public static string FixturePath=@"G:\fixture\helper.exe";
        public static uint FixtureLength=21;
        public static void Reset() {
            FixturePid=42; FixtureCreation=123456789;
            FixtureWaitStates=new uint[]{258,258}; FixtureWaitCalls=0;
            FixtureImageCalls=0; FixtureImageSucceeds=false;
            FixtureImageError=5; FixturePath=@"G:\fixture\helper.exe";
            FixtureLength=(uint)FixturePath.Length;
        }
        static uint GetProcessId(IntPtr handle) { return FixturePid; }
        static bool GetProcessTimes(IntPtr handle,out long creation,out long exit,out long kernel,out long user) {
            creation=FixtureCreation; exit=0; kernel=0; user=0; return true;
        }
        static uint WaitForSingleObject(IntPtr handle,uint milliseconds) {
            int index=Math.Min(FixtureWaitCalls++,FixtureWaitStates.Length-1);
            // Deliberately disturb last-error to check it was captured before
            // the liveness recheck rather than substituted for the query error.
            Marshal.SetLastPInvokeError(1234);
            return FixtureWaitStates[index];
        }
        static bool QueryFullProcessImageName(IntPtr handle,uint flags,StringBuilder path,ref uint length) {
            FixtureImageCalls++;
            Marshal.SetLastPInvokeError(FixtureImageError);
            if(!FixtureImageSucceeds)return false;
            path.Append(FixturePath);length=FixtureLength;return true;
        }
'@
$code=$code.Substring(0,$begin)+$seams+"`n"+$code.Substring($end)
$namespace='Bc2VrIdentityRace'+[guid]::NewGuid().ToString('N')
$code=$code.Replace('namespace Bc2Vr {',('namespace '+$namespace+' {'))
$fixture=@(Add-Type -TypeDefinition $code -PassThru|Where-Object Name -CEQ ProcessImageIdentity)[0]
$script:checks=0
function Check([bool]$Condition,[string]$Name){
    if(-not $Condition){throw ('FAILED: '+$Name)}
    $script:checks++;Write-Output ('PASS '+$Name)
}
function Failure([scriptblock]$Work){
    try{& $Work|Out-Null}catch{
        $cause=$_.Exception
        while($cause.InnerException){$cause=$cause.InnerException}
        return $cause
    }
    throw 'Expected identity refusal did not occur.'
}
$handle=[IntPtr]::new(123)

$fixture::Reset()
$errorResult=Failure {$fixture::Resolve($handle,42,123456789,$true)}
Check ($errorResult -is [ComponentModel.Win32Exception] -and $errorResult.NativeErrorCode -eq 5 -and
    $errorResult.Message -ceq 'Cannot query retained process image path') 'live query failure retains original Win32 code and message'
Check ($fixture::FixtureWaitCalls -eq 2 -and $fixture::FixtureImageCalls -eq 1) 'failed image query rechecks the same retained lifetime once'

$fixture::Reset();$fixture::FixtureWaitStates=[uint[]]@(258,0)
$errorResult=Failure {$fixture::Resolve($handle,42,123456789,$true)}
Check ($errorResult -is [InvalidOperationException] -and $errorResult.Message -ceq 'Retained process is no longer live') 'exit between initial live check and failed image query reaches existing expected-exit outcome'
Check ($fixture::FixtureWaitCalls -eq 2 -and $fixture::FixtureImageCalls -eq 1) 'exit race returns no image identity and performs no retry or PID lookup'

$fixture::Reset();$fixture::FixtureWaitStates=[uint[]]@(258,[uint32]::MaxValue)
$errorResult=Failure {$fixture::Resolve($handle,42,123456789,$true)}
Check ($errorResult -is [ComponentModel.Win32Exception] -and $errorResult.NativeErrorCode -eq 1234 -and
    $errorResult.Message -ceq 'Cannot check retained process lifetime') 'failed lifetime query remains fatal and is not normalized as an exit'

$fixture::Reset();$fixture::FixturePid=43
$errorResult=Failure {$fixture::Resolve($handle,42,123456789,$true)}
Check ($errorResult.Message -ceq 'Retained process PID differs' -and $fixture::FixtureImageCalls -eq 0) 'PID mismatch remains fatal before image fallback'

$fixture::Reset();$fixture::FixtureCreation=123456790
$errorResult=Failure {$fixture::Resolve($handle,42,123456789,$true)}
Check ($errorResult.Message -ceq 'Retained process creation time differs' -and $fixture::FixtureImageCalls -eq 0) 'creation mismatch remains fatal before image fallback'

$fixture::Reset()
$errorResult=Failure {$fixture::Resolve([IntPtr]::Zero,42,123456789,$true)}
Check ($errorResult.Message -ceq 'Retained process handle is unavailable' -and $fixture::FixtureImageCalls -eq 0) 'missing handle cannot enter the exit fallback'

$fixture::Reset();$fixture::FixtureWaitStates=[uint[]]@(0)
Check ($null -eq $fixture::Resolve($handle,42,123456789,$false)) 'diagnostic-only exited identity still permits no path'

$fixture::Reset()
$errorResult=Failure {$fixture::Resolve($handle,42,123456789,$false)}
Check ($errorResult -is [ComponentModel.Win32Exception] -and $errorResult.NativeErrorCode -eq 5) 'diagnostic live query failure remains fatal'

$fixture::Reset();$fixture::FixtureImageSucceeds=$true
Check ($fixture::Resolve($handle,42,123456789,$true) -ceq $fixture::FixturePath) 'matching live identity keeps the queried image path'

$fixture::Reset();$fixture::FixtureImageSucceeds=$true;$fixture::FixtureWaitStates=[uint[]]@(258,0)
$errorResult=Failure {$fixture::Resolve($handle,42,123456789,$true)}
Check ($errorResult.Message -ceq 'Retained process is no longer live') 'exit after successful image query still denies live authority'

$fixture::Reset();$fixture::FixtureImageSucceeds=$true;$fixture::FixturePath='relative.exe';$fixture::FixtureLength=12
$errorResult=Failure {$fixture::Resolve($handle,42,123456789,$true)}
Check ($errorResult.Message -ceq 'Retained process image path is unavailable or invalid') 'invalid image path is not normalized as expected helper exit'

Write-Output ('Process identity exit-race checks passed: '+$script:checks)
