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
function Get-FvrLauncherGamePlacement([string]$InstallRoot, [string]$GameRoot) {
    # Compare complete path components, without disclosing either private path
    # in the diagnostic. The Python cache writer independently enforces this.
    $launcher=[IO.Path]::GetFullPath($InstallRoot).Replace('/','\').TrimEnd('\')
    $game=[IO.Path]::GetFullPath($GameRoot).Replace('/','\').TrimEnd('\')
    $relationship='separate'
    if($launcher -eq $game){$relationship='game_root'}
    elseif($launcher.StartsWith($game+'\',[StringComparison]::OrdinalIgnoreCase)){
        $first=$launcher.Substring($game.Length+1).Split('\')[0]
        $relationship=switch($first.ToLowerInvariant()){
            'dist' {'game_dist';break}
            'package' {'game_package';break}
            default {'game_subfolder'}
        }
    }
    $allowed=$relationship -notin @('game_root','game_dist','game_package')
    $errorText=if($allowed){$null}else{
        'BC2 VR is installed in the game root or its protected Dist/Package folders. Install BC2 VR into a fresh, separate BC2VR-Launcher folder, then choose your existing game folder. Keep the game files and saves in place.'
    }
    return @{schema='bc2vr.launcher-placement.v1';relationship=$relationship;allowed=$allowed;error=$errorText}
}
function Get-FvrInstallDestination([string]$Selection) {
    if ($Selection -notmatch '^[A-Za-z]:[\\/]') { throw 'Choose an installation folder on a local fixed drive.' }
    $full=[IO.Path]::GetFullPath($Selection)
    if ($full.TrimEnd('\','/') -eq [IO.Path]::GetPathRoot($full).TrimEnd('\','/')) { $full=Join-Path $full 'BC2VR-Launcher' }
    # Setup does not yet know the selected game setting. Recognize an existing
    # BC2 install by its executable, before creating scratch or package files.
    for($ancestor=$full;$ancestor;$ancestor=Split-Path $ancestor -Parent){
        if(Test-Path -LiteralPath (Join-Path $ancestor 'BFBC2Game.exe') -PathType Leaf){
            $placement=Get-FvrLauncherGamePlacement $full $ancestor
            if(-not $placement.allowed){throw $placement.error}
        }
    }
    return $full
}
function Select-FvrInstallFolder($Picker=$null) {
    if ($null -eq $Picker) {
        Add-Type -AssemblyName System.Windows.Forms
        $Picker=New-Object Windows.Forms.FolderBrowserDialog
        $Picker.RootFolder=[Environment+SpecialFolder]::MyComputer
        $Picker.ShowNewFolderButton=$true
    }
    $Picker.Description='Choose a dedicated BC2 VR launcher folder, not the game folder or its Dist/Package folders. Selecting a drive creates a BC2VR-Launcher folder there.'
    try {
        if ($Picker.ShowDialog().ToString() -ne 'OK') { return $null }
        return (Get-FvrInstallDestination $Picker.SelectedPath)
    } finally { $Picker.Dispose() }
}
