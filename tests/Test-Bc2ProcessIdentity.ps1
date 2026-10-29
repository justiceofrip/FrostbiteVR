#requires -Version 7.5
param([string]$PayloadRoot)
$ErrorActionPreference='Stop'
$repo=Split-Path $PSScriptRoot -Parent
$app=if($PayloadRoot){Join-Path $PayloadRoot 'app'}else{Join-Path $repo 'launcher'}
$runtime=if($PayloadRoot){Join-Path $PayloadRoot 'runtime'}else{Join-Path $repo 'launcher/runtime'}
. (Join-Path $app 'Launcher.Core.ps1')
. (Join-Path $runtime 'Runtime.Core.ps1')
. (Join-Path $repo 'tools/StorageBudget.ps1')
$root=Join-Path $repo ('test-temp/process-identity-'+[guid]::NewGuid().ToString('N'))
$saved=@{};foreach($key in @('TEMP','TMP','TMPDIR','PYTHONDONTWRITEBYTECODE')){$saved[$key]=[Environment]::GetEnvironmentVariable($key,'Process')}
$script:checks=0;$child=$null;$shortChild=$null;$self=$null;$passed=$false;$lookupMock=$false
function Check([bool]$Good,[string]$Name){if(-not $Good){throw "FAILED: $Name"};$script:checks++;Write-Host "PASS $Name"}
function Reject([scriptblock]$Work,[string]$Message,[string]$Name){$text='';try{& $Work|Out-Null}catch{$text=$_.Exception.Message};Check ($text -like ('*'+$Message+'*')) $Name}
try {
    $null=Set-FvrProjectTemporaryStorage -RepositoryRoot $repo
    $budget=Assert-FvrStorageBudget -RepositoryRoot $repo -OutputPath $root -Operation Session -PlannedBytes 8MB -RawCaptureBytes 0 -AdditionalRoots @('G:\Unity\VRMMO\BC2VR-Hotfix018')
    if($null -eq $budget.generated.allocated_bytes){throw 'Complete storage admission required'}
    $null=Set-FvrLauncherStorage $root
    # Initialize the native helper on the real parent before the short-lived
    # child starts. No mocked process metadata is used for this 64-bit path.
    $self=Microsoft.PowerShell.Management\Get-Process -Id $PID
    $selfIdentity=Get-Bc2ProcessIdentity $self
    Check ($selfIdentity.pid -eq $PID -and [IO.Path]::IsPathFullyQualified($selfIdentity.path)) 'launcher/host-sized process retains an actual full image path'
    if(-not [Environment]::Is64BitOperatingSystem){throw 'This regression requires 64-bit Windows and its installed 32-bit PowerShell'}
    $childExe=Join-Path $env:WINDIR 'SysWOW64/WindowsPowerShell/v1.0/powershell.exe'
    if(-not(Test-Path -LiteralPath $childExe -PathType Leaf)){throw 'Installed 32-bit Windows PowerShell is unavailable'}
    $ready=Join-Path $root 'child-ready.json';$stop=Join-Path $root 'child-stop.request'
    $childText=@'
$ErrorActionPreference='Stop'
$ready='__READY__';$stop='__STOP__'
$deadline=[DateTime]::UtcNow.AddSeconds(30)
$process=[Diagnostics.Process]::GetCurrentProcess()
try {
    $record=@{pid=$PID;pointer_bits=([IntPtr]::Size*8);path=$process.MainModule.FileName;creation_filetime=$process.StartTime.ToUniversalTime().ToFileTimeUtc().ToString()}
    [IO.File]::WriteAllText($ready,($record|ConvertTo-Json -Compress))
    while([DateTime]::UtcNow -lt $deadline -and -not [IO.File]::Exists($stop)){[Threading.Thread]::Sleep(50)}
} finally {$process.Dispose()}
'@
    $childText=$childText.Replace('__READY__',$ready.Replace("'","''")).Replace('__STOP__',$stop.Replace("'","''"))
    $encoded=[Convert]::ToBase64String([Text.Encoding]::Unicode.GetBytes($childText))
    # Existing OS executable is read-only. All output/scratch is on G:. This
    # process has no profile, window or desktop input and exits by itself <=30s.
    $child=Start-Process -FilePath $childExe -ArgumentList @('-NoProfile','-NonInteractive','-EncodedCommand',$encoded) -WorkingDirectory $root -WindowStyle Hidden -PassThru -RedirectStandardOutput (Join-Path $root 'child.stdout.log') -RedirectStandardError (Join-Path $root 'child.stderr.log')
    $handle=$child.Handle;$deadline=[DateTime]::UtcNow.AddSeconds(10)
    while(-not(Test-Path -LiteralPath $ready) -and -not $child.HasExited -and [DateTime]::UtcNow -lt $deadline){Start-Sleep -Milliseconds 25}
    if(-not(Test-Path -LiteralPath $ready) -or $child.HasExited){throw 'Bounded 32-bit identity fixture did not become ready'}
    $childRecord=Read-FvrJson $ready
    Check ($childRecord.pointer_bits -eq 32 -and $childRecord.pid -eq $child.Id) 'regression uses an actual hidden 32-bit child, not a simulated process'
    $actual=Get-Bc2ProcessIdentity $child
    Check ([string]::Equals($actual.path,$childRecord.path,[StringComparison]::OrdinalIgnoreCase) -and
        [DateTimeOffset]::Parse($actual.start_utc).UtcDateTime.ToFileTimeUtc().ToString() -ceq $childRecord.creation_filetime) 'ordinary process capture agrees with child image and creation time'
    $wrapper=[pscustomobject]@{Id=$child.Id;Handle=$handle;StartTime=$child.StartTime;Path=$null}
    $recovered=Get-Bc2ProcessIdentity $wrapper
    Check ($recovered.pid -eq $actual.pid -and $recovered.start_utc -ceq $actual.start_utc -and
        [string]::Equals($recovered.path,$actual.path,[StringComparison]::OrdinalIgnoreCase)) 'null Process.Path reproducer resolves the same retained live image through Win32'
    $wrapper.Path='';$empty=Get-Bc2ProcessIdentity $wrapper;$wrapper.Path=$null
    Check ([string]::Equals($empty.path,$actual.path,[StringComparison]::OrdinalIgnoreCase)) 'empty Process.Path uses the same bounded Win32 fallback'
    # Preserve the real child's handle/PID/birth while reproducing the observed
    # startup Path value. A nonempty loader-module path is not its executable.
    $misleadingPath=Join-Path $env:WINDIR 'System32/ntdll.dll'
    $misleading=$wrapper.PSObject.Copy();$misleading.Path=$misleadingPath
    $captured=Get-Bc2ProcessIdentity $misleading
    Check ($captured.pid -eq $actual.pid -and $captured.start_utc -ceq $actual.start_utc -and
        [string]::Equals($captured.path,$childRecord.path,[StringComparison]::OrdinalIgnoreCase)) 'nonempty loader Path cannot poison ordinary retained-process identity'
    $live=Get-Bc2ProcessIdentity $misleading -RequireLive $true
    Check ($live.pid -eq $actual.pid -and $live.start_utc -ceq $actual.start_utc -and
        [string]::Equals($live.path,$childRecord.path,[StringComparison]::OrdinalIgnoreCase)) 'required-live identity queries the actual executable despite nonempty loader Path'
    $found=Find-Bc2OwnedProcess $recovered
    Check ($null -ne $found -and $found.Id -eq $child.Id) 'fresh Get-Process lookup matches the recovered actual identity'
    if($found){$found.Dispose()}
    $wrong=$recovered.Clone();$wrong.start_utc=[DateTimeOffset]::Parse($wrong.start_utc).AddSeconds(1).ToString('o')
    Check ($null -eq (Find-Bc2OwnedProcess $wrong)) 'different creation time rejects PID reuse'
    $wrong=$recovered.Clone();$wrong.path=Join-Path $root 'wrong-executable.exe'
    Check ($null -eq (Find-Bc2OwnedProcess $wrong)) 'actual path mismatch remains a rejection'
    $wrong=$recovered.Clone();$wrong.pid=$PID
    Check ($null -eq (Find-Bc2OwnedProcess $wrong)) 'different live PID cannot satisfy child identity'
    $wrong=$recovered.Clone();$wrong.path=$null
    Check ($null -eq (Find-Bc2OwnedProcess $wrong)) 'missing captured path is never accepted by null-equals-null'
    $different=$wrapper.PSObject.Copy();$different.Id=$PID
    Reject {Get-Bc2ProcessIdentity $different} 'PID differs' 'retained handle must belong to the captured PID'
    $different=$wrapper.PSObject.Copy();$different.StartTime=$child.StartTime.AddSeconds(1)
    Reject {Get-Bc2ProcessIdentity $different} 'creation time differs' 'retained handle must agree with captured creation time'
    foreach($bad in @([IntPtr]::Zero,[IntPtr]::new(-1),[IntPtr]::new(1))){
        $different=$wrapper.PSObject.Copy();$different.Handle=$bad
        Reject {Get-Bc2ProcessIdentity $different} 'retained process' 'unavailable or invalid process handle cannot supply a path'
    }
    $script:managedPathReads=0
    $throwing=$wrapper.PSObject.Copy();$throwing|Add-Member ScriptProperty Path {$script:managedPathReads++;throw 'fixture Path getter failed'} -Force
    # Image resolution must not depend on the managed Path getter, even when
    # PowerShell would convert its exception into a null value.
    $getterFallback=Get-Bc2ProcessIdentity $throwing
    Check ($getterFallback.pid -eq $actual.pid -and $getterFallback.start_utc -ceq $actual.start_utc -and
        [string]::Equals($getterFallback.path,$actual.path,[StringComparison]::OrdinalIgnoreCase)) 'failed managed Path getter resolves only the same retained-handle identity'
    $throwingHandle=$wrapper.PSObject.Copy();$throwingHandle|Add-Member ScriptProperty Handle {throw 'fixture Handle getter failed'} -Force
    Reject {Get-Bc2ProcessIdentity $throwingHandle} 'Cannot convert null to type "System.IntPtr"' 'failed Handle getter cannot fall back to a guessed process'
    # Exercise Find with the same real child handle but the fresh-lookup Path
    # getter forced null, matching the reported platform behavior on both calls.
    $script:identityLookupProcess=$wrapper;$script:identityLookupPid=$child.Id
    function Get-Process {
        param([string]$Name,$Id,$ErrorAction)
        if($Name -ceq 'BFBC2Game'){return $script:identityLookupProcess}
        if($Id -eq $script:identityLookupPid){return $script:identityLookupProcess}
        Microsoft.PowerShell.Management\Get-Process -Id $Id -ErrorAction $ErrorAction
    }
    $lookupMock=$true
    Check ($null -ne (Find-Bc2OwnedProcess $recovered)) 'fresh lookup with null Path reuses actual retained-handle path resolution'
    $wrong=$recovered.Clone();$wrong.path=Join-Path $root 'wrong-executable.exe'
    Check ($null -eq (Find-Bc2OwnedProcess $wrong)) 'null fresh Path never borrows the requested identity path'
    Check ([object]::ReferenceEquals((Get-Bc2ExistingGame $actual.path $true),$wrapper)) 'existing-game restart admission resolves null Path from the same retained live handle'
    Reject {Get-Bc2ExistingGame (Join-Path $root 'wrong-executable.exe') $true} 'Another BC2 installation' 'existing-game fallback still rejects a different installation'
    Reject {Get-Bc2ExistingGame $actual.path $false} 'BC2 is still open' 'null-Path fallback never bypasses the explicit restart option'
    $script:identityLookupProcess=$misleading
    Check ([object]::ReferenceEquals((Find-Bc2OwnedProcess $captured),$misleading)) 'fresh lookup with loader Path accepts the correctly captured executable identity'
    $poisoned=$captured.Clone();$poisoned.path=$misleadingPath
    Check ($null -eq (Find-Bc2OwnedProcess $poisoned)) 'matching saved and fresh loader Paths cannot authorize the wrong executable identity'
    $script:identityLookupProcess=$throwing
    $getterLookup=Find-Bc2OwnedProcess $recovered
    Check ($null -ne $getterLookup -and $getterLookup.Id -eq $actual.pid -and $getterLookup.Handle -eq $handle) 'fresh lookup after managed Path getter failure still proves the same live kernel identity'
    Check ($script:managedPathReads -eq 0) 'capture and fresh lookup do not invoke the managed Path getter'
    Remove-Item Function:\Get-Process;$lookupMock=$false
    [IO.File]::WriteAllText($stop,'fixture complete')
    Check ($child.WaitForExit(5000) -and $child.ExitCode -eq 0) '32-bit fixture observes stop and exits cleanly without termination'
    $finished=Get-Bc2ProcessIdentity $wrapper
    Check ($finished.pid -eq $actual.pid -and $finished.start_utc -ceq $actual.start_utc -and
        ($null -eq $finished.path -or [string]::Equals($finished.path,$actual.path,[StringComparison]::OrdinalIgnoreCase))) 'retained exited process identity remains recordable even when Windows no longer exposes its image'
    $different=$wrapper.PSObject.Copy();$different.Id=$PID
    Reject {Get-Bc2ProcessIdentity $different} 'PID differs' 'exited diagnostic fallback still verifies the retained PID'
    $different=$wrapper.PSObject.Copy();$different.StartTime=$wrapper.StartTime.AddSeconds(1)
    Reject {Get-Bc2ProcessIdentity $different} 'creation time differs' 'exited diagnostic fallback still verifies the retained creation time'
    Reject {Get-Bc2ProcessIdentity $wrapper -RequireLive $true} 'no longer live' 'action lookup cannot use a retained exited process'
    Check ($null -eq (Find-Bc2OwnedProcess $finished)) 'diagnostic identity with an unavailable exited image grants no later action'
    Check ($null -eq (Find-Bc2OwnedProcess $recovered)) 'exited child cannot satisfy a later lookup'
    $shortCommand=[Convert]::ToBase64String([Text.Encoding]::Unicode.GetBytes('exit 20'))
    $shortChild=Start-Process -FilePath $childExe -ArgumentList @('-NoProfile','-NonInteractive','-EncodedCommand',$shortCommand) -WorkingDirectory $root -WindowStyle Hidden -PassThru -RedirectStandardOutput (Join-Path $root 'short.stdout.log') -RedirectStandardError (Join-Path $root 'short.stderr.log')
    $null=$shortChild.Handle
    $shortStart=$shortChild.StartTime.ToUniversalTime().ToString('o')
    Check ($shortChild.WaitForExit(10000) -and $shortChild.ExitCode -eq 20) 'actual short-lived 32-bit helper preserves its startup failure code'
    $shortIdentity=Get-Bc2ProcessIdentity $shortChild
    Check ($shortIdentity.pid -eq $shortChild.Id -and $shortIdentity.start_utc -ceq $shortStart -and
        ($null -eq $shortIdentity.path -or [string]::Equals($shortIdentity.path,$actual.path,[StringComparison]::OrdinalIgnoreCase)) -and
        $shortChild.ExitCode -eq 20) 'recording an already-exited helper does not replace its real failure with a liveness error'
    Check ($null -eq (Find-Bc2OwnedProcess $shortIdentity)) 'recorded exited helper is never admitted for a later action'
    $passed=$true;Write-Host "BC2 process identity checks passed: $script:checks"
} finally {
    if($lookupMock){Remove-Item Function:\Get-Process}
    if($child){
        if(-not $child.HasExited){[IO.File]::WriteAllText((Join-Path $root 'child-stop.request'),'fixture cleanup');if(-not $child.WaitForExit(35000)){throw 'Owned identity fixture did not self-exit within its bounded lifetime'}}
        $child.Dispose()
    }
    if($shortChild){if(-not $shortChild.HasExited -and -not $shortChild.WaitForExit(35000)){throw 'Short-lived identity fixture did not self-exit'};$shortChild.Dispose()}
    if($self){$self.Dispose()}
    foreach($key in $saved.Keys){[Environment]::SetEnvironmentVariable($key,$saved[$key],'Process')}
    if($passed){Remove-FvrOwnedTree (Split-Path $root -Parent) (Split-Path $root -Leaf)}else{Write-Warning ('Identity fixture evidence retained: '+$root)}
}
