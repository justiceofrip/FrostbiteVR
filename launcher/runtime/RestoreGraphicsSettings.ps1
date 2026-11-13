#requires -Version 7.5
param([Parameter(Mandatory)][string]$StateRoot)
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot '../app/Launcher.Core.ps1')
. (Join-Path $PSScriptRoot 'Runtime.Core.ps1')
. (Join-Path $PSScriptRoot 'GraphicsSettings.ps1')
$null=Set-FvrLauncherStorage (Split-Path (Split-Path $StateRoot -Parent) -Parent)
do {
    $result=Restore-Bc2GraphicsSettingsLease $StateRoot
    if($result.status -in @('none','restored')){$result|ConvertTo-Json -Depth 6;exit 0}
    if($result.reason -notin @('game_alive','relevant_game_alive','supervisor_alive','game_identity_unknown','supervisor_identity_unknown','lease_busy')){
        $result|ConvertTo-Json -Depth 6;exit 2
    }
    # Lifetime can exceed Stop VR by hours. A crash leaves the same durable
    # journal for admission/recovery on the next normal launcher invocation.
    Start-Sleep -Seconds 2
} while($true)
