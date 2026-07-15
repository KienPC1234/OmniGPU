# OmniGPU Host Service Installer
# PowerShell script to install/start/stop/uninstall the Host service

param(
    [ValidateSet("install", "uninstall", "start", "stop", "restart")]
    [string]$Command = "install",
    [string]$BinaryDir = ""
)

# Auto-detect binary directory
if (-not $BinaryDir) {
    $scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
    $buildDir = Join-Path (Split-Path -Parent $scriptDir) "build\debug\bin"
    if (Test-Path (Join-Path $buildDir "omnigpu_host.exe")) {
        $BinaryDir = $buildDir
    } else {
        $BinaryDir = $scriptDir
    }
}

$hostExe = Join-Path $BinaryDir "omnigpu_host.exe"

if (-not (Test-Path $hostExe)) {
    Write-Error "omnigpu_host.exe not found at: $hostExe"
    exit 1
}

switch ($Command) {
    "install" {
        Write-Host "Installing OmniGPU Host service..." -ForegroundColor Green
        & $hostExe --install
        if ($LASTEXITCODE -eq 0) {
            Write-Host "Starting OmniGPU Host service..." -ForegroundColor Green
            Start-Service -Name "OmniGPUHost" -ErrorAction SilentlyContinue
            Write-Host "Done! Service 'OmniGPUHost' installed and started." -ForegroundColor Green
            Write-Host "Use 'Get-Service OmniGPUHost' to check status." -ForegroundColor Cyan
        }
    }
    "uninstall" {
        Write-Host "Stopping and uninstalling OmniGPU Host service..." -ForegroundColor Yellow
        Stop-Service -Name "OmniGPUHost" -ErrorAction SilentlyContinue
        & $hostExe --uninstall
        Write-Host "Service uninstalled." -ForegroundColor Green
    }
    "start" {
        Write-Host "Starting OmniGPU Host service..." -ForegroundColor Green
        Start-Service -Name "OmniGPUHost" -ErrorAction Stop
        Get-Service -Name "OmniGPUHost"
    }
    "stop" {
        Write-Host "Stopping OmniGPU Host service..." -ForegroundColor Yellow
        Stop-Service -Name "OmniGPUHost" -ErrorAction Stop
        Write-Host "Service stopped." -ForegroundColor Green
    }
    "restart" {
        Write-Host "Restarting OmniGPU Host service..." -ForegroundColor Yellow
        Restart-Service -Name "OmniGPUHost" -ErrorAction Stop
        Get-Service -Name "OmniGPUHost"
    }
}
