[CmdletBinding()]
param([Parameter(Mandatory)][string]$OutputPath)
$ErrorActionPreference='Stop';$PSNativeCommandUseErrorActionPreference=$false
$work=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../..'))
$canonical='G:\Unity\VRMMO\FrostbiteVR';$build=Join-Path $canonical 'build/x64'
$output=[IO.Path]::GetFullPath($OutputPath)
if(-not $output.StartsWith((Join-Path $work 'reports')+'\',[StringComparison]::OrdinalIgnoreCase) -or (Test-Path $output)){throw 'Fresh owned G: output required'}
function Save($name,$value){[IO.File]::WriteAllText((Join-Path $output $name),($value|ConvertTo-Json -Depth 12),[Text.UTF8Encoding]::new($false))}
function Receipt($paths){@($paths|Sort-Object -Unique|ForEach-Object {@{path=$_;sha256=(Get-FileHash -LiteralPath $_ -Algorithm SHA256).Hash.ToLowerInvariant()}})}
function Field($lines,$start,$name){
 for($i=$start+1;$i -lt $lines.Count -and $lines[$i].StartsWith('  ');$i++){
  if($lines[$i].StartsWith("  $name = ")){return $lines[$i].Substring($name.Length+5)}
 };return ''
}
function Arguments($line){@([regex]::Matches($line,'(?:[^\s"]|"[^"]*")+')|ForEach-Object {
 $_.Value.Replace('\"','"').Replace(($canonical-replace '\\','/'),($work-replace '\\','/')).Replace($canonical,$work).
 Replace((Join-Path $work 'build'),(Join-Path $canonical 'build')).Replace(($work-replace '\\','/')+'/build',($canonical-replace '\\','/')+'/build')
})}
. (Join-Path $work 'tools/StorageBudget.ps1')
$budget=Assert-FvrStorageBudget -RepositoryRoot $work -OutputPath $output -Operation Build -PlannedBytes 256MB -AdditionalRoots @($build,'G:/Unity/VRMMO/bc2vr-recovery')
$null=[IO.Directory]::CreateDirectory($output);Save 'storage.json' $budget
$saved=@{TEMP=$env:TEMP;TMP=$env:TMP;TMPDIR=$env:TMPDIR};$pushed=$false
try {
 $env:TEMP=Join-Path $output 'temp';$env:TMP=$env:TEMP;$env:TMPDIR=$env:TEMP;$null=[IO.Directory]::CreateDirectory($env:TEMP)
 $vs='C:/Program Files/Microsoft Visual Studio/18/Community'
 & (Join-Path $vs 'Common7/Tools/Launch-VsDevShell.ps1') -VsInstallationPath $vs -Arch amd64 -HostArch amd64 -SkipAutomaticLocation|Out-Null
 $ninja=[IO.File]::ReadAllLines((Join-Path $build 'build.ninja'))
 $at=-1;$linkAt=-1
 for($i=0;$i -lt $ninja.Count;$i++){
  if($ninja[$i].StartsWith('build ') -and $ninja[$i].Contains('OpenXrHost.cpp.obj:')){$at=$i}
  if($ninja[$i].StartsWith('build OpenXrPresentationTests.exe:')){$linkAt=$i}
 }
 if($at -lt0 -or $linkAt -lt0){throw 'Canonical host/test configuration unavailable'}
 # Match the normal host/test optimization and inlining profile. The first
 # custom /O1 attempt exhausted the default stack inside shader compilation.
 $common=@('/nologo','/std:c++20','/EHsc','/MT','/O2','/Ob1','/UNDEBUG','/DNOMINMAX','/DWIN32_LEAN_AND_MEAN')+
  @(Arguments (Field $ninja $at 'DEFINES'))+@(Arguments (Field $ninja $at 'INCLUDES'))+@('/I'+(Join-Path $work 'tests'))
 $libraries=@(Arguments (Field $ninja $linkAt 'LINK_LIBRARIES')|ForEach-Object {
  $candidate=Join-Path $build $_;if(Test-Path -LiteralPath $candidate){$candidate}else{$_}
 })
 $runtime=Join-Path $build 'FvrTestOpenXr.dll'
 if(-not(Test-Path -LiteralPath $runtime)){throw 'Existing emulated runtime missing'}
 $inputs=@((Join-Path $work 'src/xr/OpenXrHost.cpp'),(Join-Path $work 'tests/OpenXrPresentationTests.cpp'),
  (Join-Path $work 'include/fvr/xr/HostComposedCopyService.h'),$runtime)+@($libraries|Where-Object {[IO.Path]::IsPathRooted($_)})
 Save 'inputs.json' (Receipt $inputs)
 Push-Location $output;$pushed=$true
 $object=Join-Path $output 'OpenXrHost.obj';$exe=Join-Path $output 'OpenXrPresentationTests.exe'
 $compile=$common+@('/c',(Join-Path $work 'src/xr/OpenXrHost.cpp'),('/Fo'+$object))
 Save 'host-command.json' @{arguments=$compile}
 & cl.exe @compile *> 'host-compile.log'
 if($LASTEXITCODE){Get-Content host-compile.log -Tail 25;throw 'Host compile failed'}
 $compile=$common+@((Join-Path $work 'tests/OpenXrPresentationTests.cpp'),('/Fe'+$exe),'/link','/INCREMENTAL:NO','/DEBUG:NONE','/MANIFEST:EMBED',$object)+$libraries
 Save 'test-command.json' @{arguments=$compile;runtime=$runtime}
 & cl.exe @compile *> 'test-compile.log'
 if($LASTEXITCODE){Get-Content test-compile.log -Tail 25;throw 'Host test compile failed'}
 & $exe --normal-copy-poll *> 'cpu-test.log';$cpuExit=$LASTEXITCODE;$cpu=$cpuExit -eq0
 Save 'result.json' @{cpu_poll=$cpu;cpu_exit=$cpuExit;gpu_presentation=$false;game_started=$false;headset_checked=$false}
 if(-not $cpu){Get-Content cpu-test.log -Tail 20;throw 'Normal copy poll regression failed'}
 & $exe $runtime *> 'gpu-test.log';$gpuExit=$LASTEXITCODE;$gpu=$gpuExit -eq0
 Save 'result.json' @{cpu_poll=$cpu;cpu_exit=$cpuExit;gpu_presentation=$gpu;gpu_exit=$gpuExit;game_started=$false;headset_checked=$false}
 Get-Content gpu-test.log -Tail 16
 if(-not $gpu){throw 'Emulated-runtime GPU presentation regression failed'}
 $channelSource=Join-Path $work 'tests/PipelinedFrameChannelTests.cpp'
 $channelLibraries=@((Join-Path $build 'FvrFrameChannel.lib'),(Join-Path $build 'FvrCore.lib'),'ole32.lib')
 Save 'channel-inputs.json' (Receipt (@($channelSource)+@($channelLibraries|Where-Object {[IO.Path]::IsPathRooted($_)})))
 & cl.exe /nologo /std:c++20 /EHsc /MT /O2 /Ob1 /DNOMINMAX /DWIN32_LEAN_AND_MEAN ('/I'+(Join-Path $work 'include')) $channelSource /FePipelinedFrameChannelTests.exe /link /DEBUG:NONE /INCREMENTAL:NO /MANIFEST:EMBED @channelLibraries *> 'channel-compile.log'
 if($LASTEXITCODE){Get-Content 'channel-compile.log' -Tail 20;throw 'Deferred channel compile failed'}
 & (Join-Path $output 'PipelinedFrameChannelTests.exe') *> 'channel-test.log';$channelExit=$LASTEXITCODE
 Save 'channel-result.json' @{passed=($channelExit -eq 0);exit_code=$channelExit;game_started=$false}
 if($channelExit -ne 0){Get-Content 'channel-test.log' -Tail 20;throw 'Deferred channel check failed'}
 Write-Output 'Host CPU, emulated-runtime GPU, and deferred channel checks passed.'
} finally {
 if($pushed){Pop-Location};$env:TEMP=$saved.TEMP;$env:TMP=$saved.TMP;$env:TMPDIR=$saved.TMPDIR
}
