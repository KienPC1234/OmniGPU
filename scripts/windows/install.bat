@echo off
title OmniGPU Installer
cd /d "%~dp0"
setlocal enabledelayedexpansion

:: Request admin privileges
net session >nul 2>&1
if %errorlevel% neq 0 (
    echo [!] Requesting Administrator privileges...
    powershell -Command "Start-Process '%~f0' -Verb RunAs -Wait"
    exit /b
)

cd /d "%~dp0"
echo ===================================================
echo     OmniGPU v0.1.0 - Professional Installer
echo ===================================================
echo.

:: Auto-terminate any running instances to avoid file locks
echo [*] Checking for running OmniGPU processes...
taskkill /f /im omnigpu_host.exe >nul 2>&1
taskkill /f /im omnigpu_vk_test.exe >nul 2>&1
echo   [OK] Active processes cleared.

set INSTDIR=%ProgramFiles%\OmniGPU

:: Clean up any corrupted files created by previous buggy installations
echo [*] Validating system directories...
if exist "%WINDIR%\System32" (
    if not exist "%WINDIR%\System32\" (
        echo   [!] Found corrupted System32 file. Cleaning up...
        del /f /q "%WINDIR%\System32" >nul 2>&1
    )
)
if exist "%WINDIR%\SysWOW64" (
    if not exist "%WINDIR%\SysWOW64\" (
        echo   [!] Found corrupted SysWOW64 file. Cleaning up...
        del /f /q "%WINDIR%\SysWOW64" >nul 2>&1
    )
)
echo   [OK] System directories verified.

:: Check if already installed
if exist "%INSTDIR%" (
    echo.
    echo [!] OmniGPU is already installed at: %INSTDIR%
    set /p "REINSTALL=Would you like to reinstall? (Y/N): "
    if /i "!REINSTALL!"=="Y" (
        echo [*] Removing previous installation...
        reg delete "HKLM\SOFTWARE\Khronos\Vulkan\Drivers" /v "%INSTDIR%\x64\vk_icd.json" /f >nul 2>&1
        reg delete "HKLM\SOFTWARE\WOW6432Node\Khronos\Vulkan\Drivers" /v "%INSTDIR%\x86\vk_icd.json" /f >nul 2>&1
        reg delete "HKLM\SOFTWARE\Khronos\OpenCL\Vendors" /v "%INSTDIR%\OpenCL.dll" /f >nul 2>&1
        reg delete "HKLM\SOFTWARE\WOW6432Node\Khronos\OpenCL\Vendors" /v "%INSTDIR%\OpenCL.dll" /f >nul 2>&1
        rmdir /s /q "%INSTDIR%" >nul 2>&1
        echo   [OK] Cleaned up previous install.
    ) else (
        echo [!] Installation canceled by user.
        pause
        exit /b
    )
)

:: ========== STEP 1: Copy binaries ==========
echo.
echo [1/5] Installing OmniGPU binaries and runtime libraries...
mkdir "%INSTDIR%" >nul 2>&1
mkdir "%INSTDIR%\x64" >nul 2>&1
mkdir "%INSTDIR%\x86" >nul 2>&1

:: Copy exes, config files and all runtime DLL dependencies (like FFmpeg)
copy /y "omnigpu_host.exe" "%INSTDIR%" >nul 2>&1
copy /y "omnigpu_vk_test.exe" "%INSTDIR%" >nul 2>&1
copy /y "*.dll" "%INSTDIR%" >nul 2>&1
if exist "omnigpu_guest.json" copy /y "omnigpu_guest.json" "%INSTDIR%" >nul 2>&1
if exist "omnigpu_host.json" copy /y "omnigpu_host.json" "%INSTDIR%" >nul 2>&1

:: x64 Vulkan ICD
if exist "x64\omnigpu_guest.dll" copy /y "x64\omnigpu_guest.dll" "%INSTDIR%\x64" >nul
if exist "x64\vk_icd.json" copy /y "x64\vk_icd.json" "%INSTDIR%\x64" >nul
if exist "x64\omnigpu_guest.dll" copy /y "x64\omnigpu_guest.dll" "%WINDIR%\System32" >nul

:: x86 Vulkan ICD
if exist "x86\omnigpu_guest.dll" (
    copy /y "x86\omnigpu_guest.dll" "%INSTDIR%\x86" >nul
    copy /y "x86\vk_icd.json" "%INSTDIR%\x86" >nul
    copy /y "x86\omnigpu_guest.dll" "%WINDIR%\SysWOW64" >nul
)
echo   [OK] Binaries installed to %INSTDIR%

:: ========== STEP 2: Vulkan Loader ==========
echo.
echo [2/5] Deploying Vulkan Loader components...
if exist "vulkan-1.dll" (
    copy /y "vulkan-1.dll" "%WINDIR%\System32" >nul
    echo   [OK] vulkan-1.dll deployed to System32
)
if exist "vulkan-1.dll" (
    if exist "%WINDIR%\SysWOW64" (
        copy /y "vulkan-1.dll" "%WINDIR%\SysWOW64" >nul
        echo   [OK] vulkan-1.dll deployed to SysWOW64
    )
)

:: ========== STEP 3: Register Vulkan ICD ==========
echo.
echo [3/5] Registering Vulkan ICD drivers in Registry...
if exist "%INSTDIR%\x64\vk_icd.json" (
    reg add "HKLM\SOFTWARE\Khronos\Vulkan\Drivers" /v "%INSTDIR%\x64\vk_icd.json" /t REG_DWORD /d 0 /f >nul
    echo   [OK] 64-bit Vulkan ICD registered.
)
if exist "%INSTDIR%\x86\vk_icd.json" (
    reg add "HKLM\SOFTWARE\WOW6432Node\Khronos\Vulkan\Drivers" /v "%INSTDIR%\x86\vk_icd.json" /t REG_DWORD /d 0 /f >nul
    echo   [OK] 32-bit Vulkan ICD registered.
)

:: ========== STEP 4: Guest Config Setup ==========
echo.
echo [4/5] Setting up guest configuration...
if exist "%INSTDIR%\omnigpu_guest.json" (
    copy /y "%INSTDIR%\omnigpu_guest.json" "%INSTDIR%\x64" >nul
    copy /y "%INSTDIR%\omnigpu_guest.json" "%INSTDIR%\x86" >nul
    echo   [OK] Guest configuration deployed to driver contexts.
)

:: ========== STEP 5: Optional Mesa3D ==========
echo.
echo [5/5] Checking for optional Mesa3D OpenGL library...
if exist "mesa3d\systemwidedeploy.cmd" (
    set /p "DEPLOY=Mesa3D found. Do you want to deploy Mesa3D? (Y/N): "
    if /i "!DEPLOY!"=="Y" (
        echo [*] Deploying Mesa3D system-wide...
        call mesa3d\systemwidedeploy.cmd 1
    )
) else (
    echo   [--] Mesa3D folder not found. Skipping optional step.
)

echo.
echo ===================================================
echo   OmniGPU Installation Complete!
echo ===================================================
echo.
echo   * Host Config: %INSTDIR%\omnigpu_host.json
echo   * Guest Config: %INSTDIR%\omnigpu_guest.json
echo.
echo   To test the configuration:
echo     1. Run omnigpu_host.exe from %INSTDIR%
echo     2. Run omnigpu_vk_test.exe to verify guest driver connection.
echo.
pause
