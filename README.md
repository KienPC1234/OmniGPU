# OmniGPU

**Seamless GPU forwarding over LAN** — Forward OpenGL, Vulkan, and OpenCL commands from a guest VM or thin client to a remote host GPU over a TCP network.

## Architecture

```
┌──────────────────────────┐         TCP LAN              ┌───────────────────────────┐
│        GUEST (VM)        │  ◄───────────────────────►   │        HOST (Server)      │
│                          │  FlatBuffers · LZ4/TurboJPEG │                           │
│  Application (game/ML)   │         or Video (FFmpeg)   │  FFmpeg (NVENC/AMF/VA-API)│
│    ↓ (Vulkan ICD)        │                              │  GPU Manager              │
│  ┌─────────────────────┐ │                              │  Multi-GPU Split Renderer │
│  │ OmniGPU Guest DLL   │─┤                              │  Resource Cache           │
│  │ (Vulkan ICD)        │ │  ┌───────────────────────┐   └───────────────────────────┘
│  └───────┬─────────────┘ │  │ OmniGPU Guest Daemon  │
│          │ Named Pipe     │  │ (omnigpu_guestd.exe) │
│          ▼                │  │                       │
│  ┌───────────────────┐   │  │ Load config (JSON/env)│
│  │ DLL(nhẹ):         │   │  │ Bridge Pipe ↔ TCP     │
│  │ - intercept VK    │───┤  │ Serve config to DLL   │
│  │ - serialize       │   │  └───────────────────────┘
│  │ - decode frame    │   │
│  │ - gửi/nhận qua IPC│   │
│  └───────────────────┘   │
└──────────────────────────┘
```

The **Guest DLL** intercepts Vulkan API calls, serializes them with FlatBuffers, and sends them via Named Pipe to the **Guest Daemon**. The daemon loads config from JSON/env, bridges the data to the **Host** over TCP, which executes commands on its physical GPU(s) and returns compressed video frames — encoded via FFmpeg (NVENC/AMF/VA-API) or fallback JPEG. The DLL decodes received frames locally.

## Features

- **Multi-GPU Split Rendering** — Distribute rendering across multiple host GPUs
- **Video Encoding** — FFmpeg-powered encoding with hardware acceleration (NVENC / AMF / VA-API). Falls back to JPEG + LZ4.
- **Zink (OpenGL→Vulkan)** — Mesa's Zink translation layer runs inside the guest
- **clvk (OpenCL→Vulkan)** — OpenCL-on-Vulkan translation for compute workloads
- **Adaptive Batching** — Commands batched dynamically based on byte size and count
- **GPU Capability Caching** — GPU properties cached locally with configurable TTL
- **Windows Service Mode** — Both host and daemon can run as Windows services
- **Cross-Platform** — Windows and Linux support

## Requirements

### Build

| Component | Requirement |
|-----------|-------------|
| OS | Windows 10+ or Linux |
| CMake | 3.28+ |
| Ninja | Build system |
| Compiler | Windows: Visual Studio 2022 + Clang-cl / Linux: Clang 19 |
| vcpkg | With `VCPKG_ROOT` environment variable set |
| Python 3 | For auto-downloading Mesa3D + FFmpeg |

### Host

| Component | Requirement |
|-----------|-------------|
| GPU | Any GPU with Vulkan support (NVENC/AMF/VA-API auto-detected) |
| Driver | Latest GPU driver with Vulkan support |

### Guest

| Component | Requirement |
|-----------|-------------|
| OS | Windows or Linux VM |
| Vulkan Loader | Present in the guest OS |
| Network | TCP/IP connectivity to host on port 9443 (default) |

## Build

### Windows (Release)

One command to build and package:

```powershell
.\scripts\windows\build-and-package.ps1
```

This produces `build/dist/OmniGPU-v0.1.0/` — a ready-to-install package.

### Step-by-step (manual)

```powershell
# 1. Configure
cmake --preset release

# 2. Build
cmake --build --preset release

# 3. Package (skip rebuild)
.\scripts\windows\build-and-package.ps1 -SkipBuild
```

### Linux

```bash
cmake --preset linux
cmake --build --preset linux
```

### Package contents

```
build/dist/OmniGPU-v0.1.0/
├── install.bat              Installer (run as admin)
├── uninstall.bat            Uninstaller
├── start-daemon.bat         Start guest daemon
├── README.txt               Package README
├── omnigpu_host.exe         Host server (run on machine WITH GPU)
├── omnigpu_guestd.exe       Guest daemon
├── omnigpu_guest.dll        Vulkan ICD (x64)
├── omnigpu_vk_test.exe      Connectivity test
├── vk_icd.json              ICD manifest
├── vulkan-1.dll             Vulkan Loader
├── avcodec-63.dll           FFmpeg codecs (→ System32 by installer)
├── avutil-61.dll
├── swscale-10.dll
├── swresample-7.dll
├── omnigpu_guest.json       Guest config (edit host IP)
├── omnigpu_host.json        Host config
├── x64/                     x64 Vulkan ICD driver
│   ├── omnigpu_guest.dll
│   └── vk_icd.json
├── x86/                     x86 Vulkan ICD driver (32-bit)
│   ├── omnigpu_guest.dll
│   └── vk_icd.json
├── mesa3d/                  Mesa Zink (OpenGL→Vulkan)
├── clvk/                    OpenCL→Vulkan translation
└── docs/                    Documentation
```

### CMake Options

| Option | Default | Description |
|--------|---------|-------------|
| `OMNIGPU_BUILD_TESTS` | `ON` | Build test suite |
| `OMNIGPU_BUILD_HOST` | `ON` | Build host server |
| `OMNIGPU_BUILD_GUEST` | `ON` | Build guest client |
| `OMNIGPU_FETCH_MESA3D` | `ON` | Download Mesa3D package for the guest |
| `OMNIGPU_BUILD_FFMPEG` | `ON` | Download FFmpeg pre-built with HW acceleration |

## Quick Start

### 1. Start the Host Server

```powershell
.\build\release\bin\omnigpu_host.exe
```

Optional: specify a config file:

```powershell
.\build\release\bin\omnigpu_host.exe omnigpu_host.json
```

Run as a Windows service:

```powershell
.\build\release\bin\omnigpu_host.exe --install
.\build\release\bin\omnigpu_host.exe --service
```

### 2. Verify the Host

```powershell
.\scripts\windows\diagnose.ps1
```

### 3. Install on Guest VM

Copy the entire `OmniGPU-v0.1.0/` package to the VM, then:

```powershell
# Right-click install.bat → Run as Administrator
# This will:
#   - Copy binaries to %ProgramFiles%\OmniGPU
#   - Register Vulkan ICD
#   - Deploy vulkan-1.dll + FFmpeg codecs to System32 (global)
#   - Prompt to deploy clvk (OpenCL) and Mesa3D (OpenGL)
```

After install, start the daemon:

```powershell
start-daemon.bat
```

### 4. Run an Application on the Guest

No special setup needed — the Vulkan ICD is registered system-wide.
Just launch your application.

### 5. Automated Build + Deploy + Test

```powershell
.\scripts\windows\build_deploy_test.ps1 -SshHost 192.168.1.100 -SshUser user
```

### 6. Test Guest Connection

```powershell
.\scripts\windows\test_guest.ps1 -HostAddr 192.168.1.239 -PortNum 9443
```

## Tools

| Tool | Description |
|------|-------------|
| `tools/windows/OmniGPU-Manager.ps1` | Windows GUI manager — start/stop host, monitor sessions, view GPU info, deploy to guest |
| `scripts/windows/diagnose.ps1` | System diagnostics — checks host, GPU, Vulkan SDK, network, firewall, and build artifacts |
| `scripts/windows/build_deploy_test.ps1` | Build + deploy + test automation — builds, copies to remote VM via SSH, runs connection test |
| `scripts/windows/test_guest.ps1` | Guest connection test — runs `omnigpu_guest_test.exe` remotely and checks handshake log |
| `scripts/windows/install_guest.ps1` | Installs guest files to a remote VM over SSH |
| `scripts/windows/install-host-service.ps1` | Installs the host as a Windows service |
| `scripts/windows/build_clvk.bat` | Builds clvk from source |
| `scripts/linux/diagnose.sh` | Linux diagnostics |
| `scripts/linux/setup_guest.sh` | Linux guest setup |
| `scripts/linux/install_guest.sh` | Linux guest installation |

## Guest Architecture

The guest consists of two components:

### 1. Guest Daemon (`omnigpu_guestd.exe`)

A lightweight background process that:
- Loads config from `omnigpu_guest.json` (or env vars `OMNIGPU_HOST`/`OMNIGPU_PORT`)
- Opens a Named Pipe server (`\\.\pipe\OmniGPU_Guest`)
- For each connecting DLL, sends config JSON then bridges Pipe ↔ TCP to the Host
- Can run as a Windows service (`--install` / `--service`)

### 2. Guest DLL (`omnigpu_guest.dll`)

A **Vulkan Installable Client Driver (ICD)** — loaded automatically by the Vulkan Loader into each application process:
- Connects to the daemon via Named Pipe
- Receives config from daemon (no file I/O needed)
- Intercepts Vulkan API calls, serializes them with FlatBuffers
- Sends batched commands through the pipe → daemon → host
- Receives compressed video frames and decodes them locally (FFmpeg / MF / software)

### Deploy Config

```json
{
    "host": "192.168.1.100",
    "port": 9443,
    "cache_ttl_seconds": 86400,
    "adaptive_batching": true,
    "max_batch_interval_ms": 16,
    "min_batch_commands": 4,
    "max_batch_commands": 256,
    "min_batch_bytes": 1024,
    "max_batch_bytes": 524288
}
```

## Configuration

### Host (`omnigpu_host.json`)

Place next to `omnigpu_host.exe` or pass as CLI argument (e.g. `omnigpu_host.exe my_config.json`).

```json
{
    "port": 9443,
    "jpeg_quality": 85,
    "multi_gpu_enabled": true,
    "max_fps": 60,
    "render_width": 800,
    "render_height": 600,
    "video_codec": "h264",
    "video_bitrate_kbps": 4000,
    "video_fps": 60,
    "video_width": 800,
    "video_height": 600,
    "encoder": {
        "preset": "fast",
        "tuning": "low_latency",
        "gop_length": 0
    }
}
```

#### General Settings

| Key | Default | Description |
|-----|---------|-------------|
| `port` | `9443` | TCP listen port |
| `jpeg_quality` | `85` | JPEG compression quality (1–100). Used as fallback when no hardware encoder is available. |
| `multi_gpu_enabled` | `true` | Enable multi-GPU split rendering. Distributes frame rendering across multiple host GPUs. |
| `max_fps` | `60` | Maximum frames per second cap |
| `render_width` | `800` | Default render width (pixels) |
| `render_height` | `600` | Default render height (pixels) |

#### Video Encoder Settings

| Key | Default | Description |
|-----|---------|-------------|
| `video_codec` | `"h264"` | Codec for video encoding via FFmpeg. Supported: `"h264"`, `"hevc"`. |
| `video_bitrate_kbps` | `4000` | Target bitrate in kbps (e.g. `8000` = 8 Mbps). Higher = better quality, more bandwidth. |
| `video_fps` | `60` | Target frames per second for encoding |
| `video_width` | `800` | Video encoding width in pixels (should match `render_width`) |
| `video_height` | `600` | Video encoding height in pixels (should match `render_height`) |
| `encoder.preset` | `"fast"` | FFmpeg encoder preset. Common values: `"fast"`, `"medium"`, `"slow"`, `"p1"`-`"p7"` (NVENC). |
| `encoder.tuning` | `"low_latency"` | FFmpeg encoder tuning. Common values: `"low_latency"`, `"high_quality"`. |
| `encoder.gop_length` | `0` | Keyframe interval (GOP length). `0` = auto, `30` = ~0.5s at 60fps. |

### Guest (`omnigpu_guest.json`)

The daemon loads this file on startup. DLL requests config from daemon over IPC (no file I/O in DLL).

```json
{
    "host": "127.0.0.1",
    "port": 9443,
    "cache_ttl_seconds": 86400,
    "adaptive_batching": true,
    "max_batch_interval_ms": 16
}
```

| Key | Default | Description |
|-----|---------|-------------|
| `host` | `"127.0.0.1"` | Host server IP address or hostname |
| `port` | `9443` | Host server TCP port |
| `cache_ttl_seconds` | `86400` | GPU capability cache TTL in seconds (default 24 hours). Set to `0` to disable caching. |
| `adaptive_batching` | `true` | Dynamically batch Vulkan commands before sending to the host. Reduces network round-trips. |
| `max_batch_interval_ms` | `16` | Maximum delay in milliseconds before flushing a pending batch (~1 frame at 60 FPS). Lower = less latency, higher = better throughput. |
| `min_batch_commands` | `4` | Minimum number of queued commands before a batch is flushed |
| `max_batch_commands` | `256` | Maximum commands per batch (hard cap) |
| `min_batch_bytes` | `1024` | Minimum serialized bytes before a batch is flushed |
| `max_batch_bytes` | `524288` | Maximum serialized bytes per batch (512 KB) |

> **Tip**: For latency-sensitive applications (e.g. interactive 3D), keep `max_batch_interval_ms` at 8–16ms and ensure the host is on a wired LAN. For throughput-sensitive workloads (e.g. rendering farms), increase `max_batch_interval_ms` and `max_batch_bytes`.

## Project Structure

```
OmniGPU/
├── CMakeLists.txt              # Root CMake configuration
├── CMakePresets.json            # Build presets (release, debug, linux)
├── vcpkg.json                   # vcpkg dependencies
├── omnigpu_guest.json           # Default guest config
├── src/
│   ├── common/                  # Shared code
│   │   ├── flatbuffers_utils    # FlatBuffers serialization helpers
│   │   ├── gpu_caps.h           # GPU capability struct
│   │   ├── gpu_caps_store       # Capability caching
│   │   ├── logger               # Logging setup
│   │   └── network_utils        # TCP socket helpers
│   ├── host/                    # Host server
│   │   ├── main.cpp             # Entry point (CLI / service)
│   │   ├── server               # TCP server, session management
│   │   ├── config               # Host config loader
│   │   ├── session              # Per-client session
│   │   ├── gpu_manager          # GPU enumeration & selection
│   │   ├── renderer             # Vulkan renderer
│   │   ├── multi_gpu_renderer   # Multi-GPU split rendering
│   │   ├── resource_cache       # GPU resource caching
│   │   ├── compressor           # LZ4 compression
│   │   ├── adaptive_compressor  # Adaptive compression selection
│   │   ├── video_encoder        # Abstract video encoder interface
│   │   ├── handshake            # Guest connection handshake
│   │   ├── cli                  # Interactive CLI
│   │   └── service_manager      # Windows service management
│   ├── guest/                   # Guest client (DLL + Daemon)
│   │   ├── dll_main.cpp         # DLL entry point (auto-init in app process)
│   │   ├── guest_daemon.cpp     # Guest daemon (background IPC bridge)
│   │   ├── guest_ipc.cpp        # Named Pipe / Unix Socket IPC protocol
│   │   ├── guest_config.cpp     # Config loader (daemon only; DLL gets config via IPC)
│   │   ├── guest_init.cpp       # Initialization / shutdown
│   │   ├── vk_icd.json          # Vulkan ICD manifest
│   │   ├── icd_entrypoints      # Vulkan ICD entrypoints
│   │   ├── vk_intercept         # Vulkan API interceptor
│   │   ├── vk_intercept_gen     # Auto-generated intercept stubs
│   │   ├── vulkan_serializer    # Serialize Vulkan calls to FlatBuffers
│   │   ├── client               # TCP client (or daemon pipe proxy)
│   │   ├── command_batch        # Command batching logic
│   │   ├── cache_manager        # GPU capability cache
│   │   ├── video_decoder        # Decode compressed frames (local)
│   │   └── ffmpeg_decoder       # FFmpeg-based decoder

├── scripts/
│   ├── windows/                 # Windows automation scripts
│   └── linux/                   # Linux automation scripts
├── tools/
│   └── windows/
│       └── OmniGPU-Manager.ps1  # GUI manager
├── third_party/                 # Third-party dependencies
│   ├── fetch_mesa3d.py          # Downloads Mesa3D binaries (Zink)
│   ├── mesa3d/                  # Mesa3D runtime (downloaded)
│   └── clvk/                    # clvk submodule (OpenCL→Vulkan)
├── gen/                         # Code generation
│   ├── generate.py              # Generates vk_intercept_gen.cpp from Vulkan XML
│   └── vulkan_api.json          # Vulkan API registry data
├── tests/                       # Test suite
└── cmake/                       # CMake modules
    └── OmniGPUThirdParty.cmake  # Third-party dependency setup
```

## Troubleshooting

### "MSVC runtime not found" on guest

The guest DLL links against the MSVC runtime statically (`x64-windows-static-md` triplet). Ensure you build with the same vcpkg triplet. If deploying to a VM without Visual Studio, use the static link build.

### "Connection refused" / Port blocked

- Verify the host is running: `.\scripts\windows\diagnose.ps1`
- Check Windows Firewall: ensure port 9443 (or your configured port) is open inbound.
- On Linux: `sudo ufw allow 9443` or configure `iptables`.

### clvk not building

clvk requires a recent Clang and the OpenCL headers. On Windows, run `scripts\windows\build_clvk.bat` from a Visual Studio developer prompt. On Linux, run `scripts/linux/build_clvk.sh`. Ensure `VulkanSDK` is in your PATH.

### Guest app crashes on startup

- Check `omnigpu_guest.log` next to the application for connection errors.
- Verify the daemon is running: `start-daemon.bat`
- Verify the host is reachable from the guest (`ping <host>`).

### Zink / OpenGL apps not working

- Ensure Mesa3D was deployed during install (or run `mesa3d\systemwidedeploy.cmd`).
- Check the guest log for "Zink initialized" messages.

### Performance is poor

- Reduce `jpeg_quality` (lower = faster but lower quality).
- Switch to a video codec (`video_codec: "h264"`) for better quality at lower bitrates.
- Adjust `max_batch_interval_ms` — lower values reduce latency, higher values improve throughput.
- Use a wired LAN connection if on WiFi.
- Check `max_fps` — lower values reduce GPU load.

## License

Apache License 2.0 — see [LICENSE](LICENSE).
