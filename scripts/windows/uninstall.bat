@echo off
title OmniGPU Uninstaller
cd /d "%~dp0"

net session >nul 2>&1
if %errorlevel% neq 0 (
    echo ============================================
    echo   OmniGPU Uninstaller
    echo ============================================
    echo.
    echo Requesting Administrator privileges...
    powershell -Command "Start-Process '%~f0' -Verb RunAs -Wait"
    exit /b
)

cd /d "%~dp0"
echo ============================================
echo   OmniGPU v0.1.0 - Uninstall
echo ============================================
echo.

set INSTDIR=%ProgramFiles%\OmniGPU

:: ========== Remove Vulkan ICD registration ==========
echo [1/4] Removing Vulkan ICD registry...
if exist "%INSTDIR%\vk_icd.json" (
    reg delete "HKLM\SOFTWARE\Khronos\Vulkan\Drivers" /v "%INSTDIR%\vk_icd.json" /f >nul 2>&1
    reg delete "HKCU\SOFTWARE\Khronos\Vulkan\Drivers" /v "%INSTDIR%\vk_icd.json" /f >nul 2>&1
)
echo   [OK] Vulkan ICD registry removed

:: ========== Remove OpenCL ICD registration ==========
echo [2/4] Removing OpenCL ICD registry...
if exist "%windir%\System32\clvk.icd" del "%windir%\System32\clvk.icd" /f >nul 2>&1
reg delete "HKLM\SOFTWARE\Khronos\OpenCL\Vendors" /v "%windir%\System32\clvk.icd" /f >nul 2>&1
echo   [OK] OpenCL ICD registry removed

:: ========== Remove shortcuts ==========
echo [3/4] Removing shortcuts...
set STARTMENU=%APPDATA%\Microsoft\Windows\Start Menu\Programs\OmniGPU
if exist "%STARTMENU%" rmdir /s /q "%STARTMENU%" >nul 2>&1
echo   [OK] Shortcuts removed

:: ========== Remove install directory ==========
echo [4/4] Removing OmniGPU files...
if exist "%INSTDIR%" (
    rmdir /s /q "%INSTDIR%" >nul 2>&1
    echo   [OK] Deleted %INSTDIR%
) else (
    echo   [--] %INSTDIR% not found
)

:: ========== Done ==========
echo.
echo ============================================
echo   Uninstall complete!
echo ============================================
echo.
echo Note: Mesa3D OpenGL drivers are NOT removed.
echo To uninstall Mesa3D, run:
echo   mesa3d\systemwidedeploy.cmd (chon 10)
echo.
pause
