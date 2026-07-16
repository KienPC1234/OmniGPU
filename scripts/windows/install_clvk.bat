@echo off
title OmniGPU - clvk/OpenCL Installer
cd /d "%~dp0"

net session >nul 2>&1
if %errorlevel% neq 0 (
    echo Requesting Administrator privileges...
    powershell -Command "Start-Process '%~f0' -Verb RunAs -Wait"
    exit /b
)

cd /d "%~dp0"
echo ============================================
echo   OpenCL ICD Registration - clvk
echo ============================================
echo.
echo clvk translates OpenCL to Vulkan calls.
echo This registers clvk with the OpenCL ICD Loader.
echo.

:: Check OpenCL.dll exists
if not exist "OpenCL.dll" (
    if exist "%ProgramFiles%\OmniGPU\OpenCL.dll" (
        set CLVK_PATH=%ProgramFiles%\OmniGPU\OpenCL.dll
    ) else (
        echo ERROR: OpenCL.dll not found!
        echo Place OmniGPU's OpenCL.dll (clvk) in this folder first.
        pause
        exit /b 1
    )
) else (
    set CLVK_PATH=%cd%\OpenCL.dll
)

:: OpenCL ICD Loader uses HKLM\SOFTWARE\Khronos\OpenCL\Vendors
:: Each REG_DWORD value is the path to an .icd file
:: The .icd file contains the path to the OpenCL implementation DLL

:: Create ICD file
echo %CLVK_PATH% > "%windir%\System32\clvk.icd"

:: Register with OpenCL ICD Loader
reg add "HKLM\SOFTWARE\Khronos\OpenCL\Vendors" /v "%windir%\System32\clvk.icd" /t REG_DWORD /d 0 /f >nul

echo   [OK] clvk registered as OpenCL ICD
echo   ICD file:  %%windir%%\System32\clvk.icd
echo   OpenCL.dll: %CLVK_PATH%
echo.
echo clvk now provides OpenCL via Vulkan.
echo.
echo To verify: run "clinfo" or any OpenCL application.
echo.
pause
