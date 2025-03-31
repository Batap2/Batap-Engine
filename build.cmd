@echo off
setlocal

:: Vérifier si CMake est installé
where cmake >nul 2>nul
if %ERRORLEVEL% neq 0 (
    echo CMake is not installed
    pause
    exit /b 1
)

:: Vérifier si Ninja est installé
where ninja >nul 2>nul
if %ERRORLEVEL% neq 0 (
    echo Ninja is not installed
    pause
    exit /b 1
)

:: Définition des dossiers
set "PROJECT_ROOT=%CD%"
set "BUILD_DIR=%PROJECT_ROOT%\build"

:: Créer le dossier build s'il n'existe pas
if not exist "%BUILD_DIR%" (
    echo Create build folder
    mkdir "%BUILD_DIR%"
)

cd /d "%BUILD_DIR%"

:: Exécuter CMake pour générer compile_commands.json
echo Generate compile_commands.json...
cmake -DCMAKE_EXPORT_COMPILE_COMMANDS=ON -G "Ninja" .. 

:: Vérifier si compile_commands.json a été généré
if exist "%BUILD_DIR%\compile_commands.json" (
    echo compile_commands.json Generated
    copy /Y "%BUILD_DIR%\compile_commands.json" "%PROJECT_ROOT%"
    echo Copy of compile_commands.json to root
) else (
    echo Error : compile_commands could not be generated
)

cd /d "%PROJECT_ROOT%"

:: Empêche la fermeture automatique de la fenêtre
pause
exit /b 0