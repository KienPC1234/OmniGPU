OmniGPU v0.1.0 - GPU Forwarding over LAN
============================================

Forward Vulkan compute/OpenCL from a guest VM to a remote host GPU via TCP.

QUICK START (Guest VM - no GPU needed):
  1. Edit omnigpu_guest.json → set "host" to your host machine IP
  2. Right-click install.bat → Run as Administrator
  3. When prompted, deploy clvk for OpenCL support (optional)
  4. Double-click start-daemon.bat
  5. On host machine: run omnigpu_host.exe

INSTALL:
  Right-click install.bat → Run as Administrator.
  Script copies binaries → Program Files\OmniGPU,
  registers Vulkan/OpenCL ICD.
  clvk is optional (prompted during install).

UNINSTALL:
  Right-click uninstall.bat → Run as Administrator.
  Cleans registry and removes obsolete runtime DLLs,
  and deletes Program Files\OmniGPU.

BUILD FROM SOURCE (requires Visual Studio 2022 Build Tools):
  1. Open "Developer Command Prompt for VS 2022"
  2. cd to project root
  3. cmake --preset release
  4. cmake --build --preset release
  5. .\scripts\windows\build-and-package.ps1 -SkipBuild
     (skip -SkipBuild to also compile from scratch)

  Prerequisites:
    - CMake 3.28+
    - Ninja
    - Clang-cl or MSVC
    - vcpkg (set VCPKG_ROOT env or install in C:\Users\<user>\vcpkg)
    - Python 3 (for optional tooling)

  Output: build\dist\OmniGPU-v0.1.0\ (ready-to-install package)

PACKAGE CONTENTS:
  install.bat              Install OmniGPU + register Vulkan ICD
  start-daemon.bat         Start guest daemon
  uninstall.bat            Remove OmniGPU (incl. System32 DLLs)
  clvk/                    OpenCL → Vulkan translation layer

  omnigpu_host.exe         HOST - Run on machine WITH physical GPU
  omnigpu_guestd.exe       Guest daemon (runs in background)
  omnigpu_guest.dll        Vulkan ICD (guest)
  omnigpu_vk_test.exe      Vulkan connectivity test
  vk_icd.json              ICD manifest
  omnigpu_guest.json       Configuration template (edit IP first!)
  omnigpu_host.json        Host configuration
  vulkan-1.dll             Vulkan Loader

CONFIGURATION (omnigpu_guest.json):
  {
      "host": "192.168.1.100",     <<< CHANGE THIS
      "port": 9443
  }

For full docs: docs/installation-windows.md
