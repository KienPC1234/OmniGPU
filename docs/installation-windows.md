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

### Step 2: Install Mesa3D (OpenGL support)
Mở **mesa3d\systemwidedeploy.cmd** (Run as Administrator), chọn:

1. **Core desktop OpenGL drivers** (cung cấp Zink → OpenGL → Vulkan → OmniGPU → Host GPU)
3. **Install DirectX IL for redistribution only** (dependency)
9. **Exit**

Không cần chọn lvp (Vulkan CPU driver) vì VM dùng GPU từ xa qua OmniGPU.

### Step 3: Register OpenCL (optional)
Chuột phải **install_clvk.bat** → **Run as Administrator**

Đăng ký clvk với OpenCL ICD Loader. Mọi ứng dụng OpenCL sẽ tự động dùng OpenCL→Vulkan→OmniGPU→Host GPU.

### Step 4: Start Daemon
Nhấn đúp **start-daemon.bat**

Daemon chạy nền, kết nối tới host machine.

### Step 5: Start Host Server
Trên máy chủ (có GPU thật), chạy:

```powershell
.\omnigpu_host.exe
```

### Step 6: Set Host Address
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

### Step 7: Verify

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

## OpenGL via Mesa3D + Zink

Mesa3D's `systemwidedeploy.cmd` deploys `mesadrv.dll` (Zink) to System32.
Zink translates OpenGL → Vulkan → OmniGPU ICD → Host GPU.

## Troubleshooting

### Guest fails to connect to host
- Verify host is running: `.\omnigpu_host.exe`
- Check firewall: host port 9443 must be reachable from VM
- Verify OMNIGPU_HOST is set correctly
