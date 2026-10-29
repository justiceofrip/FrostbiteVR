#requires -Version 7.5
[CmdletBinding()]
param([Parameter(Mandatory)][ValidatePattern('^scene[0-9]{2}$')][string]$SceneName,
      [ValidatePattern('^wgc[0-9]{2}$')][string]$SampleName='wgc01',
      [ValidatePattern('^window-[a-z][a-z0-9-]{0,31}\.json$')][string]$Observation='window-before-wgc.json')
$ErrorActionPreference='Stop'
$repo='G:\Unity\VRMMO\FrostbiteVR'
$scene=Join-Path $repo ('reports/coordinator-20261028-runtime02/'+$SceneName)
$output=Join-Path $scene $SampleName
$storage=Join-Path $scene ('storage-'+$SampleName+'.json')
$stdout=Join-Path $scene ($SampleName+'-stdout.log');$stderr=Join-Path $scene ($SampleName+'-stderr.log')
$watchdog=Join-Path $scene ($SampleName+'-watchdog.json')
$lease=Get-Content -LiteralPath 'G:\Unity\VRMMO\bc2vr-task-worktrees\INTEGRATION.lock' -Raw|ConvertFrom-Json
if($lease.owner_token -cne 'campaign-coordinator-20261028-r1-runtime02-91aa4c54d34a41219b8483ccdd7629cb' -or $lease.run -cne 'coordinator-20261028-runtime02'){throw 'Integration ownership differs'}
if(Test-Path -LiteralPath (Join-Path $scene 'finish.json')){throw 'Scene already finished'}
foreach($path in @($output,$storage,$stdout,$stderr,$watchdog)){if(Test-Path -LiteralPath $path){throw 'Preserve prior WGC evidence'}}
. (Join-Path $repo 'tools/StorageBudget.ps1')
$null=Set-FvrProjectTemporaryStorage -RepositoryRoot $repo
$budget=Assert-FvrStorageBudget -RepositoryRoot $repo -OutputPath $output -Operation Capture -PlannedBytes 16MB -RawCaptureBytes 8MB -AdditionalRoots @('G:\Unity\VRMMO\BC2VR-Hotfix018','G:\Unity\VRMMO\bc2vr-task-worktrees')
[IO.File]::WriteAllText($storage,($budget|ConvertTo-Json -Depth 12),[Text.UTF8Encoding]::new($false))
$arguments=@('-B',(Join-Path $PSScriptRoot 'CaptureStockWindow.py'),'--scene-directory',$scene,'--output',$output,'--storage-receipt',$storage,'--observation',$Observation)
$child=Start-Process -FilePath 'G:\BF2142VR\tools\python-3.13.15\python.exe' -ArgumentList $arguments -WorkingDirectory $scene -WindowStyle Hidden -PassThru -RedirectStandardOutput $stdout -RedirectStandardError $stderr
$handle=$child.Handle;$timedOut=$false;$errorText=$null
try{
 if(-not $child.WaitForExit(12000)){
  $timedOut=$true;$child.Kill()
  if(-not $child.WaitForExit(2000)){throw 'Owned WGC helper remains after watchdog'}
 }
}catch{$errorText=$_.Exception.Message}
$record=@{schema='fvr.runtime05.wgc-watchdog.v1';helper_pid=$child.Id;helper_creation_filetime=[string]$child.StartTime.ToUniversalTime().ToFileTimeUtc();timed_out=$timedOut;helper_exited=$child.HasExited;exit_code=$(if($child.HasExited){$child.ExitCode}else{$null});error=$errorText;utc=[DateTime]::UtcNow.ToString('o');game_terminated=$false;input_sent=$false}
[IO.File]::WriteAllText($watchdog,($record|ConvertTo-Json -Depth 6),[Text.UTF8Encoding]::new($false))
if($errorText){throw $errorText}
if($timedOut){throw 'WGC helper exceeded12seconds; only the owned helper was terminated'}
if($child.ExitCode -ne 0){throw ('WGC capture refused; see preserved result/logs, exit '+$child.ExitCode)}
Get-Content -LiteralPath (Join-Path $output 'result.json')
