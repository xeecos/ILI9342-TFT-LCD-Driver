@echo off
setlocal

where cmake >nul 2>nul
if errorlevel 1 (
    echo CMake not found. Please install CMake and try again.
    exit /b 1
)

:: Clean previous build directory if it exists
if exist "build" (
    echo Cleaning previous build directory...
    rmdir /s /q build 2>nul
)

echo Configuring with CMake...
cmake -B build -S example -DCMAKE_BUILD_TYPE=Release -T v143
if errorlevel 1 (
    echo CMake configuration failed.
    exit /b %ERRORLEVEL%
)

echo Building...
cmake --build build --config Release
if errorlevel 1 (
    echo Build failed.
    exit /b %ERRORLEVEL%
)

:: Copy the executable to the example directory for convenience
if exist "build\Release\serial_image_gui.exe" (
    copy /Y "build\Release\serial_image_gui.exe" "example\client.exe" >nul
) else if exist "build\serial_image_gui.exe" (
    copy /Y "build\serial_image_gui.exe" "example\client.exe" >nul
)

echo Built: example\client.exe
exit /b 0
