#===============================================================================
# OmniGPU Build + Package for Windows
#
# Clean build and assemble everything into a single deployable folder:
#   build/dist/OmniGPU-v{version}/
#
# Usage:
#   .\scripts\windows\build-and-package.ps1              # 64-bit only
#   .\scripts\windows\build-and-package.ps1 -Build32      # 64 + 32 bit
#   .\scripts\windows\build-and-package.ps1 -SkipBuild    # re-package only
#   .\scripts\windows\build-and-package.ps1 -Install      # build + install
#===============================================================================

param(
    [switch]$Build32 = $false,
    [switch]$SkipBuild = $false,
    [switch]$Install = $false,
    [string]$HostAddr = "",
    [int]$HostPort = 9443
)

$ErrorActionPreference = "Stop"
$ProjectRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$Version = "0.1.0"
$DistDir = "$ProjectRoot\build\dist\OmniGPU-v$Version"
$BuildDir64 = "$ProjectRoot\build\release"
$BuildDir32 = "$ProjectRoot\build\release-x86"
$ThirdParty = "$ProjectRoot\third_party"

# ============================================================================
# Step 0: Fetch third-party dependencies (Zink + clvk)
# ============================================================================
if (-not $SkipBuild) {
    Write-Host "=== [0/5] Fetching third-party dependencies ===" -ForegroundColor Cyan
    python "$ThirdParty\fetch_zink.py" --output-dir "$ThirdParty\zink" 2>&1
    python "$ThirdParty\fetch_clvk.py" --output-dir "$ThirdParty\clvk" 2>&1
}

# ============================================================================
# Step 1: Clean + Build 64-bit
# ============================================================================
if (-not $SkipBuild) {
    # Source VS env and build
    $vcvars = "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvarsall.bat"
    $cmds = @(
        "`"$vcvars`" x64",
        "cmake --preset release -DCMAKE_CXX_FLAGS=`"/EHsc /Zi`"",
        "cmake --build --preset release --clean-first --parallel"
    )
    Write-Host "=== [1/5] Clean building 64-bit ===" -ForegroundColor Cyan
    cmd /c ($cmds -join " && ") 2>&1
    if ($LASTEXITCODE -ne 0) { Write-Error "64-bit build failed"; exit 1 }
    Write-Host "  [OK] 64-bit build complete" -ForegroundColor Green
}

# ============================================================================
# Step 2: Clean + Build 32-bit (optional)
# ============================================================================
if ($Build32 -and -not $SkipBuild) {
    $cmds32 = @(
        "`"$vcvars`" x86",
        "cmake --preset release-x86 -DCMAKE_CXX_FLAGS=`"/EHsc /Zi /m32`"",
        "cmake --build --preset release-x86 --clean-first --parallel"
    )
    Write-Host "=== [2/5] Clean building 32-bit ===" -ForegroundColor Cyan
    cmd /c ($cmds32 -join " && ") 2>&1
    if ($LASTEXITCODE -ne 0) { Write-Error "32-bit build failed"; exit 1 }
    Write-Host "  [OK] 32-bit build complete" -ForegroundColor Green
}

# ============================================================================
# Step 3: Create distribution folder
# ============================================================================
Write-Host "=== [3/5] Assembling distribution package ===" -ForegroundColor Cyan

# Clean and create dist dirs
if (Test-Path $DistDir) { Remove-Item -Recurse -Force $DistDir }
New-Item -ItemType Directory -Path $DistDir -Force | Out-Null
New-Item -ItemType Directory -Path "$DistDir\x86" -Force | Out-Null
New-Item -ItemType Directory -Path "$DistDir\scripts" -Force | Out-Null
New-Item -ItemType Directory -Path "$DistDir\docs" -Force | Out-Null

# 64-bit core files
$Bin64 = "$BuildDir64\bin"
Copy-Item -Path "$Bin64\omnigpu_guest.dll" -Destination $DistDir -Force
Copy-Item -Path "$Bin64\omnigpu_host.exe" -Destination $DistDir -Force
Copy-Item -Path "$Bin64\omnigpu_launcher.exe" -Destination $DistDir -Force
Copy-Item -Path "$Bin64\vk_icd.json" -Destination $DistDir -Force

# 64-bit Zink + Gallium DLLs
foreach ($dll in @("opengl32.dll", "libgallium_wgl.dll")) {
    $src = "$Bin64\$dll"
    if (Test-Path $src) { Copy-Item -Path $src -Destination $DistDir -Force }
}

# 64-bit clvk DLLs
foreach ($dll in @("OpenCL.dll", "clspv.dll", "clvk.dll")) {
    $src = "$Bin64\$dll"
    if (Test-Path $src) { Copy-Item -Path $src -Destination $DistDir -Force }
}

# 64-bit runtime DLLs (MSVC, Vulkan Loader)
Get-ChildItem "$Bin64\*.dll" | ForEach-Object {
    if ($_.Name -notmatch "^(omnigpu_guest|opengl32|OpenCL|libgallium_wgl|clspv|clvk)\.dll$") {
        Copy-Item -Path $_.FullName -Destination $DistDir -Force
    }
}

# 32-bit files
if ($Build32 -and (Test-Path "$BuildDir32\bin")) {
    $Bin32 = "$BuildDir32\bin"
    Copy-Item -Path "$Bin32\omnigpu_guest.dll" -Destination "$DistDir\x86" -Force
    Copy-Item -Path "$Bin32\vk_icd.json" -Destination "$DistDir\x86" -Force

    foreach ($dll in @("opengl32.dll", "libgallium_wgl.dll",
                        "OpenCL.dll", "clspv.dll", "clvk.dll")) {
        $src = "$Bin32\$dll"
        if (Test-Path $src) { Copy-Item -Path $src -Destination "$DistDir\x86" -Force }
    }

    Get-ChildItem "$Bin32\*.dll" | ForEach-Object {
        if ($_.Name -notmatch "^(omnigpu_guest|opengl32|OpenCL|libgallium_wgl|clspv|clvk)\.dll$") {
            Copy-Item -Path $_.FullName -Destination "$DistDir\x86" -Force
        }
    }
}

# ============================================================================
# Step 4: Scripts + Docs
# ============================================================================
Write-Host "=== [4/5] Adding scripts and docs ===" -ForegroundColor Cyan

# Install scripts
Copy-Item -Path "$ProjectRoot\scripts\windows\quick-install.ps1" -Destination "$DistDir\scripts" -Force
Copy-Item -Path "$ProjectRoot\scripts\windows\install_guest.ps1" -Destination "$DistDir\scripts" -Force

# Documentation
Copy-Item -Path "$ProjectRoot\docs\installation-windows.md" -Destination "$DistDir\docs" -Force
Copy-Item -Path "$ProjectRoot\README.md" -Destination "$DistDir\docs" -Force
Copy-Item -Path "$ProjectRoot\AGENTS.md" -Destination "$DistDir\docs" -Force

# Create README.txt for the package
@"
OmniGPU v$Version - GPU Forwarding over LAN
============================================

Core:
  omnigpu_guest.dll     Vulkan ICD driver (guest)
  omnigpu_host.exe      Host server (run on physical GPU machine)
  omnigpu_launcher.exe  App launcher (deploys Zink/clvk)
  vk_icd.json           ICD manifest for Vulkan Loader

OpenGL (Zink + Gallium):
  opengl32.dll          Zink OpenGL loader
  libgallium_wgl.dll    Gallium driver (Zink + softpipe, 59 MB)

OpenCL (clvk):
  OpenCL.dll            clvk OpenCL->Vulkan implementation

Runtime:
  vulkan-1.dll          Vulkan Loader
  dxil.dll              D3D12 shader compiler (for Mesa)
  *.dll                 MSVC runtime DLLs

Architecture:
  x86/                  32-bit versions (optional, if built)
  scripts/              Install scripts (PS5.1 compatible)
  docs/                 Documentation

Quick Install (run as Administrator):
  .\scripts\quick-install.ps1 -BuildDir ".." -HostAddr "192.168.1.100"

For full docs: docs\installation-windows.md
"@ | Set-Content -Path "$DistDir\README.txt" -Force

# ============================================================================
# Step 5: Optionally install
# ============================================================================
if ($Install) {
    Write-Host "=== [5/5] Installing system-wide ===" -ForegroundColor Cyan
    $installArgs = @("-BuildDir64", $BuildDir64)
    if ($Build32) { $installArgs += @("-BuildDir32", $BuildDir32) }
    if ($HostAddr) { $installArgs += @("-HostAddr", $HostAddr, "-HostPort", $HostPort) }

    & "$ProjectRoot\scripts\windows\quick-install.ps1" @installArgs
    if ($LASTEXITCODE -ne 0) { Write-Error "Install failed"; exit 1 }
}

# ============================================================================
# Summary
# ============================================================================
$sizeMB = [math]::Round((Get-ChildItem -Recurse $DistDir | Measure-Object -Property Length -Sum).Sum / 1MB, 1)
Write-Host ""
Write-Host "============================================" -ForegroundColor Green
Write-Host "  OmniGPU v$Version packaged successfully!" -ForegroundColor Green
Write-Host "  Package: $DistDir" -ForegroundColor Green
Write-Host "  Size:    $sizeMB MB" -ForegroundColor Green
Write-Host "============================================" -ForegroundColor Green
Write-Host ""
Write-Host "  To install on this machine:"
Write-Host "    .\scripts\windows\quick-install.ps1 -BuildDir `"$BuildDir64`" -HostAddr `"$HostAddr`""
Write-Host ""
Write-Host "  Or copy the dist folder to a VM and run:"
Write-Host "    cd OmniGPU-v$Version"
Write-Host "    .\scripts\quick-install.ps1 -PackageDir `"$PWD`" -HostAddr `"192.168.1.x`""
Write-Host ""
