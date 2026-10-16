[CmdletBinding()]
param()
$ErrorActionPreference='Stop'
$repository=Split-Path -Parent $PSScriptRoot
$originalEnvironment=@{}
foreach($key in @('TEMP','TMP','TMPDIR','PYTHONDONTWRITEBYTECODE')){$originalEnvironment[$key]=[Environment]::GetEnvironmentVariable($key,'Process')}
. (Join-Path $repository 'tools/StorageBudget.ps1')
$checks=[Collections.Generic.List[string]]::new()
function Check([bool]$Condition,[string]$Name){if(-not $Condition){throw "FAIL: $Name"};$checks.Add($Name)}
function Refuses([scriptblock]$Action,[string]$Pattern,[string]$Name){
    $failure=$null
    try{& $Action | Out-Null}catch{$failure=$_.Exception.Message}
    Check ($null -ne $failure -and $failure -match $Pattern) $Name
}

$fixture=Join-Path $repository ('test-temp/storage-budget-'+[guid]::NewGuid().ToString('N'))
try{
    $null=New-Item -ItemType Directory -Path (Join-Path $fixture 'child')
    [IO.File]::WriteAllBytes((Join-Path $fixture 'one.bin'),[byte[]]::new(23))
    [IO.File]::WriteAllBytes((Join-Path $fixture 'child/two.bin'),[byte[]]::new(41))
    $usage=Get-FvrStorageUsage -Roots @($fixture,(Join-Path $fixture 'absent'))
    Check ($usage.logical_bytes -eq 64 -and $usage.files -eq 2 -and $usage.allocated_bytes -ge 0) 'real tiny-tree disk/logical accounting and absent roots'
    Check ($usage.bytes -eq $usage.allocated_bytes) 'legacy byte total aliases allocated storage'
    if([Environment]::OSVersion.Platform -eq [PlatformID]::Win32NT){
        Check ($usage.accounting -eq 'GetCompressedFileSizeW') 'actual Windows allocation query used'
        $direct=[FvrStorage.NativeSize]::Scan(@($fixture,(Join-Path $fixture 'absent')))
        $individual=@(Get-ChildItem -LiteralPath $fixture -File -Recurse | ForEach-Object {Get-FvrFileAllocatedBytes -File $_} | Measure-Object -Sum).Sum
        Check ($direct.allocated_bytes -eq $individual -and $direct.logical_bytes -eq 64 -and $direct.files -eq 2 -and $direct.roots.Count -eq 2) 'compiled traversal agrees with individual allocated-byte queries and preserves absent roots'
        Refuses {[FvrStorage.NativeSize]::Get((Join-Path $fixture 'missing.bin'),$false)} 'Cannot read allocated storage' 'native allocation errors refuse missing file'
        Check ([FvrStorage.NativeSize]::AllowedFileTag([uint32]2147483671)) 'WOF compression file tag allowed'
        Check (-not [FvrStorage.NativeSize]::AllowedFileTag([uint32]2684354572) -and -not [FvrStorage.NativeSize]::AllowedFileTag([uint32]2684354563)) 'symlink and junction tags rejected'
    }
    $roots=@(Get-FvrStorageRoots -RepositoryRoot $fixture -AdditionalRoots @((Join-Path $fixture 'build/nested'),(Join-Path $fixture 'build')))
    Check (@($roots | Where-Object {$_ -eq (Join-Path $fixture 'build')}).Count -eq 1) 'duplicate roots counted once'
    Check (-not ($roots -contains (Join-Path $fixture 'build/nested'))) 'nested roots counted once'
    $previewDiscovery=& {
        function Test-Path {param($LiteralPath,$PathType) return $true}
        function Get-ChildItem {param($LiteralPath,[switch]$Directory,$Filter,$ErrorAction)
            if(-not $Directory -or $Filter -ne 'bc2vr-preview-*'){throw 'Unexpected preview discovery query'}
            [pscustomobject]@{FullName='G:\mock-storage\bc2vr-preview-alpha'}
            [pscustomobject]@{FullName='G:\mock-storage\bc2vr-preview-beta'}
        }
        $found=@(Get-FvrStorageRoots -RepositoryRoot 'G:\mock-storage\FrostbiteVR' -AdditionalRoots 'G:\mock-storage\bc2vr-preview-alpha\build')
        return ($found -contains 'G:\mock-storage\bc2vr-preview-alpha' -and $found -contains 'G:\mock-storage\bc2vr-preview-beta' -and $found -notcontains 'G:\mock-storage\bc2vr-preview-alpha\build')
    }
    Check $previewDiscovery 'same-parent preview workspaces included and nested builds counted once'

    $incomplete=& {
        $ErrorActionPreference='Continue'
        $invalid=Join-Path $fixture 'one.bin'
        try{$null=Get-FvrStorageUsage -Roots @($fixture,$invalid);return $false}
        catch{return $_.Exception.Message.Contains($invalid)}
    }
    Check $incomplete 'invalid later root under Continue returns no partial inventory and names failed path'
    if([Environment]::OSVersion.Platform -eq [PlatformID]::Win32NT){
        $link=Join-Path $fixture 'linked-child'
        $null=New-Item -ItemType Junction -Path $link -Target (Join-Path $fixture 'child')
        try{Refuses {Get-FvrStorageUsage -Roots @($fixture)} 'linked directory' 'compiled traversal rejects a real nested directory junction'}
        finally{[IO.Directory]::Delete($link)}
    }

    # Mock only external disk observations. No scan of the real development
    # trees and no builds, game processes or captures are performed.
    $script:usageCalls=0;$script:volumeCalls=0;$script:generatedBytes=4GB
    $script:outputFree=100GB;$script:tempFree=100GB
    function Get-FvrStorageUsage {param([string[]]$Roots)
        $script:usageCalls++
        return [pscustomobject]@{bytes=$script:generatedBytes;allocated_bytes=$script:generatedBytes;logical_bytes=100GB;files=1;roots=$Roots}
    }
    function Get-FvrStorageVolume {param([string]$Path)
        $script:volumeCalls++
        if($Path.StartsWith('G:',[StringComparison]::OrdinalIgnoreCase)){return [pscustomobject]@{root='G:\';free_bytes=$script:outputFree}}
        return [pscustomobject]@{root='C:\';free_bytes=$script:tempFree}
    }
    $arguments=@{RepositoryRoot=$fixture;OutputPath='G:\unused-fvr-budget-fixture';PlannedBytes=1GB}
    $ok=Assert-FvrStorageBudget @arguments
    Check ($ok.schema -eq 'fvr.storage-admission.v1' -and $script:usageCalls -eq 1) 'normal admission returns explicit receipt'
    Check ($ok.generated.logical_bytes -gt $ok.max_generated_bytes -and $ok.generated.allocated_bytes -lt $ok.max_generated_bytes) 'compressed evidence budgets actual allocation, not logical bytes'
    Check ($ok.volumes.Count -eq 1 -and $ok.volumes[0].root -eq 'G:\') 'output and redirected project temporary volume checked once'
    $script:tempFree=400MB
    $redirected=Assert-FvrStorageBudget @arguments
    Check ($redirected.volumes.Count -eq 1 -and $env:TEMP.StartsWith($fixture) -and $env:TMP -eq $env:TEMP) 'low unused C space does not reject actual G-only writes'
    $script:tempFree=100GB;$script:outputFree=15GB
    Refuses {Assert-FvrStorageBudget @arguments} 'G:.*free' 'reserve excludes planned write headroom'
    $script:outputFree=100GB;$script:generatedBytes=40GB
    Refuses {Assert-FvrStorageBudget @arguments} 'total limit' 'combined generated budget rejects another run'
    $script:generatedBytes=39GB
    $boundary=Assert-FvrStorageBudget @arguments
    Check ($boundary.generated.bytes + $boundary.planned_bytes -eq 40GB) 'exact total boundary admitted'
    $script:usageCalls=0;$script:volumeCalls=0
    Refuses {Assert-FvrStorageBudget @arguments -RawCaptureBytes 513MB} 'per-run limit' 'oversized raw capture rejected'
    Check ($script:usageCalls -eq 0 -and $script:volumeCalls -eq 0) 'oversized capture short circuits before disk work'
    Refuses {Assert-FvrStorageBudget -RepositoryRoot $fixture -OutputPath $arguments.OutputPath -PlannedBytes 1MB -RawCaptureBytes 2MB} 'does not include' 'raw bytes must fit full plan'

    foreach($name in @('Build.ps1','Build-Checkpoint.ps1','Start-NativeTrace.ps1','Start-BC2VRSession.ps1')){
        $path=Join-Path $repository $name;$tokens=$null;$errors=$null
        $ast=[Management.Automation.Language.Parser]::ParseFile($path,[ref]$tokens,[ref]$errors)
        Check ($errors.Count -eq 0) "$name parses"
        $commands=@($ast.FindAll({param($node) $node -is [Management.Automation.Language.CommandAst]},$true))
        $guard=@($commands | Where-Object {$_.GetCommandName() -eq 'Assert-FvrStorageBudget'})
        Check ($guard.Count -eq 1) "$name calls guard"
        $earlyMutation=@($commands | Where-Object {$_.GetCommandName() -in @('New-Item','Copy-Item','Start-Process','cmake') -and $_.Extent.StartOffset -lt $guard[0].Extent.StartOffset})
        Check ($earlyMutation.Count -eq 0) "$name guards before output and process commands"
    }
    [pscustomobject]@{passed=$true;checks=$checks.Count;details=$checks.ToArray();native_or_headset_test=$false} | ConvertTo-Json -Depth 4
}finally{
    foreach($key in $originalEnvironment.Keys){[Environment]::SetEnvironmentVariable($key,$originalEnvironment[$key],'Process')}
    # Only this unique, resolved synthetic fixture is removed; no project
    # evidence, user data or sibling test directory is a cleanup target.
    $resolved=[IO.Path]::GetFullPath($fixture)
    $allowed=[IO.Path]::GetFullPath((Join-Path $repository 'test-temp')).TrimEnd('\')+'\'
    if(-not $resolved.StartsWith($allowed,[StringComparison]::OrdinalIgnoreCase) -or (Split-Path -Leaf $resolved) -notmatch '^storage-budget-[0-9a-f]{32}$'){throw 'Synthetic fixture cleanup path validation failed'}
    if(Test-Path -LiteralPath $resolved){Remove-Item -LiteralPath $resolved -Recurse -Force}
}
