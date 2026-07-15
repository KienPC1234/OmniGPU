param(
    [string]$BuildType = "release",
    [string]$HostPort = 9443
)

$ErrorActionPreference = "SilentlyContinue"

Write-Host "=======================================================" -ForegroundColor Cyan
Write-Host "         OmniGPU Windows Diagnosis Tool v2.0           " -ForegroundColor Cyan
Write-Host "=======================================================" -ForegroundColor Cyan
Write-Host ""

$errors = 0
$warnings = 0
$rootDir = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$binDir = Join-Path (Join-Path (Join-Path $rootDir "build") $BuildType) "bin"
if (-not (Test-Path $binDir)) { $binDir = Join-Path $rootDir "bin" }

# =========================================================
# 1. Host Server
# =========================================================
Write-Host "[1/5] Host Server Status" -ForegroundColor Yellow

$hostProc = Get-Process -Name "omnigpu_host" -ErrorAction SilentlyContinue
if ($hostProc) {
    Write-Host "  [OK] omnigpu_host.exe running (PID: $($hostProc.Id), Start: $($hostProc.StartTime.ToShortTimeString()))" -ForegroundColor Green
} else {
    Write-Host "  [WARN] omnigpu_host.exe NOT running" -ForegroundColor Yellow
    $warnings++
}

$listener = Get-NetTCPConnection -LocalPort $HostPort -ErrorAction SilentlyContinue | Where-Object State -eq Listen
if ($listener) {
    Write-Host "  [OK] Port $HostPort listening (PID: $($listener.OwningProcess))" -ForegroundColor Green
} else {
    Write-Host "  [WARN] Port $HostPort NOT listening" -ForegroundColor Yellow
    $warnings++
}

# Active sessions
$activeConns = Get-NetTCPConnection -LocalPort $HostPort -ErrorAction SilentlyContinue | Where-Object State -eq Established
if ($activeConns) {
    Write-Host "  [OK] Active session(s):" -ForegroundColor Green
    foreach ($c in $activeConns) {
        Write-Host "       $($c.RemoteAddress):$($c.RemotePort) ($($c.State))" -ForegroundColor White
    }
} else {
    Write-Host "  [--] No active sessions" -ForegroundColor Gray
}

# Host log
$hostLog = Join-Path $rootDir "omnigpu_host.log"
if (Test-Path $hostLog) {
    $lastLines = Get-Content $hostLog -Tail 10 -ErrorAction SilentlyContinue
    Write-Host "  [OK] Host log exists ($((Get-Item $hostLog).Length / 1KB) KB)" -ForegroundColor Green
} else {
    Write-Host "  [WARN] Host log not found (run host first)" -ForegroundColor Yellow
    $warnings++
}

Write-Host ""
Write-Host "  [OK] Host GPU(s):" -ForegroundColor Green
$gpus = Get-CimInstance Win32_VideoController
foreach ($gpu in $gpus) {
    Write-Host "    - $($gpu.Caption) (VRAM: $([math]::Round($gpu.AdapterRAM/1GB, 1)) GB, Driver: $($gpu.DriverVersion))" -ForegroundColor White
}
Write-Host ""

# =========================================================
# 2. Guest Binaries
# =========================================================
Write-Host "[2/5] Guest Binaries" -ForegroundColor Yellow

if (Test-Path $binDir) {
    Write-Host "  [OK] Binary dir: $binDir" -ForegroundColor Green
} else {
    Write-Host "  [ERROR] Binary dir not found! Build first." -ForegroundColor Red
    $errors++
    exit
}

$guestDll = Join-Path $binDir "omnigpu_guest.dll"
if (Test-Path $guestDll) {
    $size = (Get-Item $guestDll).Length / 1KB
    Write-Host "  [OK] omnigpu_guest.dll ($([math]::Round($size, 1)) KB)" -ForegroundColor Green
} else {
    Write-Host "  [ERROR] omnigpu_guest.dll missing!" -ForegroundColor Red
    $errors++
}

$guestTest = Join-Path $binDir "omnigpu_guest_test.exe"
if (Test-Path $guestTest) {
    Write-Host "  [OK] omnigpu_guest_test.exe (test harness)" -ForegroundColor Green
} else {
    Write-Host "  [WARN] omnigpu_guest_test.exe missing (only needed for testing)" -ForegroundColor Yellow
    $warnings++
}

$guestHost = Join-Path $binDir "omnigpu_host.exe"
if (Test-Path $guestHost) {
    Write-Host "  [OK] omnigpu_host.exe exists" -ForegroundColor Green
} else {
    Write-Host "  [ERROR] omnigpu_host.exe missing!" -ForegroundColor Red
    $errors++
}

Write-Host ""

# =========================================================
# 3. Vulkan ICD Configuration
# =========================================================
Write-Host "[3/5] Vulkan ICD Configuration" -ForegroundColor Yellow

$icdJson = Join-Path $binDir "vk_icd.json"
if (Test-Path $icdJson) {
    Write-Host "  [OK] vk_icd.json exists" -ForegroundColor Green
    try {
        $icdContent = Get-Content $icdJson -Raw | ConvertFrom-Json
        $libPath = $icdContent.ICD.library_path
        Write-Host "    - ICD library_path: $libPath" -ForegroundColor White
        # Verify linked DLL exists
        $linkedDll = Join-Path $binDir $libPath
        if (Test-Path $linkedDll) {
            Write-Host "    [OK] Linked DLL found" -ForegroundColor Green
        } else {
            Write-Host "    [WARN] Linked DLL NOT at expected path: $linkedDll" -ForegroundColor Yellow
            $warnings++
        }
    } catch {
        Write-Host "    [ERROR] Invalid JSON: $_" -ForegroundColor Red
        $errors++
    }
} else {
    Write-Host "  [ERROR] vk_icd.json missing!" -ForegroundColor Red
    $errors++
}

# Environment
$envIcd = [Environment]::GetEnvironmentVariable("VK_ICD_FILENAMES", "Process")
if (-not $envIcd) { $envIcd = [Environment]::GetEnvironmentVariable("VK_ICD_FILENAMES", "User") }
if (-not $envIcd) { $envIcd = [Environment]::GetEnvironmentVariable("VK_ICD_FILENAMES", "Machine") }
if ($envIcd) {
    Write-Host "  [OK] VK_ICD_FILENAMES = $envIcd" -ForegroundColor Green
} else {
    Write-Host "  [WARN] VK_ICD_FILENAMES not set (Launcher sets at runtime)" -ForegroundColor Yellow
    $warnings++
}

# Registry registration
$regFound = $false
$regPaths = @("HKLM:\SOFTWARE\Khronos\Vulkan\Drivers", "HKCU:\SOFTWARE\Khronos\Vulkan\Drivers")
foreach ($path in $regPaths) {
    if (Test-Path $path) {
        $vals = Get-ItemProperty -Path $path
        foreach ($prop in $vals.PSObject.Properties.Name) {
            if ($prop -match "vk_icd") {
                Write-Host "  [OK] Registry: $path\$prop = $($vals.$prop)" -ForegroundColor Green
                $regFound = $true
            }
        }
    }
}
if (-not $regFound) {
    Write-Host "  [WARN] Guest driver NOT registered in Registry (Launcher sets per-process)" -ForegroundColor Yellow
    $warnings++
}
Write-Host ""

# =========================================================
# 4. Translation Layers
# =========================================================
Write-Host "[4/5] Translation Layers" -ForegroundColor Yellow

$zinkDll = Join-Path $binDir "opengl32.dll"
if (Test-Path $zinkDll) {
    $size = (Get-Item $zinkDll).Length / 1MB
    Write-Host "  [OK] Mesa Zink (opengl32.dll, $([math]::Round($size, 1)) MB)" -ForegroundColor Green
} else {
    Write-Host "  [WARN] Zink (opengl32.dll) missing - OpenGL→Vulkan translation unavailable" -ForegroundColor Yellow
    $warnings++
}

$clvkDll = Join-Path $binDir "OpenCL.dll"
if (Test-Path $clvkDll) {
    $size = (Get-Item $clvkDll).Length / 1MB
    Write-Host "  [OK] clvk (OpenCL.dll, $([math]::Round($size, 1)) MB)" -ForegroundColor Green
} else {
    Write-Host "  [WARN] clvk (OpenCL.dll) missing - run scripts/build_clvk_windows.bat" -ForegroundColor Yellow
    $warnings++
}

Write-Host ""

# =========================================================
# 5. Runtime Dependencies
# =========================================================
Write-Host "[5/5] Runtime Dependencies" -ForegroundColor Yellow

$runtimeDlls = @("vcruntime140.dll", "vcruntime140_1.dll", "msvcp140.dll")
$allFound = $true
foreach ($dll in $runtimeDlls) {
    $path = Join-Path $binDir $dll
    if (Test-Path $path) {
        Write-Host "  [OK] $dll (bundled)" -ForegroundColor Green
    } else {
        $sysPath = Join-Path $env:SystemRoot "System32" $dll
        if (Test-Path $sysPath) {
            Write-Host "  [OK] $dll (system)" -ForegroundColor Green
        } else {
            Write-Host "  [WARN] $dll missing - deploy MSVC runtime or copy from host" -ForegroundColor Yellow
            $warnings++
            $allFound = $false
        }
    }
}

if ($allFound) {
    Write-Host "  [OK] All MSVC runtime DLLs resolved" -ForegroundColor Green
}

Write-Host ""
Write-Host "=======================================================" -ForegroundColor Cyan
if ($errors -eq 0) {
    $status = if ($warnings -gt 0) { "STABLE (with warnings)" } else { "HEALTHY" }
    Write-Host " DIAGNOSIS: SYSTEM $status" -ForegroundColor Green
    Write-Host " Errors: $errors | Warnings: $warnings" -ForegroundColor $('Green', 'Yellow')[$warnings -gt 0]
} else {
    Write-Host " DIAGNOSIS: $errors ERROR(S) - FIX BEFORE DEPLOYMENT" -ForegroundColor Red
}
Write-Host "=======================================================" -ForegroundColor Cyan
Write-Host ""
Write-Host "Quick commands:" -ForegroundColor Gray
Write-Host "  Start host:  Start-Process -WindowStyle Hidden '$binDir\omnigpu_host.exe'" -ForegroundColor Gray
Write-Host "  Stop host:   Stop-Process -Name omnigpu_host -Force" -ForegroundColor Gray
Write-Host "  Test guest:  ssh test@192.168.1.113 powershell -File C:\Users\test\Downloads\test_guest.ps1" -ForegroundColor Gray
Write-Host "  Deploy:      scp $binDir\omnigpu_guest*.exe $binDir\omnigpu_guest.dll $binDir\vk_icd.json test@192.168.1.113:dest\path\" -ForegroundColor Gray
