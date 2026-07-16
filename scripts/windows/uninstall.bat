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
echo [1/6] Removing Vulkan ICD registry...
reg delete "HKLM\SOFTWARE\Khronos\Vulkan\Drivers" /v "%INSTDIR%\x64\vk_icd.json" /f >nul 2>&1
reg delete "HKLM\SOFTWARE\WOW6432Node\Khronos\Vulkan\Drivers" /v "%INSTDIR%\x86\vk_icd.json" /f >nul 2>&1
echo   [OK] Vulkan ICD registry removed

:: ========== Remove OpenCL ICD registration ==========
echo [2/6] Removing OpenCL ICD registry...
reg delete "HKLM\SOFTWARE\Khronos\OpenCL\Vendors" /v "%INSTDIR%\OpenCL.dll" /f >nul 2>&1
reg delete "HKLM\SOFTWARE\WOW6432Node\Khronos\OpenCL\Vendors" /v "%INSTDIR%\OpenCL.dll" /f >nul 2>&1
echo   [OK] OpenCL ICD registry removed

:: ========== Remove vulkan-1.dll from System32 ==========
echo [3/6] Removing vulkan-1.dll from System32...
if exist "%WINDIR%\System32\vulkan-1.dll" (
    del /f "%WINDIR%\System32\vulkan-1.dll" >nul 2>&1
    echo   [OK] System32\vulkan-1.dll removed
)
if exist "%WINDIR%\SysWOW64\vulkan-1.dll" (
    del /f "%WINDIR%\SysWOW64\vulkan-1.dll" >nul 2>&1
    echo   [OK] SysWOW64\vulkan-1.dll removed
)

:: ========== Remove FFmpeg DLLs from System32 ==========
echo [4/6] Removing FFmpeg DLLs from System32...
for %%f in (avcodec-*.dll avutil-*.dll swscale-*.dll swresample-*.dll) do (
    if exist "%WINDIR%\System32\%%f" del /f "%WINDIR%\System32\%%f" >nul 2>&1
    if exist "%WINDIR%\SysWOW64\%%f" del /f "%WINDIR%\SysWOW64\%%f" >nul 2>&1
)
echo   [OK] FFmpeg DLLs removed from System32

:: ========== Remove shortcuts ==========
echo [5/6] Removing shortcuts...
set STARTMENU=%APPDATA%\Microsoft\Windows\Start Menu\Programs\OmniGPU
if exist "%STARTMENU%" rmdir /s /q "%STARTMENU%" >nul 2>&1
echo   [OK] Shortcuts removed

:: ========== Remove install directory ==========
echo [6/6] Removing OmniGPU files...
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
