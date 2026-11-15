param(
    [string]$EngineRoot = 'C:/Program Files/Epic Games/UE_5.8',
    [string]$GameExecutable = '',
    [ValidateRange(1024,65535)][int]$Port = 17893,
    [ValidateRange(120,600)][int]$TestSeconds = 240,
    [ValidateRange(0,250)][int]$PacketLagMs = 0,
    [ValidateRange(0,10)][int]$PacketLossPercent = 0,
    [ValidateRange(2,8)][int]$CustomerSales=2,
    [switch]$LateJoin,
    [switch]$ManufacturingOnly,
    [switch]$PressureTest,
    [switch]$ReloadBottlePressureTest,
    [switch]$RenderClientEvidence
)
$ErrorActionPreference = 'Stop'
$assemblyRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$assemblyEditor = Join-Path $EngineRoot 'Engine/Binaries/Win64/UnrealEditor.exe'
if ($GameExecutable) { $assemblyEditor = [IO.Path]::GetFullPath($GameExecutable) }
$assemblyProject = Join-Path $assemblyRoot 'ColossalFizzCo.uproject'
if (-not (Test-Path -LiteralPath $assemblyEditor)) { throw 'Unreal Editor executable missing.' }
$assemblyRun = Join-Path $assemblyRoot ('Saved/ManufacturingQA/Network_'+(Get-Date -Format 'yyyyMMdd_HHmmss')+'_'+$Port)
New-Item -ItemType Directory -Path $assemblyRun -Force | Out-Null
$assemblyCommon = @('-game','-nullrhi','-nosound','-unattended','-nop4','-nosplash',
    '-MULTIHOME=127.0.0.1','-NormalColaReinvestmentTest','-ExecCmds="t.MaxFPS 30,t.IdleWhenNotForeground 0"','-LogCmds="LogTemp Verbose"',
    ('-PktLag='+$PacketLagMs),('-PktLoss='+$PacketLossPercent),('-NormalColaTestSales='+$CustomerSales),('-NormalColaVisitDeadline='+($TestSeconds-60)),
    '-ini:EditorPerProjectUserSettings:[/Script/ModelContextProtocolEngine.ModelContextProtocolSettings]:bAutoStartServer=False')
if ($ManufacturingOnly) { $assemblyCommon += '-ManualColaManufacturingTest' }
if ($ReloadBottlePressureTest) { $PressureTest = $true; $assemblyCommon += '-ReloadBottlePressureTest' }
if ($PressureTest) { $assemblyCommon += '-NormalColaPressureTest' }
if ($RenderClientEvidence) { $assemblyCommon += '-BottleVisualTest' }
function Start-AssemblyPeer([string]$Name,[string]$URL,[int]$Seconds) {
    $assemblyLog = Join-Path $assemblyRun ($Name+'.log')
    $assemblyUser = Join-Path $assemblyRun ($Name+'User')
    $assemblyEntry = if ($GameExecutable) { @($URL) } else { @('"'+$assemblyProject+'"',$URL) }
    $assemblyArguments = @($assemblyEntry) + $assemblyCommon + @(('-NormalColaTestExitAfter='+$Seconds),
        ('-UserDir="'+$assemblyUser+'"'),('-abslog="'+$assemblyLog+'"'))
    if ($Name -eq 'Host') { $assemblyArguments += '-port='+$Port }
    if ($Name -eq 'Client' -and $RenderClientEvidence) {
        $assemblyArguments = @($assemblyArguments | Where-Object { $_ -ne '-nullrhi' })
        $assemblyArguments += @('-Windowed','-ResX=1280','-ResY=720',('-BottleVisualEvidence="'+$assemblyRun+'"'))
    }
    Start-Process -FilePath $assemblyEditor -ArgumentList $assemblyArguments -WindowStyle Hidden -PassThru
}
$assemblyHostLog = Join-Path $assemblyRun 'Host.log'
$assemblyClientLog = Join-Path $assemblyRun 'Client.log'
$assemblyHost = Start-AssemblyPeer 'Host' '/Game/NormalColaPrototype/Lvl_NormalColaPrototype_Validation?listen' $TestSeconds
Write-Output ('Assembly network evidence: '+$assemblyRun)
$assemblyDeadline = (Get-Date).AddSeconds(90)
do {
    Start-Sleep -Seconds 2
    $assemblyText = if (Test-Path -LiteralPath $assemblyHostLog) { Get-Content -LiteralPath $assemblyHostLog -Raw } else { '' }
    if ($assemblyHost.HasExited) { throw ('Host exited before listening: '+$assemblyHostLog) }
    if ((Get-Date) -gt $assemblyDeadline) { throw ('Host listen deadline exceeded; bounded exit remains active: '+$assemblyHostLog) }
} until ($assemblyText -match 'GameNetDriver.*listening|IpNetDriver.*listening')
$assemblyClient = Start-AssemblyPeer 'Client' ('127.0.0.1:'+$Port) $TestSeconds
$assemblyLate = $null
# The peer exit ticker uses game delta time; allow a bounded wall-clock margin
# for unattended builds whose simulation runs slower than real time.
$assemblyDeadline = (Get-Date).AddSeconds([Math]::Ceiling($TestSeconds*1.5)+90)
do {
    Start-Sleep -Seconds 2
    $assemblyText = Get-Content -LiteralPath $assemblyHostLog -Raw
    if ($assemblyText -match 'NORMAL_COLA_PLAYTEST_FAILED|SHOP_SCANNER_ACTION .*result=EShopCheckoutResult::(?!Success|AlreadyCompleted)') {
        Write-Output 'Stopping isolated peers after a rejected validation action.'
        foreach ($assemblyPeer in @($assemblyHost,$assemblyClient,$assemblyLate)) {
            if ($assemblyPeer -and -not $assemblyPeer.HasExited) {
                Stop-Process -Id $assemblyPeer.Id -Force
                $assemblyPeer.WaitForExit(5000) | Out-Null
            }
        }
        break
    }
    if ($LateJoin -and -not $assemblyLate -and $assemblyText -match 'NORMAL_COLA_PEER_ACCEPTANCE authority=1 stage=200') {
        $assemblyLate = Start-AssemblyPeer 'LateClient' ('127.0.0.1:'+$Port) 45
        Write-Output 'Joining after the manufactured bottle is sealed and shelved.'
    }
    if ((Get-Date) -gt $assemblyDeadline) { throw ('Graceful exit deadline exceeded. Inspect '+$assemblyRun) }
} until ($assemblyHost.HasExited -and $assemblyClient.HasExited -and (-not $assemblyLate -or $assemblyLate.HasExited))
$assemblyHostText = Get-Content -LiteralPath $assemblyHostLog -Raw
$assemblyClientText = Get-Content -LiteralPath $assemblyClientLog -Raw
$assemblyCombined = $assemblyHostText+$assemblyClientText
$assemblyPattern = 'NORMAL_COLA_PEER_ACCEPTANCE authority=(?<authority>\d) stage=200 bank=(?<bank>-?\d+) cash=(?<cash>-?\d+) receipts=(?<receipts>\d+) bottle=\[content=(?<content>\d+) level=(?<level>[\d.]+) sealed=(?<seal>[\d.]+) cap=(?<cap>\d+) source=(?<source>[\d.]+)\]'
$assemblyServer = [regex]::Matches($assemblyHostText,$assemblyPattern) | Select-Object -Last 1
$assemblyRemote = [regex]::Matches($assemblyClientText,$assemblyPattern) | Select-Object -Last 1
$assemblyChecks = [ordered]@{
    LegacyMeshAliasOnBothPeers = $assemblyHostText -match 'COLA1_COOKED_ALIAS_PASSED' -and $assemblyClientText -match 'COLA1_COOKED_ALIAS_PASSED'
    NoUnexpectedActionRejection = $assemblyCombined -notmatch 'SHOP_SCANNER_ACTION .*result=EShopCheckoutResult::(?!Success|AlreadyCompleted)'
    NoFatalOrDisconnect = $assemblyCombined -notmatch 'Fatal error:|Assertion failed:|NORMAL_COLA_PLAYTEST_FAILED|ReliableBufferOverflow|ConnectionTimeout'
    HostCompletedManufacturing = [bool]$assemblyServer
    ClientObservedManufacturing = [bool]$assemblyRemote
    GracefulExit = $assemblyHost.ExitCode -eq 0 -and $assemblyClient.ExitCode -eq 0
}
if ($assemblyServer -and $assemblyRemote) {
    $assemblyChecks['PeerStateAgreement'] = @('bank','cash','receipts','content','level','seal','cap','source').Where({$assemblyServer.Groups[$_].Value -ne $assemblyRemote.Groups[$_].Value}).Count -eq 0
    $assemblyChecks['FullSealedBottleAndDebitedBulk'] = $assemblyServer.Groups['level'].Value -eq '1.000' -and $assemblyServer.Groups['seal'].Value -eq '1.000' -and [int]$assemblyServer.Groups['cap'].Value -gt 0 -and $assemblyServer.Groups['source'].Value -eq '0.900'
}
if ($PacketLagMs -gt 0 -or $PacketLossPercent -gt 0) {
    $assemblyChecks['AppliedNetworkEmulation'] = $assemblyHostText -match ('NORMAL_COLA_NET_PROFILE authority=1 lag_ms='+$PacketLagMs+' loss_pct='+$PacketLossPercent) -and $assemblyClientText -match ('NORMAL_COLA_NET_PROFILE authority=0 lag_ms='+$PacketLagMs+' loss_pct='+$PacketLossPercent)
}
if ($LateJoin) {
    $assemblyLateText = if ($assemblyLate) { Get-Content -LiteralPath (Join-Path $assemblyRun 'LateClient.log') -Raw } else { '' }
    $assemblyLateState = [regex]::Matches($assemblyLateText,$assemblyPattern) | Select-Object -Last 1
    $assemblyChecks['LateJoinObservedSameProduct'] = [bool]$assemblyLateState -and $assemblyServer -and @('content','level','seal','cap','source').Where({$assemblyServer.Groups[$_].Value -ne $assemblyLateState.Groups[$_].Value}).Count -eq 0
}
$assemblyPeerPattern = 'MODULAR_BOTTLE_PEER root=\d+ cap=\d+ liquid=\d+'
$assemblyHostIDs = [regex]::Match($assemblyHostText,$assemblyPeerPattern).Value
$assemblyChecks['MatchingBottleCapLiquidIdentities'] = $assemblyHostIDs.Length -gt 0 -and [regex]::Match($assemblyClientText,$assemblyPeerPattern).Value -eq $assemblyHostIDs
if ($LateJoin) {
    $assemblyChecks['LateJoinMatchingHierarchy'] = [regex]::Match($assemblyLateText,$assemblyPeerPattern).Value -eq $assemblyHostIDs -and $assemblyHostIDs.Length -gt 0
}
$assemblyExpectedPeers = if ($LateJoin) { 2 } else { 1 }
$assemblyChecks['InitialSnapshotsAcknowledged'] = ([regex]::Matches($assemblyHostText,'Prop snapshot acknowledged by ([^ ]+)') | ForEach-Object { $_.Groups[1].Value } | Select-Object -Unique | Measure-Object).Count -ge $assemblyExpectedPeers
if ($PressureTest) {
    $assemblyChecks['ServerCompletedPressureFlight'] = $assemblyHostText -match 'NORMAL_COLA_PRESSURE_FLIGHT_PASSED'
    $assemblyChecks['ClientObservedEmptyPressureHandoff'] = $assemblyClientText -match 'NORMAL_COLA_PEER_ACCEPTANCE authority=0 stage=400'
}
if ($ReloadBottlePressureTest) { $assemblyChecks['SavedFallenBottleReloadAndPhysicalShake'] = $assemblyHostText -match 'BOTTLE_RELOAD_PEER_FIXTURE_PASSED' }
$assemblyScope = if ($ManufacturingOnly) { 'Real peers and owned RPCs: manual filling, venting, solo closure, carry, shelf stock and late join. Isolated test funds; excludes NPC route/checkout acceptance.' } else { 'Real editor-game peers: customer sales, banking, supply purchasing, manual manufacturing, carry, shelf stock and late join. Hardware input/appearance need separate acceptance.' }
$assemblyResult = [ordered]@{Run=$assemblyRun;ManufacturingOnly=[bool]$ManufacturingOnly;PacketLagMs=$PacketLagMs;PacketLossPercent=$PacketLossPercent;Checks=$assemblyChecks;Scope=$assemblyScope}
$assemblyResult | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $assemblyRun 'Result.json') -Encoding utf8
$assemblyResult | ConvertTo-Json -Depth 6
if (@($assemblyChecks.Values | Where-Object { -not $_ }).Count) { throw ('Assembly network checks failed: '+$assemblyRun) }
