#requires -Version 7.5
param(
    [string]$CorePath = (Join-Path (Split-Path $PSScriptRoot -Parent) 'launcher/Launcher.Core.ps1'),
    [string]$ScratchRoot = (Join-Path (Split-Path $PSScriptRoot -Parent) 'test-temp')
)
$ErrorActionPreference='Stop'
. $CorePath
$scratchBase=[IO.Path]::GetFullPath($ScratchRoot).TrimEnd('\','/')+[IO.Path]::DirectorySeparatorChar
$testRoot=Join-Path $scratchBase ('launcher-path-safety-'+[guid]::NewGuid().ToString('N'))
$null=[IO.Directory]::CreateDirectory($testRoot)
$saved=@{};foreach($key in 'TEMP','TMP','TMPDIR'){$saved[$key]=[Environment]::GetEnvironmentVariable($key,'Process');[Environment]::SetEnvironmentVariable($key,$testRoot,'Process')}
$script:checks=0;$script:lookupPaths=[Collections.Generic.List[string]]::new()
$script:renamePath=$null;$script:renamedPath=$null;$script:failurePath=$null;$script:lookupFailure=$null
$links=[Collections.Generic.List[string]]::new()
function Check([bool]$Good,[string]$Name){if(-not $Good){throw "FAILED: $Name"};$script:checks++;Write-Host "PASS $Name"}
function Rejection([scriptblock]$Work){try{& $Work|Out-Null}catch{return $_};throw 'Expected path guard to reject.'}
# Intercept only the filesystem lookup boundary. Renaming is real and happens
# immediately before Get-Item, including after the old guard's Test-Path.
function Get-Item {
    [CmdletBinding()]
    param([string]$LiteralPath,[switch]$Force)
    $script:lookupPaths.Add($LiteralPath)
    if($LiteralPath -eq $script:renamePath){
        [IO.File]::Move($LiteralPath,$script:renamedPath)
        $script:renamePath=$null
    }
    if($LiteralPath -eq $script:failurePath){
        $category=if($script:lookupFailure -is [UnauthorizedAccessException]){[Management.Automation.ErrorCategory]::PermissionDenied}else{[Management.Automation.ErrorCategory]::ReadError}
        $PSCmdlet.WriteError([Management.Automation.ErrorRecord]::new($script:lookupFailure,'FixtureLookupFailure',$category,$LiteralPath))
        return
    }
    Microsoft.PowerShell.Management\Get-Item @PSBoundParameters
}
try{
    Write-Host ("PowerShell {0}; core SHA256 {1}" -f $PSVersionTable.PSVersion,(Get-FileHash -LiteralPath $CorePath -Algorithm SHA256).Hash)
    $regular=Join-Path $testRoot 'regular.txt';[IO.File]::WriteAllText($regular,'fixture',[Text.UTF8Encoding]::new($false))
    Assert-FvrNoLinks $regular;Check $true 'regular file is accepted'
    Assert-FvrNoLinks $testRoot;Check $true 'regular directory is accepted'
    Assert-FvrNoLinks (Join-Path $testRoot 'missing.txt');Check $true 'missing leaf is accepted'
    Assert-FvrNoLinks (Join-Path $testRoot 'absent/deeper/missing.txt');Check $true 'missing parent chain is accepted'

    $leaf=Join-Path $testRoot 'state.json.tmp';[IO.File]::WriteAllText($leaf,'fixture',[Text.UTF8Encoding]::new($false))
    $script:renamePath=$leaf;$script:renamedPath=Join-Path $testRoot 'state.json'
    $script:lookupPaths.Clear()
    Assert-FvrNoLinks $leaf
    Check ([IO.File]::Exists($script:renamedPath) -and -not [IO.File]::Exists($leaf)) 'leaf renamed at lookup boundary does not abort guard'
    Check (@($script:lookupPaths|Where-Object {$_ -eq $leaf}).Count -eq 1) 'vanished leaf has exactly one item lookup'
    Check ($script:lookupPaths.Contains($testRoot) -and $script:lookupPaths.Contains([IO.Path]::GetPathRoot($testRoot))) 'vanished leaf still checks parents through drive root'

    $target=Join-Path $testRoot 'target';$null=[IO.Directory]::CreateDirectory($target)
    $link=Join-Path $testRoot 'linked';$null=New-Item -ItemType Junction -Path $link -Target $target;$links.Add($link)
    $errorRecord=Rejection {Assert-FvrNoLinks $link}
    Check ($errorRecord.Exception.Message -eq "Linked install path refused: $link") 'existing junction is refused'
    $errorRecord=Rejection {Assert-FvrNoLinks (Join-Path $link 'missing/deeper.txt')}
    Check ($errorRecord.Exception.Message -eq "Linked install path refused: $link") 'missing leaf and parent cannot hide linked ancestor'

    $linkedLeaf=Join-Path $link 'state.json.tmp';[IO.File]::WriteAllText($linkedLeaf,'fixture',[Text.UTF8Encoding]::new($false))
    $script:renamePath=$linkedLeaf;$script:renamedPath=Join-Path $target 'state.json'
    $errorRecord=Rejection {Assert-FvrNoLinks $linkedLeaf}
    Check ([IO.File]::Exists($script:renamedPath) -and $errorRecord.Exception.Message -eq "Linked install path refused: $link") 'vanished leaf cannot hide linked ancestor'

    # Simulated provider failures are nonterminating unless the production
    # lookup explicitly requests Stop. Neither error may be treated as missing.
    foreach($failure in @([UnauthorizedAccessException]::new('fixture access denied'),[IO.IOException]::new('fixture I/O failure'))){
        $script:failurePath=$regular;$script:lookupFailure=$failure
        $errorRecord=Rejection { $ErrorActionPreference='Continue';Assert-FvrNoLinks $regular }
        Check ($errorRecord.Exception -eq $failure) ("lookup preserves fatal "+$failure.GetType().Name)
        $script:failurePath=$testRoot
        $errorRecord=Rejection { $ErrorActionPreference='Continue';Assert-FvrNoLinks (Join-Path $testRoot 'missing.txt') }
        Check ($errorRecord.Exception -eq $failure) ("missing leaf preserves fatal ancestor "+$failure.GetType().Name)
    }
    $script:failurePath=$null;$script:lookupFailure=$null
    Write-Host "$script:checks launcher path-safety checks passed; deterministic filesystem tests only."
}finally{
    $script:failurePath=$null;$script:renamePath=$null
    foreach($link in $links){[IO.Directory]::Delete($link)}
    foreach($key in $saved.Keys){[Environment]::SetEnvironmentVariable($key,$saved[$key],'Process')}
    $cleanup=(Resolve-Path -LiteralPath $testRoot).ProviderPath
    if(-not $cleanup.StartsWith($scratchBase,[StringComparison]::OrdinalIgnoreCase)){throw 'Refusing fixture cleanup outside scratch root.'}
    Remove-Item -LiteralPath $cleanup -Recurse -Force
}
