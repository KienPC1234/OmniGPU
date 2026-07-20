# OmniGPU

**Remote GPU Compute over LAN** — Forward Vulkan compute workloads from a thin client or VM to a remote host GPU over TCP. Compute-first design with rendering as secondary.

## Vision

```
  ┌──────────────────────────┐      TCP LAN        ┌────────────────────────────┐
  │       GUEST (Client)     │  ◄──────────────►   │       HOST (Server)        │
  │                          │  FlatBuffers·Binary │                            │
  │  ML Inference Engine     │  Buffer Upload/DL   │  ┌───────────────────────┐ │
  │  Scientific Computing    │  Compute Dispatch   │  │  Command Dispatcher   │ │
  │  CI/CD GPU Runner        │  Result Readback    │  │  ┌──────────────────┐ │ │
  │  Game Server             │                     │  │  │ Resource Mapper  │ │ │
  │    ↓                     │                     │  │  │ (handle remap)   │ │ │
  │  ┌───────────────────┐   │                     │  │  └──────────────────┘ │ │
  │  │ OmniGPU Guest DLL │───┤                     │  │  ┌──────────────────┐ │ │
  │  │ (Vulkan ICD)      │   │                     │  │  │ Buffer Sync      │ │ │
  │  └───────┬───────────┘   │                     │  │  │ (upload/dload)   │ │ │
  │          │               │                     │  │  └──────────────────┘ │ │
  │  - intercept VK calls    │                     │  └───────────────────────┘ │
  │  - serialize + batch     │                     │  ┌───────────────────────┐ │
  │  - manage buffer sync    │                     │  │  GPU Manager          │ │
  │  - decode compute results│                     │  │  · single/multi-GPU   │ │
  │                          │                     │  │  · capability caps    │ │
  │                          │                     │  └───────────────────────┘ │
  └──────────────────────────┘                     └────────────────────────────┘
```

**OmniGPU turns any machine into a GPU compute node.** A lightweight Vulkan ICD on the guest intercepts API calls and forwards them to a powerful remote host. This enables:

- **ML inference offload** — Run Llama, Stable Diffusion, Whisper on a remote GPU from a laptop
- **CI/CD GPU runners** — Cloud GPU for build pipelines without dedicated hardware
- **Scientific computing** — Batch processing, simulations, rendering farms
- **Game servers** — GPU-accelerated physics, AI, or compute shaders on a headless server

## Why Compute-First?

| Aspect | 3D Rendering | Compute |
|--------|-------------|---------|
| Bandwidth | Huge (60fps × 8Mpx → video stream) | Small (buffers + dispatch args) |
| Latency | Critical (<16ms per frame) | Tolerant (batch jobs, async) |
| Video encode | Required (expensive) | Not needed |
| TCP overhead | Problematic | Acceptable |
| **Real use cases** | Gaming thin client | **ML, science, CI/CD, rendering** |

Compute workloads are a **perfect fit** for TCP-based GPU forwarding because:
- **Small commands**: `vkCmdDispatch` + a few descriptors = bytes on the wire
- **Batched transfers**: Upload a model once, run 1000 dispatches, read result once
- **No video encode/decode**: The heavy encoder pipeline is eliminated
- **Async-friendly**: Submit compute jobs, poll for results later

## Features

### Compute (Primary)
- **Full Vulkan compute pipeline** — Dispatch, indirect dispatch, push constants
- **Buffer management** — Storage buffers, uniform buffers, readback via staging
- **Buffer Device Address** — Synchronous query for GPU pointers
- **Descriptor management** — Sets, pools, update templates
- **Shader modules** — SPIR-V creation and forwarding
- **Compute pipelines** — Creation, caching, specialization constants
- **Synchronization** — Fences, semaphores, pipeline barriers (Vulkan 1.3 sync2)
- **Performance** — Adaptive command batching with RTT-smoothed thresholds

### Rendering (Secondary — basic support)
- Vulkan 1.0–1.3 forwarding (instance, device, swapchain, WSI)
- Offscreen rendering + readback
- Video encoding via FFmpeg (H.264/HEVC/AV1)
- Multi-GPU split rendering

### Infrastructure
- **Guest OS**: Windows / Linux VM or thin client
- **Host OS**: Windows / Linux with any Vulkan 1.3 GPU
- **Protocol**: FlatBuffers over TCP (port 9443)
- **Code generation**: Auto-generated intercept stubs from Vulkan XML registry
- **GPU capability caching**: Cached locally with configurable TTL
- **Configurable batching**: Adaptive thresholds by count, bytes, and timer

## Requirements

### Build

| Component | Requirement |
|-----------|-------------|
| OS | Windows 10+ or Linux |
| CMake | 3.28+ |
| Ninja | Build system |
| Compiler | Windows: clang-cl (VS 2022+) / Linux: Clang 19 |
| vcpkg | `VCPKG_ROOT` environment variable set |
| Python 3 | For code generation and downloading third-party binaries |

### Host

| Component | Requirement |
|-----------|-------------|
| GPU | Any GPU with Vulkan 1.3 support |
| Driver | Latest GPU driver |

### Guest

| Component | Requirement |
|-----------|-------------|
| OS | Windows or Linux |
| Vulkan Loader | Present in guest OS |
| Network | TCP/IP to host on port 9443 (default) |

## Build

### Windows (Release)

```powershell
.\build.ps1
```

Or step by step:

```powershell
cmake --preset release
cmake --build --preset release
```

### Linux

Install the compiler and runtime development dependencies (Ubuntu 24.04 example):

```bash
sudo apt install clang-19 ninja-build pkg-config libvulkan-dev vulkan-tools \
  glslang-tools libavcodec-dev libavutil-dev libswscale-dev \
  libturbojpeg0-dev liblz4-dev

cmake --preset linux
cmake --build --preset linux
ctest --preset linux --output-on-failure
```

Install the host and guest paths:

```bash
sudo ./scripts/linux/install_host.sh --build-dir build/linux --start
sudo ./scripts/install_guest_linux.sh --build-dir build/linux
./scripts/linux/diagnose.sh
```

The Linux guest reads configuration in this order: `OMNIGPU_CONFIG`,
`$XDG_CONFIG_HOME/omnigpu/omnigpu_guest.json`,
`~/.config/omnigpu/omnigpu_guest.json`, then
`/etc/omnigpu/omnigpu_guest.json`. `OMNIGPU_HOST`, `OMNIGPU_PORT`, and
`OMNIGPU_AUTH_TOKEN` override file values.

Linux support is compute-first. The Linux ICD intentionally does not advertise
Windows swapchain/WSI extensions; native XCB/Wayland presentation remains a
separate rendering milestone.

### Package contents (post-build)

```
build/release/
├── bin/
│   ├── omnigpu_host.exe         Host server
│   ├── omnigpu_guest.dll        Vulkan ICD (x64)
│   ├── omnigpu_guest_test.exe   Standalone guest test
│   ├── omnigpu_vk_test.exe      Compute test (vector add, matmul)
│   └── omnigpu_tests.exe        GTest suite
├── omnigpu_guest.json           Guest config
├── omnigpu_host.json            Host config
└── vk_icd.json                  ICD manifest
```

## Quick Start

### 1. Start the Host Server

```powershell
.\build\release\bin\omnigpu_host.exe
```

### 2. Install on Guest VM

Copy the distribution to the guest, then:

```powershell
.\scripts\windows\install.bat
```

### 3. Configure Guest

Edit `omnigpu_guest.json`:

```json
{
    "host": "192.168.1.100",
    "port": 9443
}
```

### 4. Run Compute Test

On the guest, verify the connection:

```powershell
.\build\release\bin\omnigpu_vk_test.exe
```

This runs:
- **Vector addition** (1024 elements, random floats)
- **Matrix multiplication** (16×16, push constants)
- **Buffer device address** query

### 5. Run Your Compute Workload

Any Vulkan compute application works without modification — the ICD intercepts all calls transparently.

## Configuration

### Host (`omnigpu_host.json`)

```json
{
    "port": 9443,
    "render_width": 1920,
    "render_height": 1080,
    "multi_gpu_enabled": true
}
```

### Guest (`omnigpu_guest.json`)

```json
{
    "host": "192.168.1.100",
    "port": 9443,
    "cache_ttl_seconds": 86400,
    "adaptive_batching": true,
    "max_batch_interval_ms": 16
}
```

## Wire Protocol

Messages use FlatBuffers with a root `Message` table:

| Type | Direction | Purpose |
|------|-----------|---------|
| `CapabilitiesRequest/Response` | Guest ↔ Host | GPU handshake |
| `CommandMessage` | Guest → Host | Vulkan function call + serialized args |
| `DataMessage` | Guest → Host | Raw buffer data (upload) |
| `DataMessage` | Host → Guest | Compute results (readback) |
| `ResourceCacheUpload/Evict` | Guest → Host | VRAM cache management |
| `VideoFrame` | Host → Guest | Compressed video (rendering only) |

## Compute Test

The project includes `omnigpu_vk_test.exe` — a Vulkan compute benchmark:

```
=== OmniGPU Compute Test ===
Tests: vector addition, matrix multiplication, buffer addresses

GPU: NVIDIA RTX 4090 (api=1.3)
Compute queue family: 1

--- [Test 1] Vector Addition ---
  100 dispatches in 45.20 ms = 0.45 us/dispatch
  [PASS] All 1024 elements correct

--- [Test 2] Matrix Multiplication (16x16) ---
  [PASS] 16x16 mat mul correct, 2.30 us/matmul

--- [Test 3] Buffer Device Address ---
  [PASS] Buffer device address: 0x1a2b3c4d5e6f7000
```

## Project Structure

```
OmniGPU/
├── src/
│   ├── host/              Host server (dispatcher, GPU manager)
│   ├── guest/             Guest DLL (Vulkan ICD, serializer)
│   ├── common/            Shared (logging, networking, GPU caps)
│   ├── schemas/           FlatBuffers protocol schema
│   └── tools/
│       ├── vk_compute_test.cpp   Compute benchmark
│       ├── compute_add.comp      Vector addition shader
│       ├── compute_mul.comp      Matrix multiplication shader
│       └── *_spv.h              Generated SPIR-V headers
├── gen/                   Code generation pipeline
├── scripts/               Build & deploy automation
├── tests/                 GTest suite
└── third_party/           Vulkan-Headers, FFmpeg, Mesa3D, clvk
```

## Roadmap

### Short-term (Compute Foundation)
- [x] Vector addition compute test
- [x] Matrix multiplication with push constants
- [x] Buffer device address queries
- [ ] Optimized buffer upload/download (async DMA)
- [ ] Multiple concurrent compute streams

### Medium-term (Scale)
- [ ] Multi-GPU compute (split workloads across GPUs)
- [ ] CUDA/HIP interop (via Vulkan compute)
- [ ] Windows/Linux service hardening
- [ ] Performance benchmarks vs native

### Long-term (Ecosystem)
- [ ] Python/Web client SDK
- [ ] Kubernetes GPU operator integration
- [ ] Public GPU marketplace concept
- [ ] ML framework integration (PyTorch, TensorFlow)

## License

Apache License 2.0 — see [LICENSE](LICENSE).
