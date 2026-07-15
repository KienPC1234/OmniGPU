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

## Method 1: Quick Install (Pre-built Binaries)

For a fresh VM with no build tools, copy pre-built binaries from a dev machine
or download a release.

### Step 1: Build or Download

**Build from source** (on a dev machine with Clang 19 + CMake 3.28+):

```powershell
# 64-bit guest
cmake --preset release
cmake --build --preset release

# Also build 32-bit for 32-bit app support
cmake --preset release-x86
cmake --build --preset release-x86

# Build host
# (host is included in the 'release' preset)
```

Binaries will be in:
- `build/release/bin/` (64-bit)
- `build/release-x86/bin/` (32-bit)

### Step 2: Copy to VM

Copy these to your VM (via SCP, USB, network share):

```
build/release/bin/
├── omnigpu_guest.dll          # 64-bit ICD driver
├── omnigpu_host.exe           # Host server (run on host machine)
├── omnigpu_launcher.exe       # App launcher
├── vk_icd.json                # ICD manifest
├── opengl32.dll               # Zink (OpenGL→Vulkan), optional
└── OpenCL.dll                 # clvk (OpenCL→Vulkan), optional

build/release-x86/bin/
└── omnigpu_guest.dll          # 32-bit ICD driver (for 32-bit apps)
```

### Step 3: Install (Admin)

Run the install script **as Administrator**:

```powershell
# With both 64-bit and 32-bit builds
.\scripts\windows\install_guest.ps1 `
    -BuildDir64 build\release `
    -BuildDir32 build\release-x86 `
    -InstallDir "C:\Program Files\OmniGPU"

# Or with auto-detection (64-bit only)
.\scripts\windows\install_guest.ps1
```

This will:
1. Copy DLLs + manifest to `C:\Program Files\OmniGPU\`
2. Register ICD in `HKLM` (system-wide) and `HKCU` (per-user)
3. Register `VulkanDriverName` in GPU controller key (VkDiag compatibility)
4. Register 32-bit paths in `WOW6432Node`

### Step 4: Set Host Address

Set the host machine's IP so the guest knows where to connect:

```powershell
# Persistent machine-wide (admin)
[System.Environment]::SetEnvironmentVariable(
    'OMNIGPU_HOST', '192.168.1.100', 'Machine')

# Or per-user
[System.Environment]::SetEnvironmentVariable(
    'OMNIGPU_HOST', '192.168.1.100', 'User')
```

Alternatively, create `C:\Program Files\OmniGPU\omnigpu_guest.json`:

```json
{
    "host": "192.168.1.100",
    "port": 9443,
    "zink_enabled": true,
    "clvk_enabled": true
}
```

### Step 5: Start Host Server

On the **host machine** (with physical GPU), start the server:

```powershell
.\omnigpu_host.exe
```

Default port: 9443. To change:

```powershell
.\omnigpu_host.exe --port 9443
```

### Step 6: Verify

Any Vulkan application will now use OmniGPU automatically. To verify:

```powershell
# Launch via launcher (recommended for first use)
.\omnigpu_launcher.exe your-game.exe

# Or run VkDiag to check driver detection
.\VkDiag.exe
```

Or just launch any Vulkan app directly — it will auto-discover the driver
via Windows Registry.

## Method 2: One-Line Quick Install (Fresh VM)

Copy the guest binaries to the VM, then run this **as Administrator**:

```powershell
# Quick install from build output
.\scripts\windows\quick-install.ps1 -BuildDir ".\build\release" -HostAddr "192.168.1.100"

# Or from a pre-packaged folder
.\scripts\windows\quick-install.ps1 -PackageDir ".\omnigpu-package" -HostAddr "192.168.1.100"
```

This single command:
1. Installs DLLs + manifest to Program Files
2. Registers ICD in HKLM + HKCU for both 32/64-bit
3. Registers VulkanDriverName for VkDiag
4. **Deploys Zink + Gallium DLLs to System32/SysWOW64** (global OpenGL)
5. **Deploys clvk (OpenCL.dll + clspv.dll + clvk.dll) to System32/SysWOW64** (global OpenCL)
6. Sets OMNIGPU_HOST environment variable
7. Everything is ready — just launch any Vulkan, OpenGL, or OpenCL app

## How Global Discovery Works

After installation, the Vulkan Loader finds the OmniGPU ICD through:

| Registry Path | Bitness | Type | Requires Admin |
|--------------|---------|------|----------------|
| `HKLM\SOFTWARE\Khronos\Vulkan\Drivers` | 64-bit | System-wide | Yes |
| `HKLM\SOFTWARE\WOW6432Node\Khronos\Vulkan\Drivers` | 32-bit | System-wide | Yes |
| `HKCU\SOFTWARE\Khronos\Vulkan\Drivers` | Both | Per-user | No |
| `HKCU\SOFTWARE\WOW6432Node\Khronos\Vulkan\Drivers` | 32-bit | Per-user | No |
| `HKLM\...\Video\{GUID}\0000\VulkanDriverName` | 64-bit | VkDiag compat | Yes |
| `HKLM\...\Video\{GUID}\0000\VulkanDriverNameWoW` | 32-bit | VkDiag compat | Yes |

The guest DLL also auto-registers in HKCU when loaded (via DllMain), so even
without admin, using the launcher once makes the driver globally discoverable.

## Global OpenCL via clvk

Starting from OmniGPU 0.1.0, the install script deploys **clvk** (OpenCL → Vulkan)
to `C:\Windows\System32` (64-bit) and `C:\Windows\SysWOW64` (32-bit).

### Required DLLs

| DLL | Source | Purpose |
|-----|--------|---------|
| `OpenCL.dll` | clvk | OpenCL API implementation |
| `clspv.dll` | clvk | OpenCL C → SPIR-V compiler |
| `clvk.dll` | clvk | clvk utility library |

These are deployed to both `System32` (64-bit) and `SysWOW64` (32-bit),
ensuring all OpenCL applications — both 32-bit and 64-bit — are intercepted
globally without needing the launcher.

### How it works

```
OpenCL App → OpenCL.dll (clvk) → Vulkan Loader → OmniGPU ICD → Host GPU
```

### Uninstalling clvk

The uninstall script removes `OpenCL.dll`, `clspv.dll`, and `clvk.dll` from
both `System32` and `SysWOW64` directories. No backup is needed since clvk
is not a system component.

## Global OpenGL via Zink (Gallium)

Starting from OmniGPU 0.1.0, the install script deploys the **Mesa Gallium**
driver stack to `C:\Windows\System32` (64-bit) and `C:\Windows\SysWOW64` (32-bit).

This replaces Microsoft's `opengl32.dll` with Zink, which translates OpenGL
calls to Vulkan. The original `opengl32.dll` is backed up as `opengl32.dll.orig`.

### Required DLLs

| DLL | Source | Purpose |
|-----|--------|---------|
| `opengl32.dll` | Mesa Zink | OpenGL → Gallium state tracker |
| `libgallium_wgl.dll` | Mesa | Gallium driver core (Zink + softpipe + WGL) |

These are deployed to both `System32` (64-bit) and `SysWOW64` (32-bit),
ensuring all OpenGL applications — both 32-bit and 64-bit — are intercepted
globally without needing the launcher.

### How it works

```
OpenGL App → opengl32.dll (Zink) → Vulkan Loader → OmniGPU ICD → Host GPU
```

Since the OmniGPU ICD is already registered globally (see above), the chain
is complete: Zink calls Vulkan → Vulkan Loader finds OmniGPU via registry →
Vulkan commands are forwarded to the host GPU.

### Uninstalling Zink

The uninstall script restores the original `opengl32.dll` from backup
and removes `libgallium_wgl.dll` from system directories.

## Launcher Usage

The launcher deploys translation layers (Zink, clvk) and launches apps:

```powershell
# Basic usage
.\omnigpu_launcher.exe "C:\Path\to\game.exe"

# With arguments
.\omnigpu_launcher.exe "C:\Path\to\game.exe" --arg1 --arg2
```

After the first launcher run, the ICD is registered and future Vulkan apps
work without the launcher.

## Uninstallation

```powershell
# Using the install script (recommended — also restores opengl32.dll)
.\scripts\windows\install_guest.ps1 -Uninstall

# Or quick-uninstall
.\scripts\windows\quick-install.ps1 -Uninstall

# Or manually:
# 1. Remove registry entries
Remove-ItemProperty -Path "HKLM:\SOFTWARE\Khronos\Vulkan\Drivers" -Name "C:\Program Files\OmniGPU\vk_icd.json"
Remove-ItemProperty -Path "HKLM:\SOFTWARE\WOW6432Node\Khronos\Vulkan\Drivers" -Name "C:\Program Files\OmniGPU\x86\vk_icd.json"
Remove-ItemProperty -Path "HKCU:\SOFTWARE\Khronos\Vulkan\Drivers" -Name "C:\Program Files\OmniGPU\vk_icd.json"

# 2. Restore original OpenGL driver (64-bit)
Rename-Item -Path "$env:SystemRoot\System32\opengl32.dll.orig" -NewName "opengl32.dll"
Remove-Item -Path "$env:SystemRoot\System32\libgallium_wgl.dll" -Force
# 2b. Restore original OpenGL driver (32-bit)
Rename-Item -Path "$env:SystemRoot\SysWOW64\opengl32.dll.orig" -NewName "opengl32.dll"
Remove-Item -Path "$env:SystemRoot\SysWOW64\libgallium_wgl.dll" -Force

# 3. Remove clvk (OpenCL) DLLs (64-bit + 32-bit)
Remove-Item -Path "$env:SystemRoot\System32\OpenCL.dll" -Force
Remove-Item -Path "$env:SystemRoot\System32\clspv.dll" -Force
Remove-Item -Path "$env:SystemRoot\System32\clvk.dll" -Force
Remove-Item -Path "$env:SystemRoot\SysWOW64\OpenCL.dll" -Force
Remove-Item -Path "$env:SystemRoot\SysWOW64\clspv.dll" -Force
Remove-Item -Path "$env:SystemRoot\SysWOW64\clvk.dll" -Force

# 4. Remove install directory
Remove-Item -Recurse -Force "C:\Program Files\OmniGPU"
```

## Building from Source

### Prerequisites
- Windows 10+ with Visual Studio 2022 (or Clang 19)
- CMake 3.28+
- Ninja
- vcpkg (set `VCPKG_ROOT` environment variable)

### Build

```powershell
# Configure
cmake --preset release

# Build 64-bit
cmake --build --preset release

# (Optional) Build 32-bit for legacy app support
cmake --preset release-x86
cmake --build --preset release-x86

# Run tests
ctest --preset release --output-on-failure
```

## Troubleshooting

### VkDiag shows "No GPUs registered with Vulkan support"
This is expected if the VM has no physical GPU. Run the install script with
admin to register VulkanDriverName, or just run any Vulkan app — it will
still work.

### "Vulkan Loader not found"
Windows 10 1809+ includes the Vulkan Runtime. On older systems, install the
[Vulkan Runtime](https://www.lunarg.com/vulkan-runtime-downloads/).

### Guest fails to connect to host
- Verify host is running: `.\omnigpu_host.exe`
- Check firewall: host port 9443 must be reachable from VM
- Verify OMNIGPU_HOST is set correctly
- Test connectivity: `Test-NetConnection 192.168.1.100 -Port 9443`
