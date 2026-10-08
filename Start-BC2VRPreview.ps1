[CmdletBinding()]
param([switch]$ExperimentalMagazineReload,[switch]$ExperimentalBodyInventory,[switch]$ExperimentalBoatHeadAim,[switch]$ExperimentalBoatHeadFire,[string]$Python)
$ErrorActionPreference='Stop'
if($ExperimentalBoatHeadFire -and -not $ExperimentalBoatHeadAim){throw 'ExperimentalBoatHeadFire requires ExperimentalBoatHeadAim'}
# Accepted base features only. Experiments remain explicit and default off.
$arguments=@{SightFlip=$true;PhysicalReload=$true}
if($ExperimentalMagazineReload){$arguments.MagazineReload=$true}
if($ExperimentalBodyInventory){$arguments.BodyInventory=$true}
if($ExperimentalBoatHeadAim){$arguments.BoatHeadAim=$true}
if($ExperimentalBoatHeadFire){$arguments.BoatHeadFire=$true}
if($Python){$arguments.Python=$Python}
& (Join-Path $PSScriptRoot 'Start-BC2VRSession.ps1') @arguments
