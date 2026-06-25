@echo off
setlocal

set "SRC=example\client.cpp"
set "OUT=example\client.exe"

call :setup_msvc
if not errorlevel 1 (
    where cl >nul 2>nul
    if not errorlevel 1 (
        echo Building with MSVC...
        cl /EHsc /std:c++17 /O2 "%SRC%" /Fe"%OUT%"
        exit /b %ERRORLEVEL%
    )
)

where g++ >nul 2>nul
if not errorlevel 1 (
    echo Building with MinGW...
    g++ -std=c++17 -O2 "%SRC%" -o "%OUT%"
    exit /b %ERRORLEVEL%
)

echo No C++ compiler found. Please install MSVC or MinGW.
exit /b 1

:setup_msvc
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" set "VSWHERE=%ProgramFiles%\Microsoft Visual Studio\Installer\vswhere.exe"

if exist "%VSWHERE%" (
    for /f "usebackq tokens=*" %%I in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath 2^>nul`) do set "VSINSTALL=%%I"
)

if defined VSINSTALL (
    if exist "%VSINSTALL%\VC\Auxiliary\Build\vcvars64.bat" (
        call "%VSINSTALL%\VC\Auxiliary\Build\vcvars64.bat" >nul
        exit /b 0
    )
)

if defined VCToolsInstallDir (
    exit /b 0
)

exit /b 1
