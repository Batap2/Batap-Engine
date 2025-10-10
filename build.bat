@echo off
setlocal enabledelayedexpansion

:: Vérifier si CMake est installé
where cmake >nul 2>nul
if %ERRORLEVEL% neq 0 (
    echo CMake is not installed
    exit /b 1
)

:: Vérifier si Ninja est installé
where ninja >nul 2>nul
if %ERRORLEVEL% neq 0 (
    echo Ninja is not installed
    exit /b 1
)

:: Définition des dossiers
set "PROJECT_ROOT=%CD%"
set "BUILD_DIR=%PROJECT_ROOT%\build"

:: Créer le dossier build s'il n'existe pas
if not exist "%BUILD_DIR%" (
    echo [INFO] Creating build folder...
    mkdir "%BUILD_DIR%"
)

cd /d "%BUILD_DIR%"

:: Exécuter CMake pour générer les fichiers de build
echo.
echo [INFO] Generating build files with CMake...
cmake -DCMAKE_EXPORT_COMPILE_COMMANDS=ON -G "Ninja" .. 
if %ERRORLEVEL% neq 0 (
    echo [ERROR] CMake configuration failed
    cd /d "%PROJECT_ROOT%"
    exit /b 1
)

:: Compiler le projet
echo.
echo [INFO] Compiling project with Ninja...
ninja
if %ERRORLEVEL% neq 0 (
    echo [ERROR] Compilation failed
    cd /d "%PROJECT_ROOT%"
    exit /b 1
)

echo.
echo [SUCCESS] Compilation successful

:: Vérifier si compile_commands.json a été généré
if exist "%BUILD_DIR%\compile_commands.json" (
    echo compile_commands.json Generated
    copy /Y "%BUILD_DIR%\compile_commands.json" "%PROJECT_ROOT%"
    echo Copy of compile_commands.json to root
) else (
    echo Error : compile_commands could not be generated
)

cd /d "%PROJECT_ROOT%"
::echo.
::echo [DONE] Build process completed

exit /b 0