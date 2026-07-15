#===============================================================================
# OmniGPU Guest Installer for Windows
#
# Usage:
#   .\scripts\install_guest.ps1                           # auto-detect 64-bit build
#   .\scripts\install_guest.ps1 -BuildDir64 build\release
#   .\scripts\install_guest.ps1 -BuildDir64 build\release -BuildDir32 build\release-x86
#   .\scripts\install_guest.ps1 -InstallDir "C:\Program Files\OmniGPU"
#
# This script:
#   1. Copies 64-bit omnigpu_guest.dll + vk_icd.json + Zink/clvk to target dir
#   2. Copies 32-bit omnigpu_guest.dll to target dir\x86 (if available)
#   3. Registers ICD in HKLM (admin) + HKCU (user) for both architectures
#   4. 32-bit and 64-bit Vulkan apps auto-detect the driver
#===============================================================================

param(
    [string]$BuildDir64 = "",
    [string]$BuildDir32 = "",
    [string]$InstallDir = "",
    [switch]$Uninstall = $false,
    [switch]$Help = $false
)

if ($Help) {
    Write-Host "OmniGPU Guest Installer for Windows"
    Write-Host ""
    Write-Host "Usage:"
    Write-Host "  Install:  .\scripts\install_guest.ps1"
    Write-Host "  Install:  .\scripts\install_guest.ps1 -BuildDir64 build\release"
    Write-Host "  Install:  .\scripts\install_guest.ps1 -BuildDir64 build\release -BuildDir32 build\release-x86"
    Write-Host "  Install:  .\scripts\install_guest.ps1 -InstallDir 'C:\Program Files\OmniGPU'"
    Write-Host "  Uninstall: .\scripts\install_guest.ps1 -Uninstall"
    exit 0
}

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$ProjectRoot = Resolve-Path "$ScriptDir\..\.."

# --- Detect build directories ------------------------------------------------
if (-not $BuildDir64) {
    $candidates = @("build\release", "build\default", "build\debug")
    foreach ($c in $candidates) {
        $p = Join-Path $ProjectRoot $c
        if (Test-Path "$p\bin\omnigpu_guest.dll") {
            $BuildDir64 = $p
            break
        }
    }
    if (-not $BuildDir64) {
        Write-Error "64-bit build not found. Specify -BuildDir64 or build first."
        exit 1
    }
}

if (-not $BuildDir32) {
    $p = Join-Path $ProjectRoot "build\release-x86"
    if (Test-Path "$p\bin\omnigpu_guest.dll") {
        $BuildDir32 = $p
    }
}

if (-not $InstallDir) {
    $InstallDir = "$env:ProgramFiles\OmniGPU"
}

$BinDir64 = "$BuildDir64\bin"
if ($BuildDir32) { $BinDir32 = "$BuildDir32\bin" } else { $BinDir32 = $null }
$VkIcdSource = "$ProjectRoot\src\guest\vk_icd.json"
$ThirdPartyDir = "$ProjectRoot\third_party"

# Registry paths for Vulkan ICD discovery
$RegPath64 = "HKLM:\SOFTWARE\Khronos\Vulkan\Drivers"
$RegPath32 = "HKLM:\SOFTWARE\WOW6432Node\Khronos\Vulkan\Drivers"
$RegPathUser = "HKCU:\SOFTWARE\Khronos\Vulkan\Drivers"
$RegPathUser32 = "HKCU:\SOFTWARE\WOW6432Node\Khronos\Vulkan\Drivers"

# ============================================================================
# Uninstall
# ============================================================================
if ($Uninstall) {
    Write-Host "=== OmniGPU Guest Uninstall ==="

    $keys = @(
        "$RegPathUser",
        "$RegPathUser32",
        "$RegPath64",
        "$RegPath32"
    )

    $jsonPaths = @(
        "$InstallDir\vk_icd.json",
        "$InstallDir\x86\vk_icd.json"
    )

    foreach ($regPath in $keys) {
        foreach ($jsonPath in $jsonPaths) {
            if (Test-Path $regPath) {
                $existing = Get-ItemProperty -Path $regPath -ErrorAction SilentlyContinue
                if ($existing -and $existing.PSObject.Properties.Name -contains $jsonPath) {
                    Remove-ItemProperty -Path $regPath -Name $jsonPath -Force -ErrorAction SilentlyContinue
                    Write-Host "  [OK] Removed registry: $regPath\$jsonPath"
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

    if (Test-Path $InstallDir) {
        Remove-Item -Recurse -Force $InstallDir -ErrorAction SilentlyContinue
        Write-Host "  [OK] Removed: $InstallDir"
    }

    # Restore original opengl32.dll from backup
    $sysDirs = @("$env:SystemRoot\System32", "$env:SystemRoot\SysWOW64")
    foreach ($dir in $sysDirs) {
        $orig = "$dir\opengl32.dll.orig"
        if (Test-Path $orig) {
            Rename-Item -Path $orig -NewName "opengl32.dll" -Force -ErrorAction SilentlyContinue
            Write-Host "  [OK] Restored original opengl32.dll in $dir"
        }
        foreach ($gallium in @("libgallium_wgl.dll")) {
            $gPath = "$dir\$gallium"
            if (Test-Path $gPath) {
                Remove-Item -Path $gPath -Force -ErrorAction SilentlyContinue
                Write-Host "  [OK] Removed $gallium from $dir"
            }
        }
    }

    Write-Host "=== Uninstall complete ==="
    Write-Host "Note: Some registry keys may remain if other software uses them."
    exit 0
}

# ============================================================================
# Install
# ============================================================================
Write-Host "=== OmniGPU Guest Install ==="
Write-Host "  64-bit build: $BuildDir64"
if ($BuildDir32) { Write-Host "  32-bit build: $BuildDir32" }
Write-Host "  Install dir:  $InstallDir"
Write-Host ""

# Create directories
New-Item -ItemType Directory -Path $InstallDir -Force | Out-Null
if ($BuildDir32) {
    New-Item -ItemType Directory -Path "$InstallDir\x86" -Force | Out-Null
}

# ---------------------------------------------------------------------------
# Copy 64-bit files
# ---------------------------------------------------------------------------
Write-Host "--- 64-bit ---" -ForegroundColor Cyan
$files64 = @()

# Core guest DLL
if (Test-Path "$BinDir64\omnigpu_guest.dll") {
    $files64 += "$BinDir64\omnigpu_guest.dll"
} else {
    Write-Warning "64-bit omnigpu_guest.dll not found!"
}

# Copy 64-bit vk_icd.json — update library_path to be just the DLL name (no path)
$icdJson64 = Get-Content $VkIcdSource -Raw
Set-Content -Path "$InstallDir\vk_icd.json" -Value $icdJson64 -Force
Write-Host "  [OK] vk_icd.json -> $InstallDir\vk_icd.json"

# Translation layers
if (Test-Path "$ThirdPartyDir\zink\opengl32.dll") {
    $files64 += "$ThirdPartyDir\zink\opengl32.dll"
}
if (Test-Path "$ThirdPartyDir\clvk\OpenCL.dll") {
    $files64 += "$ThirdPartyDir\clvk\OpenCL.dll"
}

# Runtime DLLs from build
if (Test-Path $BinDir64) {
    Get-ChildItem "$BinDir64\*.dll" | ForEach-Object {
        if ($_.Name -ne "omnigpu_guest.dll") {
            $files64 += $_.FullName
        }
    }
}

foreach ($f in $files64) {
    if (Test-Path $f) {
        Copy-Item -Path $f -Destination $InstallDir -Force
        Write-Host "  [OK] $(Split-Path $f -Leaf) -> $InstallDir"
    }
}

# ---------------------------------------------------------------------------
# Copy 32-bit files
# ---------------------------------------------------------------------------
if ($BuildDir32) {
    Write-Host "--- 32-bit (x86) ---" -ForegroundColor Cyan
    $files32 = @()

    if (Test-Path "$BinDir32\omnigpu_guest.dll") {
        $files32 += "$BinDir32\omnigpu_guest.dll"
    } else {
        Write-Warning "32-bit omnigpu_guest.dll not found!"
    }

    # Copy 32-bit vk_icd.json — same content, same relative library_path
    Set-Content -Path "$InstallDir\x86\vk_icd.json" -Value $icdJson64 -Force
    Write-Host "  [OK] vk_icd.json -> $InstallDir\x86\vk_icd.json"

    # 32-bit runtime DLLs
    if (Test-Path $BinDir32) {
        Get-ChildItem "$BinDir32\*.dll" | ForEach-Object {
            if ($_.Name -ne "omnigpu_guest.dll") {
                $files32 += $_.FullName
            }
        }
    }

    foreach ($f in $files32) {
        if (Test-Path $f) {
            Copy-Item -Path $f -Destination "$InstallDir\x86" -Force
            Write-Host "  [OK] $(Split-Path $f -Leaf) -> $InstallDir\x86"
        }
    }
} else {
    Write-Host "  [--] No 32-bit build found (set -BuildDir32 for dual-arch support)" -ForegroundColor Yellow
}

# ---------------------------------------------------------------------------
# Deploy Zink + Gallium DLLs to System32 (global OpenGL without launcher)
# ---------------------------------------------------------------------------
$zinkDlls = @("opengl32.dll", "libgallium_wgl.dll")

function Get-ZinkDir {
    if (Test-Path "$BinDir64\opengl32.dll") { return $BinDir64 }
    if (Test-Path "$ThirdPartyDir\zink\opengl32.dll") { return "$ThirdPartyDir\zink" }
    return $null
}
function Get-ZinkDir32 {
    if ($BinDir32 -and (Test-Path "$BinDir32\opengl32.dll")) { return $BinDir32 }
    if (Test-Path "$ThirdPartyDir\zink\x86\opengl32.dll") { return "$ThirdPartyDir\zink\x86" }
    return $null
}

# 64-bit → System32
Write-Host "--- Global OpenGL (Zink + Gallium to System32) ---" -ForegroundColor Cyan
$sys32 = "$env:SystemRoot\System32"
$zinkSrc64 = Get-ZinkDir
if ($zinkSrc64) {
    if ((Test-Path "$sys32\opengl32.dll") -and -not (Test-Path "$sys32\opengl32.dll.orig")) {
        Rename-Item -Path "$sys32\opengl32.dll" -NewName "opengl32.dll.orig" -Force
        Write-Host "  [OK] Backed up original opengl32.dll -> opengl32.dll.orig"
    }
    foreach ($dll in $zinkDlls) {
        $src = "$zinkSrc64\$dll"
        if (Test-Path $src) {
            Copy-Item -Path $src -Destination "$sys32\$dll" -Force
            Write-Host "  [OK] $dll -> System32"
        }
    }
} else {
    Write-Host "  [--] Zink 64-bit not found (skip OpenGL)"
}

# 32-bit → SysWOW64
$syswow = "$env:SystemRoot\SysWOW64"
$zinkSrc32 = Get-ZinkDir32
if ($zinkSrc32) {
    if ((Test-Path "$syswow\opengl32.dll") -and -not (Test-Path "$syswow\opengl32.dll.orig")) {
        Rename-Item -Path "$syswow\opengl32.dll" -NewName "opengl32.dll.orig" -Force
        Write-Host "  [OK] Backed up original 32-bit opengl32.dll"
    }
    foreach ($dll in $zinkDlls) {
        $src = "$zinkSrc32\$dll"
        if (Test-Path $src) {
            Copy-Item -Path $src -Destination "$syswow\$dll" -Force
            Write-Host "  [OK] $dll -> SysWOW64"
        }
    }
} else {
    Write-Host "  [--] Zink 32-bit not found, skipping SysWOW64"
}

# ---------------------------------------------------------------------------
# Deploy clvk (OpenCL) DLLs to System32 (global OpenCL without launcher)
# ---------------------------------------------------------------------------
Write-Host "--- Global OpenCL (clvk to System32) ---" -ForegroundColor Cyan
$clvkDlls = @("OpenCL.dll", "clspv.dll", "clvk.dll")
function Get-ClvkDir {
    if (Test-Path "$BinDir64\OpenCL.dll") { return $BinDir64 }
    if (Test-Path "$ThirdPartyDir\clvk\OpenCL.dll") { return "$ThirdPartyDir\clvk" }
    return $null
}
function Get-ClvkDir32 {
    if ($BinDir32 -and (Test-Path "$BinDir32\OpenCL.dll")) { return $BinDir32 }
    if (Test-Path "$ThirdPartyDir\clvk\x86\OpenCL.dll") { return "$ThirdPartyDir\clvk\x86" }
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
$clvkSrc64 = Get-ClvkDir
$clvkSrc32 = Get-ClvkDir32
Deploy-Clvk $clvkSrc64 $sys32 "64-bit" | Out-Null
if ($clvkSrc32) { Deploy-Clvk $clvkSrc32 $syswow "32-bit" | Out-Null }
if (-not $clvkSrc64) { Write-Host "  [--] clvk not found (skip OpenCL)" }

# ============================================================================
# Register ICD in Windows Registry
# ============================================================================
Write-Host ""
Write-Host "--- Registry Registration ---" -ForegroundColor Cyan

$icdJson64Path = "$InstallDir\vk_icd.json"
$icdJson32Path = "$InstallDir\x86\vk_icd.json"

# HKLM (requires admin — will silently fail if not elevated)
$admin = $false
try {
    $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
    $principal = New-Object Security.Principal.WindowsPrincipal($identity)
    $admin = $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
} catch {}

function Register-Key($regPath, $jsonPath, $archLabel) {
    try {
        if (-not (Test-Path $regPath)) {
            New-Item -Path $regPath -Force | Out-Null
        }
        New-ItemProperty -Path $regPath -Name $jsonPath -Value 0 -PropertyType DWord -Force | Out-Null
        Write-Host "  [OK] Registered ($archLabel): $regPath" -ForegroundColor Green
    } catch {
        Write-Host "  [--] Could not write $regPath ($archLabel): $_" -ForegroundColor Yellow
    }
}

# HKCU (always works, no admin)
Register-Key $RegPathUser $icdJson64Path "HKCU 64-bit"
Register-Key $RegPathUser32 $icdJson32Path "HKCU 32-bit"

# HKLM (only if admin)
if ($admin) {
    Register-Key $RegPath64 $icdJson64Path "HKLM 64-bit"
    Register-Key $RegPath32 $icdJson32Path "HKLM 32-bit"
    Write-Host "  [OK] Admin privileges detected — also registered in HKLM (system-wide)" -ForegroundColor Green

    Write-Host ""
    Write-Host "--- VulkanDriverName Registration (VkDiag compatibility) ---" -ForegroundColor Cyan
    $baseVideoPath = "HKLM:\SYSTEM\CurrentControlSet\Control\Video"
    if (Test-Path $baseVideoPath) {
        $foundGpu = $false
        foreach ($guidKey in (Get-ChildItem $baseVideoPath)) {
            if ((Test-Path "$($guidKey.PSPath)\Video") -and (Test-Path "$($guidKey.PSPath)\0000")) {
                $outputPath = "$($guidKey.PSPath)\0000"
                $existing = (Get-ItemProperty -LiteralPath $outputPath -Name "VulkanDriverName" -ErrorAction SilentlyContinue).VulkanDriverName
                $newVals = @()
                if ($existing -is [array]) { $newVals += $existing }
                elseif ($existing) { $newVals += $existing }
                $newVals += $icdJson64Path
                Set-ItemProperty -LiteralPath $outputPath -Name "VulkanDriverName" -Value $newVals -Type MultiString -Force
                Write-Host "  [OK] VulkanDriverName on GPU $($guidKey.PSChildName)" -ForegroundColor Green

                $existing32 = (Get-ItemProperty -LiteralPath $outputPath -Name "VulkanDriverNameWoW" -ErrorAction SilentlyContinue).VulkanDriverNameWoW
                $newVals32 = @()
                if ($existing32 -is [array]) { $newVals32 += $existing32 }
                elseif ($existing32) { $newVals32 += $existing32 }
                if (Test-Path $icdJson32Path) { $newVals32 += $icdJson32Path }
                Set-ItemProperty -LiteralPath $outputPath -Name "VulkanDriverNameWoW" -Value $newVals32 -Type MultiString -Force
                Write-Host "  [OK] VulkanDriverNameWoW on GPU $($guidKey.PSChildName)" -ForegroundColor Green

                $foundGpu = $true
                break
            }
        }
        if (-not $foundGpu) {
            Write-Host "  [--] No compatible GPU controller found" -ForegroundColor Yellow
        }
    }
} else {
    Write-Host "  [--] Not running as admin — HKLM registration skipped" -ForegroundColor Yellow
    Write-Host "  [..] HKCU registration is sufficient for the current user" -ForegroundColor Gray
}

# ============================================================================
# Summary
# ============================================================================
Write-Host ""
Write-Host "=== Installation Complete ===" -ForegroundColor Green
Write-Host "  Install path: $InstallDir"
if ($BuildDir32) {
    Write-Host "  32-bit path:  $InstallDir\x86"
}
Write-Host ""
Write-Host "  Vulkan:  OmniGPU ICD registered globally in registry"
Write-Host "  OpenGL:  Zink + Gallium deployed to System32/SysWOW64"
Write-Host "  OpenCL:  clvk deployed to System32/SysWOW64"
Write-Host ""
Write-Host "  Any Vulkan, OpenGL, or OpenCL app now auto-detects OmniGPU."
Write-Host ""
Write-Host "  To uninstall: .\scripts\install_guest.ps1 -Uninstall"
Write-Host "  (original opengl32.dll restored from .orig backup)"
Write-Host "================================="
