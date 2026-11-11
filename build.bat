@echo off
setlocal
rem Usage: build.bat [x64|arm64]   (amd64 / amd64_arm64 also accepted)
set PLAT=%~1
if "%PLAT%"=="" set PLAT=x64
if /I "%PLAT%"=="amd64" set PLAT=x64
if /I "%PLAT%"=="amd64_arm64" set PLAT=ARM64
if /I "%PLAT%"=="arm64" set PLAT=ARM64
if /I "%PLAT%"=="x64" set PLAT=x64

set MSBUILD=%ProgramFiles(x86)%\Microsoft Visual Studio\2019\Professional\MSBuild\Current\Bin\MSBuild.exe
if not exist "%MSBUILD%" (
  for /f "usebackq tokens=*" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -requires Microsoft.Component.MSBuild -find MSBuild\**\Bin\MSBuild.exe`) do set MSBUILD=%%i
)
if not exist "%MSBUILD%" (
  echo MSBuild not found. Install Visual Studio 2019 with the C++ workload.
  exit /b 1
)

"%MSBUILD%" "%~dp0Win10XKeyboard.sln" /p:Configuration=Release /p:Platform=%PLAT% /m /v:minimal /nologo
if errorlevel 1 exit /b 1

echo Built build\%PLAT%\Release\kb.exe
