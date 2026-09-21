#===============================================================================
# OmniGPU Build + Package for Windows
#===============================================================================

param(
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
# Step 1: Build 64-bit
# ============================================================================
# Locate Visual Studio installation via vswhere, with hardcoded fallback
$vcvars = ""
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
if (Test-Path $vswhere) {
    $vsPath = & $vswhere -latest -property installationPath
    if ($vsPath) { $vcvars = "$vsPath\VC\Auxiliary\Build\vcvarsall.bat" }
}
if (-not $vcvars -or !(Test-Path $vcvars)) {
    $vcvars = "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvarsall.bat"
}

if (-not $SkipBuild) {
    Write-Host "=== [1/6] Building 64-bit ===" -ForegroundColor Cyan
    $cmds = @(
        "`"$vcvars`" x64",
        "cmake --preset release -DCMAKE_CXX_FLAGS=`"/EHsc /Zi`"",
        "cmake --build --preset release --clean-first --parallel"
    )
    cmd /c ($cmds -join " && ") 2>&1
    if ($LASTEXITCODE -ne 0) { Write-Error "64-bit build failed"; exit 1 }
    Write-Host "  [OK] 64-bit build complete" -ForegroundColor Green
}

# ============================================================================
# Step 2: Build 32-bit (only guest DLL for Vulkan ICD)
# ============================================================================
if (-not $SkipBuild) {
    Write-Host "=== [2/6] Building 32-bit guest DLL ===" -ForegroundColor Cyan
    $cmds32 = @(
        "`"$vcvars`" x86",
        "cmake --preset release-x86 -DCMAKE_CXX_FLAGS=`"/EHsc /Zi`"",
        "cmake --build --preset release-x86 --clean-first --parallel"
    )
    cmd /c ($cmds32 -join " && ") 2>&1
    if ($LASTEXITCODE -ne 0) { Write-Error "32-bit build failed"; exit 1 }
    Write-Host "  [OK] 32-bit build complete" -ForegroundColor Green
}

# ============================================================================
# Step 3: Create distribution folder
# ============================================================================
Write-Host "=== [3/6] Assembling distribution package ===" -ForegroundColor Cyan

if (Test-Path $DistDir) { Remove-Item -Recurse -Force $DistDir }
New-Item -ItemType Directory -Path $DistDir -Force | Out-Null
New-Item -ItemType Directory -Path "$DistDir\x64" -Force | Out-Null
New-Item -ItemType Directory -Path "$DistDir\x86" -Force | Out-Null
New-Item -ItemType Directory -Path "$DistDir\docs" -Force | Out-Null

$Bin64 = "$BuildDir64\bin"
$Bin32 = "$BuildDir32\bin"

# ----- Root: 64-bit exes + runtime DLLs -----
Copy-Item -Path "$Bin64\omnigpu_host.exe" -Destination $DistDir -Force
Copy-Item -Path "$Bin64\omnigpu_vk_test.exe" -Destination $DistDir -Force

# x64: Vulkan ICD driver
Copy-Item -Path "$Bin64\omnigpu_guest.dll" -Destination "$DistDir\x64" -Force
Copy-Item -Path "$Bin64\vk_icd.json" -Destination "$DistDir\x64" -Force
# Inject correct library_arch for 64-bit
$x64json = Get-Content "$DistDir\x64\vk_icd.json" -Raw | ConvertFrom-Json
$x64json.ICD | Add-Member -NotePropertyName "library_arch" -NotePropertyValue "64" -Force
$x64json | ConvertTo-Json -Depth 5 | Set-Content "$DistDir\x64\vk_icd.json" -Encoding UTF8
Get-ChildItem "$Bin64\*.dll" | ForEach-Object {
    if ($_.Name -notmatch "^(omnigpu_guest|vulkan-1)") {
        Copy-Item -Path $_.FullName -Destination "$DistDir\x64" -Force
    }
}

# MSVC runtime DLLs (required by omnigpu_guest.dll, must be alongside it)
$msvcRedist = ""
if ($vsPath) {
    $crtDir = Get-ChildItem "$vsPath\VC\Redist\MSVC\*\x64\Microsoft.VC145.CRT" -ErrorAction SilentlyContinue |
              Select-Object -First 1 -ExpandProperty FullName
    if ($crtDir) { $msvcRedist = $crtDir }
}
if (-not $msvcRedist -or !(Test-Path $msvcRedist)) {
    $msvcRedist = "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Redist\MSVC\14.51.36231\x64\Microsoft.VC145.CRT"
}
if (Test-Path $msvcRedist) {
    Get-ChildItem "$msvcRedist\*.dll" | ForEach-Object {
        Copy-Item -Path $_.FullName -Destination "$DistDir\x64" -Force
        # Also to root (for exes running outside install dir)
        if ($_.Name -notmatch "^vccorlib|^concrt") {
            Copy-Item -Path $_.FullName -Destination $DistDir -Force
        }
    }
}

# x86: Vulkan ICD driver
if (Test-Path "$Bin32\omnigpu_guest.dll") {
    Copy-Item -Path "$Bin32\omnigpu_guest.dll" -Destination "$DistDir\x86" -Force
    Copy-Item -Path "$Bin32\vk_icd.json" -Destination "$DistDir\x86" -Force
    # Inject correct library_arch for 32-bit
    $x86json = Get-Content "$DistDir\x86\vk_icd.json" -Raw | ConvertFrom-Json
    $x86json.ICD | Add-Member -NotePropertyName "library_arch" -NotePropertyValue "32" -Force
    $x86json | ConvertTo-Json -Depth 5 | Set-Content "$DistDir\x86\vk_icd.json" -Encoding UTF8
    Get-ChildItem "$Bin32\*.dll" | ForEach-Object {
        if ($_.Name -notmatch "^(omnigpu_guest|vulkan-1)") {
            Copy-Item -Path $_.FullName -Destination "$DistDir\x86" -Force
        }
    }
    # MSVC runtime x86 DLLs
    $msvcRedist32 = ""
    if ($vsPath) {
        $crtDir32 = Get-ChildItem "$vsPath\VC\Redist\MSVC\*\x86\Microsoft.VC145.CRT" -ErrorAction SilentlyContinue |
                    Select-Object -First 1 -ExpandProperty FullName
        if ($crtDir32) { $msvcRedist32 = $crtDir32 }
    }
    if (-not $msvcRedist32 -or !(Test-Path $msvcRedist32)) {
        $msvcRedist32 = "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Redist\MSVC\14.51.36231\x86\Microsoft.VC145.CRT"
    }
    if (Test-Path $msvcRedist32) {
        Get-ChildItem "$msvcRedist32\*.dll" | ForEach-Object {
            Copy-Item -Path $_.FullName -Destination "$DistDir\x86" -Force
        }
    }
}

# Runtime DLLs also to root (for executables)
Get-ChildItem "$Bin64\*.dll" | ForEach-Object {
    if ($_.Name -notmatch "^(omnigpu_guest)") {
        Copy-Item -Path $_.FullName -Destination $DistDir -Force
    }
}

# ============================================================================
# Step 4: Scripts + Docs
# ============================================================================
Write-Host "=== [4/6] Adding scripts and docs ===" -ForegroundColor Cyan

Copy-Item -Path "$ProjectRoot\scripts\windows\install.bat" -Destination $DistDir -Force
Copy-Item -Path "$ProjectRoot\scripts\windows\uninstall.bat" -Destination $DistDir -Force
Copy-Item -Path "$ProjectRoot\omnigpu_guest.json" -Destination $DistDir -Force
Copy-Item -Path "$ProjectRoot\omnigpu_host.json" -Destination $DistDir -Force
Copy-Item -Path "$ProjectRoot\docs\installation-windows.md" -Destination "$DistDir\docs" -Force
Copy-Item -Path "$ProjectRoot\README.md" -Destination "$DistDir\docs" -Force
Copy-Item -Path "$ProjectRoot\docs\readme-package.txt" -Destination "$DistDir\README.txt" -Force

# ============================================================================
# Step 5: Optionally install
# ============================================================================
if ($Install) {
    Write-Host "=== [5/5] Installing system-wide ===" -ForegroundColor Cyan
    & "$DistDir\install.bat"
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
Write-Host "  To install, run as admin:"
Write-Host "    install.bat"
Write-Host ""
