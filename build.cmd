@echo off
setlocal

if /i "%~1"=="gui" (
    call :build_gui
    exit /b %ERRORLEVEL%
)

python -m platformio run %*
exit /b %ERRORLEVEL%

:build_gui
set "SRC=example\client.cpp"
set "OUT=example\client.exe"
set "BUILD_DIR=build"

if not exist "%BUILD_DIR%" mkdir "%BUILD_DIR%"

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" set "VSWHERE=%ProgramFiles%\Microsoft Visual Studio\Installer\vswhere.exe"

if exist "%VSWHERE%" (
    for /f "usebackq tokens=*" %%I in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath 2^>nul`) do set "VSINSTALL=%%I"
)

if defined VSINSTALL (
    if exist "%VSINSTALL%\VC\Auxiliary\Build\vcvars64.bat" (
        call "%VSINSTALL%\VC\Auxiliary\Build\vcvars64.bat" >nul
        if not errorlevel 1 (
            echo Building GUI client with MSVC...
            cl /EHsc /std:c++17 /O2 "%SRC%" /Fe"%OUT%" /link comctl32.lib shell32.lib
            exit /b %ERRORLEVEL%
        )
    )
)

echo MSVC was not found. Please install Visual Studio Build Tools with C++ support.
exit /b 1
