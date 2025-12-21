@echo off
setlocal EnableExtensions EnableDelayedExpansion

if "%~1"=="" (
  echo [ERROR] Usage: build_msvc.bat ^<preset-name^> [--configure]
  exit /b 1
)

set "PRESET=%~1"
set "DO_CONFIGURE=0"
if /I "%~2"=="--configure" set "DO_CONFIGURE=1"

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
cmake --build --preset %PRESET%
if errorlevel 1 exit /b 1

echo.
echo [SUCCESS] Build finished: %PRESET%
exit /b 0
