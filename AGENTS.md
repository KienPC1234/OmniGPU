# OmniGPU — Agent Guide

Seamless GPU forwarding over LAN: forwards OpenGL/Vulkan/OpenCL from a guest VM to a remote host GPU via TCP.

## Build & Test

- **Prerequisites**: `VCPKG_ROOT` env var must be set. Requires Clang 19 (clang-cl on Windows), Ninja, CMake 3.28+.
- **Configure**: `cmake --preset release|debug|linux`
- **Build**: `cmake --build --preset release|debug|linux`
- **Test**: `ctest --preset release|debug|linux --output-on-failure`
- Output goes to `build/{preset}/bin/`.
- vcpkg triplet: `x64-windows-static-md` (Windows), `x64-linux` (Linux).

## Code Generation

**Must run before building** when `gen/vulkan_api.json` changes:

```
pip install -r gen/requirements.txt
python gen/generate.py --manifest gen/vulkan_api.json --output src/guest/vk_intercept_gen.cpp
```

`src/guest/vk_intercept_gen.cpp` is auto-generated and **checked in** — do not edit manually. It contains auto-generated Vulkan hook stubs. Manual hooks (`vkCreateInstance`, `vkDestroyDevice`, etc.) live in `src/guest/vk_intercept.cpp`.

## Architecture

- **Host** (`src/host/`): TCP server that receives serialized Vulkan commands, executes them on physical GPU(s), and returns compressed frames.
- **Guest** (`src/guest/`): A Vulkan Installable Client Driver (ICD) — a **shared library** (`omnigpu_guest.dll/.so`), NOT a service/daemon. Auto-initializes via `DllMain` (Windows) or `__attribute__((constructor))` (Linux) when loaded by the Vulkan Loader into the application process.
- **Launcher** (`src/launcher/`): `omnigpu_launcher.exe` deploys Zink/clvk alongside the app, sets `VK_ICD_FILENAMES`, and launches the target process.
- **Protocol** (`src/schemas/omnigpu_protocol.fbs`): FlatBuffers-based. Generated code in `src/schemas/generated/`.
- **Common** (`src/common/`): Shared FlatBuffers helpers, GPU capability cache, logging (spdlog), TCP network utils.

## CMake Options

| Option | Default | Notes |
|--------|---------|-------|
| `OMNIGPU_BUILD_TESTS` | ON | Google Test suite in `tests/` |
| `OMNIGPU_BUILD_HOST` | ON | Builds `omnigpu_host` |
| `OMNIGPU_BUILD_GUEST` | ON | Builds `omnigpu_guest.dll/.so` |
| `OMNIGPU_FETCH_ZINK` | OFF | Downloads Mesa Zink (`opengl32.dll`) — ON in release/debug presets |
| `OMNIGPU_FETCH_CLVK` | OFF | Downloads clvk (`OpenCL.dll`) — ON in release/debug presets |

## Dependencies (vcpkg)

`vulkan-headers`, `vulkan-loader`, `flatbuffers`, `lz4`, `libjpeg-turbo`, `spdlog`, `fmt`, `gtest`, `nlohmann-json`.

## Style & Linting

- **C++23**, `clang-format` style: LLVM base, 4-space indent, 100 col limit, Attach braces, Left pointer/reference alignment.
- **clang-tidy**: enabled via CMake. Checks: clang-diagnostic, clang-analyzer, modernize, performance, readability, bugprone (with some exclusions). Run: `cmake --build --preset linux -DCMAKE_CXX_CLANG_TIDY=clang-tidy-19`.
- MSVC/Clang-cl must use `/EHsc` for C++ exceptions.

## Testing

- Google Test + Google Mock in `tests/`. Uses `gtest_discover_tests`.
- Tests link `omnigpu_common` and include `src/guest/vulkan_serializer.cpp` directly.
- CTest presets defined for debug, release, linux.

## CI

- **Build** (`.github/workflows/build.yml`): Windows (debug+release) + Linux, packages artifacts.
- **Test** (`.github/workflows/test.yml`): Full test suite + codegen validation + clang-tidy lint.
- **Deploy** (`.github/workflows/deploy.yml`): On release publish + manual dispatch.
- Runners: `windows-2025`, `ubuntu-24.04`.

## Key Commands

```powershell
# Build & test
cmake --preset release
cmake --build --preset release
ctest --preset release --output-on-failure

# Run host
.\build\release\bin\omnigpu_host.exe

# Diagnose system
.\scripts\windows\diagnose.ps1

# Guest connection test (from another machine)
.\scripts\windows\test_guest.ps1 -HostAddr <IP> -PortNum 9443

# Build + deploy + test via SSH
.\scripts\windows\build_deploy_test.ps1 -SshHost <IP> -SshUser user

# Launch an app through OmniGPU
.\omnigpu_launcher.exe your_app.exe
```

## Third-Party Translation Layers

- **Zink** (OpenGL→Vulkan): Fetched via `python third_party/fetch_zink.py`. Auto-deployed by launcher.
- **clvk** (OpenCL→Vulkan): Requires building first: `scripts\windows\build_clvk.bat` (Windows) or `./scripts/linux/build_clvk.sh` (Linux).

## Vulkan Validation Layers (VVL)

Locally built from `third_party/Vulkan-ValidationLayers/`. Used to diagnose invalid API calls, memory leaks, and synchronization issues on the Host.

### Build VVL
```powershell
# Build Release VVL
cmake -S third_party/Vulkan-ValidationLayers -B third_party/Vulkan-ValidationLayers/build -G "Visual Studio 18 2026" -A x64 -D CMAKE_BUILD_TYPE=Release -D UPDATE_DEPS=ON -D BUILD_TESTS=OFF
cmake --build third_party/Vulkan-ValidationLayers/build --config Release --parallel
cmake --install third_party/Vulkan-ValidationLayers/build --config Release --prefix third_party/Vulkan-ValidationLayers/build/install

# Build Debug VVL
cmake -S third_party/Vulkan-ValidationLayers -B third_party/Vulkan-ValidationLayers/build-debug -G "Visual Studio 18 2026" -A x64 -D CMAKE_BUILD_TYPE=Debug -D UPDATE_DEPS=ON -D BUILD_TESTS=OFF
cmake --build third_party/Vulkan-ValidationLayers/build-debug --config Debug --parallel
cmake --install third_party/Vulkan-ValidationLayers/build-debug --config Debug --prefix third_party/Vulkan-ValidationLayers/build-debug/install
```

### Run Host with VVL Enabled
Activate VVL by setting the following environment variables before starting `omnigpu_host.exe`:
```powershell
# Point to either release/install/bin or build-debug/install/bin
$env:VK_LAYER_PATH = "C:\Users\kien\Documents\repos\OmniGPU\third_party\Vulkan-ValidationLayers\build-debug\install\bin"
$env:VK_INSTANCE_LAYERS = "VK_LAYER_KHRONOS_validation"

# Run Host
.\build\debug\bin\omnigpu_host.exe
```
