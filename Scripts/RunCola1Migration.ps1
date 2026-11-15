param(
    [string]$ProjectRoot = (Split-Path -Parent $PSScriptRoot),
    [string]$EngineRoot = 'C:/Program Files/Epic Games/UE_5.8'
)
$ErrorActionPreference = 'Stop'
$ProjectRoot = (Resolve-Path -LiteralPath $ProjectRoot).Path
$project = Join-Path $ProjectRoot 'ColossalFizzCo.uproject'
if (-not (Test-Path -LiteralPath $project)) { throw 'ColossalFizzCo.uproject is missing' }
$output = Join-Path $ProjectRoot 'Saved/Cola1Migration'
New-Item -ItemType Directory -Path $output -Force | Out-Null
$content = Join-Path $ProjectRoot 'Content'
$backup = Join-Path $output 'Before'
# The builder also resaves non-bottle actors. Preserve those packages before it
# runs; the Python migration separately backs up all other direct referencers.
foreach ($relative in @('__ExternalActors__/FirstPerson/Lvl_FirstPerson', '__ExternalObjects__/FirstPerson/Lvl_FirstPerson',
                        '__ExternalActors__/ColaShopTests/WalletReplication', '__ExternalObjects__/ColaShopTests/WalletReplication')) {
    $folder = Join-Path $content $relative
    if (-not (Test-Path -LiteralPath $folder)) { continue }
    foreach ($file in Get-ChildItem -LiteralPath $folder -File -Recurse) {
        $destination = Join-Path $backup ([IO.Path]::GetRelativePath($content, $file.FullName))
        if (Test-Path -LiteralPath $destination) { continue }
        New-Item -ItemType Directory -Path (Split-Path -Parent $destination) -Force | Out-Null
        Copy-Item -LiteralPath $file.FullName -Destination $destination
    }
}
$editor = Join-Path $EngineRoot 'Engine/Binaries/Win64/UnrealEditor-Cmd.exe'
function Invoke-MigrationStep([string]$Name, [string[]]$StepArguments) {
    $arguments = @(('"'+$project+'"')) + $StepArguments + @('-nullrhi', '-nosound', '-unattended', '-nop4', '-NoLiveCoding',
        ('"-UserDir='+$ProjectRoot+'/Saved/Cola1MigrationUser"'),
        '-ini:EditorPerProjectUserSettings:[/Script/ModelContextProtocolEngine.ModelContextProtocolSettings]:bAutoStartServer=False',
        ('"-abslog='+$output+'/'+$Name+'.log"'))
    $process = Start-Process -FilePath $editor -ArgumentList $arguments -WorkingDirectory $ProjectRoot -WindowStyle Hidden -PassThru -Wait
    if ($process.ExitCode -ne 0) { throw ($Name+' failed; see '+$output) }
}
Invoke-MigrationStep 'Assets' @('/Engine/Maps/Entry', '-run=pythonscript', ('"-script='+$PSScriptRoot+'/MigrateLegacyCola1.py"'))
foreach ($map in @('/Game/FirstPerson/Lvl_FirstPerson', '/Game/ColaShopTests/WalletReplication')) {
    Invoke-MigrationStep ('World_'+$map.Split('/')[-1]) @($map, '-run=WorldPartitionBuilderCommandlet', '-SCCProvider=None', '-Builder=WorldPartitionResaveActorsBuilder')
}
Invoke-MigrationStep 'Audit' @('/Engine/Maps/Entry', '-run=pythonscript', ('"-script='+$PSScriptRoot+'/AuditLegacyCola1.py"'))
Write-Output ('Cola1 migration saved. Audit: '+$ProjectRoot+'/Saved/Cola1MigrationUser/Saved/Cola1Migration/Audit.json')
