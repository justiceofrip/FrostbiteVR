$ErrorActionPreference='Stop'
$root=Split-Path -Parent $PSScriptRoot
foreach($name in @('Setup-BC2VRPreview.ps1','Start-BC2VRPreview.ps1','Start-BC2VRSession.ps1','Stop-BC2VRSession.ps1','tools\Build-SingleplayerPreview.ps1')){
    $tokens=$null;$errors=$null
    $null=[Management.Automation.Language.Parser]::ParseFile((Join-Path $root $name),[ref]$tokens,[ref]$errors)
    if($errors.Count){throw ($errors | Out-String)}
}
$source=Get-Content -LiteralPath (Join-Path $root 'Start-BC2VRPreview.ps1') -Raw
$last=$source.IndexOf('& (Join-Path')
$harness=[scriptblock]::Create($source.Substring(0,$last)+"`n`$arguments")
$normal=& $harness
if(-not $normal.SightFlip -or -not $normal.PhysicalReload -or $normal.MagazineReload -or $normal.BodyInventory -or $normal.BoatHeadAim -or $normal.BoatHeadFire){throw 'Preview defaults expose an experiment or omit accepted path'}
$mag=& $harness -ExperimentalMagazineReload
if(-not $mag.MagazineReload -or $mag.BodyInventory){throw 'Magazine opt-in routing failed'}
$holster=& $harness -ExperimentalBodyInventory
if(-not $holster.BodyInventory -or $holster.MagazineReload){throw 'Body opt-in routing failed'}
$both=& $harness -ExperimentalMagazineReload -ExperimentalBodyInventory -Python 'C:\Example\python.exe'
if(-not $both.MagazineReload -or -not $both.BodyInventory -or $both.Python -ne 'C:\Example\python.exe'){throw 'Combined experiment routing failed'}
$aim=& $harness -ExperimentalBoatHeadAim
if(-not $aim.BoatHeadAim -or $aim.BoatHeadFire){throw 'Boat aim-only routing failed'}
$fire=& $harness -ExperimentalBoatHeadAim -ExperimentalBoatHeadFire
if(-not $fire.BoatHeadAim -or -not $fire.BoatHeadFire){throw 'Boat aim/fire routing failed'}
$rejected=$false;try{$null=& $harness -ExperimentalBoatHeadFire}catch{$rejected=$true}
if(-not $rejected){throw 'Preview fire-only mode was not rejected'}
$sessionSource=Get-Content -LiteralPath (Join-Path $root 'Start-BC2VRSession.ps1') -Raw
$sessionPrefix=$sessionSource.Substring(0,$sessionSource.IndexOf('$PSNativeCommandUseErrorActionPreference='))
$sessionHarness=[scriptblock]::Create($sessionPrefix+'; @{Aim=[bool]$BoatHeadAim;Fire=[bool]$BoatHeadFire;Controllers=[bool]$Controllers;MotionAim=[bool]$MotionAim;BodyFollow=[bool]$BodyFollow;Roomscale=[bool]$Roomscale}')
$sessionOff=& $sessionHarness
if($sessionOff.Aim -or $sessionOff.Fire){throw 'Session boat defaults changed'}
$sessionAim=& $sessionHarness -BoatHeadAim
if(-not $sessionAim.Aim -or $sessionAim.Fire -or -not $sessionAim.Controllers -or -not $sessionAim.MotionAim -or -not $sessionAim.BodyFollow -or -not $sessionAim.Roomscale){throw 'Session boat prerequisites missing'}
$sessionFire=& $sessionHarness -BoatHeadAim -BoatHeadFire
if(-not $sessionFire.Aim -or -not $sessionFire.Fire){throw 'Session boat firing routing failed'}
$rejected=$false;try{$null=& $sessionHarness -BoatHeadFire}catch{$rejected=$true}
if(-not $rejected){throw 'Session fire-only mode was not rejected'}
$tokens=$null;$errors=$null
$sessionAst=[Management.Automation.Language.Parser]::ParseInput($sessionSource,[ref]$tokens,[ref]$errors)
$nativeCalls=@($sessionAst.FindAll({param($node) $node -is [Management.Automation.Language.CommandAst] -and $node.Extent.Text.StartsWith("& (Join-Path `$PSScriptRoot 'Start-NativeTrace.ps1')")},$true))
if($nativeCalls.Count -ne 1){throw 'Missing unique native trace invocation'}
$nativeParameters=@($nativeCalls[0].CommandElements | Where-Object {$_ -is [Management.Automation.Language.CommandParameterAst]} | ForEach-Object {$_.ParameterName})
if('BoatHeadAim' -notin $nativeParameters -or 'BoatHeadFire' -notin $nativeParameters){throw 'Session did not forward both boat mode switches'}
# Evaluate the real stop script's timestamp conversion without opening any process.
$stopTokens=$null;$stopErrors=$null
$stopAst=[Management.Automation.Language.Parser]::ParseFile((Join-Path $root 'Stop-BC2VRSession.ps1'),[ref]$stopTokens,[ref]$stopErrors)
if($stopErrors.Count){throw 'Stop script parse failed'}
$timeFunctions=@($stopAst.FindAll({param($node) $node -is [Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -eq 'ConvertTo-BC2SessionStartUtc'},$true))
if($timeFunctions.Count -ne 1){throw 'Missing unique session timestamp conversion'}
. ([scriptblock]::Create($timeFunctions[0].Extent.Text))
$actual=[DateTime]::SpecifyKind([DateTime]::ParseExact('2026-10-02T21:11:50.6001236','yyyy-MM-ddTHH:mm:ss.fffffff',[Globalization.CultureInfo]::InvariantCulture),[DateTimeKind]::Utc)
$parsed=('{"started_utc":"2026-10-02T21:11:50.6001236Z"}'|ConvertFrom-Json).started_utc
$cases=@($actual,$actual.ToLocalTime(),[DateTimeOffset]::new($actual),$parsed,'2026-10-02T21:11:50.6001236Z','2026-10-02T17:11:50.6001236-04:00','2026-10-03T02:41:50.6001236+05:30')
$oldCulture=[Threading.Thread]::CurrentThread.CurrentCulture
try {
    foreach($culture in @('en-US','en-GB','de-DE')){
        [Threading.Thread]::CurrentThread.CurrentCulture=[Globalization.CultureInfo]::GetCultureInfo($culture)
        foreach($value in $cases){
            $converted=ConvertTo-BC2SessionStartUtc $value
            if($converted.Kind -ne [DateTimeKind]::Utc -or $converted.Ticks -ne $actual.Ticks){throw 'Session timestamp changed instant/kind'}
            if($actual.AddMilliseconds(200) -lt $converted -or -not ($actual.AddMilliseconds(-1) -lt $converted)){throw 'Host age comparison reversed'}
        }
    }
} finally {[Threading.Thread]::CurrentThread.CurrentCulture=$oldCulture}
foreach($bad in @($null,123,'2026-10-02T21:11:50','not-a-date',[DateTime]::SpecifyKind($actual,[DateTimeKind]::Unspecified))){
    $rejected=$false;try{$null=ConvertTo-BC2SessionStartUtc $bad}catch{$rejected=$true}
    if(-not $rejected){throw 'Ambiguous session timestamp was accepted'}
}
$tempRoot=Join-Path $root 'test-temp'
New-Item -ItemType Directory -Path $tempRoot -Force | Out-Null
$fixture=Join-Path $tempRoot ('setup-'+[Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $fixture -ErrorAction Stop | Out-Null
try {
    Copy-Item -LiteralPath (Join-Path $root 'Setup-BC2VRPreview.ps1') -Destination $fixture
    $fakeGame=Join-Path $fixture 'game';New-Item -ItemType Directory -Path $fakeGame | Out-Null
    $fakeExe=Join-Path $fakeGame 'BFBC2Game.exe';[IO.File]::WriteAllText($fakeExe,'test data, never executed')
    $expectedHash=(Get-FileHash -LiteralPath $fakeExe).Hash.ToLowerInvariant()
    @{supported_game=@{tested_sha256=$expectedHash}} | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $fixture 'FEATURES.json')
    $setup=Join-Path $fixture 'Setup-BC2VRPreview.ps1'
    $null=& $setup -GamePath $fakeGame
    $local=Join-Path $fixture 'config\local.json';$before=[IO.File]::ReadAllBytes($local)
    $null=& $setup -GamePath $fakeGame
    if([Convert]::ToBase64String($before) -ne [Convert]::ToBase64String([IO.File]::ReadAllBytes($local))){throw 'Setup rewrote unchanged config'}
    if((Get-FileHash -LiteralPath $fakeExe).Hash.ToLowerInvariant() -ne $expectedHash){throw 'Setup changed the game input'}
    [IO.File]::WriteAllText($fakeExe,'incompatible test data')
    $rejected=$false;try{$null=& $setup -GamePath $fakeGame}catch{$rejected=$true}
    if(-not $rejected -or [Convert]::ToBase64String($before) -ne [Convert]::ToBase64String([IO.File]::ReadAllBytes($local))){throw 'Unsupported executable did not preserve config'}
    # A crash reporter can still own its XML when the game exits. Failure to
    # copy that optional artifact must not prevent the terminal process report.
    $nativeSource=Get-Content -LiteralPath (Join-Path $root 'Start-NativeTrace.ps1') -Raw
    $copyStart=$nativeSource.IndexOf('$crashCopyError=$null')
    $copyEnd=$nativeSource.IndexOf('$target.Refresh()',$copyStart)
    if($copyStart -lt 0 -or $copyEnd -le $copyStart){throw 'Crash collection boundary missing'}
    $copyHarness=[scriptblock]::Create($nativeSource.Substring($copyStart,$copyEnd-$copyStart)+'; $crashCopyError')
    $folder=$fixture;$newCrash=$true;$crashPath=Join-Path $fixture 'crashreport.xml'
    [IO.File]::WriteAllText($crashPath,'<crash>fixture</crash>')
    $locked=[IO.File]::Open($crashPath,[IO.FileMode]::Open,[IO.FileAccess]::ReadWrite,[IO.FileShare]::None)
    try {if(-not (& $copyHarness)){throw 'Locked crash report did not retain its copy error'}}
    finally {$locked.Dispose()}
    if(& $copyHarness){throw 'Readable crash report retained a false copy error'}
    if([IO.File]::ReadAllText((Join-Path $folder 'crashreport-after.xml')) -ne '<crash>fixture</crash>'){throw 'Crash report contents changed'}
} finally {
    $resolved=[IO.Path]::GetFullPath($fixture)
    $allowed=[IO.Path]::GetFullPath($tempRoot).TrimEnd('\')+'\'
    if(-not $resolved.StartsWith($allowed,[StringComparison]::OrdinalIgnoreCase)){throw 'Fixture cleanup path escaped test-temp'}
    Remove-Item -LiteralPath $resolved -Recurse -Force
}
'Five script parse checks and UTC/culture timestamp cases, preview/session experiment routes, native forwarding and setup preservation/hash cases passed; no game or XR access.'
