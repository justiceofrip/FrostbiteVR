# Successor admission only. All completed-build recipes and evidence stay immutable.
$originalCommon=Join-Path $PSScriptRoot 'Common.ps1'
if((Get-FileHash -LiteralPath $originalCommon -Algorithm SHA256).Hash.ToLowerInvariant() -cne '18a1c11ffd9bb058c6ab4e35c3751a9fa3c0a71e8d3e63a64bd70f2b2a82cb47'){throw 'Original completed-build validator changed'}
. $originalCommon

function Assert-ReviewedOperationHeader($b,$n,$path,$sha) {
 $reviewPath=Join-Path $RecipeRoot 'operation-header-review.json';$review=Load $reviewPath
 if($review.schema -cne 'fvr.runtime02.magazine.operation-header-review.v1' -or $sha -cne '273b0504dfa002ead9a5ee7b92edc2ff7867875cf84e95857cb255cbec17625c' -or [IO.Path]::GetFullPath($path) -ine $review.build.path -or $review.build.sha256 -cne $sha){throw 'Only the exact completed diagnostic is covered by this review'}
 foreach($pin in @($review.build,$review.original_workflow,$review.original_common,$review.original_run,$review.original_bind_failure)+@($review.successors)){CheckPin $pin}
 if($review.original_common.sha256 -cne '18a1c11ffd9bb058c6ab4e35c3751a9fa3c0a71e8d3e63a64bd70f2b2a82cb47' -or $review.original_workflow.sha256 -cne 'ba1a7904dfc38a2665954d86dc0077b0910a2d5c4d14a93368fb516ad522779e' -or $review.original_run.sha256 -cne '290662a6813c0d8211c15055a10e001e294cd6f09c4a54e7ee77149ea5dc6c2b'){throw 'Review does not bind the original completed recipes'}
 $successorNames=@($review.successors|ForEach-Object {[IO.Path]::GetFileName($_.path)}|Sort-Object)
 if(($successorNames -join '|') -cne 'BindReady.ps1|CommonReviewed.ps1|RunReady.ps1'){throw 'Exact three successor recipes required'}
 foreach($pin in $review.successors){if([IO.Path]::GetFullPath((Split-Path $pin.path)) -ine $RecipeRoot){throw 'Successor recipe outside reviewed directory'}}
 $oldRun=[IO.File]::ReadAllText($review.original_run.path)
 $oldImport=". (Join-Path `$PSScriptRoot 'Common.ps1')";$newImport=". (Join-Path `$PSScriptRoot 'CommonReviewed.ps1')"
 if([regex]::Matches($oldRun,[regex]::Escape($oldImport)).Count -ne 1 -or [IO.File]::ReadAllText((Join-Path $RecipeRoot 'RunReady.ps1')) -cne $oldRun.Replace($oldImport,$newImport)){throw 'RunReady must differ only in its one Common import'}
 $bindingPath=Join-Path $EvidenceRoot 'runtime-binding.json'
 if(Test-Path -LiteralPath $bindingPath){
  $bound=Load $bindingPath
  if(-not $bound.operation_header_review -or $bound.operation_header_review.path -ine $reviewPath){throw 'Runtime binding lacks this exact operation-header review'}
  CheckPin $bound.operation_header_review
 }
 CheckPin $n.normal_cache.x86.operation_config;CheckPin $b.generated_operation_header
 CheckPin $n.reviewed_header;CheckPin $b.reviewed_header
 if($b.reviewed_header.sha256 -cne $n.reviewed_header.sha256 -or $b.reviewed_header.bytes -ne $n.reviewed_header.bytes){throw 'Reviewed capability bytes changed'}
 if($b.generated_operation_header.sha256 -cne '3ef7981f675e5886125e8550c50eeb23c7b4f0e58a6d0ee90e67671a00737718' -or $review.generated_operation_header.sha256 -cne $b.generated_operation_header.sha256){throw 'Actual diagnostic generated-header pin differs'}
 # Latin1 is a lossless byte mapping: no newline/encoding normalization is allowed.
 $normalText=[Text.Encoding]::Latin1.GetString([IO.File]::ReadAllBytes($n.normal_cache.x86.operation_config.path))
 $actualText=[Text.Encoding]::Latin1.GetString([IO.File]::ReadAllBytes($b.generated_operation_header.path))
 $oldPath=$n.reviewed_header.path.Replace('\','/');$newPath=$b.reviewed_header.path.Replace('\','/')
 $headerMacro='(?m)^#define FVR_BC2_NATIVE_OPERATION_CAPABILITIES_HEADER "'+[regex]::Escape($oldPath)+'"\r?$'
 if([regex]::Matches($normalText,$headerMacro).Count -ne 1 -or [regex]::Matches($normalText,[regex]::Escape($oldPath)).Count -ne 1){throw 'Normal header must contain exactly one reviewed capability path'}
 if($actualText -cne $normalText.Replace($oldPath,$newPath)){throw 'Generated header differs beyond the one reviewed capability path'}
 $sourceMacro='(?m)^#define FVR_BC2_NATIVE_OPERATION_BUILD_SHA256 "([a-f0-9]{64})"\r?$'
 foreach($text in @($normalText,$actualText)){
  $matches=[regex]::Matches($text,$sourceMacro)
  if($matches.Count -ne 1 -or $matches[0].Groups[1].Value -cne $NativeSha -or $b.source_sha256 -cne $NativeSha){throw 'Generated source-hash macro changed'}
 }
 # collect() captured the diagnostic configuration before normal restoration.
 # Its recorded shared path is provenance; hash the immutable diagnostic copy.
 $p=$b.operation_provenance
 if($p.configured -ne $true -or $p.compiled_configuration_verified -ne $true -or $p.native_admission_granted_by_freezer -ne $false -or $p.source_sha256 -cne $NativeSha -or $p.generated_configuration.path -ine $n.normal_cache.x86.operation_config.path -or $p.generated_configuration.sha256 -cne $b.generated_operation_header.sha256){throw 'Collected generated-header provenance differs'}
 if($p.reviewed_header.path -ine $b.reviewed_header.path -or $p.reviewed_header.sha256 -cne $b.reviewed_header.sha256 -or $p.source_receipt.path -ine $b.source_receipt.path -or $p.source_receipt.sha256 -cne $b.source_receipt.sha256){throw 'Collected capability/source provenance differs'}
 foreach($pin in @($p.reviewed_header,$p.source_receipt,$p.verification_recipe,$p.build_provenance_recipe)){CheckPin $pin}
}

# Preserve the exact original function body except its one defective predicate.
$tokens=$null;$errors=$null
$originalAst=[Management.Automation.Language.Parser]::ParseFile($originalCommon,[ref]$tokens,[ref]$errors)
if($errors.Count){throw 'Original validator parse failed'}
$functions=@($originalAst.FindAll({param($node) $node -is [Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -ceq 'Assert-CompletedBuild'},$true))
if($functions.Count -ne 1){throw 'Expected exactly one original completed-build validator'}
$definition=$functions[0].Extent.Text
$before=' Check $b.generated_operation_header.path $n.normal_cache.x86.operation_config.sha256'
$after=' Assert-ReviewedOperationHeader $b $n $path $sha'
if([regex]::Matches($definition,[regex]::Escape($before)).Count -ne 1){throw 'Original generated-header predicate changed'}
. ([ScriptBlock]::Create($definition.Replace($before,$after)))
