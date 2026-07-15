@echo off
setlocal enabledelayedexpansion

echo ===================================================
echo   OmniGPU: Automated clvk Build Script (Windows)
echo ===================================================
echo.
echo WARNING: Building clvk requires compiling LLVM and Clang from source.
echo This process can take 2 to 4 hours and requires high RAM / disk space.
echo.

set CLVK_DIR=%~dp0..\third_party\clvk-src
set OUTPUT_DIR=%~dp0..\third_party\clvk

if not exist "%CLVK_DIR%" (
    echo [1/4] Cloning clvk repository (recursively)...
    git clone --recursive https://github.com/kpet/clvk.git "%CLVK_DIR%"
    if !errorlevel! neq 0 (
        echo ERROR: Git clone failed.
        exit /b 1
    )
) else (
    echo [1/4] clvk repository already exists.
)

echo [2/4] Fetching clspv dependencies (LLVM, Clang, SPIR-V)...
cd /d "%CLVK_DIR%\external\clspv"
python utils/fetch_sources.py
if !errorlevel! neq 0 (
    echo ERROR: Failed to fetch clspv sources.
    exit /b 1
)

echo [3/4] Configuring CMake for clvk...
mkdir "%CLVK_DIR%\build" 2>nul
cd /d "%CLVK_DIR%\build"
cmake .. -G "Visual Studio 17 2022" -A x64 -DCMAKE_BUILD_TYPE=Release
if !errorlevel! neq 0 (
    echo ERROR: CMake configuration failed.
    exit /b 1
)

echo [4/4] Compiling clvk (Release mode)...
cmake --build . --config Release --target OpenCL
if !errorlevel! neq 0 (
    echo ERROR: Compilation failed.
    exit /b 1
)

echo.
echo ===================================================
echo   Build Successful!
echo ===================================================
echo Copying binaries to third_party/clvk...
mkdir "%OUTPUT_DIR%" 2>nul
copy /y "OpenCL.dll" "%OUTPUT_DIR%\"
copy /y "clspv.dll" "%OUTPUT_DIR%\"
copy /y "clvk.dll" "%OUTPUT_DIR%\"

echo.
echo Done! Please configure CMake with -DOMNIGPU_FETCH_CLVK=ON to deploy clvk.
