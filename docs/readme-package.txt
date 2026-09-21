OmniGPU v0.1.0 - GPU Compute Forwarding over LAN
================================================

Forward Vulkan compute/AI workloads from a guest VM to a remote host GPU
via TCP. Compute-only: rendering/presentation is not supported.

QUICK START (Guest VM - no GPU needed):
  1. Edit omnigpu_guest.json -> set "host" to your host machine IP
  2. sudo ./scripts/linux/install_guest.sh
  4. On host machine: run omnigpu_host

INSTALL:
  sudo ./scripts/linux/install_guest.sh
  Copies libomnigpu_guest.so -> /usr/lib/omnigpu, registers the Vulkan ICD in
  /usr/share/vulkan/icd.d/, and configures ldconfig.

UNINSTALL:
  sudo ./scripts/linux/install_guest.sh --uninstall

BUILD FROM SOURCE (Linux, Clang 19+):
  1. cmake --preset linux
  2. cmake --build --preset linux
  3. ./scripts/linux/build-and-package.sh --skip-build

  Prerequisites:
    - CMake 3.28+
    - Ninja
    - Clang 19+
    - vcpkg (set VCPKG_ROOT or let CMake auto-detect ./vcpkg)
    - Python 3 (for code generation)

  Output: build/dist/OmniGPU-v0.1.0-linux.tar.gz (ready-to-install package)

PACKAGE CONTENTS:
  bin/omnigpu_host             HOST - run on machine WITH physical GPU
  bin/omnigpu_guest_test       Standalone guest test
  bin/omnigpu_vk_test          Vulkan compute connectivity test
  lib/libomnigpu_guest.so      Vulkan ICD (guest)
  bin/vk_icd.json              ICD manifest
  omnigpu_guest.json           Guest configuration template (edit IP first!)
  omnigpu_host.json            Host configuration
  scripts/install_guest.sh     Guest installer
  scripts/diagnose.sh          Linux diagnosis tool

CONFIGURATION (omnigpu_guest.json):
  {
      "host": "192.168.1.100",     <<< CHANGE THIS
      "port": 9443
  }
