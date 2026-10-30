[CmdletBinding()]
param([Parameter(Mandatory)][string]$ComponentReport,[Parameter(Mandatory)][string]$OutputPath,
 [Parameter(Mandatory)][string[]]$Suites,[ValidateSet('x86','x64')][string]$Architecture='x86',
 [string[]]$Recompile=@())
$ErrorActionPreference='Stop';$PSNativeCommandUseErrorActionPreference=$false
$work=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../../..'))
$canonical='G:\Unity\VRMMO\FrostbiteVR';$output=[IO.Path]::GetFullPath($OutputPath)
if(-not $output.StartsWith((Join-Path $work 'reports')+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase) -or (Test-Path -LiteralPath $output)){throw 'Fresh report directory required'}
. (Join-Path $work 'tools/StorageBudget.ps1')
$budget=Assert-FvrStorageBudget -RepositoryRoot $work -OutputPath $output -Operation Build -PlannedBytes 256MB -AdditionalRoots @((Join-Path $canonical 'build'),'G:/Unity/VRMMO/bc2vr-recovery')
$null=[IO.Directory]::CreateDirectory($output)
function Save($name,$value){[IO.File]::WriteAllText((Join-Path $output $name),($value|ConvertTo-Json -Depth 12),[Text.UTF8Encoding]::new($false))}
Save 'storage.json' $budget
$env:TEMP=Join-Path $output 'temp';$env:TMP=$env:TEMP;$env:TMPDIR=$env:TEMP;$null=[IO.Directory]::CreateDirectory($env:TEMP)
$vs='C:\Program Files\Microsoft Visual Studio\18\Community'
$arch=if($Architecture -eq 'x64'){'amd64'}else{'x86'}
& (Join-Path $vs 'Common7/Tools/Launch-VsDevShell.ps1') -VsInstallationPath $vs -Arch $arch -HostArch amd64 -SkipAutomaticLocation | Out-Null
$command=Get-Content -LiteralPath (Join-Path $ComponentReport 'component-command.json') -Raw|ConvertFrom-Json
$common=@($command.arguments[0..([Array]::IndexOf($command.arguments,'/c')-1)])
$libraries=@('BC2Camera.lib','BC2Profile.lib','FrostbiteDiscovery.lib','FrostbiteCamera.lib','FvrCore.lib','FvrD3D11RigidProp.lib','FvrRigidPropGeometry.lib')|ForEach-Object {Join-Path $canonical ('build/'+$Architecture+'/'+$_)}
$libraries+=@('d3d11.lib','d3dcompiler.lib')
Push-Location $output
try {
 $objects=@()
 foreach($name in $Recompile){
  if($name -notmatch '^Bc2[A-Za-z0-9]+$'){throw 'Expected BC2 component basename'}
  & cl.exe @common /c (Join-Path $work ('src/games/bc2/'+$name+'.cpp')) *> ($name+'-compile.log')
  if($LASTEXITCODE -ne 0){Get-Content ($name+'-compile.log') -Tail 20;throw 'Component recompile failed'}
  $objects+=Join-Path $output ($name+'.obj')
 }
 $results=@()
 foreach($suite in $Suites){
  if($suite -notmatch '^[A-Za-z0-9]+$'){throw 'Expected suite basename'}
  $arguments=$common+@((Join-Path $work ('tests/'+$suite+'Tests.cpp')),('/Fe:'+$suite+'.exe'),'/link')+$objects+@((Join-Path $ComponentReport 'WeaponsCandidate.lib'))+$libraries
  Save ($suite+'-command.json') @{arguments=$arguments;reused_components=$ComponentReport}
  & cl.exe @arguments *> ($suite+'-compile.log')
  $compiled=$LASTEXITCODE -eq 0;$passed=$false
  if($compiled){& (Join-Path $output ($suite+'.exe')) *> ($suite+'-test.log');$passed=$LASTEXITCODE -eq 0}
  $results+=@{suite=$suite;compiled=$compiled;passed=$passed}
  Write-Output "$suite compiled=$compiled passed=$passed"
  if(-not $compiled){Get-Content ($suite+'-compile.log') -Tail 20}
  elseif(-not $passed){Get-Content ($suite+'-test.log') -Tail 20}
 }
 Save 'results.json' @{architecture=$Architecture;checks=$results;normal_build=$false;installed=$false}
 if($results.Where({-not $_.passed}).Count){throw 'Targeted checks failed'}
} finally {Pop-Location}
