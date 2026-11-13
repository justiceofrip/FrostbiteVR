#requires -Version 7.2
# Offline, bounded Ghidra export. All generated data stays under build/ghidra.
[CmdletBinding(DefaultParameterSetName='Functions')]
param(
    [Parameter(Mandatory)][string]$Executable,
    [Parameter(Mandatory,ParameterSetName='Functions')][ValidateCount(1,16)][string[]]$Addresses,
    [Parameter(Mandatory,ParameterSetName='Members')][ValidateCount(1,4)][string[]]$MemberOffsets,
    [Parameter(Mandatory,ParameterSetName='Members')][string]$SearchStartAddress,
    [Parameter(Mandatory,ParameterSetName='Members')][string]$SearchEndAddress,
    [Parameter(ParameterSetName='Members')][ValidateRange(1,5000000)][int]$MaxInstructions=5000000,
    [Parameter(ParameterSetName='Members')][ValidateRange(1,1024)][int]$MaxMatches=256,
    [Parameter(ParameterSetName='Members')][ValidateRange(1,256)][int]$MaxFunctions=128,
    [Parameter(ParameterSetName='Members')][ValidateRange(1,120)][int]$SearchSeconds=60,
    [Parameter(Mandatory)][string]$OutputPath,
    [string]$GhidraHome,
    [string]$JdkHome,
    [ValidateRange(1,[long]::MaxValue)][long]$MaxGeneratedBytes=40GB,
    [string]$GeneratedBudgetReason
)
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
$repository=[IO.Path]::GetFullPath((Split-Path -Parent $PSScriptRoot))
$generated=[IO.Path]::GetFullPath((Join-Path $repository 'build/ghidra'))
$memberSearch=$PSCmdlet.ParameterSetName -eq 'Members'
$scriptName=if($memberSearch){'SearchBc2MemberReferences.java'}else{'ExportBc2Functions.java'}
$exporter=Join-Path $PSScriptRoot ('ghidra/'+$scriptName)
if($MaxGeneratedBytes -gt 40GB -and [string]::IsNullOrWhiteSpace($GeneratedBudgetReason)){
    throw 'MaxGeneratedBytes above 40 GiB requires an explicit GeneratedBudgetReason.'
}

function Assert-UnlinkedPath([string]$Path) {
    $ancestor=[IO.Path]::GetFullPath($Path)
    while($ancestor){
        if((Test-Path -LiteralPath $ancestor) -and
           ((Get-Item -LiteralPath $ancestor -Force).Attributes -band [IO.FileAttributes]::ReparsePoint)){
            throw "Ghidra storage refuses a linked path: $ancestor"
        }
        $ancestor=Split-Path -Parent $ancestor
    }
}
function Write-NewJson([string]$Path,$Value) {
    $bytes=[Text.UTF8Encoding]::new($false).GetBytes(($Value | ConvertTo-Json -Depth 30))
    $stream=[IO.File]::Open($Path,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::None)
    try{$stream.Write($bytes,0,$bytes.Length)}finally{$stream.Dispose()}
}
function Read-Properties([string]$Path) {
    $values=@{}
    foreach($line in [IO.File]::ReadAllLines($Path)){
        if($line -match '^\s*([^#!\s=]+)\s*=\s*(.*)$'){$values[$matches[1]]=$matches[2].Trim().Trim('"')}
    }
    return $values
}

$configPath=Join-Path $repository 'build/toolkit/ghidra.local.json'
$config=if(Test-Path -LiteralPath $configPath){Get-Content -LiteralPath $configPath -Raw -Encoding utf8 | ConvertFrom-Json -AsHashtable}else{@{}}
if(-not $GhidraHome){$GhidraHome=if($config['GhidraHome']){$config['GhidraHome']}else{$env:GHIDRA_HOME}}
if(-not $JdkHome){$JdkHome=if($config['JdkHome']){$config['JdkHome']}else{$env:JAVA_HOME}}
if(-not $GhidraHome -or -not $JdkHome){throw 'Set GhidraHome/JdkHome, build/toolkit/ghidra.local.json, or GHIDRA_HOME/JAVA_HOME.'}
$GhidraHome=[IO.Path]::GetFullPath($GhidraHome)
$JdkHome=[IO.Path]::GetFullPath($JdkHome)
$java=Join-Path $JdkHome 'bin/java.exe'
$utilityJar=Join-Path $GhidraHome 'Ghidra/Framework/Utility/lib/Utility.jar'
$launchProperties=Join-Path $GhidraHome 'support/launch.properties'
foreach($path in @($java,(Join-Path $JdkHome 'bin/javac.exe'),$utilityJar,$launchProperties,$exporter)){
    if(-not (Test-Path -LiteralPath $path -PathType Leaf)){throw "Required toolkit file missing: $path"}
}
$ghidraProperties=Read-Properties (Join-Path $GhidraHome 'Ghidra/application.properties')
$jdkProperties=Read-Properties (Join-Path $JdkHome 'release')
if(-not $ghidraProperties['application.version'] -or -not $jdkProperties['JAVA_VERSION']){throw 'Cannot establish installed Ghidra/JDK versions.'}
$javaMajor=[int]($jdkProperties['JAVA_VERSION'].Split('.')[0])
if(($ghidraProperties['application.java.min'] -and $javaMajor -lt [int]$ghidraProperties['application.java.min']) -or
   ($ghidraProperties['application.java.max'] -and $javaMajor -gt [int]$ghidraProperties['application.java.max'])){
    throw 'The selected JDK is outside this Ghidra distribution''s supported Java version range.'
}

$Executable=[IO.Path]::GetFullPath($Executable)
if(-not (Test-Path -LiteralPath $Executable -PathType Leaf)){throw "Executable is not a file: $Executable"}
$OutputPath=[IO.Path]::GetFullPath($OutputPath)
if(-not $OutputPath.StartsWith($generated+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase) -or
   [IO.Path]::GetExtension($OutputPath) -ne '.json'){
    throw 'OutputPath must be a new .json file inside this checkout''s build/ghidra directory.'
}
if(Test-Path -LiteralPath $OutputPath){throw "Refusing to overwrite output: $OutputPath"}
Assert-UnlinkedPath $generated
Assert-UnlinkedPath $OutputPath
if($PSScriptRoot.Contains(';')){throw 'Ghidra script paths cannot contain the script-list separator (;).'}
$queryValues=if($memberSearch){$MemberOffsets}else{$Addresses}
$normalized=@(foreach($address in $queryValues){
    if($address -notmatch '^(?:0[xX])?([0-9a-fA-F]{1,16})$'){throw "Expected a hexadecimal virtual address, got: $address"}
    '0x'+([Convert]::ToUInt64($matches[1],16).ToString('x'))
})
if(@($normalized | Select-Object -Unique).Count -ne $normalized.Count){throw 'Addresses must be distinct.'}
if($memberSearch){
    if(@($normalized | Where-Object {[Convert]::ToUInt64($_.Substring(2),16) -gt 0xffffffff}).Count){throw 'Member offsets must fit an unsigned 32-bit value.'}
    $range=@(foreach($address in @($SearchStartAddress,$SearchEndAddress)){
        if($address -notmatch '^(?:0[xX])?([0-9a-fA-F]{1,16})$'){throw 'Search range requires hexadecimal virtual addresses.'}
        [Convert]::ToUInt64($matches[1],16)
    })
    if($range[0] -gt $range[1]){throw 'Search range start must not exceed its inclusive end.'}
}

$savedEnvironment=@{}
foreach($name in @('TEMP','TMP','TMPDIR','PYTHONDONTWRITEBYTECODE')){$savedEnvironment[$name]=[Environment]::GetEnvironmentVariable($name,'Process')}
$source=$null;$token=$null;$process=$null;$started=$false;$stdout=$null;$stderr=$null
try{
    # The read handle denies writes/deletion for the complete import/export, while
    # allowing Ghidra to open the same immutable executable for reading.
    $source=[IO.File]::Open($Executable,[IO.FileMode]::Open,[IO.FileAccess]::Read,[IO.FileShare]::Read)
    $sha=(Get-FileHash -InputStream $source -Algorithm SHA256).Hash.ToLowerInvariant()
    $projectDirectory=Join-Path $generated ('projects/'+$sha)
    $manifestPath=Join-Path $projectDirectory 'analysis.json'
    if($memberSearch -and -not (Test-Path -LiteralPath $manifestPath)){
        throw 'Member search requires a completed cached analysis; it never imports or analyzes an executable.'
    }
    # Disk reservation covers the project database and script cache, not the
    # separate in-memory Java heap. Allow 64x input size with a 1GiB floor.
    $planned=if(Test-Path -LiteralPath $manifestPath){512MB}else{[Math]::Max(1GB,$source.Length*64)}
    . (Join-Path $PSScriptRoot 'StorageBudget.ps1')
    $siblings=Split-Path -Parent $repository
    $additional=@('bc2vr-task-worktrees','BC2VR-Launcher' | ForEach-Object {Join-Path $siblings $_} | Where-Object {Test-Path -LiteralPath $_})
    $admission=Assert-FvrStorageBudget -RepositoryRoot $repository -OutputPath $generated -Operation Session -PlannedBytes $planned -MaxGeneratedBytes $MaxGeneratedBytes -AdditionalRoots $additional
    $admission|Add-Member -NotePropertyName generated_budget_reason -NotePropertyValue $GeneratedBudgetReason
    Assert-UnlinkedPath $projectDirectory
    $null=[IO.Directory]::CreateDirectory($projectDirectory)
    # Persistent zero-byte token; FileShare.None serializes every invocation for
    # this executable. A crashed process releases its OS handle automatically.
    $token=[IO.File]::Open((Join-Path $projectDirectory 'wrapper.lock'),[IO.FileMode]::OpenOrCreate,[IO.FileAccess]::ReadWrite,[IO.FileShare]::None)
    $projectFile=Join-Path $projectDirectory 'bc2.gpr'
    $cached=Test-Path -LiteralPath $manifestPath
    $manifest=$null
    if($cached){
        $manifest=Get-Content -LiteralPath $manifestPath -Raw -Encoding utf8 | ConvertFrom-Json
        if($manifest.schema -ne 'bc2.ghidra-analysis.v1' -or $manifest.executable_sha256 -ne $sha -or
           $manifest.ghidra_version -ne $ghidraProperties['application.version'] -or -not (Test-Path -LiteralPath $projectFile)){
            throw 'Cached project identity/version is inconsistent. Preserve it for review; automatic reimport or migration is disabled.'
        }
    }elseif((Test-Path -LiteralPath $projectFile) -or (Test-Path -LiteralPath (Join-Path $projectDirectory 'bc2.rep'))){
        throw 'An unfinished project exists without a completed analysis receipt. Preserve and inspect it; automatic replacement is disabled.'
    }
    $runDirectory=Join-Path $generated ('runs/'+[DateTime]::UtcNow.ToString('yyyyMMddTHHmmssfffZ')+'-'+[guid]::NewGuid().ToString('N'))
    $runtime=Join-Path $projectDirectory 'runtime'
    $scratch=Join-Path $runtime 'tmp'
    foreach($path in @($runDirectory,$scratch,(Join-Path $runtime 'home'),(Join-Path $runtime 'settings'),(Join-Path $runtime 'cache'),(Split-Path -Parent $OutputPath))){
        Assert-UnlinkedPath $path
        $null=[IO.Directory]::CreateDirectory($path)
    }
    $requestPath=Join-Path $runDirectory 'invocation.json'
    # Match the installed distribution's VM options, then override every storage
    # location. Direct ArgumentList invocation preserves spaces without cmd.exe.
    $vmArguments=@(foreach($line in [IO.File]::ReadAllLines($launchProperties)){
        if($line -match '^\s*VMARGS(?:_WINDOWS)?=(.+)$'){$matches[1].Trim()}
    })
    $vmArguments+=@('-Xmx2G','-XX:ParallelGCThreads=2','-XX:CICompilerCount=2','-Djava.awt.headless=true',
        ('-Duser.home='+(Join-Path $runtime 'home')),('-Djava.io.tmpdir='+$scratch),
        ('-Dapplication.tempdir='+$scratch),('-Dapplication.settingsdir='+(Join-Path $runtime 'settings')),
        ('-Dapplication.cachedir='+(Join-Path $runtime 'cache')))
    $headlessArguments=@($projectDirectory,'bc2')
    if($cached){$headlessArguments+=@('-process',[string]$manifest.program_name,'-noanalysis','-readOnly')}
    else{$headlessArguments+=@('-import',$Executable,'-analysisTimeoutPerFile','1800')}
    $headlessArguments+=@('-max-cpu','2','-scriptPath',(Join-Path $PSScriptRoot 'ghidra'),
        '-log',(Join-Path $runDirectory 'ghidra.log'),'-scriptlog',(Join-Path $runDirectory 'scripts.log'),
        '-postScript',$scriptName,$requestPath,$OutputPath)
    $javaArguments=$vmArguments+@('-cp',$utilityJar,'ghidra.Ghidra','ghidra.app.util.headless.AnalyzeHeadless')+$headlessArguments
    $request=[ordered]@{
        schema='bc2.ghidra-invocation.v1';tool_version='1.0';created_utc=[DateTime]::UtcNow.ToString('o')
        executable_path=$Executable;executable_sha256=$sha;executable_bytes=$source.Length
        addresses=$normalized;mode=$(if($cached){'cached_readonly_export'}else{'import_analyze_export'})
        installed_ghidra_version=$ghidraProperties['application.version'];installed_jdk_version=$jdkProperties['JAVA_VERSION']
        wrapper_sha256=(Get-FileHash -LiteralPath $PSCommandPath -Algorithm SHA256).Hash.ToLowerInvariant()
        exporter_sha256=(Get-FileHash -LiteralPath $exporter -Algorithm SHA256).Hash.ToLowerInvariant()
        launch_properties_sha256=(Get-FileHash -LiteralPath $launchProperties -Algorithm SHA256).Hash.ToLowerInvariant()
        project_path=$projectFile;analysis_receipt=$manifest;output_path=$OutputPath
        java_executable=$java;java_arguments=$javaArguments;headless_arguments=$headlessArguments
        storage_admission=$admission
    }
    if($memberSearch){
        $request.schema='bc2.ghidra-member-invocation.v1';$request.tool_version='1.1'
        $request.mode='cached_readonly_member_search'
        $request.Remove('addresses')
        $request.member_offsets=$normalized
        $request.search_start_address='0x'+$range[0].ToString('x')
        $request.search_end_address='0x'+$range[1].ToString('x')
        $request.max_instructions=$MaxInstructions;$request.max_matches=$MaxMatches
        $request.max_functions=$MaxFunctions;$request.search_seconds=$SearchSeconds
    }
    Write-NewJson $requestPath $request
    $start=[Diagnostics.ProcessStartInfo]::new()
    $start.FileName=$java;$start.UseShellExecute=$false;$start.CreateNoWindow=$true
    $start.WorkingDirectory=$runDirectory;$start.RedirectStandardOutput=$true;$start.RedirectStandardError=$true
    foreach($argument in $javaArguments){$start.ArgumentList.Add([string]$argument)}
    foreach($name in @('TEMP','TMP','TMPDIR')){$start.Environment[$name]=$scratch}
    # Ambient Java injection could override storage flags or load agents.
    foreach($name in @('_JAVA_OPTIONS','JAVA_TOOL_OPTIONS','JDK_JAVA_OPTIONS')){$null=$start.Environment.Remove($name)}
    $stdout=[IO.File]::Open((Join-Path $runDirectory 'stdout.log'),[IO.FileMode]::CreateNew)
    $stderr=[IO.File]::Open((Join-Path $runDirectory 'stderr.log'),[IO.FileMode]::CreateNew)
    Write-Host "Ghidra $($request.installed_ghidra_version): $($request.mode), $($normalized.Count) query value(s). Invocation: $requestPath"
    $process=[Diagnostics.Process]::new();$process.StartInfo=$start
    if(-not $process.Start()){throw 'Java failed to start.'}
    $started=$true
    $outCopy=$process.StandardOutput.BaseStream.CopyToAsync($stdout)
    $errCopy=$process.StandardError.BaseStream.CopyToAsync($stderr)
    $process.WaitForExit()
    [Threading.Tasks.Task]::WaitAll([Threading.Tasks.Task[]]@($outCopy,$errCopy))
    Write-NewJson (Join-Path $runDirectory 'completion.json') @{exit_code=$process.ExitCode;completed_utc=[DateTime]::UtcNow.ToString('o');output_exists=(Test-Path -LiteralPath $OutputPath)}
    if($process.ExitCode -ne 0){throw "Ghidra exited with $($process.ExitCode). Preserved logs: $runDirectory"}
    # Headless can return zero after a script error; require its complete result.
    if(-not (Test-Path -LiteralPath $OutputPath)){throw "Exporter produced no result. Preserved logs: $runDirectory"}
    $result=Get-Content -LiteralPath $OutputPath -Raw -Encoding utf8 | ConvertFrom-Json
    if($result.executable_sha256 -ne $sha -or $result.ghidra_version -ne $request.installed_ghidra_version){throw "Mismatched Ghidra result: $OutputPath"}
    if($memberSearch){
        if($result.schema -ne 'bc2.ghidra-member-search.v1' -or $result.status -notin @('search_complete','search_truncated') -or
           @($result.matches).Count -gt $MaxMatches -or @($result.functions).Count -gt $MaxFunctions -or
           $result.instructions_examined -gt $MaxInstructions){throw "Incomplete or mismatched member search result: $OutputPath"}
    }elseif($result.schema -ne 'bc2.ghidra-functions.v1' -or $result.status -ne 'export_complete' -or
       @($result.functions).Count -ne $normalized.Count){throw "Incomplete or mismatched Ghidra result: $OutputPath"}
    if(-not $cached){
        if(-not (Test-Path -LiteralPath $projectFile)){throw 'Ghidra did not save the analyzed project.'}
        Write-NewJson $manifestPath @{schema='bc2.ghidra-analysis.v1';executable_sha256=$sha;
            ghidra_version=$result.ghidra_version;jdk_version=$result.java_version;program_name=$result.program_name;
            completed_utc=[DateTime]::UtcNow.ToString('o');invocation_path=$requestPath;initial_output_path=$OutputPath}
    }
    if($memberSearch){
        [pscustomobject]@{output_path=$OutputPath;invocation_path=$requestPath;executable_sha256=$sha;mode=$request.mode;
            status=$result.status;instructions_examined=$result.instructions_examined;matches=@($result.matches).Count;
            functions=@($result.functions).Count;stop_reason=$result.stop_reason;next_scan_address=$result.next_scan_address}
    }else{
        [pscustomobject]@{output_path=$OutputPath;invocation_path=$requestPath;executable_sha256=$sha;mode=$request.mode;
            functions=@($result.functions | Select-Object requested_address,function_address,status)}
    }
}finally{
    if($process){
        try{if($started -and -not $process.HasExited){$process.Kill($true);$process.WaitForExit()}}
        catch{Write-Warning "Unable to finish owned Ghidra process cleanup: $($_.Exception.Message)"}
        finally{$process.Dispose()}
    }
    if($stdout){$stdout.Dispose()};if($stderr){$stderr.Dispose()}
    if($token){$token.Dispose()};if($source){$source.Dispose()}
    foreach($name in $savedEnvironment.Keys){[Environment]::SetEnvironmentVariable($name,$savedEnvironment[$name],'Process')}
}
