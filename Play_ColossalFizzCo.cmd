@echo off
setlocal
set "PrototypeEngine=%UE_ENGINE_ROOT%"
if not defined PrototypeEngine set "PrototypeEngine=C:\Program Files\Epic Games\UE_5.8"
set "PrototypePython=%PrototypeEngine%\Engine\Binaries\ThirdParty\Python3\Win64\python.exe"
if not exist "%PrototypePython%" (
  echo Unreal Engine Python was not found. Set UE_ENGINE_ROOT to your Unreal installation.
  pause
  exit /b 1
)
"%PrototypePython%" "%~dp0Scripts\LaunchPrototype.py" %*
if errorlevel 1 pause
