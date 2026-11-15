param(
 [Parameter(Mandatory=$true)][string]$SDKToolsDirectory,
 [Parameter(Mandatory=$true)][string]$GameExecutable,
 [Parameter(Mandatory=$true)][string]$OutputExecutable
)
$ErrorActionPreference='Stop'
$tool=Join-Path $SDKToolsDirectory 'EOSBootstrapperTool.exe'
$game=[IO.Path]::GetFullPath($GameExecutable)
$output=[IO.Path]::GetFullPath($OutputExecutable)
if(-not(Test-Path -LiteralPath $tool) -or -not(Test-Path -LiteralPath $game)){throw 'SDK bootstrapper tool or packaged game executable is missing.'}
if(Test-Path -LiteralPath $output){throw 'Output already exists. Choose a new bootstrapper path.'}
New-Item -ItemType Directory -Force -Path ([IO.Path]::GetDirectoryName($output)) | Out-Null
$outputDir=[IO.Path]::GetDirectoryName($output)
$baseUri=[Uri]($outputDir.TrimEnd('\')+'\')
$relative=[Uri]::UnescapeDataString($baseUri.MakeRelativeUri([Uri]$game).ToString()).Replace('/','\')
# Generate portable packaging files only. Do not install a service or launch the game here.
Push-Location -LiteralPath $outputDir
try { & $tool --output-path $output --app-path $relative --wait-for-exit true; $code=$LASTEXITCODE } finally { Pop-Location }
if($code -ne 0){throw 'EOS bootstrapper generation failed.'}
Write-Output 'Generated EOS desktop bootstrapper/config. Portable relative app path selected; validate the staged layout through both launchers before distribution.'
