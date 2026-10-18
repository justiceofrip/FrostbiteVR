#requires -Version 5.1
# Public installer storage policy. The developer workspace's drive preference
# and 15 GiB reserve are intentionally not requirements for public users.
function Get-FvrDriveInfo([string]$DriveRoot) {
    return [IO.DriveInfo]::new($DriveRoot)
}
function Get-FvrInstallDrive([string]$Root) {
    if ($Root -notmatch '^[A-Za-z]:[\\/]') { throw 'Choose a folder on a local fixed drive, such as C:\Games\BC2VR.' }
    $full=[IO.Path]::GetFullPath($Root)
    $drive=Get-FvrDriveInfo ([IO.Path]::GetPathRoot($full))
    if (-not $drive.IsReady -or $drive.DriveType -ne [IO.DriveType]::Fixed) { throw 'The selected folder must be on a ready local fixed drive.' }
    return $drive
}
function Assert-FvrFreeSpace([string]$Root, [long]$Bytes) {
    if ($Bytes -lt 0 -or $Bytes -gt 8GB) { throw 'Invalid bounded working-space requirement.' }
    $drive=Get-FvrInstallDrive $Root
    # Retained files already reduce AvailableFreeSpace. Reserve only this
    # operation's declared additional peak, plus 128 MiB for small logs/metadata.
    # A 512 MiB floor covers the current 37 MiB runtime and 224 MiB decoder peak.
    [long]$required=[Math]::Max([long]512MB, $Bytes+[long]128MB)
    if ($drive.AvailableFreeSpace -lt $required) {
        throw ('{0} needs {1:N0} MiB free for this operation; {2:N0} MiB is available. Free {3:N0} MiB or choose another folder.' -f $drive.Name,($required/1MB),($drive.AvailableFreeSpace/1MB),([Math]::Ceiling(($required-$drive.AvailableFreeSpace)/1MB)))
    }
}
function Assert-FvrStorageNoLinks([string]$Path) {
    for($check=[IO.Path]::GetFullPath($Path);$check;$check=Split-Path $check -Parent){
        if((Test-Path -LiteralPath $check) -and ((Get-Item -LiteralPath $check -Force).Attributes -band [IO.FileAttributes]::ReparsePoint)){
            throw 'Choose a folder without junctions or symbolic links.'
        }
    }
}
function New-FvrStorageDirectory([string]$Path) { $null=[IO.Directory]::CreateDirectory($Path) }
function Set-FvrProcessStorage([string]$Root) {
    $null=Get-FvrInstallDrive $Root
    $scratch=Join-Path ([IO.Path]::GetFullPath($Root)) 'data/temp'
    Assert-FvrStorageNoLinks $scratch
    New-FvrStorageDirectory $scratch
    # Process only: child helpers inherit this location. No user/machine edits.
    $env:TEMP=$scratch;$env:TMP=$scratch;$env:TMPDIR=$scratch
    $env:PYTHONDONTWRITEBYTECODE='1'
    return $scratch
}
function Get-FvrInstallDestination([string]$Selection) {
    if ($Selection -notmatch '^[A-Za-z]:[\\/]') { throw 'Choose an installation folder on a local fixed drive.' }
    $full=[IO.Path]::GetFullPath($Selection)
    if ($full.TrimEnd('\','/') -eq [IO.Path]::GetPathRoot($full).TrimEnd('\','/')) { return (Join-Path $full 'BC2VR-Launcher') }
    return $full
}
function Select-FvrInstallFolder($Picker=$null) {
    if ($null -eq $Picker) {
        Add-Type -AssemblyName System.Windows.Forms
        $Picker=New-Object Windows.Forms.FolderBrowserDialog
        $Picker.RootFolder=[Environment+SpecialFolder]::MyComputer
        $Picker.ShowNewFolderButton=$true
    }
    $Picker.Description='Choose or create the BC2 VR installation folder on any fixed drive, including C:. Selecting a drive creates a BC2VR-Launcher folder there.'
    try {
        if ($Picker.ShowDialog().ToString() -ne 'OK') { return $null }
        return (Get-FvrInstallDestination $Picker.SelectedPath)
    } finally { $Picker.Dispose() }
}
