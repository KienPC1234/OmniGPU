@echo off
setlocal

:: ============================================================================
:: OmniGPU Build Script for Windows
::
:: Usage:
::   build.bat              Incremental build (fast, default)
::   build.bat clean        Full clean build (slow - rebuilds Qt5 + FFmpeg)
::   build.bat debug        Build debug preset
::   build.bat help         Show this help
:: ============================================================================

set "VCVARS=C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvarsall.bat"

if /I "%1"=="help" (
    echo.
    echo OmniGPU Build Script
    echo ====================
    echo.
    echo   build.bat              Incremental build (release, default)
    echo   build.bat clean        Full clean build ^(slow! rebuilds Qt5 + FFmpeg^)
    echo   build.bat debug        Build debug preset incrementally
    echo   build.bat debug clean  Full clean debug build
    echo   build.bat help         This message
    echo.
    echo NOTE: Clean builds rebuild FFmpeg + Qt5 from source ^(~30 min^).
    echo       Use plain "build.bat" for daily work.
    echo.
    exit /b 0
)

if not exist "%VCVARS%" (
    echo Error: Could not find Visual Studio vcvarsall.bat at %VCVARS%
    exit /b 1
)

call "%VCVARS%" x64

set "PRESET=release"
set "CLEAN="

:parse
if /I "%1"=="debug" set "PRESET=debug"
if /I "%1"=="clean" set "CLEAN=--clean-first"
shift /1
if not "%1"=="" goto parse

echo.
echo ========================================================
echo Configuring CMake (preset: %PRESET%)...
echo ========================================================
cmake --preset %PRESET% -DCMAKE_CXX_CLANG_TIDY=""

if %ERRORLEVEL% neq 0 (
    echo CMake configuration failed!
    exit /b %ERRORLEVEL%
)

echo.
echo ========================================================
echo Building the project...
echo ========================================================
cmake --build --preset %PRESET% %CLEAN% --parallel

if %ERRORLEVEL% eq 0 (
    echo.
    echo Build completed successfully!
    echo Binaries: build\%PRESET%\bin\
) else (
    echo.
    echo Build failed!
)
