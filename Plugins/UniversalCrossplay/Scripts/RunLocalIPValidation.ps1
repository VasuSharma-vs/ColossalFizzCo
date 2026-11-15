param(
 [Parameter(Mandatory=$true)][string]$ProjectFile,
 [Parameter(Mandatory=$true)][string]$EditorExecutable,
 [Parameter(Mandatory=$true)][string]$OutputDirectory,
 [int]$Port=17962
)
$ErrorActionPreference='Stop'
$project=(Resolve-Path -LiteralPath $ProjectFile).Path
$editor=(Resolve-Path -LiteralPath $EditorExecutable).Path
if($Port -lt 1024 -or $Port -gt 65535){throw 'Use an unused port between 1024 and 65535.'}
$run=[IO.Path]::GetFullPath($OutputDirectory)
if(Test-Path -LiteralPath $run){throw 'Use a new output directory; previous evidence is never overwritten.'}
New-Item -ItemType Directory -Path $run | Out-Null
function Peer([string]$name,[string]$url) {
 $a=@(('"'+$project+'"'),$url,'-game','-nullrhi','-nosound','-unattended','-nop4','-CrossplayNetworkTest',('-port='+$Port),'-PktLag=50','-PktLoss=2','-ExecCmds="t.MaxFPS 30,t.IdleWhenNotForeground 0"',('-UserDir="'+$run+'/'+$name+'User"'),('-abslog="'+$run+'/'+$name+'.log"'))
 Start-Process -FilePath $editor -ArgumentList $a -WindowStyle Hidden -PassThru
}
$hostPeer=Peer 'Host' '/Engine/Maps/Entry?listen'
$deadline=(Get-Date).AddSeconds(100)
do {
 Start-Sleep -Seconds 2
 $hostLog=if(Test-Path (Join-Path $run 'Host.log')){Get-Content (Join-Path $run 'Host.log') -Raw}else{''}
 if($hostPeer.HasExited -or (Get-Date) -gt $deadline){throw 'Host did not start listening. Inspect the host log.'}
}until($hostLog -match 'GameNetDriver.*listening|IpNetDriver.*listening')
$clientPeer=Peer 'Client' ('127.0.0.1:'+$Port)
$deadline=(Get-Date).AddSeconds(150)
do {Start-Sleep -Seconds 2;if((Get-Date) -gt $deadline){throw 'Independent test deadline exceeded. Inspect both logs.'}}until($hostPeer.HasExited -and $clientPeer.HasExited)
$h=Get-Content (Join-Path $run 'Host.log') -Raw
$c=Get-Content (Join-Path $run 'Client.log') -Raw
$checks=[ordered]@{
 ServerAccepted=$h -match 'UCP_NETWORK_SERVER_PASSED'
 ClientObserved=$c -match 'UCP_NETWORK_CLIENT_PASSED'
 CorrectSDK=$h -match 'Initializing EOSSDK Version:1.19.2.1' -and $c -match 'Initializing EOSSDK Version:1.19.2.1'
 NoFatal=($h+$c) -notmatch 'UCP_NETWORK_FAILED|Fatal error:|Assertion failed:|ReliableBufferOverflow'
 CleanExit=$hostPeer.ExitCode -eq 0 -and $clientPeer.ExitCode -eq 0
}
$result=[ordered]@{Run=$run;Checks=$checks;Scope='Offline independent IP listen server/client. Requested 50ms/2% emulation. Does not verify Steam/Epic authentication, EOS P2P or RTC.'}
$result | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $run 'Result.json') -Encoding utf8
$result | ConvertTo-Json -Depth 5
if(@($checks.Values | Where-Object {-not $_}).Count){throw 'Independent plugin network validation failed.'}
