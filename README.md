# OmniGPU

**Seamless GPU forwarding over LAN** — Forward OpenGL, Vulkan, and OpenCL commands from a guest VM or thin client to a remote host GPU over a TCP network.

## Architecture

```
┌──────────────────────────┐         TCP LAN              ┌───────────────────────────┐
│        GUEST (VM)        │  ◄───────────────────────►   │        HOST (Server)      │
│                          │  FlatBuffers · LZ4/TurboJPEG │                           │
│  Application (game/ML)   │                              │  FFmpeg (NVENC/AMF/VA-API)│
│    ↓ (intercepted)       │                              │  GPU Manager              │
│  ┌─────────────────────┐ │                              │  Multi-GPU Split Renderer │
│  │ Zink (OpenGL→Vulkan)│ │                              │  Resource Cache           │
│  │ clvk (OpenCL→Vulkan)│ │                              └───────────────────────────┘
│  └───────┬─────────────┘ │
│          │ serialized    │
│  ┌───────┴───────────┐   │
│  │ OmniGPU Guest DLL │───┤
│  │ (Vulkan ICD)      │   │
│  └───────────────────┘   │
│  │ OmniGPU Guest DLL │───┤
│  │ (Vulkan ICD)      │   │
│  └───────────────────┘   │
└──────────────────────────┘
```

The **Guest** intercepts graphics/compute API calls via Zink (OpenGL→Vulkan) and clvk (OpenCL→Vulkan), serializes them with FlatBuffers, and ships them over TCP to the **Host**, which executes them on its physical GPU(s) and returns compressed framebuffer data — encoded via FFmpeg (NVENC/AMF/VA-API) or fallback JPEG.

## Features

- **Multi-GPU Split Rendering** — Distribute rendering across multiple host GPUs. Each GPU renders a horizontal strip of the frame.
- **Video Encoding** — FFmpeg-powered encoding with hardware acceleration (NVENC / AMF / VA-API). Falls back to JPEG + LZ4.
- **Zink (OpenGL→Vulkan)** — Mesa's Zink translation layer runs inside the guest, translating OpenGL calls to Vulkan before serialization.
- **clvk (OpenCL→Vulkan)** — OpenCL-on-Vulkan translation for compute workloads.
- **Adaptive Batching** — Commands are batched dynamically based on byte size and command count (`min_batch_bytes` / `max_batch_bytes`, `min_batch_commands` / `max_batch_commands`), with a max latency cap (`max_batch_interval_ms`).
- **GPU Capability Caching** — GPU properties are queried once on first connection and cached locally (`cache_ttl_seconds`) to avoid repeated synchronous round-trips.
- **Windows Service Mode** — The host can run as a Windows service (`--install` / `--uninstall` / `--service`).
- **Cross-Platform** — Windows and Linux support for both host and guest.

## Requirements

### Host

| Component | Requirement |
|-----------|-------------|
| OS | Windows 10+ or Linux |
| GPU | Any GPU with Vulkan support (NVENC/AMF/VA-API auto-detected via FFmpeg) |
| Driver | Latest GPU driver with Vulkan support |
| Vulkan SDK | [Vulkan SDK](https://vulkan.lunarg.com/) 1.3+ |
| CMake | 3.28+ |
| Ninja | Build system |
| Compiler | Windows: Visual Studio 2022 + Clang-cl / Linux: Clang 19 |
| vcpkg | With `VCPKG_ROOT` environment variable set |

### Guest

| Component | Requirement |
|-----------|-------------|
| OS | Windows or Linux VM |
| Vulkan Loader | Present in the guest OS |
| Network | TCP/IP connectivity to host on port 9443 (default) |
| SSH | (Optional) For automated deployment scripts |

## Build

### Windows (Release)

```powershell
cmake --preset release
cmake --build --preset release
```

Output goes to `build/release/bin/`.

### CMake Options

| Option | Default | Description |
|--------|---------|-------------|
| `OMNIGPU_BUILD_TESTS` | `ON` | Build test suite |
| `OMNIGPU_BUILD_HOST` | `ON` | Build host server |
| `OMNIGPU_BUILD_GUEST` | `ON` | Build guest client |
| `OMNIGPU_FETCH_MESA3D` | `OFF` | Download Mesa3D package for the guest |

| `OMNIGPU_BUILD_FFMPEG` | `ON` | Download FFmpeg pre-built with HW acceleration (NVENC/AMF/QSV) |

### Mesa3D (OpenGL support)

Mesa3D (Zink OpenGL→Vulkan) is auto-downloaded when `OMNIGPU_FETCH_MESA3D=ON` (default).
Includes `opengl32.dll` (Zink) and `libgallium_wgl.dll` (OpenGL compatibility).

### clvk (OpenCL support)

clvk translates OpenCL to Vulkan. Place the built `OpenCL.dll` in `third_party/clvk-bin/`
and CMake automatically enables OpenCL forwarding. No build option needed.

### Linux

```bash
cmake --preset linux
cmake --build --preset linux
```

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

Checks: host process running, port listening, GPU info, Vulkan SDK, Zink/clvk files, firewall rules, and network connectivity.

### 3. Deploy to Guest VM

Copy these files from `build/release/bin/` to the VM:

| File | Purpose |
|------|---------|
| `omnigpu_guest.dll` | Vulkan ICD driver (intercepts Vulkan calls) |
| `vk_icd.json` | Vulkan ICD manifest pointing to `omnigpu_guest.dll` |
| `opengl32.dll` | Mesa Zink (OpenGL→Vulkan) — only if `OMNIGPU_FETCH_MESA3D=ON` |
| `OpenCL.dll` | clvk (OpenCL→Vulkan) — only if built and present |
| `omnigpu_guest.json` | Guest configuration file (optional, uses defaults if absent) |

### 4. Run an Application on the Guest

**Manually:**

```powershell
set VK_ICD_FILENAMES=C:\path\to\vk_icd.json
your_app.exe
```

The guest DLL auto-initializes on load (via `DllMain` on Windows, `__attribute__((constructor))` on Linux) and connects to the host.

### 5. Automated Build + Deploy + Test

```powershell
.\scripts\windows\build_deploy_test.ps1 -SshHost 192.168.1.100 -SshUser user
```

Builds, copies binaries to the remote VM over SSH, starts the guest, and verifies connectivity.

### 6. Test Guest Connection

```powershell
.\scripts\windows\test_guest.ps1 -HostAddr 192.168.1.239 -PortNum 9443
```

Runs `omnigpu_guest_test.exe` on the remote VM and checks the handshake log.

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

## Guest Architecture (No Service Required)

The **guest is NOT a background service or daemon**. It is a **Vulkan Installable Client Driver (ICD)** — a shared library (`omnigpu_guest.dll` / `omnigpu_guest.so`) that is loaded **automatically** by the Vulkan Loader into each application process.

### How the Guest Loads

- **Windows**: `DllMain` in `dll_main.cpp` spawns an initialization thread on `DLL_PROCESS_ATTACH`, connects to the host, and hooks Vulkan APIs — all inside the application process.
- **Linux**: An `__attribute__((constructor))` function performs the same steps.
- **No separate process, no service, no daemon.** The guest lives entirely inside your application.

### Deployment Models

| Method | Mechanism | Use Case |
|--------|-----------|----------|

| **System-wide (Windows)** | Register `vk_icd.json` in registry | All Vulkan apps auto-use OmniGPU |
| **System-wide (Linux)** | Copy ICD manifest to `/usr/share/vulkan/icd.d/` | All Vulkan apps auto-use OmniGPU |

> If you started the host and ran a Vulkan application on the guest but see no "guest service" running — **that is correct**. The guest only exists inside the application's process memory. Check `omnigpu_guest.log` next to your application for connection details.

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
    "nvenc": {
        "preset": "p1",
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
| `nvenc.preset` | `"p1"` | NVENC-specific: encoder preset (`"p1"` fastest — `"p7"` best compression). Ignored on non-NVIDIA GPUs. |
| `nvenc.tuning` | `"low_latency"` | NVENC-specific: tuning profile (`"high_quality"`, `"low_latency"`, `"ultra_low_latency"`, `"lossless"`, `"ultra_high_quality"`). Ignored on non-NVIDIA GPUs. |
| `nvenc.gop_length` | `0` | NVENC-specific: GOP length. `0` = infinite (keyframe only at start). Ignored on non-NVIDIA GPUs. |

### Guest (`omnigpu_guest.json`)

**Windows**: place in the same directory as `omnigpu_guest.dll` (or next to your application).
**Linux**: `~/.config/omnigpu_guest.json`.

```json
{
    "host": "127.0.0.1",
    "port": 9443,
    "zink_enabled": true,
    "clvk_enabled": true,
    "cache_ttl_seconds": 86400,
    "adaptive_batching": true,
    "max_batch_interval_ms": 16
}
```

| Key | Default | Description |
|-----|---------|-------------|
| `host` | `"127.0.0.1"` | Host server IP address or hostname |
| `port` | `9443` | Host server TCP port |
| `zink_enabled` | `true` | Enable OpenGL→Vulkan translation via Mesa Zink. Disable if not running OpenGL applications. |
| `clvk_enabled` | `true` | Enable OpenCL→Vulkan translation via clvk. Disable if not running OpenCL workloads. |
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
│   │   ├── nvenc_encoder        # NVIDIA NVENC encoder
│   │   ├── amf_encoder          # AMD AMF encoder
│   │   ├── vaapi_encoder        # Intel VA-API encoder
│   │   ├── handshake            # Guest connection handshake
│   │   ├── cli                  # Interactive CLI
│   │   └── service_manager      # Windows service management
│   ├── guest/                   # Guest client
│   │   ├── dll_main.cpp         # DLL entry point (auto-init)
│   │   ├── vk_icd.json          # Vulkan ICD manifest
│   │   ├── icd_entrypoints      # Vulkan ICD entrypoints
│   │   ├── vk_intercept         # Vulkan API interceptor
│   │   ├── vk_intercept_gen     # Auto-generated intercept stubs
│   │   ├── vulkan_serializer    # Serialize Vulkan calls to FlatBuffers
│   │   ├── command_batch        # Command batching logic
│   │   ├── client               # TCP client connection
│   │   ├── guest_config         # Guest config loader
│   │   ├── guest_init           # Initialization / shutdown
│   │   ├── cache_manager        # GPU capability cache
│   │   ├── loader               # Shared library loader
│   │   ├── resource_tracker     # Guest-side resource tracking
│   │   └── video_decoder        # Decode compressed frames

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

- Confirm `VK_ICD_FILENAMES` points to the correct `vk_icd.json` path.
- Check `omnigpu_guest.log` next to the application for connection errors.
- Verify the host is reachable from the guest (`ping <host>`).

### Zink / OpenGL apps not working

- Ensure `opengl32.dll` is deployed next to the application.
- Verify `zink_enabled: true` in `omnigpu_guest.json`.
- Check the guest log for "Zink initialized" messages.

### Performance is poor

- Reduce `jpeg_quality` (lower = faster but lower quality).
- Switch to a video codec (`video_codec: "h264"`) for better quality at lower bitrates.
- Adjust `max_batch_interval_ms` — lower values reduce latency, higher values improve throughput.
- Use a wired LAN connection if on WiFi.
- Check `max_fps` — lower values reduce GPU load.

## License

Apache License 2.0 — see [LICENSE](LICENSE).
