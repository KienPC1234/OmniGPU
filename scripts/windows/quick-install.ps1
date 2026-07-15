#===============================================================================
# OmniGPU Quick Install for Windows
#
# One-command setup for a fresh VM. Run this as Administrator and any
# Vulkan application will auto-discover the OmniGPU driver.
#
# Usage:
#   .\quick-install.ps1 -BuildDir "..\..\build\release"
#   .\quick-install.ps1 -BuildDir "..\..\build\release" -BuildDir32 "..\..\build\release-x86"
#   .\quick-install.ps1 -PackageDir "C:\OmniGPU-Package"
#   .\quick-install.ps1 -BuildDir "..\..\build\release" -HostAddr "192.168.1.100"
#===============================================================================

param(
    [string]$BuildDir = "",
    [string]$BuildDir32 = "",
    [string]$PackageDir = "",
    [string]$InstallDir = "$env:ProgramFiles\OmniGPU",
    [string]$HostAddr = "",
    [int]$HostPort = 9443,
    [switch]$Uninstall = $false,
    [switch]$Help = $false
)

if ($Help) {
    Write-Host "OmniGPU Quick Install for Windows"
    Write-Host ""
    Write-Host "Usage:"
    Write-Host "  .\quick-install.ps1 -BuildDir `"build\release`""
    Write-Host "  .\quick-install.ps1 -PackageDir `"C:\OmniGPU-Package`""
    Write-Host "  .\quick-install.ps1 -BuildDir `"build\release`" -HostAddr `"192.168.1.100`""
    Write-Host "  .\quick-install.ps1 -Uninstall"
    Write-Host ""
    Write-Host "Parameters:"
    Write-Host "  -BuildDir     Path to 64-bit build output (contains bin/omnigpu_guest.dll)"
    Write-Host "  -BuildDir32   Path to 32-bit build output (optional)"
    Write-Host "  -PackageDir   Path to pre-packaged OmniGPU folder (instead of -BuildDir)"
    Write-Host "  -InstallDir   Where to install (default: C:\Program Files\OmniGPU)"
    Write-Host "  -HostAddr     IP/hostname of the OmniGPU host machine"
    Write-Host "  -HostPort     Port of the OmniGPU host (default: 9443)"
    Write-Host "  -Uninstall    Remove OmniGPU"
    Write-Host "  -Help         This message"
    exit 0
}

$ErrorActionPreference = "SilentlyContinue"

# ============================================================================
# Uninstall
# ============================================================================
if ($Uninstall) {
    Write-Host "=== OmniGPU Quick Uninstall ==="

    # Remove registry entries
    $regPaths = @(
        "HKLM:\SOFTWARE\Khronos\Vulkan\Drivers",
        "HKLM:\SOFTWARE\WOW6432Node\Khronos\Vulkan\Drivers",
        "HKCU:\SOFTWARE\Khronos\Vulkan\Drivers",
        "HKCU:\SOFTWARE\WOW6432Node\Khronos\Vulkan\Drivers"
    )
    $jsonPaths = @("$InstallDir\vk_icd.json", "$InstallDir\x86\vk_icd.json")

    foreach ($regPath in $regPaths) {
        if (Test-Path $regPath) {
            foreach ($jsonPath in $jsonPaths) {
                $props = Get-ItemProperty -Path $regPath -ErrorAction SilentlyContinue
                if ($props -and $props.PSObject.Properties.Name -contains $jsonPath) {
                    Remove-ItemProperty -Path $regPath -Name $jsonPath -Force -ErrorAction SilentlyContinue
                    Write-Host "  [OK] Removed: $regPath\$jsonPath"
                }
            }
        }
    }

    # Remove OMNIGPU_HOST/PORT env vars
    [System.Environment]::SetEnvironmentVariable('OMNIGPU_HOST', $null, 'Machine')
    [System.Environment]::SetEnvironmentVariable('OMNIGPU_HOST', $null, 'User')
    [System.Environment]::SetEnvironmentVariable('OMNIGPU_PORT', $null, 'Machine')
    [System.Environment]::SetEnvironmentVariable('OMNIGPU_PORT', $null, 'User')

    # Remove clvk DLLs from system dirs
    $clvkDlls = @("OpenCL.dll", "clspv.dll", "clvk.dll")
    foreach ($dir in @("$env:SystemRoot\System32", "$env:SystemRoot\SysWOW64")) {
        foreach ($dll in $clvkDlls) {
            $p = "$dir\$dll"
            if (Test-Path $p) {
                Remove-Item -Path $p -Force -ErrorAction SilentlyContinue
                Write-Host "  [OK] Removed $dll from $dir"
            }
        }
    }

    # Restore original opengl32.dll from backup
    $sysDirs = @("$env:SystemRoot\System32", "$env:SystemRoot\SysWOW64")
    foreach ($dir in $sysDirs) {
        $orig = "$dir\opengl32.dll.orig"
        if (Test-Path $orig) {
            Rename-Item -Path $orig -NewName "opengl32.dll" -Force -ErrorAction SilentlyContinue
            Write-Host "  [OK] Restored original opengl32.dll in $dir"
        }
        # Also remove Gallium DLLs we deployed
        foreach ($gallium in @("libgallium_wgl.dll")) {
            $gPath = "$dir\$gallium"
            if (Test-Path $gPath) {
                Remove-Item -Path $gPath -Force -ErrorAction SilentlyContinue
                Write-Host "  [OK] Removed $gallium from $dir"
            }
        }
    }

    # Remove service (if exists)
    Get-Service "OmniGPU_Guest" -ErrorAction SilentlyContinue | Stop-Service -ErrorAction SilentlyContinue
    & sc.exe delete OmniGPU_Guest 2>&1 | Out-Null
    Write-Host "  [OK] Removed OmniGPU_Guest service"

    # Remove Run registry key
    Remove-ItemProperty -Path "HKCU:\Software\Microsoft\Windows\CurrentVersion\Run" -Name "OmniGPU_Daemon" -Force -ErrorAction SilentlyContinue
    Write-Host "  [OK] Removed Run registry key"

    # Remove shortcuts
    $startup = [Environment]::GetFolderPath("Startup")
    Remove-Item "$startup\OmniGPU_Daemon.lnk" -Force -ErrorAction SilentlyContinue
    $desktop = [Environment]::GetFolderPath("Desktop")
    Remove-Item "$desktop\OmniGPU Control.lnk" -Force -ErrorAction SilentlyContinue
    Write-Host "  [OK] Removed shortcuts"

    # Stop daemon
    Stop-Process -Name "omnigpu_guestd" -Force -ErrorAction SilentlyContinue
    Write-Host "  [OK] Daemon stopped"

    # Remove install directory
    if (Test-Path $InstallDir) {
        Remove-Item -Recurse -Force $InstallDir -ErrorAction SilentlyContinue
        Write-Host "  [OK] Removed: $InstallDir"
    }

    Write-Host "=== Uninstall complete ==="
    exit 0
}

# ============================================================================
# Resolve source files
# ============================================================================
if ($PackageDir) {
    # Use pre-packaged folder
    $SrcDir64 = $PackageDir
    $SrcDir32 = "$PackageDir\x86"
} elseif ($BuildDir) {
    $SrcDir64 = "$BuildDir\bin"
    if ($BuildDir32) { $SrcDir32 = "$BuildDir32\bin" } else { $SrcDir32 = $null }
} else {
    Write-Error "Specify -BuildDir, -PackageDir, or -Uninstall"
    exit 1
}

if (-not (Test-Path "$SrcDir64\omnigpu_guest.dll")) {
    Write-Error "omnigpu_guest.dll not found in $SrcDir64. Check -BuildDir or -PackageDir."
    exit 1
}

# ============================================================================
# Install
# ============================================================================
Write-Host "=== OmniGPU Quick Install ===" -ForegroundColor Cyan
Write-Host "  Source:   $SrcDir64"
if ($SrcDir32 -and (Test-Path "$SrcDir32\omnigpu_guest.dll")) {
    Write-Host "  Source32: $SrcDir32"
}
Write-Host "  Target:   $InstallDir"
if ($HostAddr) {
    Write-Host "  Host:     $HostAddr`:$HostPort"
}
Write-Host ""

# --- Create directories ---
New-Item -ItemType Directory -Path $InstallDir -Force | Out-Null
if ($SrcDir32 -and (Test-Path "$SrcDir32\omnigpu_guest.dll")) {
    New-Item -ItemType Directory -Path "$InstallDir\x86" -Force | Out-Null
}

# --- Copy 64-bit files ---
Write-Host "--- 64-bit ---" -ForegroundColor Cyan
Copy-Item -Path "$SrcDir64\omnigpu_guest.dll" -Destination $InstallDir -Force
Write-Host "  [OK] omnigpu_guest.dll -> $InstallDir"

# Copy ICD manifest
$manifestSource = if (Test-Path "$SrcDir64\vk_icd.json") { "$SrcDir64\vk_icd.json" }
                  else { "$PSScriptRoot\..\..\src\guest\vk_icd.json" }
if (Test-Path $manifestSource) {
    Copy-Item -Path $manifestSource -Destination $InstallDir -Force
    Write-Host "  [OK] vk_icd.json -> $InstallDir"
}

# Copy all other DLLs (Zink, clvk, runtime)
Get-ChildItem "$SrcDir64\*.dll" | ForEach-Object {
    if ($_.Name -ne "omnigpu_guest.dll") {
        Copy-Item -Path $_.FullName -Destination $InstallDir -Force
        Write-Host "  [OK] $($_.Name) -> $InstallDir"
    }
}

# Copy launcher and host if present
Get-ChildItem "$SrcDir64\*.exe" | ForEach-Object {
    Copy-Item -Path $_.FullName -Destination $InstallDir -Force
    Write-Host "  [OK] $($_.Name) -> $InstallDir"
}

# --- Copy 32-bit files ---
if ($SrcDir32 -and (Test-Path "$SrcDir32\omnigpu_guest.dll")) {
    Write-Host "--- 32-bit ---" -ForegroundColor Cyan
    Copy-Item -Path "$SrcDir32\omnigpu_guest.dll" -Destination "$InstallDir\x86" -Force
    Write-Host "  [OK] omnigpu_guest.dll -> $InstallDir\x86"

    if (Test-Path "$SrcDir32\vk_icd.json") {
        Copy-Item -Path "$SrcDir32\vk_icd.json" -Destination "$InstallDir\x86" -Force
    } else {
        Copy-Item -Path $manifestSource -Destination "$InstallDir\x86" -Force
    }
    Write-Host "  [OK] vk_icd.json -> $InstallDir\x86"

    Get-ChildItem "$SrcDir32\*.dll" | ForEach-Object {
        if ($_.Name -ne "omnigpu_guest.dll") {
            Copy-Item -Path $_.FullName -Destination "$InstallDir\x86" -Force
            Write-Host "  [OK] $($_.Name) -> $InstallDir\x86"
        }
    }
}

# --- Deploy Zink + Gallium DLLs to System32 (global OpenGL) ---
$zinkDlls = @("opengl32.dll", "libgallium_wgl.dll")

$sys32 = "$env:SystemRoot\System32"
$syswow = "$env:SystemRoot\SysWOW64"

# 64-bit → System32
Write-Host "--- Global OpenGL (Zink + Gallium) ---" -ForegroundColor Cyan
$zinkSrc64 = $SrcDir64
# Fallback to third_party if build dir doesn't have Zink
if (-not (Test-Path "$zinkSrc64\opengl32.dll")) {
    $tp = "$PSScriptRoot\..\..\third_party\zink"
    if (Test-Path "$tp\opengl32.dll") { $zinkSrc64 = $tp }
}
if (Test-Path "$zinkSrc64\opengl32.dll") {
    if ((Test-Path "$sys32\opengl32.dll") -and -not (Test-Path "$sys32\opengl32.dll.orig")) {
        Rename-Item -Path "$sys32\opengl32.dll" -NewName "opengl32.dll.orig" -Force
        Write-Host "  [OK] Backed up original opengl32.dll -> opengl32.dll.orig"
    }
    foreach ($dll in $zinkDlls) {
        $srcPath = "$zinkSrc64\$dll"
        if (Test-Path $srcPath) {
            Copy-Item -Path $srcPath -Destination "$sys32\$dll" -Force
            Write-Host "  [OK] $dll -> System32 (64-bit OpenGL)"
        }
    }
} else {
    Write-Host "  [--] Zink 64-bit not found (skip OpenGL)"
}

# 32-bit → SysWOW64
$zinkSrc32 = $SrcDir32
if (-not $zinkSrc32 -or -not (Test-Path "$zinkSrc32\opengl32.dll")) {
    $tp32 = "$PSScriptRoot\..\..\third_party\zink\x86"
    if (Test-Path "$tp32\opengl32.dll") { $zinkSrc32 = $tp32 }
}
if ($zinkSrc32 -and (Test-Path "$zinkSrc32\opengl32.dll")) {
    if ((Test-Path "$syswow\opengl32.dll") -and -not (Test-Path "$syswow\opengl32.dll.orig")) {
        Rename-Item -Path "$syswow\opengl32.dll" -NewName "opengl32.dll.orig" -Force
        Write-Host "  [OK] Backed up original opengl32.dll (32-bit)"
    }
    foreach ($dll in $zinkDlls) {
        $srcPath = "$zinkSrc32\$dll"
        if (Test-Path $srcPath) {
            Copy-Item -Path $srcPath -Destination "$syswow\$dll" -Force
            Write-Host "  [OK] $dll -> SysWOW64 (32-bit OpenGL)"
        }
    }
} else {
    Write-Host "  [--] Zink 32-bit not found, skipping SysWOW64"
}

# --- Deploy clvk (OpenCL) DLLs to System32 (global OpenCL) ---
Write-Host "--- Global OpenCL (clvk) ---" -ForegroundColor Cyan
$clvkDlls = @("OpenCL.dll", "clspv.dll", "clvk.dll")
function Get-ClvkSrc($dir) {
    if ($dir -and (Test-Path "$dir\OpenCL.dll")) { return $dir }
    $tp = "$PSScriptRoot\..\..\third_party\clvk"
    if (Test-Path "$tp\OpenCL.dll") { return $tp }
    return $null
}
function Deploy-Clvk($srcDir, $sysDir, $arch) {
    if (-not $srcDir) { return $false }
    foreach ($dll in $clvkDlls) {
        $src = "$srcDir\$dll"
        if (Test-Path $src) {
            Copy-Item -Path $src -Destination "$sysDir\$dll" -Force
            Write-Host "  [OK] $dll -> $sysDir ($arch)"
        }
    }
    return $true
}
$clvkSrc64 = Get-ClvkSrc $SrcDir64
$clvkSrc32 = Get-ClvkSrc $SrcDir32
Deploy-Clvk $clvkSrc64 $sys32 "64-bit" | Out-Null
if ($clvkSrc32) { Deploy-Clvk $clvkSrc32 $syswow "32-bit" | Out-Null }
if (-not $clvkSrc64) { Write-Host "  [--] clvk not found (skip OpenCL)" }

# --- Set OMNIGPU_HOST ---
if ($HostAddr) {
    # Try machine-wide (needs admin), fall back to user-level
    try {
        [System.Environment]::SetEnvironmentVariable('OMNIGPU_HOST', $HostAddr, 'Machine')
        [System.Environment]::SetEnvironmentVariable('OMNIGPU_PORT', "$HostPort", 'Machine')
        Write-Host "  [OK] OMNIGPU_HOST=$HostAddr (machine-wide)" -ForegroundColor Green
    } catch {
        [System.Environment]::SetEnvironmentVariable('OMNIGPU_HOST', $HostAddr, 'User')
        [System.Environment]::SetEnvironmentVariable('OMNIGPU_PORT', "$HostPort", 'User')
        Write-Host "  [OK] OMNIGPU_HOST=$HostAddr (user-level)" -ForegroundColor Green
    }

    # Also create omnigpu_guest.json config
    $configJson = @"
{
    "host": "$HostAddr",
    "port": $HostPort,
    "zink_enabled": true,
    "clvk_enabled": true
}
"@
    Set-Content -Path "$InstallDir\omnigpu_guest.json" -Value $configJson -Force
    Write-Host "  [OK] omnigpu_guest.json -> $InstallDir" -ForegroundColor Green
}

# ============================================================================
# Registry Registration
# ============================================================================
Write-Host ""
Write-Host "--- Registry Registration ---" -ForegroundColor Cyan

$icdJson64 = "$InstallDir\vk_icd.json"
$icdJson32 = "$InstallDir\x86\vk_icd.json"

$isAdmin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)

function Reg-SetDword($path, $name) {
    try {
        if (-not (Test-Path $path)) { New-Item -Path $path -Force | Out-Null }
        New-ItemProperty -Path $path -Name $name -Value 0 -PropertyType DWord -Force | Out-Null
        return $true
    } catch { return $false }
}

function Reg-AppendMultiSz($path, $name, $value) {
    try {
        $existing = (Get-ItemProperty -LiteralPath $path -Name $name -ErrorAction SilentlyContinue).$name
        $vals = @()
        if ($existing -is [array]) { $vals += $existing }
        elseif ($existing) { $vals += $existing }
        if ($vals -notcontains $value) { $vals += $value }
        Set-ItemProperty -LiteralPath $path -Name $name -Value $vals -Type MultiString -Force
        return $true
    } catch { return $false }
}

# HKCU (no admin)
if (Reg-SetDword "HKCU:\SOFTWARE\Khronos\Vulkan\Drivers" $icdJson64) {
    Write-Host "  [OK] HKCU 64-bit"
}
if (Test-Path $icdJson32) {
    if (Reg-SetDword "HKCU:\SOFTWARE\WOW6432Node\Khronos\Vulkan\Drivers" $icdJson32) {
        Write-Host "  [OK] HKCU 32-bit"
    }
}

# HKLM + VulkanDriverName (admin only)
if ($isAdmin) {
    Reg-SetDword "HKLM:\SOFTWARE\Khronos\Vulkan\Drivers" $icdJson64 | Out-Null
    Write-Host "  [OK] HKLM 64-bit"

    if (Test-Path $icdJson32) {
        Reg-SetDword "HKLM:\SOFTWARE\WOW6432Node\Khronos\Vulkan\Drivers" $icdJson32 | Out-Null
        Write-Host "  [OK] HKLM 32-bit"
    }

    # VulkanDriverName for VkDiag
    Write-Host "--- VulkanDriverName (VkDiag) ---" -ForegroundColor Cyan
    $baseVideo = "HKLM:\SYSTEM\CurrentControlSet\Control\Video"
    if (Test-Path $baseVideo) {
        $found = $false
        foreach ($guid in (Get-ChildItem $baseVideo)) {
            $psp = $guid.PSPath
            if ((Test-Path "$psp\Video") -and (Test-Path "$psp\0000")) {
                $outputPath = "$psp\0000"
                Reg-AppendMultiSz $outputPath "VulkanDriverName" $icdJson64 | Out-Null
                Write-Host "  [OK] VulkanDriverName on $($guid.PSChildName)"
                if (Test-Path $icdJson32) {
                    Reg-AppendMultiSz $outputPath "VulkanDriverNameWoW" $icdJson32 | Out-Null
                    Write-Host "  [OK] VulkanDriverNameWoW on $($guid.PSChildName)"
                }
                $found = $true
                break
            }
        }
        if (-not $found) {
            Write-Host "  [--] No compatible GPU found"
        }
    }
}

# ============================================================================
# Install Service + Shortcuts
# ============================================================================
if ($isAdmin) {
    Write-Host "--- Service + Shortcuts ---" -ForegroundColor Cyan

    # Stop and remove old service if exists
    Get-Service "OmniGPU_Guest" -ErrorAction SilentlyContinue | Stop-Service -ErrorAction SilentlyContinue
    & sc.exe delete OmniGPU_Guest 2>&1 | Out-Null

    # Clean up old shortcuts and Run key if any
    $startup = [Environment]::GetFolderPath("Startup")
    Remove-Item "$startup\OmniGPU_Daemon.lnk" -Force -ErrorAction SilentlyContinue
    Remove-ItemProperty -Path "HKCU:\Software\Microsoft\Windows\CurrentVersion\Run" -Name "OmniGPU_Daemon" -Force -ErrorAction SilentlyContinue

    # Install the service
    & "$InstallDir\omnigpu_guestd.exe" --install
    if ($LASTEXITCODE -eq 0) {
        Write-Host "  [OK] OmniGPU_Guest service installed"
        Start-Service "OmniGPU_Guest" -ErrorAction SilentlyContinue
        Write-Host "  [OK] OmniGPU_Guest service started"
    } else {
        Write-Warning "  [WARN] Failed to install service, running daemon in background"
        Start-Process -FilePath "$InstallDir\omnigpu_guestd.exe" -WindowStyle Hidden
    }

    # Shortcuts
    $wsh = New-Object -ComObject WScript.Shell

    # Desktop shortcut (GUI control panel)
    $desktop = [Environment]::GetFolderPath("Desktop")
    $lnk2 = $wsh.CreateShortcut("$desktop\OmniGPU Control.lnk")
    $lnk2.TargetPath = "$InstallDir\omnigpu_guestgui.exe"
    $lnk2.WorkingDirectory = $InstallDir
    $lnk2.Description = "OmniGPU Guest Control Panel"
    $lnk2.Save()
    Write-Host "  [OK] Desktop shortcut created (OmniGPU Control)"
}

# ============================================================================
# Done
# ============================================================================
Write-Host ""
Write-Host "=== Installation Complete ===" -ForegroundColor Green
Write-Host ""
Write-Host "  Installed: $InstallDir"
if ($HostAddr) {
    Write-Host "  Host:      $HostAddr`:$HostPort"
}
Write-Host ""
Write-Host "  -> Vulkan:  OmniGPU ICD registered globally"
Write-Host "  -> OpenGL:  Zink + Gallium deployed to System32/SysWOW64"
Write-Host "  -> OpenCL:  clvk deployed to System32/SysWOW64"
Write-Host "  -> Daemon:  auto-start on login (Startup + Run registry)"
Write-Host "  -> GUI:     Desktop shortcut 'OmniGPU Control'"
Write-Host ""
Write-Host "  Any Vulkan, OpenGL, or OpenCL app now auto-detects OmniGPU."
Write-Host "  Uninstall: .\quick-install.ps1 -Uninstall"
Write-Host "  (original opengl32.dll backed up as .orig)"
Write-Host "================================="
