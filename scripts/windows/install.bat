@echo off
title OmniGPU Installer
cd /d "%~dp0"
setlocal enabledelayedexpansion

:: Check admin
net session >nul 2>&1
if %errorlevel% neq 0 (
    echo ============================================
    echo   OmniGPU + Mesa3D Installer
    echo ============================================
    echo.
    echo Requesting Administrator privileges...
    powershell -Command "Start-Process '%~f0' -Verb RunAs -Wait"
    exit /b
)

cd /d "%~dp0"
echo ============================================
echo   OmniGPU v0.1.0 + Mesa3D - Full Install
echo ============================================
echo.

:: ========== STEP 1: Install OmniGPU ==========
echo [1/4] Installing OmniGPU binaries...
set INSTDIR=%ProgramFiles%\OmniGPU
if not exist "%INSTDIR%" mkdir "%INSTDIR%"

copy /y "omnigpu_guest.dll"  "%INSTDIR%\" >nul
copy /y "omnigpu_host.exe"   "%INSTDIR%\" >nul
copy /y "omnigpu_guestd.exe" "%INSTDIR%\" >nul
copy /y "omnigpu_vk_test.exe" "%INSTDIR%\" >nul
copy /y "vk_icd.json"           "%INSTDIR%\" >nul
copy /y "vulkan-1.dll"          "%INSTDIR%\" >nul
if exist "omnigpu_guest.json" copy /y "omnigpu_guest.json" "%INSTDIR%\" >nul

:: Optional: clvk OpenCL.dll
if exist "OpenCL.dll" copy /y "OpenCL.dll" "%INSTDIR%\" >nul

:: Optional: FFmpeg shared DLLs
for %%f in (avcodec-*.dll avutil-*.dll swscale-*.dll swresample-*.dll) do (
    if exist "%%f" copy /y "%%f" "%INSTDIR%\" >nul
)
echo   [OK] OmniGPU -> %INSTDIR%

:: ========== STEP 2: Register Vulkan ICD ==========
echo [2/4] Registering Vulkan ICD...
reg add "HKLM\SOFTWARE\Khronos\Vulkan\Drivers" /v "%INSTDIR%\vk_icd.json" /t REG_DWORD /d 0 /f >nul
echo   [OK] Vulkan ICD registered (HKLM)

:: ========== STEP 3: Mesa3D (OpenGL + Vulkan) ==========
echo [3/4] Mesa3D...
if exist "mesa3d" (
    echo   Mesa3D folder included at .\mesa3d\
    echo   To deploy Mesa3D system-wide, run AS ADMIN:
    echo     mesa3d\systemwidedeploy.cmd
    echo   Or for per-app: mesa3d\perappdeploy.cmd
) else (
    echo   [--] Mesa3D folder not found, skipping
)

:: ========== STEP 4: Shortcuts ==========
echo [4/4] Creating shortcuts...
if not exist "%APPDATA%\Microsoft\Windows\Start Menu\Programs\OmniGPU" mkdir "%APPDATA%\Microsoft\Windows\Start Menu\Programs\OmniGPU"
copy /y "start-daemon.bat" "%APPDATA%\Microsoft\Windows\Start Menu\Programs\OmniGPU\" >nul 2>&1

echo.
echo ============================================
echo   Installation complete!
echo ============================================
echo.
echo Installed:
echo   OmniGPU:  %INSTDIR%
echo   Mesa3D:   System32 (OpenGL + Vulkan)
echo.
echo Next steps:
echo   1. Run install_clvk.bat to install OpenCL support
echo   2. Run start-daemon.bat to start background daemon
echo   3. Launch omnigpu_host.exe on your GPU machine
echo.
pause
