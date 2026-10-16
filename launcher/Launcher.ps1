#requires -Version 7.5
[CmdletBinding()]
param([Parameter(Mandatory)][string]$InstallRoot, [string]$PreviewPath, [switch]$SmokeTest,
      [ValidateSet('Validate','Stop','Check')][string]$SmokeAction)
$ErrorActionPreference='Stop'
Add-Type -AssemblyName PresentationFramework,PresentationCore,WindowsBase
. (Join-Path $PSScriptRoot 'Launcher.Core.ps1')
$null=Set-FvrLauncherStorage $InstallRoot
$data=Join-Path $InstallRoot 'data'
$settings=Read-FvrJson (Join-Path $data 'settings.json')
$active=Read-FvrJson (Join-Path $InstallRoot 'active.json')
$package=Read-FvrJson (Join-Path (Split-Path $PSScriptRoot -Parent) 'package.json')
$mutex=[Threading.Mutex]::new($false,'Local\FrostbiteVR.DesktopLauncher')
$ownsMutex=$false
if(-not $SmokeTest){
    try{$ownsMutex=$mutex.WaitOne(0)}catch [Threading.AbandonedMutexException]{$ownsMutex=$true}
    if(-not $ownsMutex){[Windows.MessageBox]::Show('The BC2 VR launcher is already open.','BC2 VR')|Out-Null;$mutex.Dispose();return}
}
[xml]$markup=@'
<Window xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation" xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml"
 Title="Battlefield Bad Company 2 — VR" Width="800" Height="640" MinWidth="740" MinHeight="600" WindowStartupLocation="CenterScreen"
 Background="#15191C" Foreground="#EDF0EC" FontFamily="Segoe UI" FontSize="14">
 <Window.Resources>
  <Style TargetType="Button"><Setter Property="Background" Value="#30383D"/><Setter Property="Foreground" Value="#F2F1EC"/><Setter Property="Padding" Value="16,10"/><Setter Property="Margin" Value="0,0,10,0"/><Setter Property="BorderThickness" Value="0"/><Setter Property="Cursor" Value="Hand"/>
   <Setter Property="Template"><Setter.Value><ControlTemplate TargetType="Button"><Border x:Name="bg" Background="{TemplateBinding Background}" Padding="{TemplateBinding Padding}" CornerRadius="3"><ContentPresenter HorizontalAlignment="Center" VerticalAlignment="Center"/></Border><ControlTemplate.Triggers><Trigger Property="IsMouseOver" Value="True"><Setter TargetName="bg" Property="Opacity" Value="0.8"/></Trigger><Trigger Property="IsEnabled" Value="False"><Setter TargetName="bg" Property="Opacity" Value="0.4"/></Trigger></ControlTemplate.Triggers></ControlTemplate></Setter.Value></Setter></Style>
  <Style TargetType="CheckBox"><Setter Property="Foreground" Value="#C5CDC8"/><Setter Property="Margin" Value="0,0,20,10"/><Setter Property="VerticalContentAlignment" Value="Center"/></Style>
 </Window.Resources>
 <Grid Margin="30,26">
  <Grid.RowDefinitions><RowDefinition Height="Auto"/><RowDefinition Height="Auto"/><RowDefinition Height="Auto"/><RowDefinition Height="*"/><RowDefinition Height="Auto"/></Grid.RowDefinitions>
  <StackPanel Grid.Row="0">
   <TextBlock Text="FROSTBITE VR  /  SINGLEPLAYER PREVIEW" Foreground="#E6A04A" FontSize="12" FontWeight="SemiBold"/>
   <TextBlock Text="BATTLEFIELD" FontSize="39" FontWeight="Black" Margin="0,8,0,0"/>
   <TextBlock Text="BAD COMPANY 2" FontSize="25" FontWeight="Light" Foreground="#D5DBD7"/>
   <TextBlock x:Name="BuildLabel" Margin="0,12,0,18" Foreground="#99AAA1"/>
  </StackPanel>
  <StackPanel Grid.Row="1">
   <Button x:Name="Launch" Content="LAUNCH CAMPAIGN IN VR" FontWeight="Bold" FontSize="17" Background="#E6A04A" Foreground="#161B1E" Padding="20,17" Margin="0,0,0,15" HorizontalAlignment="Stretch"/>
   <WrapPanel><CheckBox x:Name="Fresh" Content="Restart BC2 before launch"/><CheckBox x:Name="Local" Content="Use local checkpoint"/></WrapPanel>
   <TextBlock Text="Connect your headset first. SteamVR starts automatically. Your campaign save is kept." FontSize="12" Foreground="#99AAA1" TextWrapping="Wrap" Margin="0,0,0,18"/>
  </StackPanel>
  <StackPanel Grid.Row="2" Orientation="Horizontal" Margin="0,0,0,18">
   <Button x:Name="Stop" Content="Stop VR"/><Button x:Name="Validate" Content="Check setup"/><Button x:Name="Logs" Content="Open logs"/>
  </StackPanel>
  <Border Grid.Row="3" Background="#20272B" Padding="17" CornerRadius="4" Margin="0,0,0,18">
   <Grid><Grid.RowDefinitions><RowDefinition Height="Auto"/><RowDefinition Height="*"/><RowDefinition Height="Auto"/></Grid.RowDefinitions>
    <TextBlock Text="STATUS" Foreground="#E6A04A" FontSize="11" FontWeight="Bold"/>
    <TextBlock x:Name="Status" Grid.Row="1" Text="Ready when you are." TextWrapping="Wrap" Margin="0,9,0,10"/>
    <ProgressBar x:Name="Progress" Grid.Row="2" Height="3" Foreground="#E6A04A" Background="#30383D" IsIndeterminate="False" BorderThickness="0"/>
   </Grid>
  </Border>
  <StackPanel Grid.Row="4">
   <WrapPanel Margin="0,0,0,8"><Button x:Name="Check" Content="Check for updates"/><Button x:Name="Update" Content="Install update"/><Button x:Name="Rollback" Content="Roll back"/></WrapPanel>
   <CheckBox x:Name="Prerelease" Content="Include preview hotfixes" IsChecked="True" FontSize="12"/>
   <TextBlock Text="Updates keep your settings and one previous version. Close BC2 before installing." Foreground="#99AAA1" FontSize="11" TextWrapping="Wrap"/>
  </StackPanel>
 </Grid>
</Window>
'@
$window=[Windows.Markup.XamlReader]::Load([Xml.XmlNodeReader]::new($markup))
$controls=@{}
foreach($name in @('BuildLabel','Launch','Fresh','Local','Stop','Validate','Logs','Status','Progress','Check','Update','Rollback','Prerelease')){$controls[$name]=$window.FindName($name)}
$runtimeLabel=if($package.runtime_entry){'Installed VR runtime'}elseif($settings.local_backend){$settings.local_backend.label}else{'Launcher installed • VR runtime not installed yet'}
$controls.BuildLabel.Text="$runtimeLabel  •  Launcher $($active.current)"
$controls.Local.IsChecked=[bool](-not $package.runtime_entry -and $settings.local_backend)
$controls.Local.IsEnabled=[bool]($package.runtime_entry -and $settings.local_backend)
$script:job=$null;$script:reopenRequired=$false;$script:jobStarted=$null
$actions=@('Launch','Stop','Validate','Check','Update','Rollback')
function Set-Busy([bool]$Busy){
    foreach($name in $actions){$controls[$name].IsEnabled=-not $Busy -and -not $script:reopenRequired}
    $controls.Fresh.IsEnabled=-not $Busy
    $controls.Progress.IsIndeterminate=$Busy
}
function Begin-Action([string]$Action){
    try{
        # ProcessStartInfo.ArgumentList handles spaces and quotes without a shell.
        $start=[Diagnostics.ProcessStartInfo]::new((Join-Path $PSHOME 'pwsh.exe'))
        $start.UseShellExecute=$false;$start.CreateNoWindow=$true
        $start.RedirectStandardOutput=$true;$start.RedirectStandardError=$true
        foreach($arg in @('-NoProfile','-NonInteractive','-File',(Join-Path $PSScriptRoot 'Worker.ps1'),'-InstallRoot',$InstallRoot,'-Action',$Action)){$start.ArgumentList.Add($arg)}
        if($controls.Fresh.IsChecked){$start.ArgumentList.Add('-Fresh')}
        if($controls.Local.IsChecked){$start.ArgumentList.Add('-UseLocal')}
        if($controls.Prerelease.IsChecked){$start.ArgumentList.Add('-Prerelease')}
        $script:jobStarted=[DateTime]::UtcNow
        $script:job=[Diagnostics.Process]::Start($start)
        $script:stdout=$script:job.StandardOutput.ReadToEndAsync();$script:stderr=$script:job.StandardError.ReadToEndAsync()
        $controls.Status.Text='Starting…';Set-Busy $true
    }catch{$controls.Status.Text=$_.Exception.Message;Set-Busy $false}
}
$controls.Launch.Add_Click({Begin-Action 'Launch'})
$controls.Stop.Add_Click({Begin-Action 'Stop'})
$controls.Validate.Add_Click({Begin-Action 'Validate'})
$controls.Check.Add_Click({Begin-Action 'Check'})
$controls.Update.Add_Click({Begin-Action 'Update'})
$controls.Rollback.Add_Click({Begin-Action 'Rollback'})
$controls.Logs.Add_Click({Start-Process explorer.exe -ArgumentList ('"'+$data+'"')})
$timer=[Windows.Threading.DispatcherTimer]::new();$timer.Interval=[TimeSpan]::FromMilliseconds(400)
$timer.Add_Tick({
    try{
        $statusPath=Join-Path $data 'operation.json'
        if(Test-Path -LiteralPath $statusPath){
            try{
                $state=Read-FvrJson $statusPath
                if(-not $script:jobStarted -or [DateTimeOffset]::Parse($state.utc).UtcDateTime -ge $script:jobStarted){
                    $controls.Status.Text=$state.message
                    if($state.state -eq 'restart_required'){$script:reopenRequired=$true}
                }
            }catch{} # Atomic rename may briefly contend with antivirus/readers.
        }
        if($script:job -and $script:job.HasExited){
            $out=$script:stdout.GetAwaiter().GetResult();$err=$script:stderr.GetAwaiter().GetResult()
            # Overwrite two compact operation logs, never accumulate recordings.
            [IO.File]::WriteAllText((Join-Path $data 'launcher-last.log'),$out.Substring([Math]::Max(0,$out.Length-262144))+[Environment]::NewLine+$err.Substring([Math]::Max(0,$err.Length-65536)))
            if($script:job.ExitCode -ne 0 -and $err){$controls.Status.Text=($controls.Status.Text+"`nDetails saved in Open logs.")}
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
