@echo off
setlocal enabledelayedexpansion

set "SCRIPT_DIR=%~dp0"
set "PROJECT_DIR=%SCRIPT_DIR%.."
set "PIO_CMD=python -m platformio"
set "ENV=ch32vdev"

if "%1"=="" (
    set "ACTION=upload"
) else if "%1"=="build" (
    set "ACTION=build"
) else if "%1"=="upload" (
    set "ACTION=upload"
) else if "%1"=="erase" (
    set "ACTION=erase"
) else if "%1"=="monitor" (
    set "ACTION=monitor"
) else if "%1"=="clean" (
    set "ACTION=clean"
) else (
    echo Unknown action: %1
    echo Usage: flash.bat {build^|upload^|erase^|monitor^|clean}
    exit /b 1
)

echo ========================================
echo ILI9342 TFT LCD - WCH-Link Flasher
echo Target: CH32V305
echo Env:    %ENV%
echo Action: %ACTION%
echo ========================================
echo.

cd /d "%PROJECT_DIR%"

if "%ACTION%"=="build" (
    echo [*] Building firmware...
    %PIO_CMD% run -e %ENV%
    if !ERRORLEVEL! neq 0 (
        echo [FAIL] Build failed
        exit /b 1
    )
    echo [OK] Build complete
    goto :end
)

if "%ACTION%"=="upload" (
    echo [*] Building and uploading...
    %PIO_CMD% run -e %ENV% -t upload
    if !ERRORLEVEL! neq 0 (
        echo [FAIL] Upload failed
        echo.
        echo Possible causes:
        echo   1. WCH-Link not connected
        echo   2. Driver not installed
        echo   3. Target board not powered
        exit /b 1
    )
    echo [OK] Upload successful
    goto :end
)

if "%ACTION%"=="erase" (
    echo [*] Erasing chip...
    %PIO_CMD% run -e %ENV% -t erase
    if !ERRORLEVEL! neq 0 (
        echo [FAIL] Erase failed
        exit /b 1
    )
    echo [OK] Erase complete
    goto :end
)

if "%ACTION%"=="monitor" (
    echo [*] Opening serial monitor...
    echo Hint: Press Ctrl+C to exit
    %PIO_CMD% device monitor -e %ENV%
    goto :end
)

if "%ACTION%"=="clean" (
    echo [*] Cleaning build artifacts...
    %PIO_CMD% run -e %ENV% -t clean
    if !ERRORLEVEL! neq 0 (
        echo [FAIL] Clean failed
        exit /b 1
    )
    echo [OK] Clean complete
    goto :end
)

:end
cd /d "%SCRIPT_DIR%"
echo.
echo Done.
exit /b 0
