#requires -Version 7.5
[CmdletBinding()]
param([string]$Version='0.1.0', [string]$OutputPath,
      [string]$RuntimeRoot, [string]$RuntimeEntry='Entry.ps1',
      [string]$RuntimeFiles, [string]$Repository='justiceofrip/BC2VR',
      [string]$BootstrapManifest, [switch]$InstallerBundle)
$ErrorActionPreference='Stop'
$repo=Split-Path $PSScriptRoot -Parent
. (Join-Path $repo 'launcher/Launcher.Core.ps1')
. (Join-Path $repo 'tools/StorageBudget.ps1')
$null=Assert-FvrVersion $Version
if(-not $OutputPath){$OutputPath=Join-Path $repo 'dist/desktop-launcher'}
$OutputPath=[IO.Path]::GetFullPath($OutputPath)
Assert-FvrNoLinks $OutputPath
$sources=[Collections.Generic.List[object]]::new()
foreach($name in @('Launcher.ps1','Worker.ps1','Launcher.Core.ps1','Launcher.Feedback.ps1','Launcher.Settings.ps1','Launcher.Storage.ps1')){$sources.Add(@{source=(Join-Path $repo "launcher/$name");path="app/$name"})}
$sources.Add(@{source=(Join-Path $repo 'LICENSE');path='LICENSE'})
$runtimePath=$null
if($RuntimeRoot){
    # Explicit reviewed file list only: never recursively package a development
    # tree, local.json, game assets, dumps, PDBs or diagnostic captures.
    if(-not $RuntimeFiles){throw 'Supply a reviewed JSON array of relative runtime files with -RuntimeFiles.'}
    $allowlist=Read-FvrJson $RuntimeFiles
    if($allowlist -isnot [array] -or $allowlist.Count -eq 0){throw 'Runtime file list must be a nonempty JSON array.'}
    foreach($name in $allowlist){
        $source=Get-FvrChild $RuntimeRoot $name
        if($name -match '(?i)(^|/)(BFBC2Game\.exe|local\.json|settings\.json)$|\.(fbrb|dbx|res|fvrprop|fvrmat|pdb|dmp|rgba|mp4|mkv)$'){throw "Private/game-derived file cannot ship: $name"}
        Assert-FvrNoLinks $source
        $sources.Add(@{source=$source;path=('runtime/'+$name)})
    }
    if($RuntimeEntry -notin $allowlist){throw 'Runtime entry is not in the reviewed list.'}
    $runtimePath='runtime/'+$RuntimeEntry
}
[long]$payloadBytes=0
foreach($source in $sources){$payloadBytes+=(Get-Item -LiteralPath $source.source).Length}
if($payloadBytes -gt 1GB){throw 'Runtime exceeds the update package size limit.'}
$admission=Assert-FvrStorageBudget -RepositoryRoot $repo -OutputPath $OutputPath -Operation Build -PlannedBytes ([long]($payloadBytes*4+4MB)) -RawCaptureBytes 0
$null=New-Item -ItemType Directory -Path $OutputPath -Force
$asset="BC2VR-$Version-win64.zip";$zip=Join-Path $OutputPath $asset
if(Test-Path -LiteralPath $zip){throw 'Version package already exists; use a new version or a new output directory.'}
$stage=Join-Path $OutputPath ('stage-'+[guid]::NewGuid().ToString('N'))
try{
    $null=New-Item -ItemType Directory -Path $stage
    $entries=@(foreach($source in $sources){
        $dest=Get-FvrChild $stage $source.path
        $null=New-Item -ItemType Directory -Path (Split-Path $dest -Parent) -Force
        Copy-Item -LiteralPath $source.source -Destination $dest
        @{path=$source.path;sha256=(Get-FileHash -LiteralPath $dest -Algorithm SHA256).Hash.ToLowerInvariant()}
    })
    $runtimeInfo=if($RuntimeRoot -and (Test-Path -LiteralPath (Join-Path $RuntimeRoot 'runtime.json'))){Read-FvrJson (Join-Path $RuntimeRoot 'runtime.json')}else{$null}
    Write-FvrJson (Join-Path $stage 'package.json') @{schema='fvr.desktop.package.v1';version=$Version;runtime_entry=$runtimePath;files=$entries;
        release_repository=$Repository;feedback_repository=$Repository;runtime_version=$Version;
        runtime_build=$(if($runtimeInfo){$runtimeInfo.source_sha256}else{$null});source_revision=$(if($runtimeInfo){$runtimeInfo.source_sha256}else{$null})}
    $null=Test-FvrPackage $stage $Version
    $expanded=[long](Get-ChildItem -LiteralPath $stage -Recurse -File|Measure-Object Length -Sum).Sum
    [IO.Compression.ZipFile]::CreateFromDirectory($stage,$zip,[IO.Compression.CompressionLevel]::Optimal,$false)
    $manifest=@{schema='fvr.desktop.release.v1';version=$Version;asset=$asset;bytes=(Get-Item -LiteralPath $zip).Length;unpacked_bytes=$expanded;sha256=(Get-FileHash -LiteralPath $zip -Algorithm SHA256).Hash.ToLowerInvariant()}
    Assert-FvrManifest $manifest
    Write-FvrJson (Join-Path $OutputPath 'bc2vr-update.json') $manifest
    $installerFiles=@('Install.cmd','Bootstrap.ps1','Install-Package.ps1','Install-BC2VRLauncher.ps1','Start-Launcher.ps1','Launcher.Core.ps1','Launcher.Settings.ps1','Launcher.Storage.ps1')
    foreach($file in $installerFiles){Copy-Item -LiteralPath (Join-Path $repo "launcher/$file") -Destination (Join-Path $OutputPath $file)}
    if($BootstrapManifest){Copy-Item -LiteralPath $BootstrapManifest -Destination (Join-Path $OutputPath 'bootstrap.json')}
    if($InstallerBundle){
        if(-not $BootstrapManifest){throw 'InstallerBundle requires a reviewed portable PowerShell bootstrap manifest.'}
        $setup=Join-Path $OutputPath "BC2VR-Setup-$Version.zip"
        if(Test-Path -LiteralPath $setup){throw 'Installer bundle already exists.'}
        $archive=[IO.Compression.ZipFile]::Open($setup,[IO.Compression.ZipArchiveMode]::Create)
        try {foreach($file in @($installerFiles)+@('bootstrap.json','bc2vr-update.json',$asset)){
            $null=[IO.Compression.ZipFileExtensions]::CreateEntryFromFile($archive,(Join-Path $OutputPath $file),$file,[IO.Compression.CompressionLevel]::Optimal)
        }}finally{$archive.Dispose()}
    }
    $admission|ConvertTo-Json -Depth 10|Set-Content -LiteralPath (Join-Path $OutputPath 'storage-admission.json')
    $manifest|ConvertTo-Json
}finally{Remove-FvrOwnedTree $OutputPath (Split-Path $stage -Leaf)}
