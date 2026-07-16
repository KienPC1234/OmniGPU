OmniGPU v0.1.0 - GPU Forwarding over LAN
============================================

Forward Vulkan/OpenGL/OpenCL from a guest VM to a remote host GPU via TCP.

QUICK START (Guest VM - no GPU needed):
  1. Edit omnigpu_guest.json → set "host" to your host machine IP
  2. Right-click install.bat → Run as Administrator
  3. Run mesa3d\systemwidedeploy.cmd, select: 1 → 3 → 9
  4. Right-click install_clvk.bat → Run as Administrator (optional, OpenCL)
  5. Double-click start-daemon.bat
  6. On host machine: run omnigpu_host.exe

PACKAGE CONTENTS:
  install.bat              Install OmniGPU + register Vulkan ICD
  install_clvk.bat         Register OpenCL (clvk) ICD
  start-daemon.bat         Start guest daemon
  uninstall.bat            Remove OmniGPU
  mesa3d/                  Full Mesa3D (OpenGL, Vulkan, OpenCL drivers)

  omnigpu_host.exe         HOST - Run on machine WITH physical GPU
  omnigpu_guest.dll        Vulkan ICD (guest)
  omnigpu_guestd.exe       Guest daemon
  omnigpu_vk_test.exe      Vulkan connectivity test
  vk_icd.json              ICD manifest
  omnigpu_guest.json       Configuration template (edit IP first!)
  OpenCL.dll               clvk (OpenCL → Vulkan)
  vulkan-1.dll             Vulkan Loader

CONFIGURATION (omnigpu_guest.json):
  {
      "host": "192.168.1.100",     <<< CHANGE THIS
      "port": 9443
  }

For full docs: docs/installation-windows.md
