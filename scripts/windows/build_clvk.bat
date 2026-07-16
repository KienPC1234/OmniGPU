@echo off
setlocal enabledelayedexpansion

echo ===================================================
echo   OmniGPU: Automated clvk Build Script (Windows)
echo ===================================================
echo.

set CLVK_DIR=%~dp0..\..\third_party\clvk

if not exist "%CLVK_DIR%\CMakeLists.txt" (
    echo ERROR: clvk submodule is not initialized. Run:
    echo   git submodule update --init --recursive
    exit /b 1
)

echo [1/3] Fetching clspv dependencies (LLVM, Clang, SPIR-V)...
cd /d "%CLVK_DIR%\external\clspv"
python utils/fetch_sources.py
if !errorlevel! neq 0 (
    echo ERROR: Failed to fetch clspv sources.
    exit /b 1
)

echo [2/3] Configuring CMake for clvk...
mkdir "%CLVK_DIR%\build" 2>nul
cd /d "%CLVK_DIR%\build"
cmake .. -G "Visual Studio 18 2026" -A x64 -DCMAKE_BUILD_TYPE=Release
if !errorlevel! neq 0 (
    echo ERROR: CMake configuration failed.
    exit /b 1
)

echo [3/3] Compiling clvk (Release mode)...
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
copy /y "OpenCL.dll" "%CLVK_DIR%\"
copy /y "clspv.dll" "%CLVK_DIR%\"
copy /y "clvk.dll" "%CLVK_DIR%\"

echo.
echo Done! clvk is ready to be used by OmniGPU guest.
