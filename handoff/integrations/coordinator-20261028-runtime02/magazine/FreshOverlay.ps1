# Pinned existing invalidation/compile-evidence algorithm, with lease fences.
$ancestor='G:\Unity\VRMMO\bc2vr-recovery\performance436\build-overlay-fresh-r50-prep\FreshOverlay.ps1'
Check $ancestor '6a7d5bde817db79f8c87d3e22e5fa03ed3138f5ab50deec45ba46dccd98b31d0'
$code=[IO.File]::ReadAllText($ancestor)
$code=$code.Replace('if($before){Remove-Item -LiteralPath $path}','if($before){Assert-Lease;Remove-Item -LiteralPath $path}')
$code=$code.Replace('  & cmake --build $build','  Assert-Lease; & cmake --build $build')
. ([ScriptBlock]::Create($code))
