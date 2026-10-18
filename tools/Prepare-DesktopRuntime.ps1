#requires -Version 7.5
param([Parameter(Mandatory)][string]$BuildReceipt,[Parameter(Mandatory)][ValidatePattern('^[a-f0-9]{64}$')][string]$ReceiptSha256,
      [Parameter(Mandatory)][string]$OutputPath,[string]$Version='0.1.4',[string]$Python='python.exe',[string]$DependencyManifest)
$ErrorActionPreference='Stop'
$repo=Split-Path $PSScriptRoot -Parent
. (Join-Path $repo 'tools/StorageBudget.ps1')
$null=Assert-FvrStorageBudget -RepositoryRoot $repo -OutputPath $OutputPath -Operation Build -PlannedBytes 512MB -RawCaptureBytes 0
$arguments=@('-B',(Join-Path $PSScriptRoot 'prepare_desktop_runtime.py'),'--repo',$repo,'--receipt',$BuildReceipt,'--receipt-sha256',$ReceiptSha256,'--output',$OutputPath,'--version',$Version)
if($DependencyManifest){$arguments+=@('--dependencies',$DependencyManifest)}
& $Python @arguments
if($LASTEXITCODE){throw 'Portable runtime preparation failed.'}
