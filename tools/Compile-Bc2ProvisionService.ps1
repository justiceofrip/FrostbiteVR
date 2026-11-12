# ROOT-only deterministic Service34 checks. One fresh pwsh -NoProfile -File process per architecture.
# The config's retained Service receipts seed dependencies, not current-source acceptance.
# Replace those receipts with result.json/checks.log after both fresh runs; Source63 is separate.
[CmdletBinding()]
param(
    [Parameter(Mandatory)][ValidateSet('x86','amd64')][string]$Architecture,
    [Parameter(Mandatory)][string]$Config,
    [Parameter(Mandatory)][ValidatePattern('^[0-9a-f]{64}$')][string]$ConfigSha256,
    [Parameter(Mandatory)][string]$OutputPath,
    [Parameter(Mandatory)][string]$Python,
    [string]$VsDevShell='C:/Program Files/Microsoft Visual Studio/18/Community/Common7/Tools/Launch-VsDevShell.ps1'
)
$ErrorActionPreference='Stop'
$PSNativeCommandUseErrorActionPreference=$false
$utf8=[Text.UTF8Encoding]::new($false)
$output=[IO.Path]::GetFullPath($OutputPath)
$checker=Join-Path $PSScriptRoot 'native_batch.py'
$suite='Bc2CampaignProvisioningTests'
$banner='PASS 34 campaign provisioning groups; real service/policy, native assignment fixture; no native acceptance'

function File-Pin([string]$Path){
    $file=Get-Item -LiteralPath $Path -ErrorAction Stop
    if($file.PSIsContainer){throw "File required: $Path"}
    return [ordered]@{path=$file.FullName;bytes=$file.Length;sha256=(Get-FileHash -Algorithm SHA256 -LiteralPath $file.FullName).Hash.ToLowerInvariant()}
}
function Read-Json([string]$Path){return [IO.File]::ReadAllText($Path) | ConvertFrom-Json -DateKind String}
function Equal-Path([string]$A,[string]$B){return [IO.Path]::GetFullPath($A) -ieq [IO.Path]::GetFullPath($B)}
function Save-Json([string]$Name,$Value){
    $bytes=$utf8.GetBytes(($Value | ConvertTo-Json -Depth 30)+"`n")
    $stream=[IO.File]::Open((Join-Path $output $Name),[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::None)
    try{$stream.Write($bytes,0,$bytes.Length)}finally{$stream.Dispose()}
}
function Assert-Pins($Pins){
    foreach($pin in $Pins){
        $actual=File-Pin $pin.path
        if($actual.sha256 -cne $pin.sha256 -or $actual.bytes -ne $pin.bytes){throw "Pinned input changed: $($pin.path)"}
    }
}
function Git-Read([string]$Checkout,[string[]]$Arguments){
    $answer=@(& git --no-optional-locks -C $Checkout @Arguments 2>&1)
    if($LASTEXITCODE -ne 0){throw "Read-only Git check failed: $answer"}
    return ($answer -join "`n").Trim()
}
function Assert-Commit {
    foreach($checkout in @($repo,$wt)){
        if((Git-Read $checkout @('rev-parse','HEAD')) -cne $c.source_commit){throw 'Both checkouts must match the exact ROOT final commit'}
        if(Git-Read $checkout @('status','--porcelain','--untracked-files=no')){throw 'Tracked checkout must be clean'}
    }
}
function Assert-Idle {
    if(Get-Process BFBC2Game,BC2XrHost,BC2NativeTrace,BC2NativeIpcProbe,BC2NativeResolution,BC2ResolutionBootstrap,Bc2CampaignProvisioningCommand,cl,link,cmake,ninja,ctest,MSBuild -ErrorAction SilentlyContinue){throw 'Game/session or shared build resources busy'}
    if($c.offline_resource_release){
        Assert-Pins @($c.offline_resource_release)
        $release=Read-Json $c.offline_resource_release.path
        Assert-Pins @($release.last_session)
        foreach($prior in (Read-Json $release.last_session.path).processes){
            $live=Get-Process -Id ([int]$prior.identity.pid) -ErrorAction SilentlyContinue
            if($live){
                $started=[DateTimeOffset]::Parse($prior.identity.start_utc,[Globalization.CultureInfo]::InvariantCulture).UtcDateTime
                if([Math]::Abs(($live.StartTime.ToUniversalTime()-$started).TotalMilliseconds) -lt 1){throw "Original session process still active: $($prior.name)"}
            }
        }
    }
}

# Deliberately do not call native_batch.context: it requires the fresh focused results
# this runner is about to produce. Reuse the shared offline release verifier verbatim.
$reviewCode=@'
import importlib.util, re, sys
from pathlib import Path
checker, config, digest, runner = map(str, sys.argv[1:])
spec = importlib.util.spec_from_file_location('focused_native_batch', checker)
b = importlib.util.module_from_spec(spec)
spec.loader.exec_module(b)
b.need(b.pin(config)['sha256'] == digest, 'Configuration hash differs')
c = b.load(config)
b.need(c['schema'] == 'fvr.native-batch.v1' and c['root_accepted'] is True, 'Explicit ROOT config acceptance required')
b.need(re.fullmatch('[0-9a-f]{40}', c['source_commit'] or '') is not None, 'Final source commit required')
b.need(re.fullmatch('[0-9a-f]{64}', c['source_sha256'] or '') is not None, 'Final source closure required')
root, task, run = (Path(c[key]).resolve() for key in ('repository', 'task_checkout', 'run'))
b.need(run.is_relative_to(root / 'reports') and run != root / 'reports', 'Canonical report run required')
b.need(Path(runner).resolve() in {root / 'tools/Compile-Bc2ProvisionService.ps1', task / 'tools/Compile-Bc2ProvisionService.ps1'}, 'Shared repository runner required')
b.verify_owner_release(c)
leases = b.load(b.checked(c['root_leases']))
for name in ('COORDINATOR', 'INTEGRATION'):
    lease = leases[name]
    b.need(lease['owner_token'] and b.load(lease['path'])['owner_token'] == lease['owner_token'], 'Current ROOT lease differs: ' + name)
    b.need(Path(lease['path']).resolve() == root.parent / 'bc2vr-task-worktrees' / (name + '.lock'), 'Unexpected ROOT lease path')
tool_paths = {b.checked(row).resolve() for row in c['tool_pins']}
for tool in (Path(checker).resolve(), Path(runner).resolve(), root / 'tools/StorageBudget.ps1'):
    b.need(tool in tool_paths, 'Actual operational helper must be config-pinned: ' + str(tool))
b.need(c['operation_records_changed'] is False and c['normal_feature_grants_changed'] is False, 'No native capability changes allowed')
recipe = b.checked(c['source_recipe'])
b.need(recipe == root / 'tools/bc2_native_operation_receipts.py', 'Final repository source recipe required')
proof = b.module('focused_operation_recipe', recipe)
proof.verify_source(root, b.checked(c['source_receipt']), c['source_sha256'], b.checked(c['reviewed_header']))
for row in c['candidate_files']:
    target = (root / row['relative_path']).resolve()
    b.need(target.is_relative_to(root) and b.pin(target)['sha256'] == row['sha256'], 'Applied candidate bytes changed')
budget = c['budget']
b.need(budget['root_authorized'] is True and budget['explicit_private_exception'] is True and isinstance(budget['reason'], str) and budget['reason'].strip(), 'Explicit ROOT storage exception and rationale required')
b.need(budget['build_ceiling_bytes'] in (57 * 1024**3, 60 * 1024**3) and budget['floor_bytes'] == 15 * 1024**3, 'Explicit reviewed 57/60GiB ceiling and 15GiB floor required')
inventory = b.load(b.checked(budget['fresh_storage_inspection']))
roots = {str(Path(row['path']).resolve()).casefold() for row in inventory['generated']['roots']}
b.need(len(roots) == 13 and {str(Path(p).resolve()).casefold() for p in budget['roots']} == roots, 'Complete reviewed thirteen-root inventory required')
'@
function Assert-Review {
    Assert-Pins $inputPins
    Assert-Idle
    Assert-Commit
    & $Python -B -c $reviewCode $checker $Config $ConfigSha256 $PSCommandPath
    if($LASTEXITCODE -ne 0){throw 'ROOT source/owner/lease/pin/storage review refused'}
}
function Relative-Source([string]$Path){
    $absolute=[IO.Path]::GetFullPath($Path)
    foreach($checkout in @($wt,$repo)){
        $prefix=[IO.Path]::GetFullPath($checkout)+'\'
        if($absolute.StartsWith($prefix,[StringComparison]::OrdinalIgnoreCase)){return $absolute.Substring($prefix.Length).Replace('\','/')}
    }
    throw "Dependency outside reviewed checkouts: $Path"
}
function Get-SourcePins {
    # Retain every prior dependency and follow literal local includes, including inactive branches.
    $seen=[Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    $pending=[Collections.Generic.Stack[string]]::new()
    foreach($source in $sources){$pending.Push($source)}
    foreach($relative in $requiredDependencies){$pending.Push((Join-Path $wt $relative))}
    while($pending.Count){
        $path=[IO.Path]::GetFullPath($pending.Pop())
        if(-not $path.StartsWith(([IO.Path]::GetFullPath($wt)+'\'),[StringComparison]::OrdinalIgnoreCase)){throw "Dependency outside task checkout: $path"}
        if(-not $seen.Add($path)){continue}
        foreach($line in [IO.File]::ReadLines($path)){
            $directive=[regex]::Match($line,'^\s*#\s*include\b([^\r\n]*)')
            if(-not $directive.Success){continue}
            $match=[regex]::Match($directive.Groups[1].Value,'^\s*(["<])([^">]+)[">]')
            if(-not $match.Success){throw "Nonliteral include needs explicit review: $path"}
            $name=$match.Groups[2].Value
            $quote=$match.Groups[1].Value -eq '"'
            $search=@();if($quote){$search+=Split-Path -Parent $path};$search+=$includes
            $found=$null
            foreach($directory in $search){
                $candidate=Join-Path $directory $name
                if(Test-Path -LiteralPath $candidate -PathType Leaf){$found=$candidate;break}
            }
            if($found){$pending.Push($found)}elseif($quote -or $name.StartsWith('fvr/')){throw "Unresolved project include $name in $path"}
        }
    }
    $pins=@($seen | Sort-Object | ForEach-Object {File-Pin $_})
    $relativePaths=@($pins | ForEach-Object {Relative-Source $_.path})
    $null=Git-Read $wt (@('--literal-pathspecs','ls-files','--error-unmatch','--')+$relativePaths)
    foreach($pin in $pins){
        $relative=Relative-Source $pin.path
        $canonical=File-Pin (Join-Path $repo $relative)
        if($canonical.sha256 -cne $pin.sha256 -or $canonical.bytes -ne $pin.bytes){throw "Canonical/task dependency differs: $relative"}
    }
    return $pins
}
function PE-Machine([string]$Path){
    $stream=[IO.File]::OpenRead($Path)
    try{
        $reader=[IO.BinaryReader]::new($stream)
        if($reader.ReadUInt16() -ne 0x5a4d){throw 'Missing MZ signature'}
        $stream.Position=0x3c;$offset=$reader.ReadUInt32();$stream.Position=$offset
        if($reader.ReadUInt32() -ne 0x4550){throw 'Missing PE signature'}
        return $reader.ReadUInt16()
    }finally{$stream.Dispose()}
}

$commandLine=[Environment]::GetCommandLineArgs()
$fileIndex=[Array]::FindIndex($commandLine,[Predicate[string]]{param($value) $value -ieq '-File'})
if($fileIndex -lt 0 -or $fileIndex+1 -ge $commandLine.Length -or [IO.Path]::GetFullPath($commandLine[$fileIndex+1]) -ine $PSCommandPath -or '-NoProfile' -notin $commandLine){throw 'Use a fresh pwsh -NoProfile -File process for each architecture'}
if($env:VSCMD_VER -or $env:VSCMD_ARG_TGT_ARCH -or $env:VCINSTALLDIR){throw 'Fresh shell required; inherited Visual Studio environment is not accepted'}
if($env:CL -or $env:_CL_ -or $env:LINK -or $env:_LINK_){throw 'Ambient compiler/linker arguments are not accepted'}
$configPin=File-Pin $Config
if($configPin.sha256 -cne $ConfigSha256){throw 'Configuration hash differs'}
$Config=$configPin.path
$c=Read-Json $Config
$ceiling=[long]$c.budget.build_ceiling_bytes
$repo=[IO.Path]::GetFullPath($c.repository)
$wt=[IO.Path]::GetFullPath($c.task_checkout)
$run=[IO.Path]::GetFullPath($c.run)
if([IO.Path]::GetPathRoot($repo) -ine 'G:\' -or [IO.Path]::GetPathRoot($wt) -ine 'G:\' -or -not $output.StartsWith(($run.TrimEnd('\')+'\'),[StringComparison]::OrdinalIgnoreCase)){throw 'Choose a fresh output below the reviewed canonical run on G:'}
if(Test-Path -LiteralPath $output){throw 'Preserve attempted focused build; choose a fresh output directory'}
$ancestor=$output
while($ancestor){
    if((Test-Path -LiteralPath $ancestor) -and ((Get-Item -LiteralPath $ancestor -Force).Attributes -band [IO.FileAttributes]::ReparsePoint)){throw "Linked output path refused: $ancestor"}
    $ancestor=Split-Path -Parent $ancestor
}
# Check shared checker bytes before importing it; the Python loader also rejects duplicate JSON keys.
foreach($path in @($checker,$PSCommandPath,(Join-Path $repo 'tools/StorageBudget.ps1'))){
    $pins=@($c.tool_pins | Where-Object {Equal-Path $_.path $path})
    if($pins.Count -ne 1){throw "Unique actual tool pin required: $path"}
    Assert-Pins $pins
}
$pythonCommand=Get-Command $Python -CommandType Application -ErrorAction Stop | Select-Object -First 1
$Python=$pythonCommand.Source
$inputPins=@($configPin;File-Pin $Python;File-Pin $VsDevShell;File-Pin $PSCommandPath;File-Pin $checker;File-Pin (Join-Path $repo 'tools/StorageBudget.ps1'))
Assert-Review
$receiptArch=if($Architecture -eq 'amd64'){'x64'}else{'x86'}
$retained=@($c.reused_private_focused_evidence | Where-Object {$_.suite -ceq $suite -and $_.architecture -ceq $receiptArch})
if($retained.Count -ne 1 -or $retained[0].root_accepted -ne $true){throw 'Unique ROOT-accepted prior Service dependency inventory required'}
Assert-Pins @($retained[0].result,$retained[0].output_proof)
$old=Read-Json $retained[0].result.path
$oldArch=if($old.architecture -eq 'amd64'){'x64'}else{$old.architecture}
if($old.suite -cne $suite -or $oldArch -cne $receiptArch -or $old.compile_exit -ne 0 -or $old.test_exit -ne 0 -or $old.evidence_valid -ne $true -or -not $old.source_files){throw 'Retained Service compiler/dependency receipt differs'}
$expectedPattern='(?m)^'+[regex]::Escape($banner)+'\r?$'
if(-not ([IO.File]::ReadAllText($retained[0].output_proof.path) -match $expectedPattern)){throw 'Retained Service34 output differs'}
$rel=@('tests/Bc2CampaignProvisioningTests.cpp','src/games/bc2/Bc2CampaignProvisioning.cpp','src/games/bc2/Bc2CampaignProvisioningAssignment.cpp','src/games/bc2/Bc2HolsterInput.cpp')
$requiredDependencies=@(@($old.source_files | ForEach-Object {Relative-Source $_.path})+@('src/games/bc2/Bc2CampaignProvisioning.h','src/games/bc2/Bc2CampaignProvisioningInputHoldDiagnostics.h','src/games/bc2/Bc2CampaignProvisioningControlDiagnostics.h','include/fvr/interaction/HandInteraction.h','include/fvr/interaction/FeedInteraction.h','src/games/bc2/Bc2AmmoResourceService.h') | Sort-Object -Unique)
$sources=@($rel | ForEach-Object {Join-Path $wt $_})
$includes=@('src/games/bc2','tools','include' | ForEach-Object {Join-Path $wt $_})
$sourcePins=@(Get-SourcePins)
$abi=[IO.File]::ReadAllText((Join-Path $wt 'src/games/bc2/Bc2CampaignProvisioning.h'))
foreach($assertion in @('static_assert\([^;]*sizeof\(Request\)\s*==\s*1264','static_assert\([^;]*sizeof\(WireReceipt\)\s*==\s*1272')){
    if($abi -notmatch $assertion){throw 'Required Request1264/WireReceipt1272 compile-time ABI assertion missing'}
}
$inputPins+=@($retained[0].result,$retained[0].output_proof)
$roots=@($c.budget.roots | ForEach-Object {[IO.Path]::GetFullPath($_)} | Sort-Object -Unique)
. (Join-Path $repo 'tools/StorageBudget.ps1')
$admission=Assert-FvrStorageBudget -RepositoryRoot $repo -OutputPath $output -Operation Build -PlannedBytes 128MB -MaxGeneratedBytes $ceiling -MinFreeBytes 15GB -AdditionalRoots $roots
$actualRoots=@($admission.generated.roots | ForEach-Object {[IO.Path]::GetFullPath($_.path)} | Sort-Object -Unique)
if($actualRoots.Count -ne 13 -or @(Compare-Object $roots $actualRoots).Count){throw 'Fresh admission must include exactly all thirteen reviewed roots'}
foreach($temporary in @($env:TEMP,$env:TMP,$env:TMPDIR)){
    if(-not $temporary -or [IO.Path]::GetPathRoot($temporary) -ine 'G:\'){throw 'G: compiler scratch routing failed'}
}
Assert-Review
Assert-Pins $sourcePins
$null=New-Item -ItemType Directory -Path $output -ErrorAction Stop
Save-Json 'storage.json' $admission
Save-Json 'inputs.json' ([ordered]@{config=$configPin;source_commit=$c.source_commit;check='Service';suite=$suite;source_files=$sourcePins;input_pins=$inputPins;retained_dependencies=$requiredDependencies;wire_abi='Request1264/WireReceipt1272';reviewed_build_ceiling_bytes=$ceiling;planned_bytes=128MB;min_free_bytes=15GB})
$compile=$null;$test=$null;$failure=$null;$compilerPin=$null;$linkerPin=$null;$binaryPin=$null;$outputPin=$null;$machine=$null
try{
    $scratch=Join-Path $output 'scratch'
    $null=[IO.Directory]::CreateDirectory($scratch)
    $env:TEMP=$scratch;$env:TMP=$scratch;$env:TMPDIR=$scratch
    & $VsDevShell -Arch $Architecture -HostArch amd64 -SkipAutomaticLocation *> (Join-Path $output 'environment.log')
    $vsEnvironmentOk=$?
    $validTargets=if($Architecture -eq 'amd64'){@('amd64','x64')}else{@('x86')}
    if(-not $vsEnvironmentOk -or $env:VSCMD_ARG_TGT_ARCH -notin $validTargets){throw 'Requested Visual Studio target architecture was not established'}
    foreach($temporary in @($env:TEMP,$env:TMP,$env:TMPDIR)){
        if(-not $temporary -or [IO.Path]::GetPathRoot($temporary) -ine 'G:\'){throw 'G: compiler scratch routing changed'}
    }
    $compiler=(Get-Command cl.exe -CommandType Application -ErrorAction Stop | Select-Object -First 1).Source
    $linker=(Get-Command link.exe -CommandType Application -ErrorAction Stop | Select-Object -First 1).Source
    $targetLeaf=if($Architecture -eq 'amd64'){'x64'}else{'x86'}
    foreach($tool in @($compiler,$linker)){
        if($tool -notmatch ('(?i)\\Hostx64\\'+$targetLeaf+'\\')){throw "Wrong compiler/linker target: $tool"}
    }
    $compilerPin=File-Pin $compiler;$linkerPin=File-Pin $linker
    Assert-Review
    Assert-Pins $sourcePins
    $arguments=@('/nologo','/std:c++20','/EHsc','/W4','/O2','/Ob1','/DNDEBUG','/MT')+@($includes | ForEach-Object {'/I'+$_})+@(('/Fo'+$output+'/'),('/Fe'+(Join-Path $output 'provision-tests.exe')))+$sources
    Save-Json 'command.json' ([ordered]@{compiler=$compiler;working_directory=$output;arguments=$arguments})
    Push-Location $output
    try{
        & $compiler @arguments *> (Join-Path $output 'compile.log')
        $compile=$LASTEXITCODE
        if($compile -eq 0){
            Assert-Review
            Assert-Pins ($sourcePins+@($compilerPin,$linkerPin))
            $binaryPin=File-Pin (Join-Path $output 'provision-tests.exe')
            $machine=PE-Machine $binaryPin.path
            $expectedMachine=if($Architecture -eq 'amd64'){0x8664}else{0x14c}
            if($machine -ne $expectedMachine){throw 'Compiled focused executable machine mismatch'}
            & $binaryPin.path *> (Join-Path $output 'checks.log')
            $test=$LASTEXITCODE
            $outputPin=File-Pin (Join-Path $output 'checks.log')
            if($test -eq 0 -and -not ([IO.File]::ReadAllText($outputPin.path) -match $expectedPattern)){throw 'Expected complete authentic Service34 output missing'}
        }
    }finally{Pop-Location}
    Assert-Review
    Assert-Pins ($sourcePins+@($compilerPin,$linkerPin))
    if($binaryPin){Assert-Pins @($binaryPin)}
    if($outputPin){Assert-Pins @($outputPin)}
}catch{$failure=$_.Exception.Message}
$result=[ordered]@{schema='fvr.focused-provision-service-result.v1';evidence_level='deterministic tests only';check='Service';suite=$suite;architecture=$Architecture;source_commit=$c.source_commit;compile_exit=$compile;test_exit=$test;source_files=$sourcePins;draft_only=$true;native_calls=$false;game_launched=$false;config=$configPin;input_pins=$inputPins;compiler=$compilerPin;linker=$linkerPin;test_binary=$binaryPin;pe_machine=$machine;output_proof=$outputPin;expected_cases=34;wire_abi='Request1264/WireReceipt1272';reviewed_build_ceiling_bytes=$ceiling;failure=$failure;evidence_valid=($null -eq $failure -and $compile -eq 0 -and $test -eq 0)}
# A post-build owner/pin/banner failure must never look like passing evidence to native_batch.
if($failure -and $result.compile_exit -eq 0 -and $result.test_exit -eq 0){$result.test_exit=$null}
Save-Json 'result.json' $result
if($failure){throw $failure}
if($compile -ne 0){Get-Content -LiteralPath (Join-Path $output 'compile.log') -Tail 45;exit $compile}
Get-Content -LiteralPath (Join-Path $output 'checks.log')
if($test -ne 0){exit $test}
Write-Output "PROVISION_SERVICE_${Architecture}_PASS"
