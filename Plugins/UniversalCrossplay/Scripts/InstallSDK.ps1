param([string]$SDKRoot='')
$ErrorActionPreference='Stop'
$plugin=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$source=if($SDKRoot){Join-Path $SDKRoot 'SDK'}else{Join-Path $plugin 'ThirdParty/EOSSDK'}
$dll=Join-Path $source 'Bin/EOSSDK-Win64-Shipping.dll'
if(-not(Test-Path -LiteralPath $dll)){throw 'The supplied EOS SDK DLL was not found.'}
$target=Join-Path $plugin 'Binaries/ThirdParty/Win64'
New-Item -ItemType Directory -Force -Path $target | Out-Null
Copy-Item -LiteralPath $dll -Destination (Join-Path $target 'EOSSDK-Win64-Shipping.dll')
Write-Output 'Project-local EOS runtime installed. Engine files were not modified.'
