OmniGPU v0.1.0 - GPU Forwarding over LAN
============================================

Forward Vulkan/OpenGL/OpenCL from a guest VM to a remote host GPU via TCP.

QUICK START (Guest VM - no GPU needed):
  1. Edit omnigpu_guest.json → set "host" to your host machine IP
  2. Right-click install.bat → Run as Administrator
  3. Run mesa3d\systemwidedeploy.cmd, select: 1 → 3 → 9
  4. When prompted, deploy clvk for OpenCL support (optional)
  5. Double-click start-daemon.bat
  6. On host machine: run omnigpu_host.exe

INSTALL:
  Right-click install.bat → Run as Administrator.
  Script copies binaries → Program Files\OmniGPU,
  registers Vulkan/OpenCL ICD,
  deploys FFmpeg codecs to System32 (global).
  clvk and Mesa3D are optional (prompted during install).

UNINSTALL:
  Right-click uninstall.bat → Run as Administrator.
  Cleans registry, removes System32 DLLs (vulkan-1 + FFmpeg),
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
    - Python 3 (for auto-fetching Mesa3D + FFmpeg)

  Output: build\dist\OmniGPU-v0.1.0\ (ready-to-install package)

PACKAGE CONTENTS:
  install.bat              Install OmniGPU + register Vulkan ICD
  start-daemon.bat         Start guest daemon
  uninstall.bat            Remove OmniGPU (incl. System32 DLLs)
  mesa3d/                  Full Mesa3D (OpenGL, Vulkan, OpenCL drivers)
  clvk/                    OpenCL → Vulkan translation layer

  omnigpu_host.exe         HOST - Run on machine WITH physical GPU
  omnigpu_guestd.exe       Guest daemon (runs in background)
  omnigpu_guest.dll        Vulkan ICD (guest)
  omnigpu_vk_test.exe      Vulkan connectivity test
  vk_icd.json              ICD manifest
  omnigpu_guest.json       Configuration template (edit IP first!)
  omnigpu_host.json        Host configuration
  vulkan-1.dll             Vulkan Loader
  avcodec-63.dll           FFmpeg (codecs, globally installed)
  avutil-61.dll            FFmpeg utilities
  swscale-10.dll           FFmpeg scaling
  swresample-7.dll         FFmpeg audio resample

CONFIGURATION (omnigpu_guest.json):
  {
      "host": "192.168.1.100",     <<< CHANGE THIS
      "port": 9443
  }

For full docs: docs/installation-windows.md
