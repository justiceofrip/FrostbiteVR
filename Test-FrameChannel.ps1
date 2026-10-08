[CmdletBinding()]
param([switch]$Staged,[switch]$FrameBridge,[switch]$LegacyRelay,[switch]$LegacyIpc)
if($LegacyRelay -and $LegacyIpc){throw "Select one legacy mode"}
if($LegacyRelay -or $LegacyIpc){$FrameBridge=$true}
if($Staged -and $FrameBridge){throw "Select one producer mode"}
$ErrorActionPreference='Stop'
$PSNativeCommandUseErrorActionPreference=$false
$hostExe=Join-Path $PSScriptRoot 'build\x64\BC2IpcProbe.exe'
$producerExe=Join-Path $PSScriptRoot 'build\x86\BC2IpcProbe.exe'
$probeArguments=@('--producer-exe',$producerExe)
if($Staged){$probeArguments+='--staged'}
if($LegacyIpc){$probeArguments+='--frame-bridge-legacy-ipc'}elseif($LegacyRelay){$probeArguments+='--frame-bridge-legacy'}elseif($FrameBridge){$probeArguments+='--frame-bridge'}
$resultText=& $hostExe @probeArguments
if($LASTEXITCODE -ne 0){throw "Frame-channel GPU probe failed ($LASTEXITCODE)."}
$result=$resultText | ConvertFrom-Json
$result | Add-Member -NotePropertyName completed_utc -NotePropertyValue ([DateTime]::UtcNow.ToString('o'))
$result | Add-Member -NotePropertyName host_sha256 -NotePropertyValue (Get-FileHash -LiteralPath $hostExe).Hash
$result | Add-Member -NotePropertyName producer_sha256 -NotePropertyValue (Get-FileHash -LiteralPath $producerExe).Hash
$result | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $PSScriptRoot $(if($LegacyIpc){'reports\ipc-legacy-textures-cross-architecture.json'}elseif($LegacyRelay){'reports\ipc-legacy-relay-cross-architecture.json'}elseif($FrameBridge){'reports\ipc-frame-bridge-cross-architecture.json'}elseif($Staged){'reports\ipc-staged-cross-architecture.json'}else{'reports\ipc-cross-architecture.json'}))
$result | ConvertTo-Json
