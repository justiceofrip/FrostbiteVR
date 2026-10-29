[CmdletBinding()]
param([ValidateSet('x86','x64')][string]$Architecture,
      [ValidateSet('Anchor','Input','GameplaySyntax')][string]$Suite,
      [Parameter(Mandatory)][string]$OutputPath)
$ErrorActionPreference='Stop'
$PSNativeCommandUseErrorActionPreference=$false
$work=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../..'))
$canonical='G:\Unity\VRMMO\FrostbiteVR'
$output=[IO.Path]::GetFullPath($OutputPath)
$reportRoot=Join-Path $work 'reports/coordinator-20261029-polish11'
if(-not $output.StartsWith($reportRoot+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase)) { throw 'Output must be inside this run' }
if(Test-Path -LiteralPath $output) { throw 'Use a fresh output folder; failed checks must remain' }
. (Join-Path $work 'tools/StorageBudget.ps1')
$budget=Assert-FvrStorageBudget -RepositoryRoot $work -OutputPath $output -Operation Build -PlannedBytes 96MB -RawCaptureBytes 0 -AdditionalRoots @((Join-Path $canonical 'build'),(Join-Path $canonical 'reports'),'G:\Unity\VRMMO\BC2VR-Hotfix018','G:\Unity\VRMMO\bc2vr-task-worktrees')
$null=New-Item -ItemType Directory -Path $output
function SaveJson($Name,$Value) { [IO.File]::WriteAllText((Join-Path $output $Name),($Value|ConvertTo-Json -Depth 8),[Text.UTF8Encoding]::new($false)) }
SaveJson 'storage.json' $budget
$env:TEMP=Join-Path $reportRoot 'temp';$env:TMP=$env:TEMP;$env:TMPDIR=$env:TEMP
$null=New-Item -ItemType Directory -Path $env:TEMP -Force
$vs='C:\Program Files\Microsoft Visual Studio\18\Community'
$arch=if($Architecture -eq 'x64'){'amd64'}else{'x86'}
& (Join-Path $vs 'Common7/Tools/Launch-VsDevShell.ps1') -VsInstallationPath $vs -Arch $arch -HostArch amd64 -SkipAutomaticLocation | Out-Null
$inputs=[Collections.Generic.List[string]]::new()
$arguments=@('/nologo','/std:c++20','/EHsc','/MT','/O2','/UNDEBUG','/DNOMINMAX',('/I'+(Join-Path $work 'include')),('/I'+(Join-Path $work 'src/games/bc2')),('/I'+(Join-Path $work 'tests')))
if($Suite -eq 'GameplaySyntax') {
    if($Architecture -ne 'x86'){throw 'Native gameplay is an x86 target'}
    $ninja=Get-Content -LiteralPath (Join-Path $canonical 'build/x86/build.ninja')
    $index=0;while($index -lt $ninja.Count -and $ninja[$index] -notlike 'build *Bc2Gameplay.cpp.obj:*') {++$index}
    if($index -eq $ninja.Count){throw 'No baseline gameplay compile rule'}
    $definitionLine=($ninja | Select-Object -Skip ($index+1) -First 6 | Where-Object {$_ -like '  DEFINES = *'})
    $defines=($definitionLine.Substring(12) -split ' ')|ForEach-Object {$_.Replace('\"','"')}
    $inputs.Add((Join-Path $work 'src/games/bc2/Bc2Gameplay.cpp'))
    $arguments+=@('/Zs','/DWIN32','/D_WINDOWS','/DWIN32_LEAN_AND_MEAN',('/I'+(Join-Path $canonical 'build/x86/generated/bc2-operation')),('/I'+(Join-Path $work 'third_party/minhook-1.3.4/include'))) + $defines + $inputs.ToArray()
} else {
    $test=if($Suite -eq 'Anchor'){'TrackedBodyAnchorTests'}else{'Bc2InputBindingTests'}
    $inputs.Add((Join-Path $work ('tests/'+$test+'.cpp')))
    if($Suite -eq 'Input'){$inputs.Add((Join-Path $work 'src/games/bc2/Bc2InputBinding.cpp'))}
    $arguments+=$inputs.ToArray()+@('/Fe:check.exe','/link',(Join-Path $canonical ('build/'+$Architecture+'/FvrCore.lib')),(Join-Path $canonical ('build/'+$Architecture+'/BC2Camera.lib')))
}
$identities=@($inputs|ForEach-Object {@{path=$_;sha256=(Get-FileHash -LiteralPath $_).Hash.ToLowerInvariant()}})
SaveJson 'command.json' @{compiler=(Get-Command cl.exe).Source;arguments=$arguments;inputs=$identities;unchanged_baseline='7063c0003fa5e41027bf831e3bb9c70093962da8'}
Push-Location $output
try {
    & cl.exe @arguments *> compile.log
    if($LASTEXITCODE -ne 0){Get-Content compile.log -Tail 25;throw "Compile failed: $LASTEXITCODE"}
    if($Suite -ne 'GameplaySyntax'){
        & .\check.exe *> test.log
        if($LASTEXITCODE -ne 0){Get-Content test.log -Tail 25;throw "Test failed: $LASTEXITCODE"}
        Get-Content test.log
    }
    SaveJson 'result.json' @{suite=$Suite;architecture=$Architecture;passed=$true;utc=[DateTime]::UtcNow.ToString('o');full_normal_build=$false;installed=$false}
} catch { SaveJson 'failure.json' @{suite=$Suite;architecture=$Architecture;error=$_.Exception.Message};throw }
finally {Pop-Location}
