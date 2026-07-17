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

:: ========== STEP 0: Full cleanup of all old remains ==========
echo [*] Cleaning up previous installations...

set "REG_HKLM=HKLM\SOFTWARE\Khronos\Vulkan\Drivers"
set "REG_WOW=HKLM\SOFTWARE\WOW6432Node\Khronos\Vulkan\Drivers"
set "REG_HKCU=HKCU\SOFTWARE\Khronos\Vulkan\Drivers"

:: 0a. Delete old DLLs from System32 / SysWOW64 (bad practice from old installer)
if exist "%WINDIR%\System32\omnigpu_guest.dll" (
    takeown /f "%WINDIR%\System32\omnigpu_guest.dll" >nul 2>&1
    del /f /q "%WINDIR%\System32\omnigpu_guest.dll" >nul 2>&1
    echo   [*] Removed old omnigpu_guest.dll from System32
)
if exist "%WINDIR%\SysWOW64\omnigpu_guest.dll" (
    takeown /f "%WINDIR%\SysWOW64\omnigpu_guest.dll" >nul 2>&1
    del /f /q "%WINDIR%\SysWOW64\omnigpu_guest.dll" >nul 2>&1
    echo   [*] Removed old omnigpu_guest.dll from SysWOW64
)

:: 0b. Clean ALL possible ICD registry entries (all views, all paths)
for %%V in ("%REG_HKLM%" "%REG_WOW%" "%REG_HKCU%") do (
    :: Program Files entries
    reg delete %%~V /v "%INSTDIR%\vk_icd.json" /f >nul 2>&1
    reg delete %%~V /v "%INSTDIR%\x64\vk_icd.json" /f >nul 2>&1
    reg delete %%~V /v "%INSTDIR%\x86\vk_icd.json" /f >nul 2>&1
    :: Mesa3D entries
    reg delete %%~V /v "%INSTDIR%\x64\lvp_icd.x86_64.json" /f >nul 2>&1
    reg delete %%~V /v "%INSTDIR%\x64\dzn_icd.x86_64.json" /f >nul 2>&1
    reg delete %%~V /v "%INSTDIR%\x86\lvp_icd.x86.json" /f >nul 2>&1
    reg delete %%~V /v "%INSTDIR%\x86\dzn_icd.x86.json" /f >nul 2>&1
)

:: 0b2. Clean PnP device VulkanDriverName entries (cause of loader dedup bug)
powershell -NoProfile -Command ^
  "Get-ChildItem 'HKLM:\SYSTEM\CurrentControlSet\Control\Video' -ErrorAction SilentlyContinue |" ^
  "ForEach-Object { Get-ItemProperty -Path $_.PSPath -Name 'VulkanDriverName' -ErrorAction SilentlyContinue |" ^
  "ForEach-Object { $list = $_.VulkanDriverName; if ($list -match 'OmniGPU') {" ^
  "Write-Host '  [*] Cleaning VulkanDriverName from PnP: ' $_.PSPath; Remove-ItemProperty -Path $_.PSPath -Name 'VulkanDriverName' }}}" ^
  >nul 2>&1
powershell -NoProfile -Command ^
  "Get-ChildItem 'HKLM:\SYSTEM\CurrentControlSet\Control\Video' -ErrorAction SilentlyContinue |" ^
  "ForEach-Object { Get-ChildItem -Path (Join-Path $_.PSPath '0000') -ErrorAction SilentlyContinue |" ^
  "Get-ItemProperty -Name 'VulkanDriverName','VulkanDriverNameWoW' -ErrorAction SilentlyContinue |" ^
  "ForEach-Object { $v = $_.VulkanDriverName; $w = $_.VulkanDriverNameWoW; if (($v -match 'OmniGPU') -or ($w -match 'OmniGPU')) {" ^
  "Write-Host '  [*] Cleaning VulkanDriverName from PnP 0000: ' $_.PSPath; Remove-ItemProperty -Path $_.PSPath -Name 'VulkanDriverName' -ErrorAction SilentlyContinue;" ^
  "Remove-ItemProperty -Path $_.PSPath -Name 'VulkanDriverNameWoW' -ErrorAction SilentlyContinue }}}" ^
  >nul 2>&1
echo   [*] Cleaned PnP VulkanDriverName entries.

:: 0c. Clean desktop/dist entries from all possible user-profile based paths
if defined USERPROFILE (
    for %%V in ("%REG_HKLM%" "%REG_WOW%" "%REG_HKCU%") do (
        reg delete %%~V /v "%USERPROFILE%\Desktop\OmniGPU-v0.1.0\vk_icd.json" /f >nul 2>&1
        reg delete %%~V /v "%USERPROFILE%\Desktop\OmniGPU-v0.1.0\x64\vk_icd.json" /f >nul 2>&1
        reg delete %%~V /v "%USERPROFILE%\Desktop\OmniGPU-v0.1.0\x64\lvp_icd.x86_64.json" /f >nul 2>&1
        reg delete %%~V /v "%USERPROFILE%\Desktop\OmniGPU-v0.1.0\x64\dzn_icd.x86_64.json" /f >nul 2>&1
        reg delete %%~V /v "%USERPROFILE%\Desktop\OmniGPU-v0.1.0\x86\lvp_icd.x86.json" /f >nul 2>&1
        reg delete %%~V /v "%USERPROFILE%\Desktop\OmniGPU-v0.1.0\x86\dzn_icd.x86.json" /f >nul 2>&1
    )
)

:: 0d. Clean current directory entries (where install.bat itself is running from)
for %%V in ("%REG_HKLM%" "%REG_WOW%" "%REG_HKCU%") do (
    reg delete %%~V /v "%cd%\vk_icd.json" /f >nul 2>&1
    reg delete %%~V /v "%cd%\x64\vk_icd.json" /f >nul 2>&1
    reg delete %%~V /v "%cd%\x86\vk_icd.json" /f >nul 2>&1
    reg delete %%~V /v "%cd%\x64\lvp_icd.x86_64.json" /f >nul 2>&1
    reg delete %%~V /v "%cd%\x64\dzn_icd.x86_64.json" /f >nul 2>&1
)

:: 0e. Clean OpenCL stale entries
reg delete "HKLM\SOFTWARE\Khronos\OpenCL\Vendors" /v "%INSTDIR%\OpenCL.dll" /f >nul 2>&1
reg delete "HKLM\SOFTWARE\WOW6432Node\Khronos\OpenCL\Vendors" /v "%INSTDIR%\OpenCL.dll" /f >nul 2>&1

echo   [OK] Registry cleaned.

:: 0f. Remove old program directory if exists
if exist "%INSTDIR%" (
    echo [!] Old installation found at: %INSTDIR%
    set /p "REINSTALL=Would you like to reinstall? (Y/N): "
    if /i "!REINSTALL!"=="Y" (
        echo [*] Removing previous installation...
        rmdir /s /q "%INSTDIR%" >nul 2>&1
        echo   [OK] Old directory removed.
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

:: Copy exes, config files and all runtime DLLs
copy /y "omnigpu_host.exe" "%INSTDIR%" >nul 2>&1
copy /y "omnigpu_vk_test.exe" "%INSTDIR%" >nul 2>&1
copy /y "*.dll" "%INSTDIR%" >nul 2>&1
if exist "omnigpu_guest.json" copy /y "omnigpu_guest.json" "%INSTDIR%" >nul 2>&1
if exist "omnigpu_host.json" copy /y "omnigpu_host.json" "%INSTDIR%" >nul 2>&1

:: x64 Vulkan ICD + all dependency DLLs (MSVC runtime, etc.)
if exist "x64\omnigpu_guest.dll" (
    copy /y "x64\omnigpu_guest.dll" "%INSTDIR%\x64" >nul
    if exist "x64\vk_icd.json" copy /y "x64\vk_icd.json" "%INSTDIR%\x64" >nul
    for %%f in ("x64\*.dll") do (
        if /i not "%%~nxf"=="omnigpu_guest.dll" (
            copy /y "%%f" "%INSTDIR%\x64" >nul 2>&1
        )
    )
)

:: x86 Vulkan ICD + all dependency DLLs
if exist "x86\omnigpu_guest.dll" (
    copy /y "x86\omnigpu_guest.dll" "%INSTDIR%\x86" >nul
    if exist "x86\vk_icd.json" copy /y "x86\vk_icd.json" "%INSTDIR%\x86" >nul
    for %%f in ("x86\*.dll") do (
        if /i not "%%~nxf"=="omnigpu_guest.dll" (
            copy /y "%%f" "%INSTDIR%\x86" >nul 2>&1
        )
    )
)
echo   [OK] Binaries installed to %INSTDIR%

:: ========== STEP 2: Vulkan Loader (only if system is missing) ==========
echo.
echo [2/5] Checking Vulkan Loader...
if exist "%WINDIR%\System32\vulkan-1.dll" (
    echo   [OK] System Vulkan loader found: %WINDIR%\System32\vulkan-1.dll
) else if exist "vulkan-1.dll" (
    echo   [*] No system Vulkan loader, deploying bundled vulkan-1.dll...
    copy /y "vulkan-1.dll" "%WINDIR%\System32" >nul
    if exist "%WINDIR%\SysWOW64" (
        copy /y "vulkan-1.dll" "%WINDIR%\SysWOW64" >nul
    )
    echo   [OK] vulkan-1.dll deployed.
) else (
    echo   [!] Warning: No Vulkan loader found. Please install Vulkan Runtime.
)

:: ========== STEP 3: Register Vulkan ICD ==========
echo.
echo [3/5] Registering Vulkan ICD drivers in Registry...
if exist "%INSTDIR%\x64\vk_icd.json" (
    reg add "HKLM\SOFTWARE\Khronos\Vulkan\Drivers" /v "%INSTDIR%\x64\vk_icd.json" /t REG_DWORD /d 0 /f >nul
    echo   [OK] 64-bit Vulkan ICD registered: %INSTDIR%\x64\vk_icd.json
)
if exist "%INSTDIR%\x86\vk_icd.json" (
    reg add "HKLM\SOFTWARE\WOW6432Node\Khronos\Vulkan\Drivers" /v "%INSTDIR%\x86\vk_icd.json" /t REG_DWORD /d 0 /f >nul
    echo   [OK] 32-bit Vulkan ICD registered: %INSTDIR%\x86\vk_icd.json
)

:: ========== STEP 4: Guest Config Setup ==========
echo.
echo [4/5] Setting up guest configuration...
if exist "%INSTDIR%\omnigpu_guest.json" (
    copy /y "%INSTDIR%\omnigpu_guest.json" "%INSTDIR%\x64" >nul
    copy /y "%INSTDIR%\omnigpu_guest.json" "%INSTDIR%\x86" >nul
    echo   [OK] Guest configuration deployed to driver contexts.
) else (
    echo   [--] No guest config file found.
)

:: ========== STEP 5: Optional Mesa3D ==========
echo.
echo [5/5] Checking for optional Mesa3D OpenGL library...
if exist "mesa3d\systemwidedeploy.cmd" (
    set /p "DEPLOY=Mesa3D found. Do you want to deploy Mesa3D? (Y/N): "
    if /i "!DEPLOY!"=="Y" (
        echo [*] Deploying Mesa3D system-wide...
        call mesa3d\systemwidedeploy.cmd 1
    ) else (
        echo   [--] Skipped Mesa3D deployment.
    )
) else (
    echo   [--] Mesa3D folder not found. Skipping optional step.
)

echo.
echo ===================================================
echo   OmniGPU Installation Complete!
echo ===================================================
echo.
echo   * 64-bit ICD: %INSTDIR%\x64\vk_icd.json
echo   * Host Config: %INSTDIR%\omnigpu_host.json
echo   * Guest Config: %INSTDIR%\omnigpu_guest.json
echo.
echo   To verify: run omnigpu_vk_test.exe from %INSTDIR%
echo.
pause
