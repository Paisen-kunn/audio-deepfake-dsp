@echo off
setlocal
cd /d "%~dp0"

where cmake >nul 2>nul
if errorlevel 1 (
    echo CMake is not installed. Install CMake and a Visual Studio C++ workload once,
    echo then run this file again. The application itself needs no internet connection.
    pause
    exit /b 1
)

cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
if errorlevel 1 goto build_failed
cmake --build build --config Release
if errorlevel 1 goto build_failed

if exist "build\Release\audio_forensics.exe" (
    start "" "build\Release\audio_forensics.exe"
) else if exist "build\audio_forensics.exe" (
    start "" "build\audio_forensics.exe"
) else (
    echo Could not find the built application.
    pause
    exit /b 1
)
exit /b 0

:build_failed
echo The C++ application could not be built. Check that Visual Studio C++ tools are installed.
pause
exit /b 1
