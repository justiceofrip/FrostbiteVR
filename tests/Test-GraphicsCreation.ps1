#requires -Version 7.5
# Root-owned deterministic check. No native DLL load, game or helper process.
param([string]$RepositoryRoot=([IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))))
$ErrorActionPreference='Stop'
. (Join-Path $RepositoryRoot 'launcher/Launcher.Core.ps1')
. (Join-Path $RepositoryRoot 'launcher/runtime/Runtime.Core.ps1')
. (Join-Path $RepositoryRoot 'launcher/runtime/GraphicsCreation.ps1')
$script:checks=0
function Check([bool]$Good,[string]$Label){if(-not $Good){throw "FAILED: $Label"};$script:checks++;Write-Host "PASS $Label"}
function Refused([scriptblock]$Action,[string]$Label){$rejected=$false;try{& $Action}catch{$rejected=$true};Check $rejected $Label}
. (Join-Path $RepositoryRoot 'tools/StorageBudget.ps1')
$folder=Join-Path $RepositoryRoot ('test-temp/graphics-creation-'+[guid]::NewGuid().ToString('N'))
$null=Assert-FvrStorageBudget -RepositoryRoot $RepositoryRoot -OutputPath $folder -Operation Session -PlannedBytes 2MB -RawCaptureBytes 0
$null=[IO.Directory]::CreateDirectory($folder)
$savedEnvironment=@{}
foreach($name in 'TEMP','TMP','TMPDIR','FVR_GRAPHICS_TEST_INHERIT','FVR_GRAPHICS_TEST_REMOVE','FVR_GRAPHICS_TEST_OVERRIDE'){$savedEnvironment[$name]=[Environment]::GetEnvironmentVariable($name,'Process')}
try {
    # Generated compiler scratch remains on the repository drive.
    $env:TEMP=$folder;$env:TMP=$folder;$env:TMPDIR=$folder
    foreach($name in 'GraphicsCreation.ps1','Session.ps1'){
        $tokens=$null;$errors=$null
        $null=[Management.Automation.Language.Parser]::ParseFile((Join-Path $RepositoryRoot ('launcher/runtime/'+$name)),[ref]$tokens,[ref]$errors)
        Check ($errors.Count -eq 0) ($name+' parses')
    }
    Initialize-Bc2GraphicsCreationAbi
    foreach($entry in @{Config=48;Ticket=48;Record=128;StartupInfo=104;ProcessInfo=24}.GetEnumerator()){
        $type=('Bc2Vr.GraphicsCreation.'+$entry.Key) -as [type]
        Check ([Runtime.InteropServices.Marshal]::SizeOf([Activator]::CreateInstance($type)) -eq $entry.Value) ($entry.Key+' matches the frozen ABI size')
    }
    Refused {[Bc2Vr.GraphicsCreation.Bridge]::new('Bc2GraphicsCreation64.dll')} 'relative bridge path refused before load'
    Refused {[Bc2Vr.GraphicsCreation.Bridge]::new((Join-Path $folder 'Wrong64.dll'))} 'wrong bridge identity refused before load'
    Refused {[Bc2Vr.GraphicsCreation.Bridge]::new((Join-Path $folder (([string][char]0xE9)+'/Bc2GraphicsCreation64.dll')))} 'non-ASCII bridge path refused before load'
    $env:FVR_GRAPHICS_TEST_INHERIT='keep'
    $env:FVR_GRAPHICS_TEST_REMOVE='remove'
    $env:FVR_GRAPHICS_TEST_OVERRIDE='old'
    $unicodeName='FVR_GRAPHICS_TEST_ORDER_'+[char]0xE9
    $unicodeValue=([string][char]0x03A9)+'='+[char]0x4E2D
    $childOverrides=@{FVR_GRAPHICS_TEST_REMOVE=$null;FVR_GRAPHICS_TEST_OVERRIDE='new';FVR_GRAPHICS_TEST_PREFIX='base';'FVR_GRAPHICS_TEST_PREFIX(x86)'='suffix';FVR_GRAPHICS_TEST_ORDER_Z='ascii'}
    $childOverrides[$unicodeName]=$unicodeValue
    $block=Get-Bc2GraphicsEnvironmentBlock $childOverrides
    $entries=$block.Split([char]0,[StringSplitOptions]::RemoveEmptyEntries)
    Check ($block.EndsWith(([string][char]0)+[char]0)) 'Unicode environment block is double-NUL terminated'
    Check ('FVR_GRAPHICS_TEST_INHERIT=keep' -cin $entries -and 'FVR_GRAPHICS_TEST_OVERRIDE=new' -cin $entries) 'inherited environment survives explicit audio-style overrides'
    Check (@($entries|Where-Object {$_ -like 'FVR_GRAPHICS_TEST_REMOVE=*'}).Count -eq 0) 'explicit null removes the child variable'
    Check ('PATH='+$env:PATH -iin $entries) 'the inherited PATH is retained'
    # Windows sorts by variable name, not the whole name=value entry. Sorting
    # entries incorrectly puts PREFIX(x86)= before PREFIX= because '(' < '='.
    # Start at index 1 to retain the name of a possible hidden =C: drive entry.
    [string[]]$names=@($entries|ForEach-Object {$_.Substring(0,$_.IndexOf('=',1))})
    [string[]]$sorted=$names.Clone();[Array]::Sort($sorted,[StringComparer]::OrdinalIgnoreCase)
    Check (($names -join [char]0) -ceq ($sorted -join [char]0)) 'environment names use Windows ordinal case-insensitive sorting'
    Check ('FVR_GRAPHICS_TEST_PREFIX=base' -cin $entries -and 'FVR_GRAPHICS_TEST_PREFIX(x86)=suffix' -cin $entries -and
        [Array]::IndexOf($names,'FVR_GRAPHICS_TEST_PREFIX') -lt [Array]::IndexOf($names,'FVR_GRAPHICS_TEST_PREFIX(x86)')) 'prefix variable names precede their punctuation suffixes'
    Check (($unicodeName+'='+$unicodeValue) -cin $entries) 'Unicode variable names and values including equals are retained exactly'
    Check ('FVR_GRAPHICS_TEST_ORDER_Z=ascii' -cin $entries -and [Array]::IndexOf($names,'FVR_GRAPHICS_TEST_ORDER_Z') -lt [Array]::IndexOf($names,$unicodeName)) 'Unicode name order is ordinal rather than linguistic'
    Check ($env:FVR_GRAPHICS_TEST_OVERRIDE -ceq 'old' -and $env:FVR_GRAPHICS_TEST_REMOVE -ceq 'remove') 'child overrides never change supervisor environment'

    $options=@{version='0.1.99';source_sha256=('a'*64)}
    $identity=@{pid=12345;start_utc='2026-11-02T12:00:01.0000000Z';path='G:\fixture\BFBC2Game.exe'}
    $owner=@{pid=$PID;start_utc='2026-11-02T12:00:00.0000000Z';path='G:\fixture\pwsh.exe'}
    $state=@{schema='bc2vr.session.v1';version=$options.version;source_sha256=$options.source_sha256;game=$identity;supervisor=$owner}
    $ticket=[Bc2Vr.GraphicsCreation.Ticket]::new();$ticket.bytes=48;$ticket.version=1;$ticket.targetPid=$identity.pid;$ticket.ownerPid=$PID
    $ticket.targetCreated=[DateTimeOffset]::Parse($identity.start_utc).UtcDateTime.ToFileTimeUtc()
    $ticket.ownerCreated=[DateTimeOffset]::Parse($owner.start_utc).UtcDateTime.ToFileTimeUtc();$ticket.token=[byte[]](1..16)
    $state.graphics_creation=@{ticket=$ticket;resumed=$false}
    $stateFile=Join-Path $folder 'session.json';Write-FvrJson $stateFile $state
    Assert-Bc2GraphicsReceipt (Read-FvrJson $stateFile) $identity $ticket $ticket.ownerCreated
    Check $true 'real atomic JSON receipt round-trip retains exact numeric births and token'
    foreach($field in 'pid','start_utc','path'){
        $saved=Read-FvrJson $stateFile;$saved.game[$field]='changed'
        Refused {Assert-Bc2GraphicsReceipt $saved $identity $ticket $ticket.ownerCreated} ('changed game '+$field+' blocks resume')
        $saved=Read-FvrJson $stateFile;$saved.supervisor[$field]='changed'
        Refused {Assert-Bc2GraphicsReceipt $saved $identity $ticket $ticket.ownerCreated} ('changed supervisor '+$field+' blocks resume')
    }
    foreach($field in 'bytes','version','targetPid','ownerPid','targetCreated','ownerCreated'){
        $saved=Read-FvrJson $stateFile;$saved.graphics_creation.ticket[$field]++
        Refused {Assert-Bc2GraphicsReceipt $saved $identity $ticket $ticket.ownerCreated} ('changed ticket '+$field+' blocks resume')
    }
    $saved=Read-FvrJson $stateFile;$saved.graphics_creation.ticket.token[0]=99
    Refused {Assert-Bc2GraphicsReceipt $saved $identity $ticket $ticket.ownerCreated} 'changed persisted token blocks resume'
    $saved=Read-FvrJson $stateFile;$saved.graphics_creation.resumed=$true
    Refused {Assert-Bc2GraphicsReceipt $saved $identity $ticket $ticket.ownerCreated} 'already-resumed receipt blocks resume'
    $saved=Read-FvrJson $stateFile;$saved.source_sha256='b'*64
    Refused {Assert-Bc2GraphicsReceipt $saved $identity $ticket $ticket.ownerCreated} 'runtime source mismatch blocks resume'
    $saved=Read-FvrJson $stateFile
    Refused {Assert-Bc2GraphicsReceipt $saved $identity $ticket ($ticket.ownerCreated+1)} 'actual supervisor birth mismatch blocks resume'

    $statusReader=[pscustomobject]@{Values=[int[]]@(1,0,0,0,0,0,0,0);Succeeds=$true}
    $statusReader|Add-Member ScriptMethod Invoke {param($Ticket,$Destination,$Count);if($Count -ne 8){throw 'status ABI count'};for($n=0;$n -lt 8;$n++){$Destination[$n]=$this.Values[$n]};return $this.Succeeds}
    $stop=[pscustomobject]@{Calls=0;Reader=$statusReader}
    $stop|Add-Member ScriptMethod Invoke {param($Ticket);$this.Calls++;$this.Reader.Values[1]=1;$this.Reader.Values[2]=1;return $true}
    $abort=[pscustomobject]@{Calls=0}
    $abort|Add-Member ScriptMethod Invoke {param($Ticket);$this.Calls++;throw 'Resumed target must never be aborted'}
    $reader=[pscustomobject]@{Calls=0}
    $reader|Add-Member ScriptMethod Invoke {param($Ticket,$Index,$Row,$Bytes);$this.Calls++;return $false}
    $script:bc2GraphicsCreation=@{created=$true;resumed=$true;collect=$false;ticket=$ticket;process=[Bc2Vr.GraphicsCreation.ProcessInfo]::new();bridge=@{ReadStatus=$statusReader;StopCollection=$stop;AbortSuspended=$abort;ReadRecord=$reader}}
    foreach($reason in 0..4){
        $statusReader.Values=[int[]]@(1,$reason,[int]($reason -ne 0),128,1,0,128,0)
        Assert-Bc2GraphicsArmed
        Check $true ('collection reason '+$reason+' and full evidence never revoke creation admission')
    }
    $statusReader.Values[0]=0
    Refused {Assert-Bc2GraphicsArmed} 'unarmed bootstrap refuses VR'
    $statusReader.Values[0]=1;$statusReader.Values[7]=5
    Refused {Assert-Bc2GraphicsArmed} 'attach error refuses VR even when armed'
    $statusReader.Values[7]=0;$statusReader.Succeeds=$false
    Refused {Assert-Bc2GraphicsArmed} 'failed status read refuses VR'
    $statusReader.Succeeds=$true;$statusReader.Values[3]=129
    Refused {Assert-Bc2GraphicsArmed} 'malformed reservation count refuses VR'
    $statusReader.Values[3]=128;$script:bc2GraphicsCreation.resumed=$false
    Refused {Assert-Bc2GraphicsArmed} 'suspended ownership alone refuses VR'
    $script:bc2GraphicsCreation.resumed=$true
    Complete-Bc2GraphicsCreation
    Check ($stop.Calls -eq 1 -and $abort.Calls -eq 0 -and $statusReader.Values[0] -eq 1) 'resumed cleanup only stops collection and leaves arming intact'
    Check (-not(Test-Path -LiteralPath (Join-Path $folder 'graphics-creation.json'))) 'normal sessions emit no export evidence file'
    $script:bc2GraphicsCreation.collect=$true
    Complete-Bc2GraphicsCreation
    $snapshot=Read-FvrJson (Join-Path $folder 'graphics-creation.json')
    Check ($reader.Calls -eq 128 -and $snapshot.incomplete_indices.Count -eq 128 -and $snapshot.records.Count -eq 0) 'opt-in evidence stays bounded and retains all incomplete records honestly'

    $source=[IO.File]::ReadAllText((Join-Path $RepositoryRoot 'launcher/runtime/GraphicsCreation.ps1'))
    Check ([regex]::Matches($source,'\$bridge\.Resume\.Invoke').Count -eq 1) 'there is exactly one production resume call site'
    $start=$source.Substring($source.IndexOf('function Start-Bc2GraphicsGame'))
    Check ($start.IndexOf('Assert-Bc2GraphicsReceipt (Read-FvrJson $stateFile)') -lt $start.IndexOf('$bridge.Resume.Invoke')) 'receipt is independently reread before the sole resume'
    Check ($start.IndexOf('Pre-resume game, parent or stop check failed.') -lt $start.IndexOf('$bridge.Resume.Invoke')) 'last executable/parent/stop gate precedes resume'
    $session=[IO.File]::ReadAllText((Join-Path $RepositoryRoot 'launcher/runtime/Session.ps1'))
    Check (-not $session.Contains('Start-Process -FilePath $exe')) 'session has no bypass through ordinary game Start-Process'
    Check ($session.Contains('Start-Bc2GraphicsGame $exe $config') -and [regex]::Matches($session,'Assert-Bc2GraphicsArmed').Count -eq 2) 'session uses the owned launch and rechecks before resolution and VR attach'
    Write-FvrJson (Join-Path $folder 'deterministic-result.json') @{schema='bc2vr.graphics-launcher.tests.v1';passed=$true;checks=$script:checks;fixture=$folder;evidence='Deterministic managed and mocked-boundary tests; no native DLL/game/headset or production acceptance.'}
    Write-Host "Graphics launcher checks passed: $script:checks"
} finally {foreach($name in $savedEnvironment.Keys){[Environment]::SetEnvironmentVariable($name,$savedEnvironment[$name],'Process')}}
