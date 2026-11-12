#requires -Version 7.5
[CmdletBinding()]
param([string]$RepositoryRoot=(Split-Path $PSScriptRoot -Parent))
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest

# Exercise the real Bind and Find bodies with inert retained objects. The
# process lookup, kernel identity query and parent lookup are the only seams;
# this test never starts a child, game, input, or launcher session.
$tokens=$null;$parseErrors=$null
$runner=[Management.Automation.Language.Parser]::ParseFile((Join-Path $RepositoryRoot 'tools/Invoke-Bc2XrGameSimulation.ps1'),[ref]$tokens,[ref]$parseErrors)
if($parseErrors.Count){throw 'Runner parse failed.'}
$runtime=[Management.Automation.Language.Parser]::ParseFile((Join-Path $RepositoryRoot 'launcher/runtime/Runtime.Core.ps1'),[ref]$tokens,[ref]$parseErrors)
if($parseErrors.Count){throw 'Runtime parse failed.'}
$namespace='XrGameShortLived'+[guid]::NewGuid().ToString('N')
$parentType=Add-Type -TypeDefinition @"
namespace $namespace {
    public static class ParentFixture {
        public static int Calls, ExpectedPid=42, Parent=7;
        public static int ParentPid(int pid) {
            Calls++;
            if(pid!=ExpectedPid)throw new System.InvalidOperationException("Wrong parent lookup PID");
            return Parent;
        }
    }
}
"@ -PassThru
foreach($name in 'Require','SameIdentity','Bind','Find-Bc2OwnedProcess'){
    $source=if($name -ceq 'Find-Bc2OwnedProcess'){$runtime}else{$runner}
    $fn=$source.Find({param($n) $n -is [Management.Automation.Language.FunctionDefinitionAst] -and $n.Name -ceq $name}.GetNewClosure(),$true)
    if(-not $fn){throw "Missing production function $name"}
    . ([scriptblock]::Create($fn.Extent.Text.Replace('[FvrStorage.NativeSize]',('['+$parentType.FullName+']'))))
}
$script:checks=0
function Check([bool]$Condition,[string]$Name){
    if(-not $Condition){throw "FAILED: $Name"}
    $script:checks++
}
function Get-Process($Id,$ErrorAction){
    $script:f.lookups++
    Check ($Id -eq $script:f.identity.pid) 'Lookup only the supplied exact PID'
    return $script:f.process
}
function Get-Bc2ProcessIdentity($Process,[bool]$RequireLive){
    $script:f.identityReads++
    Check ([object]::ReferenceEquals($Process,$script:f.process) -and $Process.Handle -eq [IntPtr]123) 'Same retained process and handle'
    Check $RequireLive 'Every identity proof requires liveness'
    if($script:f.identityReads -eq 1){return $script:f.firstIdentity}
    if($script:f.failure){
        $Process.Exited=$script:f.exitOnFailure
        throw $script:f.failure
    }
    return $script:f.secondIdentity
}
function Save([string]$Name,$Value){$script:f.saved[$Name]=$Value}
function NewFailure([string]$Kind){
    switch($Kind){
        query {return [ComponentModel.Win32Exception]::new(5,'Cannot query retained process image path')}
        exited {return [InvalidOperationException]::new('Retained process is no longer live')}
        other_win32 {return [ComponentModel.Win32Exception]::new(6,'Cannot check retained process lifetime')}
        wrong_class {return [InvalidOperationException]::new('Cannot query retained process image path')}
        wrong_pid {return [InvalidOperationException]::new('Retained process PID differs')}
        wrong_birth {return [InvalidOperationException]::new('Retained process creation time differs')}
        wrong_handle {return [InvalidOperationException]::new('Retained process handle is unavailable')}
        none {return $null}
        default {throw 'Unknown fixture exception'}
    }
}
function RunCase([string]$Case,[string]$Name,[string]$Failure,[bool]$Exited,[string]$Outcome,[scriptblock]$Change={}){
    $identity=@{pid=42;start_utc='2026-11-12T20:25:51.0005529Z';path='G:\fixture\BC2NativeResolution.exe'}
    $process=[pscustomobject]@{Id=42;Handle=[IntPtr]123;Exited=$false;ExitReads=0;DisposeCalls=0;ThrowOnExitRead=$false}
    $process|Add-Member ScriptProperty HasExited {
        $this.ExitReads++
        if($this.ThrowOnExitRead){throw 'Cannot prove retained process exit'}
        return $this.Exited
    }
    $process|Add-Member ScriptMethod Dispose {$this.DisposeCalls++}
    $script:f=@{identity=$identity;firstIdentity=$identity.Clone();secondIdentity=$identity.Clone();process=$process;lookups=0;identityReads=0;failure=(NewFailure $Failure);exitOnFailure=$Exited;saved=@{}}
    $parentType::Calls=0;$parentType::Parent=7
    $owned=@{};$launchUtc=[DateTime]::Parse('2026-11-12T20:25:44.0000000Z').ToUniversalTime()
    $expectedPath=$identity.path
    & $Change $script:f
    $caught=$null
    try{Bind $Name $identity $expectedPath 7}catch{$caught=$_.Exception}
    $bound=$owned.ContainsKey($Name)
    switch($Outcome){
        skipped {
            Check ($null -eq $caught -and -not $bound -and $process.DisposeCalls -eq 1) "$Case skips and disposes the proven exited handle"
            Check ($script:f.lookups -eq 1 -and $script:f.identityReads -eq 2 -and $parentType::Calls -eq 1 -and $process.ExitReads -eq 1) "$Case preserves prior identity and parent proof without reopening"
            if($Failure -ceq 'query'){
                $saved=$script:f.saved[$Name+'-binding-exit.json']
                Check ($saved.query_native_error_code -eq 5 -and $saved.exact_handle_exit_observed -and -not $saved.live_authority_granted) "$Case records the unknown query fault and proven exit separately"
                SameIdentity $saved.identity $identity
            }
            Check (-not $script:f.saved.ContainsKey($Name+'-identity.json')) "$Case grants no live ownership"
        }
        bound {
            Check ($null -eq $caught -and $bound -and $process.DisposeCalls -eq 0) "$Case retains a matching live process"
            Check ([object]::ReferenceEquals($owned[$Name].process,$process) -and $script:f.lookups -eq 1 -and $script:f.identityReads -eq 2) "$Case retains only the queried exact handle"
        }
        refused {
            Check ($null -ne $caught -and -not $bound -and $script:f.saved.Count -eq 0) "$Case retains the identity failure"
            if($script:f.lookups -eq 1){Check ($process.DisposeCalls -eq 1) "$Case disposes the failed binding"}
            if($Failure -ceq 'query' -and -not $Exited){
                $cause=$caught;while($cause.InnerException){$cause=$cause.InnerException}
                Check ($cause -is [ComponentModel.Win32Exception] -and $cause.NativeErrorCode -eq 5) "$Case preserves the original live query error"
            }
        }
        not_found {
            Check ($null -eq $caught -and -not $bound -and $script:f.saved.Count -eq 0) "$Case cannot supply ownership"
            Check ($script:f.lookups -eq 1 -and $script:f.identityReads -eq 1 -and $parentType::Calls -eq 0 -and $process.ExitReads -eq 0) "$Case cannot reach the exit exception path"
        }
        default {throw 'Unknown fixture outcome'}
    }
    Check ($script:f.lookups -le 1) "$Case never reopens a PID"
    Write-Output ('PASS '+$Case)
}
foreach($name in 'resolution_helper','resolution_release_helper'){
    RunCase ($name+' query then proven exit') $name query $true skipped
    RunCase ($name+' known exit') $name exited $true skipped
    RunCase ($name+' live query failure') $name query $false refused
}
RunCase 'matching live helper' resolution_helper none $false bound
foreach($name in 'game','host','trace','supervisor','setup_helper'){
    RunCase ($name+' cannot use helper exception') $name query $true refused
}
foreach($failure in 'other_win32','wrong_class','wrong_pid','wrong_birth','wrong_handle'){
    RunCase ('unrelated '+$failure) resolution_helper $failure $true refused
}
RunCase 'unprovable exit' resolution_helper query $true refused {param($f) $f.process.ThrowOnExitRead=$true}
RunCase 'wrong parent even if exited' resolution_helper query $true refused {$parentType::Parent=8}
RunCase 'unexpected session path' resolution_helper query $true refused {param($f) $f.identity.path='G:\other\BC2NativeResolution.exe'}
RunCase 'session predates launch' resolution_helper query $true refused {param($f) $f.identity.start_utc='2026-11-11T20:25:51.0005529Z'}
RunCase 'Find rejects different PID' resolution_helper query $true not_found {param($f) $f.firstIdentity.pid=43}
RunCase 'Find rejects reused PID birth' resolution_helper query $true not_found {param($f) $f.firstIdentity.start_utc='2026-11-12T20:25:52.0005529Z'}
RunCase 'Find rejects different image' resolution_helper query $true not_found {param($f) $f.firstIdentity.path='G:\other\helper.exe'}
RunCase 'recheck rejects changed PID' resolution_helper none $false refused {param($f) $f.secondIdentity.pid=43}
RunCase 'recheck rejects changed birth' resolution_helper none $false refused {param($f) $f.secondIdentity.start_utc='2026-11-12T20:25:52.0005529Z'}
RunCase 'recheck rejects changed image' resolution_helper none $false refused {param($f) $f.secondIdentity.path='G:\other\helper.exe'}
Write-Output ('Short-lived process binding checks passed: '+$script:checks)
