# OmniGPU Windows Installation Guide

## Overview

OmniGPU forwards Vulkan/OpenGL/OpenCL from a guest VM to a remote host GPU over TCP.
Once installed, any Vulkan application on the VM automatically discovers the OmniGPU
driver without environment variables or launcher scripts.

## Architecture

```
VM (guest)                          Host machine (physical)
┌─────────────────┐                 ┌──────────────────────┐
│ Vulkan App      │                 │  Physical GPU        │
│  ↓              │     TCP         │  (NVIDIA/AMD/Intel)  │
│ OmniGPU ICD     │ ──────────────→ │  omniGPU Host        │
│ (omnigpu_guest.dll)│              │  (omnigpu_host.exe)  │
└─────────────────┘                 └──────────────────────┘
```

## Prerequisites

### VM (Guest)
- Windows 10 1809+ (64-bit)
- Vulkan Runtime (included in Windows 10 1809+)
- No physical GPU required

### Host Machine
- Windows 10/11 or Linux
- Physical GPU with Vulkan support (NVIDIA, AMD, Intel)
- Network reachable from the VM

## Quick Install (Pre-built Package)

Copy the `OmniGPU-v0.1.0` folder to your VM, then:

### Step 1: Install OmniGPU
Chuột phải **install.bat** → **Run as Administrator**

This copies OmniGPU binaries to `C:\Program Files\OmniGPU` and registers the Vulkan ICD in HKLM.

### Step 2: Register OpenCL (optional)
Chuột phải **install_clvk.bat** → **Run as Administrator**

Đăng ký clvk với OpenCL ICD Loader. Mọi ứng dụng OpenCL sẽ tự động dùng OpenCL→Vulkan→OmniGPU→Host GPU.

### Step 3: Start Daemon
Nhấn đúp **start-daemon.bat**

Daemon chạy nền, kết nối tới host machine.

### Step 4: Start Host Server
Trên máy chủ (có GPU thật), chạy:

```powershell
.\omnigpu_host.exe
```

### Step 5: Set Host Address
```cmd
set OMNIGPU_HOST=192.168.1.100
set OMNIGPU_PORT=9443
```

Hoặc tạo `C:\Program Files\OmniGPU\omnigpu_guest.json`:
```json
{
    "host": "192.168.1.100",
    "port": 9443
}
```

### Step 6: Verify

Any Vulkan application now uses OmniGPU automatically:

```powershell
.\omnigpu_vk_test.exe
```

## How Global Discovery Works

After installation, the Vulkan Loader finds the OmniGPU ICD through:

| Registry Path | Bitness | Type | Requires Admin |
|--------------|---------|------|----------------|
| `HKLM\SOFTWARE\Khronos\Vulkan\Drivers` | 64-bit | System-wide | Yes |
| `HKCU\SOFTWARE\Khronos\Vulkan\Drivers` | Both | Per-user | No |

## OpenCL via clvk

`install_clvk.bat` registers clvk with the OpenCL ICD Loader via:

- ICD file: `%windir%\System32\clvk.icd` (contains path to OpenCL.dll)
- Registry: `HKLM\SOFTWARE\Khronos\OpenCL\Vendors`

## Troubleshooting

### Guest fails to connect to host
- Verify host is running: `.\omnigpu_host.exe`
- Check firewall: host port 9443 must be reachable from VM
- Verify OMNIGPU_HOST is set correctly
