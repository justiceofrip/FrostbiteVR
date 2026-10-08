[CmdletBinding()]
param([ValidateRange(1,600)][int]$WaitSeconds=120,[ValidateRange(1,900)][int]$MonitorSeconds=180,[string]$GamePath='')
$ErrorActionPreference='Stop'
if(-not $GamePath){$GamePath=(Get-Content -LiteralPath (Join-Path $PSScriptRoot 'config\local.json') -Raw | ConvertFrom-Json).game_path}
if(-not $GamePath -or -not [IO.Path]::IsPathRooted($GamePath)){throw 'Specify an absolute game path.'}
$expected=[IO.Path]::GetFullPath((Join-Path $GamePath 'BFBC2Game.exe'))
$stamp=[DateTime]::UtcNow.ToString('yyyyMMdd-HHmmss-fff')
$folder=Join-Path $PSScriptRoot "reports\diagnostics\$stamp"
New-Item -ItemType Directory -Path $folder -Force | Out-Null
$crashPath=Join-Path ([Environment]::GetFolderPath('MyDocuments')) 'BFBC2\crashreport.xml'
$originalCrash=if(Test-Path -LiteralPath $crashPath){(Get-Item -LiteralPath $crashPath).LastWriteTimeUtc}else{[DateTime]::MinValue}
$record=[ordered]@{schema=1;state='waiting_for_game';created_utc=[DateTime]::UtcNow.ToString('o');game_path=$GamePath;game_memory_written=$false;gpu_devices_created=$false;module_snapshots=0;crash_report_copied=$false;errors=@()}
function Save-Record {
 $record.updated_utc=[DateTime]::UtcNow.ToString('o')
 $record | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $folder 'session.json') -Encoding utf8
}
Save-Record
$timer=[Diagnostics.Stopwatch]::StartNew();$gameProcess=$null
while($timer.Elapsed.TotalSeconds -lt $WaitSeconds -and -not $gameProcess){
 foreach($candidate in @(Get-Process -Name 'BFBC2Game' -ErrorAction SilentlyContinue)){
  try {if([StringComparer]::OrdinalIgnoreCase.Equals([IO.Path]::GetFullPath($candidate.Path),$expected)){$gameProcess=$candidate;break}}catch{}
 }
 if(-not $gameProcess){Start-Sleep -Milliseconds 500}
}
if(-not $gameProcess){$record.state='game_not_started';Save-Record;$record | ConvertTo-Json -Depth 6;exit 2}
try {
 # Cache the process handle so the OS exit code remains available after exit.
 $heldProcessHandle=$gameProcess.Handle
 $record.pid=$gameProcess.Id;$record.process_start_utc=$gameProcess.StartTime.ToUniversalTime().ToString('o');$record.state='observing'
 $timer.Restart();$previousModules='';$moduleIndex=0;Save-Record
 while($timer.Elapsed.TotalSeconds -lt $MonitorSeconds){
  if($gameProcess.HasExited){$record.state='process_exited';$record.exit_code=$gameProcess.ExitCode;break}
  try {
   $gameProcess.Refresh()
   $modules=@($gameProcess.Modules | ForEach-Object {[ordered]@{name=$_.ModuleName;path=$_.FileName;base=$_.BaseAddress.ToInt64();size=$_.ModuleMemorySize}})
   $serialized=$modules | ConvertTo-Json -Depth 4
   if($serialized -ne $previousModules){
    $moduleIndex++;$file='modules-{0:d3}.json' -f $moduleIndex
    [ordered]@{observed_utc=[DateTime]::UtcNow.ToString('o');pid=$gameProcess.Id;modules=$modules} | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $folder $file) -Encoding utf8
    $previousModules=$serialized;$record.module_snapshots=$moduleIndex;Save-Record
   }
  }catch {
   $record.errors+=@{at_utc=[DateTime]::UtcNow.ToString('o');message=$_.Exception.Message}
   if($record.errors.Count -gt 5){$record.errors=@($record.errors | Select-Object -Last 5)}
  }
  if(Test-Path -LiteralPath $crashPath){
   $current=(Get-Item -LiteralPath $crashPath).LastWriteTimeUtc
   if($current -gt $originalCrash){Copy-Item -LiteralPath $crashPath -Destination (Join-Path $folder 'crashreport.xml') -Force;$record.crash_report_copied=$true;$originalCrash=$current;Save-Record}
  }
  Start-Sleep -Milliseconds 1000
 }
 if($record.state -eq 'observing'){$record.state='observation_complete_game_running'}
 if(Test-Path -LiteralPath $crashPath){
  if((Get-Item -LiteralPath $crashPath).LastWriteTimeUtc -gt $originalCrash){Copy-Item -LiteralPath $crashPath -Destination (Join-Path $folder 'crashreport.xml') -Force;$record.crash_report_copied=$true}
 }
}catch{$record.state='observation_error';$record.errors+=@{at_utc=[DateTime]::UtcNow.ToString('o');message=$_.Exception.Message}}
finally{if($gameProcess){$gameProcess.Dispose()};Save-Record}
$record | ConvertTo-Json -Depth 6
