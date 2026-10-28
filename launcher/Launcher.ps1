#requires -Version 7.5
[CmdletBinding()]
param([Parameter(Mandatory)][string]$InstallRoot, [string]$PreviewPath, [switch]$SmokeTest,
      [ValidateSet('Validate','Stop','Check')][string]$SmokeAction)
$ErrorActionPreference='Stop'
Add-Type -AssemblyName PresentationFramework,PresentationCore,WindowsBase
. (Join-Path $PSScriptRoot 'Launcher.Core.ps1')
. (Join-Path $PSScriptRoot 'Launcher.Settings.ps1')
. (Join-Path $PSScriptRoot 'Launcher.Feedback.ps1')
$null=Set-FvrLauncherStorage $InstallRoot
$data=Join-Path $InstallRoot 'data'
$script:settingsState=Read-FvrLauncherSettings -InstallRoot $InstallRoot
$script:settings=$script:settingsState.settings
$active=Read-FvrJson (Join-Path $InstallRoot 'active.json')
$packageState=Get-FvrInstalledPackageState (Join-Path $InstallRoot ('versions/'+$active.current)) $active.current
$package=$packageState.package
$mutex=[Threading.Mutex]::new($false,'Local\FrostbiteVR.DesktopLauncher')
$ownsMutex=$false
if(-not $SmokeTest){
    try{$ownsMutex=$mutex.WaitOne(0)}catch [Threading.AbandonedMutexException]{$ownsMutex=$true}
    if(-not $ownsMutex){[Windows.MessageBox]::Show('The BC2 VR launcher is already open.','BC2 VR')|Out-Null;$mutex.Dispose();return}
}
[xml]$markup=@'
<Window xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation" xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml"
 Title="Battlefield Bad Company 2 — VR" Width="800" Height="780" MinWidth="540" MinHeight="520" WindowStartupLocation="CenterScreen"
 Background="#15191C" Foreground="#EDF0EC" FontFamily="Segoe UI" FontSize="14">
 <Window.Resources>
  <Style TargetType="Button"><Setter Property="Background" Value="#30383D"/><Setter Property="Foreground" Value="#F2F1EC"/><Setter Property="Padding" Value="16,10"/><Setter Property="Margin" Value="0,0,10,0"/><Setter Property="BorderThickness" Value="0"/><Setter Property="Cursor" Value="Hand"/>
   <Setter Property="Template"><Setter.Value><ControlTemplate TargetType="Button"><Border x:Name="bg" Background="{TemplateBinding Background}" Padding="{TemplateBinding Padding}" CornerRadius="3"><ContentPresenter HorizontalAlignment="Center" VerticalAlignment="Center"/></Border><ControlTemplate.Triggers><Trigger Property="IsMouseOver" Value="True"><Setter TargetName="bg" Property="Opacity" Value="0.8"/></Trigger><Trigger Property="IsEnabled" Value="False"><Setter TargetName="bg" Property="Opacity" Value="0.4"/></Trigger></ControlTemplate.Triggers></ControlTemplate></Setter.Value></Setter></Style>
  <Style TargetType="CheckBox"><Setter Property="Foreground" Value="#C5CDC8"/><Setter Property="Margin" Value="0,0,20,10"/><Setter Property="VerticalContentAlignment" Value="Center"/></Style>
 </Window.Resources>
 <ScrollViewer x:Name="PageScroll" VerticalScrollBarVisibility="Auto" HorizontalScrollBarVisibility="Disabled" Padding="24,20">
 <Grid>
  <Grid.RowDefinitions><RowDefinition Height="Auto"/><RowDefinition Height="Auto"/><RowDefinition Height="Auto"/><RowDefinition Height="Auto"/><RowDefinition Height="Auto"/><RowDefinition Height="Auto"/></Grid.RowDefinitions>
  <StackPanel Grid.Row="0">
   <TextBlock Text="BC2 VR  /  SINGLEPLAYER PRE-ALPHA" Foreground="#E6A04A" FontSize="12" FontWeight="SemiBold"/>
   <TextBlock Text="BATTLEFIELD" FontSize="34" FontWeight="Black" Margin="0,6,0,0"/>
   <TextBlock Text="BAD COMPANY 2" FontSize="23" FontWeight="Light" Foreground="#D5DBD7"/>
   <TextBlock x:Name="BuildLabel" Margin="0,10,0,14" Foreground="#99AAA1" TextWrapping="Wrap"/>
  </StackPanel>
  <StackPanel Grid.Row="1" Margin="0,0,0,16">
   <TextBlock Text="GAME FOLDER" Foreground="#E6A04A" FontSize="11" FontWeight="Bold" Margin="0,0,0,6"/>
   <TextBox x:Name="GameFolder" IsReadOnly="True" Background="#20272B" Foreground="#EDF0EC" BorderThickness="0" Padding="10" TextWrapping="Wrap" MinHeight="38" MaxHeight="84" VerticalScrollBarVisibility="Auto" AutomationProperties.Name="Selected game folder"/>
   <Button x:Name="ChooseGame" Content="Choose game folder…" HorizontalAlignment="Left" Margin="0,8,0,0" AutomationProperties.Name="Choose game folder"/>
   <TextBlock x:Name="SettingsNotice" Foreground="#E6BD83" TextWrapping="Wrap" FontSize="12" Margin="0,8,0,0"/>
  </StackPanel>
  <StackPanel Grid.Row="2">
   <Button x:Name="Launch" Content="LAUNCH BC2 IN VR" FontWeight="Bold" FontSize="17" Background="#E6A04A" Foreground="#161B1E" Padding="20,17" Margin="0,0,0,15" HorizontalAlignment="Stretch"/>
   <WrapPanel><CheckBox x:Name="Fresh" Content="Restart BC2 before launch"/><CheckBox x:Name="Local" Content="Use local checkpoint"/></WrapPanel>
   <CheckBox x:Name="SteamCompatibility" Content="Steam compatibility (experimental)" ToolTip="Enable for the Steam edition. Saved for your next launch."/>
   <TextBlock Text="Choose Campaign from the menu in VR. Point with your right controller and select with its trigger. Hold X + Y on the left controller to open the pause menu; B goes back." FontSize="12" Foreground="#99AAA1" TextWrapping="Wrap" Margin="0,0,0,18"/>
  </StackPanel>
  <WrapPanel Grid.Row="3" Margin="0,0,0,10">
   <Button x:Name="Stop" Content="Stop VR" Margin="0,0,10,8"/><Button x:Name="Validate" Content="Check setup" Margin="0,0,10,8"/><Button x:Name="Logs" Content="Open logs" Margin="0,0,10,8"/><Button x:Name="Feedback" Content="Report feedback" Margin="0,0,10,8"/>
  </WrapPanel>
  <Border Grid.Row="4" Background="#20272B" Padding="17" CornerRadius="4" Margin="0,0,0,18" MinHeight="110">
   <Grid><Grid.RowDefinitions><RowDefinition Height="Auto"/><RowDefinition Height="*"/><RowDefinition Height="Auto"/></Grid.RowDefinitions>
    <TextBlock Text="STATUS" Foreground="#E6A04A" FontSize="11" FontWeight="Bold"/>
    <TextBlock x:Name="Status" Grid.Row="1" Text="Ready when you are." TextWrapping="Wrap" Margin="0,9,0,10"/>
    <ProgressBar x:Name="Progress" Grid.Row="2" Height="3" Foreground="#E6A04A" Background="#30383D" IsIndeterminate="False" BorderThickness="0"/>
   </Grid>
  </Border>
  <StackPanel Grid.Row="5">
   <WrapPanel><Button x:Name="Check" Content="Check for updates" Margin="0,0,10,8"/><Button x:Name="Update" Content="Install update" Margin="0,0,10,8"/><Button x:Name="Rollback" Content="Roll back" Margin="0,0,10,8"/></WrapPanel>
   <CheckBox x:Name="Prerelease" Content="Include pre-alpha hotfixes" IsChecked="True" FontSize="12"/>
   <TextBlock Text="Updates keep your settings and one previous version. Close BC2 before installing." Foreground="#99AAA1" FontSize="11" TextWrapping="Wrap"/>
  </StackPanel>
 </Grid>
 </ScrollViewer>
</Window>
'@
$window=[Windows.Markup.XamlReader]::Load([Xml.XmlNodeReader]::new($markup))
$controls=@{}
foreach($name in @('BuildLabel','GameFolder','ChooseGame','SettingsNotice','Launch','Fresh','Local','SteamCompatibility','Stop','Validate','Logs','Feedback','Status','Progress','Check','Update','Rollback','Prerelease')){$controls[$name]=$window.FindName($name)}
$script:job=$null;$script:reopenRequired=$false;$script:jobStarted=$null
$actions=@('Launch','Stop','Validate','Check','Update','Rollback','Feedback')
function Show-Settings {
    $script:settings=$script:settingsState.settings
    $backend=$settings.local_backend
    $script:hasLocalBackend=$backend -is [Collections.IDictionary] -and $backend.Contains('label')
    $runtimeLabel=if(-not $packageState.verified){'Installation needs repair'}elseif($package.runtime_entry){'Installed VR runtime'}elseif($script:hasLocalBackend){[string]$backend.label}else{'Launcher installed • VR runtime not installed yet'}
    $controls.BuildLabel.Text="$runtimeLabel  •  Launcher $($active.current)"
    $controls.Local.IsChecked=[bool](-not $package.runtime_entry -and $script:hasLocalBackend)
    $controls.SteamCompatibility.IsChecked=($settings.steam_compatibility -is [bool] -and $settings.steam_compatibility)
    $controls.ChooseGame.Content=if($script:settingsState.needs_reset){'Reset and choose game folder…'}else{'Choose game folder…'}
    $controls.GameFolder.Text=if($settings.game_path -is [string] -and $settings.game_path){$settings.game_path}else{'No game folder selected'}
    $controls.SettingsNotice.Text=$script:settingsState.notice
    if(-not $packageState.verified){$controls.SettingsNotice.Text=($controls.SettingsNotice.Text+"`n"+$packageState.error).Trim()}
    $script:gameFolderReady=$false
    if(-not $script:settingsState.needs_reset -and -not $script:settingsState.read_only){
        try{$null=Resolve-FvrGameFolder $settings.game_path;$script:gameFolderReady=$true}catch{
            if(-not $controls.SettingsNotice.Text){$controls.SettingsNotice.Text='Choose BFBC2Game.exe in your game folder to get started.'}
        }
    }
}
function Set-Busy([bool]$Busy){
    foreach($name in $actions){$controls[$name].IsEnabled=-not $Busy -and -not $script:reopenRequired}
    foreach($name in @('Launch','Validate')){$controls[$name].IsEnabled=$controls[$name].IsEnabled -and $script:gameFolderReady -and $packageState.verified}
    $controls.ChooseGame.IsEnabled=-not $Busy -and -not $script:reopenRequired -and -not $script:settingsState.read_only
    $controls.Local.IsEnabled=-not $Busy -and [bool]($package.runtime_entry -and $script:hasLocalBackend)
    $controls.Fresh.IsEnabled=-not $Busy
    $controls.SteamCompatibility.IsEnabled=-not $Busy -and -not $script:reopenRequired -and -not $script:settingsState.read_only -and -not $script:settingsState.needs_reset
    $controls.Progress.IsIndeterminate=$Busy
}
function Begin-Action([string]$Action){
    try{
        if($Action -in @('Launch','Validate') -and -not $script:gameFolderReady){throw 'Choose your game folder before continuing.'}
        # ProcessStartInfo.ArgumentList handles spaces and quotes without a shell.
        $start=[Diagnostics.ProcessStartInfo]::new((Join-Path $PSHOME 'pwsh.exe'))
        $start.UseShellExecute=$false;$start.CreateNoWindow=$true
        $start.RedirectStandardOutput=$true;$start.RedirectStandardError=$true
        foreach($arg in @('-NoProfile','-NonInteractive','-ExecutionPolicy','Bypass','-File',(Join-Path $PSScriptRoot 'Worker.ps1'),'-InstallRoot',$InstallRoot,'-Action',$Action)){$start.ArgumentList.Add($arg)}
        # Only the player's Launch click requests one initial game activation.
        # Validate, update, stop and automatic recovery never request focus.
        if($Action -eq 'Launch'){$start.ArgumentList.Add('-ActivateGameWindow')}
        if($controls.Fresh.IsChecked){$start.ArgumentList.Add('-Fresh')}
        if($controls.Local.IsChecked){$start.ArgumentList.Add('-UseLocal')}
        if($controls.Prerelease.IsChecked){$start.ArgumentList.Add('-Prerelease')}
        $script:jobStarted=[DateTime]::UtcNow
        $script:jobAction=$Action
        $script:job=[Diagnostics.Process]::Start($start)
        $script:stdout=$script:job.StandardOutput.ReadToEndAsync();$script:stderr=$script:job.StandardError.ReadToEndAsync()
        $controls.Status.Text='Starting…';Set-Busy $true
    }catch{$controls.Status.Text=$_.Exception.Message;Set-Busy $false}
}
Show-Settings;Set-Busy $false
$controls.SteamCompatibility.Add_Click({
    if(-not $controls.SteamCompatibility.IsEnabled){return}
    try{
        $script:settingsState=Save-FvrSteamCompatibility -InstallRoot $InstallRoot -Enabled ([bool]$controls.SteamCompatibility.IsChecked)
    }catch{
        $message=$_.Exception.Message
        $script:settingsState=Read-FvrLauncherSettings -InstallRoot $InstallRoot -RefuseRepair
        $script:settingsState.notice=$message
    }
    Show-Settings;Set-Busy $false
})
$controls.ChooseGame.Add_Click({
    try{
        $reset=[bool]$script:settingsState.needs_reset
        if($reset){
            $answer=[Windows.MessageBox]::Show($window,"Reset unreadable settings and choose the game folder? Any local-checkpoint profile remains in the original backup:`n$($script:settingsState.backup)",'Recover launcher settings',[Windows.MessageBoxButton]::YesNo,[Windows.MessageBoxImage]::Warning)
            if($answer -ne [Windows.MessageBoxResult]::Yes){return}
        }
        $picker=[Microsoft.Win32.OpenFileDialog]::new()
        $picker.Title='Choose BFBC2Game.exe in your game folder'
        $picker.Filter='Battlefield Bad Company 2 (BFBC2Game.exe)|BFBC2Game.exe'
        $picker.CheckFileExists=$true;$picker.Multiselect=$false
        if($script:gameFolderReady){$picker.InitialDirectory=Resolve-FvrGameFolder $settings.game_path}
        if($picker.ShowDialog($window) -eq $true){
            $script:settingsState=Save-FvrGameFolder -InstallRoot $InstallRoot -Selection $picker.FileName -ResetInvalidSettings:$reset
            Show-Settings;Set-Busy $false
        }
    }catch{$controls.SettingsNotice.Text=$_.Exception.Message}
})
$controls.Launch.Add_Click({Begin-Action 'Launch'})
$controls.Stop.Add_Click({Begin-Action 'Stop'})
$controls.Validate.Add_Click({Begin-Action 'Validate'})
$controls.Check.Add_Click({Begin-Action 'Check'})
$controls.Update.Add_Click({Begin-Action 'Update'})
$controls.Rollback.Add_Click({Begin-Action 'Rollback'})
$controls.Logs.Add_Click({Start-Process explorer.exe -ArgumentList ('"'+$data+'"')})
$controls.Feedback.Add_Click({
    try {
        $draft=Get-FvrFeedbackDraft $package $settings
        [xml]$feedbackMarkup=@'
<Window xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation" xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml" Title="BC2 VR — Report feedback" Width="540" SizeToContent="Height" ResizeMode="NoResize" WindowStartupLocation="CenterOwner" Background="#20272B" Foreground="#EDF0EC" FontFamily="Segoe UI">
 <StackPanel Margin="24">
  <TextBlock Text="Report a bug or suggest an improvement" FontSize="19" FontWeight="SemiBold" Margin="0,0,0,14"/>
  <TextBlock Text="Open a GitHub issue with your installed build details and a short steps-to-reproduce template. You review and submit it yourself." TextWrapping="Wrap" Margin="0,0,0,18"/>
  <Button x:Name="OpenIssue" Content="Open feedback form on GitHub" Padding="12" Margin="0,0,0,18"/>
  <TextBlock Text="Optional: save a small text-only diagnostics bundle. Local paths and common credentials are removed, but review Report.txt before sharing. No files are uploaded automatically." TextWrapping="Wrap" Margin="0,0,0,12"/>
  <Button x:Name="SaveBundle" Content="Save diagnostics for review" Padding="12"/>
 </StackPanel>
</Window>
'@
        $dialog=[Windows.Markup.XamlReader]::Load([Xml.XmlNodeReader]::new($feedbackMarkup));$dialog.Owner=$window
        $dialog.FindName('OpenIssue').Add_Click({Start-Process -FilePath $draft.url}.GetNewClosure())
        $dialog.FindName('SaveBundle').Add_Click({$dialog.Tag='SaveDiagnostics';$dialog.Close()}.GetNewClosure())
        $null=$dialog.ShowDialog()
        # The modal callback's closure cannot resolve script-local functions.
        # Dispatch here so worker ownership stays with the main launcher.
        if($dialog.Tag -eq 'SaveDiagnostics'){Begin-Action 'Feedback'}
    } catch {$controls.Status.Text=$_.Exception.Message}
})
$timer=[Windows.Threading.DispatcherTimer]::new();$timer.Interval=[TimeSpan]::FromMilliseconds(400)
$timer.Add_Tick({
    try{
        $statusPath=Join-Path $data 'operation.json'
        if(Test-Path -LiteralPath $statusPath){
            try{
                $state=Read-FvrJson $statusPath
                if(-not $script:jobStarted -or [DateTimeOffset]::Parse($state.utc).UtcDateTime -ge $script:jobStarted){
                    $controls.Status.Text=$state.message
                    if(-not $script:job -and $package.runtime_entry){
                        $sessionNotice=Get-FvrRuntimeSessionNotice -InstallRoot $InstallRoot -Version $package.version -Operation $state
                        if($sessionNotice){$controls.Status.Text=$sessionNotice.message}
                    }
                    if($state.state -eq 'restart_required'){$script:reopenRequired=$true}
                }
            }catch{} # Atomic rename may briefly contend with antivirus/readers.
        }
        if($script:job -and $script:job.HasExited){
            $out=$script:stdout.GetAwaiter().GetResult();$err=$script:stderr.GetAwaiter().GetResult()
            # Overwrite two compact operation logs, never accumulate recordings.
            [IO.File]::WriteAllText((Join-Path $data 'launcher-last.log'),$out.Substring([Math]::Max(0,$out.Length-262144))+[Environment]::NewLine+$err.Substring([Math]::Max(0,$err.Length-65536)))
            if($script:job.ExitCode -ne 0 -and $err){$controls.Status.Text=($controls.Status.Text+"`nDetails saved in Open logs.")}
            if($script:job.ExitCode -eq 0 -and $script:jobAction -eq 'Feedback') {
                Start-Process explorer.exe -ArgumentList ('"'+(Get-FvrChild $data 'feedback')+'"')
            }
            $script:job.Dispose();$script:job=$null;Set-Busy $false
        }
    }catch{$controls.Status.Text=$_.Exception.Message}
})
$window.Add_Closed({$timer.Stop()}) # Deliberately do not terminate a game/session or launch worker.
$window.Add_Closing({
    param($sender,$eventArgs)
    # Keep draining the launch worker's pipes while it is starting the session.
    # Once startup is finished, closing the UI has no effect on the session.
    if($script:job -and -not $script:job.HasExited){$eventArgs.Cancel=$true;$window.WindowState='Minimized'}
})
try{
    if($SmokeTest){
        $window.Show();$window.UpdateLayout()
        if($SmokeAction){
            $controls[$SmokeAction].RaiseEvent([Windows.RoutedEventArgs]::new([Windows.Controls.Primitives.ButtonBase]::ClickEvent))
            $timer.Start();$until=[DateTime]::UtcNow.AddSeconds(45)
            while($script:job -and [DateTime]::UtcNow -lt $until){
                $null=$window.Dispatcher.Invoke([Action]{},[Windows.Threading.DispatcherPriority]::Background)
                Start-Sleep -Milliseconds 100
            }
            if($script:job){throw 'UI worker smoke test timed out.'}
            if($controls.Progress.IsIndeterminate -or -not $controls.Validate.IsEnabled){throw 'UI did not recover after worker completion.'}
            Write-Output ('Button result: '+$controls.Status.Text)
        }
        if($PreviewPath){
            $bitmap=[Windows.Media.Imaging.RenderTargetBitmap]::new([int]$window.ActualWidth,[int]$window.ActualHeight,96,96,[Windows.Media.PixelFormats]::Pbgra32)
            $bitmap.Render($window);$encoder=[Windows.Media.Imaging.PngBitmapEncoder]::new();$encoder.Frames.Add([Windows.Media.Imaging.BitmapFrame]::Create($bitmap))
            $file=[IO.File]::Create($PreviewPath);try{$encoder.Save($file)}finally{$file.Dispose()}
        }
        $window.Close();Write-Output 'Launcher UI smoke check passed; no game or SteamVR process started.'
    }else{$timer.Start();$null=$window.ShowDialog()}
}finally{if($ownsMutex){$mutex.ReleaseMutex()};$mutex.Dispose()}
