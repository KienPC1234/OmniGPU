param(
    [string]$SshHost = "192.168.1.113",
    [string]$SshUser = "test"
)

$ErrorActionPreference = "Stop"
$env:SSH_ASKPASS = "$PSScriptRoot\askpass.bat"
$env:DISPLAY = "dummy"
$env:SSH_ASKPASS_REQUIRE = "force"

Write-Host "=== OmniGPU Global Installer for VM (32-bit & 64-bit) ===" -ForegroundColor Cyan

$buildDir64 = "$PSScriptRoot\..\..\build\release\bin"
$buildDir32 = "$PSScriptRoot\..\..\build\release-x86\bin"

# ---- Step 1: Check build outputs ----
if (!(Test-Path "$buildDir64\omnigpu_guest.dll")) {
    Write-Error "64-bit driver not found at $buildDir64\omnigpu_guest.dll. Please build preset 'release'."
}
if (!(Test-Path "$buildDir32\omnigpu_guest.dll")) {
    Write-Error "32-bit driver not found at $buildDir32\omnigpu_guest.dll. Please build preset 'release-x86'."
}

# ---- Step 2: Deploy to VM System32 (64-bit) and SysWOW64 (32-bit) ----
Write-Host "`n[1/3] Deploying DLLs to VM System32 and SysWOW64..." -ForegroundColor Yellow

$icdJson = @'
{
    "file_format_version": "1.0.0",
    "ICD": {
        "library_path": "omnigpu_guest.dll",
        "api_version": "1.3.296"
    }
}
'@

# Save vk_icd_omnigpu.json locally
Set-Content -Path "$PSScriptRoot\vk_icd_omnigpu.json" -Value $icdJson -Force

Write-Host "  Copying 64-bit driver to System32..."
& scp -o StrictHostKeyChecking=no "$buildDir64\omnigpu_guest.dll" "${SshUser}@${SshHost}:C:\Windows\System32\" 2>&1 | Out-Null
& scp -o StrictHostKeyChecking=no "$PSScriptRoot\vk_icd_omnigpu.json" "${SshUser}@${SshHost}:C:\Windows\System32\" 2>&1 | Out-Null

Write-Host "  Copying 32-bit driver to SysWOW64..."
& scp -o StrictHostKeyChecking=no "$buildDir32\omnigpu_guest.dll" "${SshUser}@${SshHost}:C:\Windows\SysWOW64\" 2>&1 | Out-Null
& scp -o StrictHostKeyChecking=no "$PSScriptRoot\vk_icd_omnigpu.json" "${SshUser}@${SshHost}:C:\Windows\SysWOW64\" 2>&1 | Out-Null

Remove-Item "$PSScriptRoot\vk_icd_omnigpu.json" -Force

# ---- Step 3: Register ICD system-wide in Windows Registry ----
Write-Host "`n[2/3] Registering OmniGPU natively in Windows Registry..." -ForegroundColor Yellow

$registryScript = @'
# Remove old VK_ICD_FILENAMES variable if it exists to avoid conflicts
[System.Environment]::SetEnvironmentVariable('VK_ICD_FILENAMES', $null, 'Machine')
[System.Environment]::SetEnvironmentVariable('VK_ICD_FILENAMES', $null, 'User')

# 64-bit Registration
$path64 = 'HKLM:\SOFTWARE\Khronos\Vulkan\Drivers'
if (!(Test-Path $path64)) { New-Item -Path $path64 -Force }
New-ItemProperty -Path $path64 -Name 'C:\Windows\System32\vk_icd_omnigpu.json' -Value 0 -PropertyType DWord -Force | Out-Null

# 32-bit Registration
$path32 = 'HKLM:\SOFTWARE\WOW6432Node\Khronos\Vulkan\Drivers'
if (!(Test-Path $path32)) { New-Item -Path $path32 -Force }
New-ItemProperty -Path $path32 -Name 'C:\Windows\SysWOW64\vk_icd_omnigpu.json' -Value 0 -PropertyType DWord -Force | Out-Null
'@

# Execute the registry script on the VM
$encodedCommand = [Convert]::ToBase64String([System.Text.Encoding]::Unicode.GetBytes($registryScript))
& ssh -o StrictHostKeyChecking=no "${SshUser}@${SshHost}" "powershell -EncodedCommand $encodedCommand" 2>&1 | Out-Null

Write-Host "`n[3/3] OmniGPU Global Installation Complete!" -ForegroundColor Green
Write-Host "All Vulkan applications (32-bit and 64-bit) on the VM will now automatically use the OmniGPU driver." -ForegroundColor Green
