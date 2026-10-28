#requires -Version 7.5
param([string]$PayloadRoot)
# The actual settings helpers, XAML and Click registration are exercised
# offscreen. No game, worker, native helper, process or desktop input is used.
$ErrorActionPreference='Stop'
$repo=Split-Path $PSScriptRoot -Parent
$app=if($PayloadRoot){Join-Path $PayloadRoot 'app'}else{Join-Path $repo 'launcher'}
. (Join-Path $app 'Launcher.Core.ps1')
. (Join-Path $app 'Launcher.Settings.ps1')
. (Join-Path $repo 'tools/StorageBudget.ps1')
$root=Join-Path $repo ('test-temp/steam-option-'+[guid]::NewGuid().ToString('N'))
$saved=@{};foreach($key in @('TEMP','TMP','TMPDIR','PYTHONDONTWRITEBYTECODE')){$saved[$key]=[Environment]::GetEnvironmentVariable($key,'Process')}
$script:checks=0;$passed=$false;$window=$null
function Check([bool]$Good,[string]$Name){if(-not $Good){throw "FAILED: $Name"};$script:checks++;Write-Host "PASS $Name"}
function Reject([scriptblock]$Work,[string]$Name){$rejected=$false;try{& $Work|Out-Null}catch{$rejected=$true};Check $rejected $Name}
try{
    $budget=Assert-FvrStorageBudget -RepositoryRoot $repo -AdditionalRoots 'G:\Unity\VRMMO\BC2VR-Hotfix018' -OutputPath $root -Operation Session -PlannedBytes 256KB -RawCaptureBytes 0
    if($null -eq $budget.generated.allocated_bytes){throw 'Complete storage admission required'}
    $null=Set-FvrLauncherStorage $root
    $InstallRoot=$root;$path=Join-Path $root 'data/settings.json'
    $state=Read-FvrLauncherSettings $root
    Check ($state.settings.steam_compatibility -is [bool] -and -not $state.settings.steam_compatibility) 'missing settings default to boolean false'
    Check (-not(Test-Path -LiteralPath $path)) 'default presentation does not create settings'
    $game=Join-Path $root 'Game';$null=[IO.Directory]::CreateDirectory($game)
    [IO.File]::WriteAllText((Join-Path $game 'BFBC2Game.exe'),'fixture, never executed')
    $original=@{schema='fvr.desktop.settings.v1';game_path=$game;local_backend=@{label='Retained';pins=@('one','two')};custom=@{enabled=$false;number=7}}
    Write-FvrLauncherSettings $path $original
    $oldHash=(Get-FileHash -LiteralPath $path).Hash
    $opened=Read-FvrLauncherSettings $root
    Check (-not $opened.settings.steam_compatibility -and (Get-FileHash -LiteralPath $path).Hash -ceq $oldHash) 'legacy settings default off without rewriting the file'
    $original.local_backend.label='Updated since window opened';$original.custom.number=9
    Write-FvrLauncherSettings $path $original
    $enabled=Save-FvrSteamCompatibility $root $true
    $reopened=Read-FvrLauncherSettings $root
    Check ($enabled.settings.steam_compatibility -eq $true -and $reopened.settings.steam_compatibility -is [bool] -and $reopened.settings.steam_compatibility) 'opt-in persists as JSON boolean and survives reopen'
    Check ($reopened.settings.game_path -ceq $game -and $reopened.settings.local_backend.label -ceq 'Updated since window opened' -and $reopened.settings.local_backend.pins.Count -eq 2 -and $reopened.settings.custom.number -eq 9 -and -not $reopened.settings.custom.enabled) 'save rereads and preserves latest backend, folder and unknown fields'
    $null=Save-FvrSteamCompatibility $root $false
    $off=Read-FvrLauncherSettings $root
    Check ($off.settings.steam_compatibility -is [bool] -and -not $off.settings.steam_compatibility) 'explicit false remains boolean false after reopen'
    foreach($badValue in @('"false"','"true"','0','1','null','[]','{}')){
        [IO.File]::WriteAllText($path,('{"steam_compatibility":'+$badValue+',"custom":"keep"}'))
        $hash=(Get-FileHash -LiteralPath $path).Hash
        $bad=Read-FvrLauncherSettings $root
        Check ($bad.needs_reset -and $bad.settings.steam_compatibility -is [bool] -and -not $bad.settings.steam_compatibility) ('non-boolean field never enables compatibility: '+$badValue)
        Reject {Save-FvrSteamCompatibility $root $true} ('refuse click save for malformed field '+$badValue)
        Check ((Get-FileHash -LiteralPath $path).Hash -ceq $hash) 'malformed field retains original bytes'
    }
    foreach($invalid in @('{"steam_compatibility":','{"game_path":"G:\Broken","steam_compatibility":false}')){
        [IO.File]::WriteAllText($path,$invalid);$hash=(Get-FileHash -LiteralPath $path).Hash
        Reject {Save-FvrSteamCompatibility $root $true} 'Steam option cannot repair or replace malformed JSON, including a repairable path'
        Check ((Get-FileHash -LiteralPath $path).Hash -ceq $hash) 'malformed JSON is unchanged after rejected save'
    }
    $original.steam_compatibility=$false;Write-FvrLauncherSettings $path $original
    $hash=(Get-FileHash -LiteralPath $path).Hash;$lock=Enter-FvrLock $root
    try{
        Reject {Save-FvrSteamCompatibility $root $true} 'active settings lock prevents racing update'
        $lockedState=Read-FvrLauncherSettings $root
        Check ($lockedState.read_only -and (Get-FileHash -LiteralPath $path).Hash -ceq $hash) 'lock conflict leaves settings intact and read-only'
    }finally{$lock.Dispose()}

    $source=[IO.File]::ReadAllText((Join-Path $app 'Launcher.ps1'))
    $tokens=$null;$errors=$null;$ast=[Management.Automation.Language.Parser]::ParseInput($source,[ref]$tokens,[ref]$errors)
    Check ($errors.Count -eq 0) 'actual launcher source parses'
    foreach($functionName in @('Show-Settings','Set-Busy')){
        $functions=@($ast.FindAll({param($n) $n -is [Management.Automation.Language.FunctionDefinitionAst] -and $n.Name -ceq $functionName}.GetNewClosure(),$true))
        Check ($functions.Count -eq 1) ('locate production '+$functionName)
        . ([scriptblock]::Create($functions[0].Extent.Text))
    }
    $registrations=@($ast.FindAll({param($n)
        $n -is [Management.Automation.Language.InvokeMemberExpressionAst] -and
        $n.Member.Value -ceq 'Add_Click' -and $n.Expression.Extent.Text -ceq '$controls.SteamCompatibility'
    },$true))
    Check ($registrations.Count -eq 1 -and -not $registrations[0].Extent.Text.Contains('GetNewClosure')) 'one actual Click handler keeps file-local settings helpers accessible'
    Check (-not [regex]::IsMatch($source,'\$controls\.SteamCompatibility\.Add_(Checked|Unchecked)\s*\(')) 'programmatic checked-state updates have no save handler'
    $markupMatch=[regex]::Match($source,'(?s)\[xml\]\$markup=@''\r?\n(.*?)\r?\n''@')
    Check $markupMatch.Success 'locate actual launcher XAML'
    Add-Type -AssemblyName PresentationFramework,PresentationCore,WindowsBase
    [xml]$markup=$markupMatch.Groups[1].Value
    $window=[Windows.Markup.XamlReader]::Load([Xml.XmlNodeReader]::new($markup))
    $controls=@{};foreach($name in @('BuildLabel','GameFolder','ChooseGame','SettingsNotice','Launch','Fresh','Local','SteamCompatibility','Stop','Validate','Logs','Feedback','Status','Progress','Check','Update','Rollback','Prerelease')){$controls[$name]=$window.FindName($name)}
    $actions=@('Launch','Stop','Validate','Check','Update','Rollback','Feedback')
    $package=@{runtime_entry='runtime/Entry.ps1'};$packageState=@{verified=$true;error=$null};$active=@{current='fixture'}
    $script:reopenRequired=$false;$script:job=$null;$script:settingsState=Read-FvrLauncherSettings $root
    . ([scriptblock]::Create($registrations[0].Extent.Text))
    Show-Settings;Set-Busy $false
    Check ($controls.SteamCompatibility.Content -ceq 'Steam compatibility (experimental)' -and $controls.SteamCompatibility.IsEnabled -and -not $controls.SteamCompatibility.IsChecked) 'actual checkbox is default-off and available with valid settings'
    $hash=(Get-FileHash -LiteralPath $path).Hash
    $controls.SteamCompatibility.IsChecked=$true
    Check ((Get-FileHash -LiteralPath $path).Hash -ceq $hash) 'programmatic check does not save'
    $controls.SteamCompatibility.RaiseEvent([Windows.RoutedEventArgs]::new([Windows.Controls.Primitives.ButtonBase]::ClickEvent))
    Check ((Read-FvrLauncherSettings $root).settings.steam_compatibility -eq $true -and $controls.SettingsNotice.Text -ceq 'Steam compatibility saved.') 'actual registered Click persists checked state'
    $script:settingsState=Read-FvrLauncherSettings $root;Show-Settings
    Check ($controls.SteamCompatibility.IsChecked -eq $true) 'reopened presenter restores saved opt-in'
    $hash=(Get-FileHash -LiteralPath $path).Hash
    Show-Settings
    Check ((Get-FileHash -LiteralPath $path).Hash -ceq $hash) 'presenter refresh never rewrites settings'
    Set-Busy $true;$controls.SteamCompatibility.IsChecked=$false
    $controls.SteamCompatibility.RaiseEvent([Windows.RoutedEventArgs]::new([Windows.Controls.Primitives.ButtonBase]::ClickEvent))
    Check (-not $controls.SteamCompatibility.IsEnabled -and (Get-FileHash -LiteralPath $path).Hash -ceq $hash) 'busy checkbox refuses even a synthetic routed Click'
    Set-Busy $false;Show-Settings;$controls.SteamCompatibility.IsChecked=$false
    $controls.SteamCompatibility.RaiseEvent([Windows.RoutedEventArgs]::new([Windows.Controls.Primitives.ButtonBase]::ClickEvent))
    Check (-not (Read-FvrLauncherSettings $root).settings.steam_compatibility) 'actual Click persists explicit opt-out'
    $script:settingsState=$lockedState;Show-Settings;Set-Busy $false
    Check (-not $controls.SteamCompatibility.IsEnabled) 'read-only settings disable checkbox'
    $script:settingsState=$bad;Show-Settings;Set-Busy $false
    Check (-not $controls.SteamCompatibility.IsEnabled) 'malformed settings disable checkbox'
    $script:settingsState=Read-FvrLauncherSettings $root;$script:reopenRequired=$true;Show-Settings;Set-Busy $false
    Check (-not $controls.SteamCompatibility.IsEnabled) 'required launcher reopen disables checkbox'
    $script:reopenRequired=$false;Set-Busy $false
    [IO.File]::WriteAllText($path,'{"steam_compatibility":"false"}');$hash=(Get-FileHash -LiteralPath $path).Hash
    $controls.SteamCompatibility.IsChecked=$true
    $controls.SteamCompatibility.RaiseEvent([Windows.RoutedEventArgs]::new([Windows.Controls.Primitives.ButtonBase]::ClickEvent))
    Check ((Get-FileHash -LiteralPath $path).Hash -ceq $hash -and -not $controls.SteamCompatibility.IsChecked -and -not $controls.SteamCompatibility.IsEnabled -and $controls.SettingsNotice.Text -like '*unchanged*') 'concurrent malformed edit is preserved and actual Click restores safe UI state'
    $passed=$true
    Write-Host "Steam compatibility checks passed: $script:checks; no shown UI, processes, game or native execution."
}finally{
    if($window){$window.Close()}
    foreach($key in $saved.Keys){[Environment]::SetEnvironmentVariable($key,$saved[$key],'Process')}
    if($passed){Remove-FvrOwnedTree (Split-Path $root -Parent) (Split-Path $root -Leaf)}else{Write-Warning ('Fixture evidence retained: '+$root)}
}
