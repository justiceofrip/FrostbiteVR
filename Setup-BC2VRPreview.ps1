[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$GamePath)
$ErrorActionPreference='Stop'
if(-not [IO.Path]::IsPathRooted($GamePath)){throw 'GamePath must be an absolute installation folder.'}
$folder=[IO.Path]::GetFullPath($GamePath)
$game=Join-Path $folder 'BFBC2Game.exe'
if(-not (Test-Path -LiteralPath $game -PathType Leaf)){throw 'BFBC2Game.exe was not found in GamePath.'}
$features=Get-Content -LiteralPath (Join-Path $PSScriptRoot 'FEATURES.json') -Raw | ConvertFrom-Json
$hash=(Get-FileHash -LiteralPath $game -Algorithm SHA256).Hash.ToLowerInvariant()
if($hash -ne $features.supported_game.tested_sha256){throw "This preview has not verified that game executable (SHA256 $hash). No files were changed."}
$destination=Join-Path $PSScriptRoot 'config\local.json'
if(Test-Path -LiteralPath $destination){
    $old=Get-Content -LiteralPath $destination -Raw | ConvertFrom-Json
    if($old.game_path -ne $folder){throw 'A different local configuration already exists. Review config\local.json before changing the target.'}
} else {
    New-Item -ItemType Directory -Path (Split-Path -Parent $destination) -Force | Out-Null
    $json=[ordered]@{game_path=$folder} | ConvertTo-Json
    $stream=[IO.File]::Open($destination,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::None)
    try{$data=[Text.UTF8Encoding]::new($false).GetBytes($json);$stream.Write($data,0,$data.Length)}finally{$stream.Dispose()}
}
Write-Output 'Game path configured. Load campaign on the monitor, connect the headset, then run Start-BC2VRPreview.ps1.'
