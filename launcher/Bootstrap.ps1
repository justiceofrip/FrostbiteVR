#requires -Version 5.1
# First-run installer. Downloads only a hash-pinned official portable PowerShell
# when no compatible copy exists. All scratch and tools stay on the chosen drive.
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot 'Launcher.Storage.ps1')
Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.IO.Compression.FileSystem
$root=Select-FvrInstallFolder
if(-not $root){return}
Assert-FvrFreeSpace $root 1MB
$scratch=Set-FvrProcessStorage $root
$pwsh=$null
foreach($candidate in @(Get-Command pwsh.exe -CommandType Application -ErrorAction SilentlyContinue)){
    $v=& $candidate.Source -NoProfile -NonInteractive -Command '$PSVersionTable.PSVersion.ToString()'
    if($LASTEXITCODE -eq 0 -and [version]$v -ge [version]'7.5'){$pwsh=$candidate.Source;break}
}
if(-not $pwsh){
    $manifest=Get-Content -LiteralPath (Join-Path $PSScriptRoot 'bootstrap.json') -Raw|ConvertFrom-Json
    $tool=$manifest.powershell
    if($manifest.schema -ne 'bc2vr.bootstrap.v1' -or $tool.version -notmatch '^7\.5\.\d+$' -or $tool.sha256 -notmatch '^[a-f0-9]{64}$' -or $tool.bytes -le 0 -or $tool.bytes -gt 150MB){throw 'Invalid portable PowerShell manifest.'}
    $expected='https://github.com/PowerShell/PowerShell/releases/download/v'+$tool.version+'/PowerShell-'+$tool.version+'-win-x64.zip'
    if($tool.url -cne $expected){throw 'Portable PowerShell must come from its official release.'}
    $dest=Join-Path $root ('tools/powershell-'+$tool.version);$pwsh=Join-Path $dest 'pwsh.exe'
    $receipt=Join-Path $dest '.bc2vr-archive-sha256'
    if(-not(Test-Path -LiteralPath $pwsh) -or -not(Test-Path -LiteralPath $receipt) -or (Get-Content -LiteralPath $receipt -Raw).Trim() -ne $tool.sha256){
        if(Test-Path -LiteralPath $dest){throw 'Portable PowerShell folder is incomplete or changed. Choose a fresh install folder.'}
        # Bounded archive plus its verified 512 MiB maximum expansion coexist.
        Assert-FvrFreeSpace $root ([long]$tool.bytes+512MB)
        $zip=Join-Path $scratch ('powershell-'+[guid]::NewGuid().ToString('N')+'.zip')
        [Net.ServicePointManager]::SecurityProtocol=[Net.SecurityProtocolType]::Tls12
        try {
            Invoke-WebRequest -UseBasicParsing -Uri $tool.url -OutFile $zip
            if((Get-Item -LiteralPath $zip).Length -ne $tool.bytes -or (Get-FileHash -LiteralPath $zip -Algorithm SHA256).Hash.ToLowerInvariant() -ne $tool.sha256){throw 'Portable PowerShell download failed verification.'}
            $archive=[IO.Compression.ZipFile]::OpenRead($zip)
            try {
                [long]$total=0
                foreach($entry in $archive.Entries){
                    $relative=$entry.FullName.Replace('\','/')
                    if($relative.StartsWith('/') -or $relative.Contains(':') -or @($relative.Split('/')|Where-Object{$_ -eq '..'}).Count){throw 'Unsafe path in portable tool archive.'}
                    $total+=$entry.Length;if($total -gt 512MB){throw 'Portable tool expands beyond its budget.'}
                }
            }finally{$archive.Dispose()}
            [IO.Compression.ZipFile]::ExtractToDirectory($zip,$dest)
            [IO.File]::WriteAllText($receipt,$tool.sha256)
        }finally{if(Test-Path -LiteralPath $zip){Remove-Item -LiteralPath $zip -Force}}
    }
}
& $pwsh -NoProfile -NonInteractive -ExecutionPolicy Bypass -STA -File (Join-Path $PSScriptRoot 'Install-Package.ps1') -InstallRoot $root
if($LASTEXITCODE){throw 'BC2 VR installation failed.'}
[Windows.Forms.MessageBox]::Show('BC2 VR is installed. Open BC2 VR from your desktop, choose the game folder, connect your headset through its PC runtime, then launch.','BC2 VR')|Out-Null
