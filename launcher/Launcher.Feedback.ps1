#requires -Version 7.5
# Offline report preparation only. The UI opens the issue form; nothing is posted.
function Protect-FvrFeedbackText([string]$Text) {
    $value=$Text -replace '(?im)^.*(?:authorization|password|passwd|api[_-]?key|token|cookie|secret|username|computername)["'']?\s*[:=].*$', '[credential line removed]'
    $value=$value -replace '(?i)(?:gh[pousr]_[A-Za-z0-9_]+|github_pat_[A-Za-z0-9_]+|Bearer\s+\S+)', '[credential removed]'
    $value=$value -replace '(?i)https?://[^\s"''<>]*\?[^\s"''<>]+', '[URL query removed]'
    # Remove the rest of an absolute-path line, including paths containing spaces
    # and JSON-escaped backslashes. Favor privacy over retaining a trailing value.
    $value=$value -replace '(?im)(?:[a-z]:[\\/]|\\\\)[^\r\n]*', '[local path removed]'
    $value=$value -replace '(?i)/(?:home|Users)/[^\s"''<>]+', '[personal path removed]'
    $value=$value -replace '(?i)\b[A-Z0-9._%+-]+@[A-Z0-9.-]+\.[A-Z]{2,}\b', '[email removed]'
    $value=$value -replace '\b7656119[0-9]{10}\b', '[Steam ID removed]'
    foreach($identity in @($env:USERNAME,$env:COMPUTERNAME)) {
        if($identity -and $identity.Length -ge 3){$value=[regex]::Replace($value,'(?i)(?<!\w)'+[regex]::Escape($identity)+'(?!\w)','[local identity removed]')}
    }
    return $value
}
function Get-FvrFeedbackMetadata($Package, $Settings) {
    $lines=[Collections.Generic.List[string]]::new()
    $lines.Add('BC2 VR PRE-ALPHA')
    $lines.Add('Launcher/package: '+(Assert-FvrVersion $Package.version).ToString())
    foreach($key in @('runtime_version','runtime_build','source_revision')) {
        if($Package.ContainsKey($key) -and $Package[$key]) {
            $value=Protect-FvrFeedbackText ([string]$Package[$key])
            $value=($value -replace '[\r\n]',' ').Trim()
            $lines.Add($key+': '+$value.Substring(0,[Math]::Min(128,$value.Length)))
        }
    }
    if(-not $Package.runtime_entry -and $Settings.local_backend) {
        $label=Protect-FvrFeedbackText ([string]$Settings.local_backend.label)
        $lines.Add('Runtime: local checkpoint '+($label -replace '[\r\n]',' ').Substring(0,[Math]::Min(128,$label.Length)))
    } elseif(-not $Package.ContainsKey('runtime_version')) {$lines.Add('Runtime: '+$(if($Package.runtime_entry){'bundled with this package'}else{'not installed'}))}
    return ($lines -join "`n")
}
function Get-FvrFeedbackDraft($Package, $Settings) {
    Set-FvrPackageRepositories $Package
    $metadata=Get-FvrFeedbackMetadata $Package $Settings
    $body=@"
$metadata

Headset / connection (Steam Link, cable, etc.):

What happened:

Steps to reproduce:
1.
2.

Expected result:

Weapon / mission (if relevant):

Optional: attach a reviewed BC2VR-diagnostics.zip from Report feedback.
Remove personal details before submitting. No diagnostics are uploaded automatically.
"@
    return @{body=$body;url=('https://github.com/'+$script:FvrFeedbackRepository+'/issues/new?title='+[uri]::EscapeDataString('[PRE-ALPHA] Brief issue description')+'&body='+[uri]::EscapeDataString($body))}
}
function Read-FvrFeedbackTail([string]$Path) {
    Assert-FvrNoLinks $Path
    if(-not (Test-Path -LiteralPath $Path -PathType Leaf)){return $null}
    $file=[IO.File]::Open($Path,'Open','Read',([IO.FileShare]::ReadWrite -bor [IO.FileShare]::Delete))
    try {
        $count=[int][Math]::Min(32768,$file.Length)
        $null=$file.Seek(-$count,[IO.SeekOrigin]::End)
        $bytes=[byte[]]::new($count);$read=0
        while($read -lt $count){$n=$file.Read($bytes,$read,$count-$read);if($n -eq 0){break};$read+=$n}
        if([Array]::IndexOf($bytes,[byte]0) -ge 0){return '[binary content omitted]'}
        return Protect-FvrFeedbackText ([Text.Encoding]::UTF8.GetString($bytes,0,$read))
    } finally {$file.Dispose()}
}
function New-FvrFeedbackBundle([string]$InstallRoot, $Package, $Settings) {
    $null=Set-FvrLauncherStorage $InstallRoot
    Assert-FvrFreeSpace $InstallRoot 2MB
    $data=Get-FvrChild $InstallRoot 'data'
    $folder=Get-FvrChild $data 'feedback';Assert-FvrNoLinks $folder
    $null=[IO.Directory]::CreateDirectory($folder)
    $parts=[Collections.Generic.List[string]]::new()
    $parts.Add((Get-FvrFeedbackMetadata $Package $Settings))
    $parts.Add('Prepared UTC: '+[DateTime]::UtcNow.ToString('o'))
    $parts.Add('Review this text before attaching its ZIP. Redaction is best effort. No upload was performed.')
    $sources=[Collections.Generic.List[object]]::new()
    $sources.Add(@{name='launcher-last.log';path=(Get-FvrChild $data 'launcher-last.log')})
    # Public runtime reports must remain below this installation's state root.
    # Never enumerate a user's disk, developer captures, assets or memory dumps.
    $runtime=Get-FvrChild $data 'runtime'
    foreach($name in @('launch-check.json','xr-selection.json','xr-preflight.stdout.log','xr-preflight.stderr.log')) {
        $sources.Add(@{name=$name;path=(Get-FvrChild $runtime $name)})
    }
    $record=Get-FvrChild $runtime 'active-session.json'
    if(Test-Path -LiteralPath $record -PathType Leaf) {
        try {
            Assert-FvrNoLinks $record
            if((Get-Item -LiteralPath $record).Length -gt 32768){throw 'Oversized session record.'}
            $session=Read-FvrJson $record
            if($session.relative_folder) {
                $relative=[string]$session.relative_folder
                if(-not $relative.StartsWith('sessions/',[StringComparison]::Ordinal)){throw 'Session log path is outside launcher runtime sessions.'}
                $run=Get-FvrChild $runtime $relative
                Assert-FvrNoLinks $run
                foreach($name in @('session.json','menu-ready.json','menu-ready.log','supervisor.stdout.log','supervisor.stderr.log','host.stdout.log','host.stderr.log','native.stdout.log','native.stderr.log','session-telemetry.jsonl')) {
                    $sources.Add(@{name=$name;path=(Get-FvrChild $run $name)})
                }
            }
        } catch {$parts.Add('Session logs unavailable or outside the permitted diagnostics folder; omitted.')}
    }
    foreach($source in $sources) {
        try{$tail=Read-FvrFeedbackTail $source.path;if($null -ne $tail){$parts.Add("--- $($source.name) (last 32 KiB, sanitized) ---`n"+$tail)}}
        catch{$parts.Add('--- '+$source.name+' unavailable; omitted ---')}
    }
    $report=$parts -join "`r`n`r`n"
    $utf8=[Text.UTF8Encoding]::new($false)
    if($utf8.GetByteCount($report) -gt 512KB){throw 'Sanitized diagnostics exceeded the 512 KiB limit.'}
    $readme="Review Report.txt before sharing. Attach BC2VR-diagnostics.zip yourself to the GitHub issue.`r`nOnly bounded text logs and build details are included. No game assets, settings, screenshots or memory dumps. Redaction is best effort; remove anything private before sharing. Creating a new bundle replaces these launcher-owned files.`r`n"
    $reportPath=Get-FvrChild $folder 'Report.txt';$readmePath=Get-FvrChild $folder 'README.txt';$zipPath=Get-FvrChild $folder 'BC2VR-diagnostics.zip'
    foreach($path in @($reportPath,$readmePath,$zipPath)){Assert-FvrNoLinks $path}
    $temp=Get-FvrChild $folder ('bundle-'+[guid]::NewGuid().ToString('N')+'.tmp')
    try {
        $file=[IO.File]::Open($temp,'CreateNew','Write','None')
        $archive=[IO.Compression.ZipArchive]::new($file,[IO.Compression.ZipArchiveMode]::Create,$false)
        try {
            foreach($item in @(@{name='Report.txt';text=$report},@{name='README.txt';text=$readme})) {
                $entry=$archive.CreateEntry($item.name,[IO.Compression.CompressionLevel]::Optimal)
                $writer=[IO.StreamWriter]::new($entry.Open(),$utf8)
                try{$writer.Write($item.text)}finally{$writer.Dispose()}
            }
        } finally {$archive.Dispose();$file.Dispose()}
        [IO.File]::WriteAllText($reportPath,$report,$utf8);[IO.File]::WriteAllText($readmePath,$readme,$utf8)
        [IO.File]::Move($temp,$zipPath,$true)
    } finally {if(Test-Path -LiteralPath $temp){Remove-Item -LiteralPath $temp -Force}}
    return @{folder=$folder;zip=$zipPath;report=$reportPath;bytes=(Get-Item -LiteralPath $zipPath).Length;uploaded=$false}
}
