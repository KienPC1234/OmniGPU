param(
    [string]$BuildType = "release",
    [int]$HostPort = 9443,
    [string]$SshHost = "192.168.1.113",
    [string]$SshUser = "test",
    [string]$SshPass = "Htt@123456",
    [string]$RemoteDir = "C:\Users\test\Downloads\GPU_Caps_Viewer_1.64.3.0\GPU_Caps_Viewer",
    [switch]$SkipBuild,
    [switch]$SkipDeploy,
    [switch]$SkipConnectTest
)

$ErrorActionPreference = "Stop"
$rootDir = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$binDir = Join-Path (Join-Path (Join-Path $rootDir "build") $BuildType) "bin"
$askpass = Join-Path $PSScriptRoot "askpass.bat"
$testScript = Join-Path $PSScriptRoot "test_guest.ps1"

$env:SSH_ASKPASS = $askpass
$env:DISPLAY = "dummy"
$env:SSH_ASKPASS_REQUIRE = "force"
$sshOpts = "-o StrictHostKeyChecking=no"
$sshBase = "${SshUser}@${SshHost}"

Write-Host "=============================================" -ForegroundColor Cyan
Write-Host "   OmniGPU Build + Deploy + Test Tool v1.0   " -ForegroundColor Cyan
Write-Host "=============================================" -ForegroundColor Cyan
Write-Host ""

# STEP 1: BUILD
if (-not $SkipBuild) {
    Write-Host "[1/4] Building $BuildType..." -ForegroundColor Yellow

    # Kill host if running (so build can overwrite)
    Stop-Process -Name omnigpu_host -Force -ErrorAction SilentlyContinue
    Start-Sleep 2

    $result = cmd /c "`"C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvarsall.bat`" x64 && cmake --build --preset $BuildType 2>&1"
    $buildOk = $LASTEXITCODE -eq 0
    if ($buildOk) {
        Write-Host "  [OK] Build succeeded" -ForegroundColor Green
    } else {
        Write-Host "  [ERROR] Build failed (exit code: $LASTEXITCODE)" -ForegroundColor Red
        Write-Host "$result"
        exit 1
    }
    Write-Host ""
} else {
    Write-Host "[1/4] Build SKIPPED" -ForegroundColor Gray
    Write-Host ""
}

# STEP 2: START HOST
Write-Host "[2/4] Starting Host..." -ForegroundColor Yellow
Stop-Process -Name omnigpu_host -Force -ErrorAction SilentlyContinue
Start-Sleep 1
Remove-Item (Join-Path $rootDir "omnigpu_host.log") -ErrorAction SilentlyContinue
Remove-Item (Join-Path $rootDir "omnigpu_guest.log") -ErrorAction SilentlyContinue

Start-Process -WindowStyle Hidden -FilePath (Join-Path $binDir "omnigpu_host.exe")
Start-Sleep 3

$listener = Get-NetTCPConnection -LocalPort $HostPort -ErrorAction SilentlyContinue | Where-Object State -eq Listen
if ($listener) {
    Write-Host "  [OK] Host running on port $HostPort (PID: $($listener.OwningProcess))" -ForegroundColor Green
} else {
    Write-Host "  [ERROR] Host failed to start on port $HostPort" -ForegroundColor Red
    $hostLog = Get-Content (Join-Path $rootDir "omnigpu_host.log") -ErrorAction SilentlyContinue
    if ($hostLog) { Write-Host "$hostLog" }
    exit 1
}
Write-Host ""

# STEP 3: DEPLOY
if (-not $SkipDeploy) {
    Write-Host "[3/4] Deploying to $SshHost..." -ForegroundColor Yellow

    $dlls = @("omnigpu_guest.dll", "omnigpu_guest_test.exe", "vk_icd.json")
    if (Test-Path (Join-Path $binDir "opengl32.dll")) {
        $dlls += "opengl32.dll"
    }

    foreach ($file in $dlls) {
        $localPath = Join-Path $binDir $file
        if (Test-Path $localPath) {
            Write-Host "  Copying $file..." -ForegroundColor Gray
            $null = ssh $sshOpts "$sshBase" "powershell -Command `"Stop-Process -Name omnigpu_guest_test -Force -ErrorAction SilentlyContinue`"" 2>&1 | Out-Null
            Start-Sleep 1
            $null = scp $sshOpts $localPath "${sshBase}:${RemoteDir}\" 2>&1 | Out-Null
        }
    }

    # Also deploy MSVC runtime DLLs if needed
    $msvcDlls = @("vcruntime140.dll", "vcruntime140_1.dll", "msvcp140.dll")
    foreach ($dll in $msvcDlls) {
        $localPath = Join-Path $binDir $dll
        if (-not (Test-Path $localPath)) {
            $localPath = Join-Path (Join-Path $env:SystemRoot "System32") $dll
        }
        if (Test-Path $localPath) {
            $null = scp $sshOpts $localPath "${sshBase}:${RemoteDir}\" 2>&1 | Out-Null
        }
    }

    Write-Host "  [OK] Deploy completed" -ForegroundColor Green
    Write-Host ""
} else {
    Write-Host "[3/4] Deploy SKIPPED" -ForegroundColor Gray
    Write-Host ""
}

# STEP 4: TEST CONNECTION
if (-not $SkipConnectTest) {
    Write-Host "[4/4] Testing connection..." -ForegroundColor Yellow

    # Copy test script
    $null = scp $sshOpts $testScript "${sshBase}:C:\Users\${SshUser}\Downloads\test_guest.ps1" 2>&1 | Out-Null

    # Run test
    Start-Sleep 2
    $output = ssh $sshOpts "$sshBase" "powershell -ExecutionPolicy Bypass -File C:\Users\${SshUser}\Downloads\test_guest.ps1" 2>&1

    Write-Host ""
    Write-Host "--- Guest Test Output ---" -ForegroundColor Cyan
    Write-Host "$output"
    Write-Host "--- End ---" -ForegroundColor Cyan
    Write-Host ""

    # Check results
    if ($output -match "Host GPU capabilities received" -or $output -match "Using cached GPU capabilities") {
        Write-Host "  [OK] Handshake complete: Guest received host GPU info!" -ForegroundColor Green
    } elseif ($output -match "Connected to") {
        Write-Host "  [WARN] Connected but handshake incomplete" -ForegroundColor Yellow
    } elseif ($output -match "Failed to connect") {
        Write-Host "  [ERROR] Connection failed" -ForegroundColor Red
    } else {
        Write-Host "  [?] Unknown result - check logs" -ForegroundColor Yellow
    }

    # Show host log summary
    Start-Sleep 1
    $hostLog = @(Get-Content (Join-Path $rootDir "omnigpu_host.log") -ErrorAction SilentlyContinue)
    Write-Host ""
    Write-Host "--- Host Log Summary ---" -ForegroundColor Cyan
    if ($hostLog -match "Guest requested capabilities") {
        Write-Host "  [OK] Host received guest handshake" -ForegroundColor Green
    } else {
        # Show last 5 lines safely
        if ($hostLog.Count -gt 0) {
            $startIdx = [math]::max(0, $hostLog.Count - 5)
            $hostLog[$startIdx..($hostLog.Count - 1)] | ForEach-Object { Write-Host "  $_" -ForegroundColor Gray }
        }
    }
    Write-Host ""

} else {
    Write-Host "[4/4] Connect test SKIPPED" -ForegroundColor Gray
}

Write-Host "=============================================" -ForegroundColor Cyan
Write-Host "   Build-Deploy-Test Complete                " -ForegroundColor Cyan
Write-Host "=============================================" -ForegroundColor Cyan
