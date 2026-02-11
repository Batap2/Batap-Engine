@echo off
setlocal EnableExtensions EnableDelayedExpansion

REM --- ANSI colors ---
for /f %%a in ('echo prompt $E ^| cmd') do set "ESC=%%a"
set "RED=%ESC%[31m"
set "RESET=%ESC%[0m"

if "%~1"=="" (
  echo %RED%[ERROR]%RESET% Usage: build_msvc.bat ^<preset-name^> [--configure] [--no-format]
  exit /b 1
)

set "PRESET=%~1"
set "DO_CONFIGURE=0"
set "NO_FORMAT=0"

if /I "%~2"=="--configure" set "DO_CONFIGURE=1"
if /I "%~2"=="--no-format"   set "NO_FORMAT=1"
if /I "%~3"=="--configure" set "DO_CONFIGURE=1"
if /I "%~3"=="--no-format"   set "NO_FORMAT=1"

echo [INFO] Preset: %PRESET%

REM --- MSVC env via vswhere ---
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
  echo [ERROR] vswhere not found. Install Visual Studio Build Tools.
  exit /b 1
)

for /f "usebackq delims=" %%i in (
  `"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`
) do set "VSINSTALL=%%i"

if not defined VSINSTALL (
  echo [ERROR] MSVC Build Tools not found.
  exit /b 1
)

call "%VSINSTALL%\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 >nul
if errorlevel 1 (
  echo [ERROR] Failed to init MSVC environment.
  exit /b 1
)

REM --- optional configure ---
if "%DO_CONFIGURE%"=="1" (
  echo [INFO] Configuring...
  cmake --preset %PRESET%
  if errorlevel 1 exit /b 1
)

REM --- build (fast path) ---
echo [INFO] Building...

set "LOGFILE=%TEMP%\BatapEngine_build_%RANDOM%.log"

cmake --build --preset %PRESET% > "%LOGFILE%" 2>&1
set "BUILD_RC=%ERRORLEVEL%"

if "%NO_FORMAT%"=="1" (
  type "%LOGFILE%"
) else (
  powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0format_clang.ps1" -Path "%LOGFILE%"
)

del /q "%LOGFILE%" >nul 2>&1

rem --- ANSI colors ---
for /f %%a in ('echo prompt $E ^| cmd') do set "ESC=%%a"

if not "%BUILD_RC%"=="0" (
  echo.
  echo %ESC%[31m[BUILD FAILED]%ESC%[0m
  exit /b %BUILD_RC%
)

echo.
echo %ESC%[32m[BUILD SUCCESS]%ESC%[0m
exit /b 0
