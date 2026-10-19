#requires -Version 7.5
param([string]$PreviewDirectory)
$ErrorActionPreference='Stop'
$repo=Split-Path $PSScriptRoot -Parent
. (Join-Path $repo 'launcher/Launcher.Core.ps1')
. (Join-Path $repo 'launcher/Launcher.Settings.ps1')
$root=Join-Path $repo ('test-temp/settings-'+[guid]::NewGuid().ToString('N'))
$saved=@{};foreach($key in @('TEMP','TMP','TMPDIR','PYTHONDONTWRITEBYTECODE')){$saved[$key]=[Environment]::GetEnvironmentVariable($key,'Process')}
$script:checks=0
function Check([bool]$Good,[string]$Name){if(-not $Good){throw "FAILED: $Name"};$script:checks++;Write-Host "PASS $Name"}
function Reject([scriptblock]$Work,[string]$Name){$bad=$false;try{& $Work|Out-Null}catch{$bad=$true};Check $bad $Name}
function Equal-Settings($Expected,$Actual){return (($Expected|ConvertTo-Json -Depth 100 -Compress) -ceq ($Actual|ConvertTo-Json -Depth 100 -Compress))}
try {
    $null=Set-FvrLauncherStorage $root
    $path=Join-Path $root 'data/settings.json'
    $first=Read-FvrLauncherSettings $root
    Check (-not $first.read_only -and -not $first.needs_reset -and $first.settings.game_path -eq '') 'missing settings start safely with a folder prompt'
    Check (-not(Test-Path -LiteralPath $path)) 'reading missing settings does not create an unconfirmed reset'
    $game=Join-Path $root 'Test Game é (2)';$null=[IO.Directory]::CreateDirectory($game)
    $exe=Join-Path $game 'BFBC2Game.exe';[IO.File]::WriteAllText($exe,'test fixture, never executed')
    Check ((Resolve-FvrGameFolder $game) -ceq $game -and (Resolve-FvrGameFolder $exe) -ceq $game) 'folder and executable selections resolve spaces and Unicode safely'
    $initial=[ordered]@{schema='fvr.desktop.settings.v1';game_path='G:\Previous';local_backend=[ordered]@{label='Local checkpoint';launch='G:\Tools\launch.ps1';pins=@(@{path='G:\Tools\launch.ps1';sha256='abc'})};custom=[ordered]@{theme='blue';number=42;enabled=$false}}
    Write-FvrLauncherSettings $path $initial
    $initial.local_backend.label='Changed by installer after UI opened';Write-FvrLauncherSettings $path $initial
    $result=Save-FvrGameFolder $root $exe
    $expected=$initial;$expected.game_path=$game
    Check (Equal-Settings $expected (Read-FvrJson $path)) 'safe save rereads and preserves backend and unknown settings'
    Check ($result.settings.game_path -ceq $game -and ([IO.File]::ReadAllText($path)).Contains('\\')) 'selected Windows path is JSON escaped and returned to UI'
    $unchanged=(Get-FileHash -LiteralPath $path).Hash
    foreach($selection in @('relative\BFBC2Game.exe',(Join-Path $root 'missing'),$root)){
        Reject {Save-FvrGameFolder $root $selection} ('reject invalid game selection '+$selection)
    }
    $otherExe=Join-Path $game 'Other.exe';[IO.File]::WriteAllText($otherExe,'never executed')
    Reject {Save-FvrGameFolder $root $otherExe} 'reject a different executable'
    Check ((Get-FileHash -LiteralPath $path).Hash -ceq $unchanged) 'invalid picker selections never change settings'

    $raw='{"schema":"fvr.desktop.settings.v1","game_path":"G:\New folder (2)\Battlefield Bad Company 2","local_backend":{"label":"Preserved","launch":"G:\\Tools\\launch.ps1","pins":[{"sha256":"abc"}]},"custom":{"enabled":true}}'
    [IO.File]::WriteAllText($path,$raw,[Text.UTF8Encoding]::new($false));$rawHash=(Get-FileHash -LiteralPath $path).Hash
    $recovered=Read-FvrLauncherSettings $root
    Check ($recovered.repaired -and -not $recovered.needs_reset -and -not $recovered.read_only) 'one unambiguous raw Windows game path is repaired at startup'
    Check ($recovered.settings.game_path -ceq 'G:\New folder (2)\Battlefield Bad Company 2' -and $recovered.settings.local_backend.launch -ceq 'G:\Tools\launch.ps1' -and $recovered.settings.custom.enabled) 'repair preserves valid backend escapes and unrelated settings'
    Check ((Get-FileHash -LiteralPath $recovered.backup).Hash -ceq $rawHash -and $recovered.notice.Contains('other settings were kept')) 'repair retains exact original bytes and explicit recovery notice'
    $backupCount=@(Get-ChildItem -LiteralPath (Split-Path $recovered.backup -Parent) -File).Count
    $again=Read-FvrLauncherSettings $root
    Check (-not $again.repaired -and @(Get-ChildItem -LiteralPath (Split-Path $recovered.backup -Parent) -File).Count -eq $backupCount) 'repeated startup of repaired settings creates no extra backup'
    [IO.File]::WriteAllText($path,$raw,[Text.UTF8Encoding]::new($false));$same=Read-FvrLauncherSettings $root
    Check ($same.backup -ceq $recovered.backup) 'identical malformed input reuses its verified immutable backup'

    $invalid='{"game_path":"G:\Game","local_backend":{"label":"keep me",BROKEN}}'
    [IO.File]::WriteAllText($path,$invalid);$invalidHash=(Get-FileHash -LiteralPath $path).Hash
    $broken=Read-FvrLauncherSettings $root
    Check ($broken.needs_reset -and -not $broken.read_only -and -not $broken.repaired) 'unrelated JSON damage requires explicit reset'
    Check ((Get-FileHash -LiteralPath $path).Hash -ceq $invalidHash -and (Get-FileHash -LiteralPath $broken.backup).Hash -ceq $invalidHash) 'unresolved damage keeps original and exact backup unchanged'
    $lock=Enter-FvrLock $root
    try{
        $workerState=Read-FvrLauncherSettings $root -LockHeld
        Check ($workerState.needs_reset -and $workerState.settings.game_path -eq '' -and (Get-FileHash -LiteralPath $path).Hash -ceq $invalidHash) 'worker safe defaults allow recovery actions without rewriting damaged settings'
    }finally{$lock.Dispose()}
    Reject {Save-FvrGameFolder $root $exe} 'ordinary selection cannot silently reset damaged backend settings'
    Check ((Get-FileHash -LiteralPath $path).Hash -ceq $invalidHash) 'refused reset leaves damaged original untouched'
    $reset=Save-FvrGameFolder $root $exe -ResetInvalidSettings
    Check (-not $reset.needs_reset -and $reset.settings.game_path -ceq $game -and $null -eq $reset.settings.local_backend) 'explicit reset saves selected folder with defined defaults'
    Check ((Get-FileHash -LiteralPath $reset.backup).Hash -ceq $invalidHash -and $reset.notice.Contains('local-checkpoint')) 'explicit reset preserves and identifies original backend backup'
    foreach($ambiguous in @('{"game_path":"G:\Game","game_path":"G:\Other"}','{"nested":{"game_path":"G:\Game"}}','[]','null')){
        [IO.File]::WriteAllText($path,$ambiguous);$hash=(Get-FileHash -LiteralPath $path).Hash
        $state=Read-FvrLauncherSettings $root
        Check ($state.needs_reset -and -not $state.repaired -and (Get-FileHash -LiteralPath $path).Hash -ceq $hash) ('do not guess ambiguous or non-object settings: '+$ambiguous)
    }
    Write-FvrLauncherSettings $path $expected
    $lock=Enter-FvrLock $root
    try{
        $blocked=Read-FvrLauncherSettings $root
        Check ($blocked.read_only -and -not $blocked.needs_reset) 'active operation becomes a safe read-only startup notice'
        $held=Read-FvrLauncherSettings $root -LockHeld
        Check (Equal-Settings $expected $held.settings) 'installer can read settings while holding its existing lock'
        Reject {Save-FvrGameFolder $root $exe} 'picker save cannot race an installer operation'
    }finally{$lock.Dispose()}
    [IO.File]::WriteAllText($path,('x'*(1MB+1)))
    $oversize=Read-FvrLauncherSettings $root
    Check ($oversize.read_only -and -not $oversize.needs_reset -and (Get-Item -LiteralPath $path).Length -eq 1MB+1) 'oversized settings remain untouched with a bounded-read notice'

    $runtime=Join-Path $root 'data/runtime';$sessionFolder=Join-Path $runtime 'sessions/test-owned'
    $null=[IO.Directory]::CreateDirectory($sessionFolder)
    $sessionPath=Join-Path $sessionFolder 'session.json';$activePath=Join-Path $runtime 'active-session.json'
    $operation=@{action='Launch';state='started';utc='2026-10-18T15:00:00Z'}
    $identity=@{pid=123;start_utc='2026-10-18T14:59:00Z';path='G:\Owned\pwsh.exe'}
    $activeSession=@{schema='bc2vr.active-session.v1';version='0.1.6';relative_folder='sessions/test-owned';supervisor=$identity}
    $session=@{schema='bc2vr.session.v1';version='0.1.6';state='failed';error='Headset disconnected.';started_utc='2026-10-18T14:59:01Z';ended_utc='2026-10-18T15:01:00Z';supervisor=$identity}
    Write-FvrJson $activePath $activeSession;Write-FvrJson $sessionPath $session
    $notice=Get-FvrRuntimeSessionNotice $root '0.1.6' $operation
    Check ($notice.state -eq 'failed' -and $notice.message -like '*Headset disconnected*') 'later owned session failure replaces successful launch status'
    foreach($terminal in @('ended','stopped')){
        $session.state=$terminal;Write-FvrJson $sessionPath $session
        Check ((Get-FvrRuntimeSessionNotice $root '0.1.6' $operation).state -eq $terminal) ('owned session '+$terminal+' is reported')
    }
    foreach($state in @('starting','running')){
        $session.state=$state;Write-FvrJson $sessionPath $session
        Check ($null -eq (Get-FvrRuntimeSessionNotice $root '0.1.6' $operation)) ('nonterminal '+$state+' leaves operation status alone')
    }
    $session.state='failed';Write-FvrJson $sessionPath $session
    foreach($action in @('Validate','Feedback','Check','Update','Rollback','Stop')){
        $otherOperation=$operation.Clone();$otherOperation.action=$action
        Check ($null -eq (Get-FvrRuntimeSessionNotice $root '0.1.6' $otherOperation)) ('session failure cannot overwrite '+$action+' status')
    }
    $otherOperation=$operation.Clone();$otherOperation.state='working'
    Check ($null -eq (Get-FvrRuntimeSessionNotice $root '0.1.6' $otherOperation)) 'launch progress remains authoritative until worker completes'
    Check ($null -eq (Get-FvrRuntimeSessionNotice $root '0.1.5' $operation)) 'different package version cannot replace status'
    $session.ended_utc='2026-10-18T14:59:30Z';Write-FvrJson $sessionPath $session
    Check ($null -eq (Get-FvrRuntimeSessionNotice $root '0.1.6' $operation)) 'older terminal session cannot replace newer launch status'
    $operation.started_utc='2026-10-18T14:58:30Z'
    Check ((Get-FvrRuntimeSessionNotice $root '0.1.6' $operation).state -eq 'failed') 'failure between runtime readiness and final worker status remains visible'
    $session.started_utc='2026-10-18T14:58:00Z';$session.ended_utc='2026-10-18T15:01:00Z';Write-FvrJson $sessionPath $session
    Check ($null -eq (Get-FvrRuntimeSessionNotice $root '0.1.6' $operation)) 'session beginning before this worker launch cannot replace status'
    $session.started_utc='2026-10-18T15:00:01Z';Write-FvrJson $sessionPath $session
    Check ($null -eq (Get-FvrRuntimeSessionNotice $root '0.1.6' $operation)) 'newer active session cannot be attributed to a stale completed launch'
    $session.started_utc='2026-10-18T14:59:01Z'
    $session.ended_utc='2026-10-18T15:01:00Z';$session.supervisor=$identity.Clone();$session.supervisor.pid=456;Write-FvrJson $sessionPath $session
    Check ($null -eq (Get-FvrRuntimeSessionNotice $root '0.1.6' $operation)) 'supervisor lifetime must join current and session records'
    $session.supervisor=$identity;Write-FvrJson $sessionPath $session
    foreach($relative in @('sessions/../../outside','sessions\test-owned','other/test-owned')){
        $activeSession.relative_folder=$relative;Write-FvrJson $activePath $activeSession
        Check ($null -eq (Get-FvrRuntimeSessionNotice $root '0.1.6' $operation)) ('refuse non-owned session path '+$relative)
    }
    $activeSession.relative_folder='sessions/test-owned';Write-FvrJson $activePath $activeSession
    [IO.File]::WriteAllText($sessionPath,('x'*32769))
    Check ($null -eq (Get-FvrRuntimeSessionNotice $root '0.1.6' $operation)) 'oversized session record cannot allocate an unbounded notice'
    [IO.File]::WriteAllText($sessionPath,'{"state":')
    Check ($null -eq (Get-FvrRuntimeSessionNotice $root '0.1.6' $operation)) 'partially written session record is retried without crashing UI'

    foreach($file in @('launcher/Launcher.ps1','launcher/Launcher.Settings.ps1')){
        $tokens=$null;$errors=$null;$null=[Management.Automation.Language.Parser]::ParseFile((Join-Path $repo $file),[ref]$tokens,[ref]$errors)
        Check ($errors.Count -eq 0) ('parse '+$file)
    }
    Add-Type -AssemblyName PresentationFramework,PresentationCore,WindowsBase
    $source=[IO.File]::ReadAllText((Join-Path $repo 'launcher/Launcher.ps1'))
    $tokens=$null;$errors=$null;$ast=[Management.Automation.Language.Parser]::ParseInput($source,[ref]$tokens,[ref]$errors)
    foreach($functionName in @('Show-Settings','Set-Busy')){
        $functionAst=$ast.Find({param($node) $node -is [Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -eq $functionName}.GetNewClosure(),$true)
        . ([scriptblock]::Create($functionAst.Extent.Text))
    }
    $block=[regex]::Match($source,'(?s)\[xml\]\$markup=@''\r?\n(.*?)\r?\n''@')
    Check $block.Success 'actual launcher XAML located'
    foreach($dimensions in @(@(800,740,1),@(540,480,2))){
        [xml]$xml=$block.Groups[1].Value
        $window=[Windows.Markup.XamlReader]::Load([Xml.XmlNodeReader]::new($xml))
        try{
            $controls=@{};foreach($name in @('BuildLabel','GameFolder','ChooseGame','SettingsNotice','Launch','Fresh','Local','Stop','Validate','Logs','Feedback','Status','Progress','Check','Update','Rollback','Prerelease')){$controls[$name]=$window.FindName($name)}
            $package=@{runtime_entry='runtime/Entry.ps1'};$packageState=@{verified=$true;error=$null};$active=@{current='0.1.6'};$script:reopenRequired=$false
            $actions=@('Launch','Stop','Validate','Check','Update','Rollback','Feedback')
            $script:settingsState=$broken
            Show-Settings;Set-Busy $false
            Check (-not $controls.Launch.IsEnabled -and $controls.ChooseGame.IsEnabled -and $controls.ChooseGame.Content -like 'Reset*' -and $controls.SettingsNotice.Text -like '*original*') 'actual startup presenter shows recovery and picker instead of launching malformed settings'
            $script:settingsState=$result
            Show-Settings;Set-Busy $false
            Check ($controls.Launch.IsEnabled -and $controls.Validate.IsEnabled -and $controls.GameFolder.Text -ceq $game) 'actual startup presenter displays valid folder and enables launch'
            $packageState=@{verified=$false;error='Package identity mismatch. Use Install update to repair.'}
            Show-Settings;Set-Busy $false
            Check (-not $controls.Launch.IsEnabled -and -not $controls.Validate.IsEnabled -and $controls.Update.IsEnabled -and $controls.Feedback.IsEnabled -and $controls.SettingsNotice.Text -like '*Install update*') 'damaged package disables launch while recovery remains visible and available'
            $packageState=@{verified=$true;error=$null};Show-Settings;Set-Busy $false
            Set-Busy $true
            Check (-not $controls.ChooseGame.IsEnabled -and -not $controls.Launch.IsEnabled -and -not $controls.Local.IsEnabled) 'active worker prevents picker and backend changes'
            Set-Busy $false
            $named=@{};foreach($name in @('BuildLabel','GameFolder','SettingsNotice','ChooseGame','Prerelease')){$named[$name]=$window.FindName($name)}
            $page=$window.Content;$window.Content=$null # Offscreen layout: never create/show a desktop window.
            $page.Resources=$window.Resources;$page.Background=$window.Background;$page.Foreground=$window.Foreground;$page.FontFamily=$window.FontFamily;$page.FontSize=$window.FontSize
            $named.BuildLabel.Text='Installed VR runtime • Launcher 0.1.6'
            $named.GameFolder.Text='G:\Games\Battlefield Bad Company 2'
            $named.SettingsNotice.Text='Choose BFBC2Game.exe in your game folder.'
            $size=[Windows.Size]::new($dimensions[0],$dimensions[1]);$page.Measure($size);$page.Arrange([Windows.Rect]::new($size));$page.UpdateLayout()
            $picker=$named.ChooseGame;$folder=$named.GameFolder
            $position=$picker.TranslatePoint([Windows.Point]::new(0,0),$page)
            Check ($picker.ActualWidth -gt 100 -and $picker.ActualHeight -gt 30 -and $position.Y -ge 0 -and $position.Y+$picker.ActualHeight -le $page.ActualHeight -and $position.X+$picker.ActualWidth -le $page.ActualWidth) ("folder picker visible at $($dimensions[0]) x $($dimensions[1]) device-independent units")
            Check ($folder.ActualWidth -gt 200 -and $folder.ActualWidth -le $page.ActualWidth -and $page.HorizontalScrollBarVisibility -eq 'Disabled') 'selected folder wraps within available width'
            $page.ScrollToEnd();$page.UpdateLayout();$bottom=$named.Prerelease;$point=$bottom.TranslatePoint([Windows.Point]::new(0,0),$page)
            Check ($point.Y -ge 0 -and $point.Y+$bottom.ActualHeight -le $page.ActualHeight) 'lower actions remain reachable by vertical scrolling'
            if($PreviewDirectory){
                $null=[IO.Directory]::CreateDirectory($PreviewDirectory);$page.ScrollToTop();$page.UpdateLayout()
                $scale=$dimensions[2]
                $bitmap=[Windows.Media.Imaging.RenderTargetBitmap]::new($dimensions[0]*$scale,$dimensions[1]*$scale,96*$scale,96*$scale,[Windows.Media.PixelFormats]::Pbgra32)
                $bitmap.Render($page);$encoder=[Windows.Media.Imaging.PngBitmapEncoder]::new();$encoder.Frames.Add([Windows.Media.Imaging.BitmapFrame]::Create($bitmap))
                $stream=[IO.File]::Create((Join-Path $PreviewDirectory ("launcher-$($dimensions[0])-scale$scale.png")));try{$encoder.Save($stream)}finally{$stream.Dispose()}
            }
        }finally{$window.Close()}
    }
    Write-Host "$script:checks settings/layout checks passed; no game, SteamVR, picker, or desktop window opened."
}finally{
    foreach($key in $saved.Keys){[Environment]::SetEnvironmentVariable($key,$saved[$key],'Process')}
    Remove-FvrOwnedTree (Split-Path $root -Parent) (Split-Path $root -Leaf)
}
