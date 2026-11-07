@echo off
setlocal
rem Usage: build.bat [amd64|amd64_arm64] [x64|arm64]
set ARCH=%~1
if "%ARCH%"=="" set ARCH=amd64
set SUFFIX=%~2
if "%SUFFIX%"=="" set SUFFIX=x64

set VSDIR=D:\Microsoft Visual Studio\2019\Professional
if not exist "%VSDIR%\VC\Auxiliary\Build\vcvarsall.bat" (
  echo VS2019 not found at %VSDIR%
  exit /b 1
)

call "%VSDIR%\VC\Auxiliary\Build\vcvarsall.bat" %ARCH% >nul
if errorlevel 1 (
  echo vcvarsall failed for %ARCH%
  exit /b 1
)

if not exist build mkdir build

cl /nologo /W4 /EHsc /std:c++17 /O2 /DUNICODE /D_UNICODE /D_WIN32_WINNT=0x0A00 ^
   src\main.cpp src\layout.cpp src\render.cpp src\input.cpp ^
   /Fe:build\kb_%SUFFIX%.exe /Fo:build\ ^
   /link /SUBSYSTEM:WINDOWS
if errorlevel 1 exit /b 1

echo Built build\kb_%SUFFIX%.exe
