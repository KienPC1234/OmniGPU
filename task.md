# Báo Cáo Phân Tích Dự Án OmniGPU — FINAL

> **Tổng: ~110 issues. ~85 fixed. ~25 infra + 4 ML review issues.**

---

## ✅ ML Implementation — Code Review

**Phase 1 (llama.cpp Minimum)** — 90% hoàn thành:
| Item | Status | Note |
|------|--------|------|
| gpu_caps.h +6 fields | ✅ | Cả Phase 1 + 2 |
| .fbs schema +6 fields | ✅ | Đã regenerate |
| vk_intercept.cpp extensions | ✅ | 5 extensions added including coopmat |
| vk_intercept.cpp pNext chain | ✅ | 5 case mới trong switch |
| vk_intercept.cpp Vulkan11/12 Features | ✅ | shaderFloat16, shaderInt8, storage bits |
| guest_init.cpp parse | ✅ | 6 field parsed |
| handshake.cpp send response | ✅ | Đủ 6 field trong response |
| Subgroup bug fix | ✅ | `subgroup_size` đã sửa |

### ⚠️ 5 vấn đề còn lại (vừa tìm thấy ML5 khi debug llama.cpp):

| # | Mức | Mô tả | File:Line |
|---|-----|-------|-----------|
| **ML1** | HIGH | ML features hardcoded TRUE (không query GPU) | `handshake.cpp:153-163` |
| **ML2** | MEDIUM | `subgroupSize` đã dùng `caps.subgroup_size` ✅ | `vk_intercept.cpp:634` |
| **ML3** | LOW | Coopmat tile sizes hardcoded 16x16x16 | `handshake.cpp:160-162` |
| **ML4** | LOW | `VK_EXT_shader_subgroup_size_control` chưa thêm | `vk_intercept.cpp` |
| **ML5** | **CRITICAL** | `cache_manager.cpp` KHÔNG save/load 6 ML fields → cached caps cũ dùng default false → llama.cpp "does not support 16-bit storage" | `cache_manager.cpp:68-172` |

### Fix ML5 (cache_manager.cpp):

Thêm vào `save()` (sau dòng 172):
```cpp
entry["supports_16bit_storage"] = caps.supports_16bit_storage;
entry["supports_8bit_storage"]  = caps.supports_8bit_storage;
entry["supports_float16_int8"]  = caps.supports_float16_int8;
entry["supports_cooperative_matrix"] = caps.supports_cooperative_matrix;
entry["coopmat_m"] = caps.coopmat_m;
entry["coopmat_n"] = caps.coopmat_n;
entry["coopmat_k"] = caps.coopmat_k;
entry["supports_integer_dot_product"] = caps.supports_integer_dot_product;
```

Thêm vào `load()` (sau dòng 105):
```cpp
caps.supports_16bit_storage = data.value("supports_16bit_storage", false);
caps.supports_8bit_storage  = data.value("supports_8bit_storage", false);
caps.supports_float16_int8  = data.value("supports_float16_int8", false);
caps.supports_cooperative_matrix = data.value("supports_cooperative_matrix", false);
caps.coopmat_m = data.value("coopmat_m", 16U);
caps.coopmat_n = data.value("coopmat_n", 16U);
caps.coopmat_k = data.value("coopmat_k", 16U);
caps.supports_integer_dot_product = data.value("supports_integer_dot_product", false);
```

**Immediate workaround trên VM test:**
```powershell
del "%APPDATA%\omnigpu\omnigpu_caps_cache.json"
```
→ Guest sẽ re-query host, nhận ML fields mới → llama.cpp chạy được.

**ML2** — Sửa 1 dòng:
```cpp
// Dòng 633: thay "32" thành "caps.subgroup_size"
p11->subgroupSize = caps.valid() ? caps.subgroup_size : 32;
```

---

## ✅ TẤT CẢ BUG CRITICAL + SPEC ĐÃ FIX

### Phase 6-7 Compute bugs đã xác nhận fix:
| Bug | Status | Verify |
|-----|--------|--------|
| S28 vkWaitSemaphores | ✅ | sync_query_ext(0x8f), host blocks + responds |
| C7 vkCmdDispatchBase baseGroup | ✅ | `vkCmdDispatchBase(cb, bx, by, bz, x, y, z)` |
| C8 vkCmdResetEvent2 u32/u64 | ✅ | Guest u64, host read_u64 |
| C9 vkCmdWriteTimestamp2 u32/u64 | ✅ | Guest u64, host read_u64 + uses stage |

### Security — Deserializer:
| Item | Status |
|------|--------|
| read_array kMaxArrayElements=1M | ✅ |
| read_raw error_ flag | ✅ |
| read_* error propagation | ✅ |
| dispatch() checks reader.ok() | ✅ |
| skip() sets error_ | ✅ |

### Phase 5 — All fixed:
- N0-N2, N4, N6, N7 ✅
- N3 cached pfn* ✅
- N5 handshake timeout ✅

### Phase 1-4 — All fixed:
- 60 bugs ✅

---

## ⚠️ REMAINING — Infra & Quality (25 issues, không crash)

### D1-D26 from infrastructure review:

| # | Mức | Mô tả |
|---|-----|-------|
| 1 | HIGH | GPU indices leaked on external session stop |
| 2 | HIGH | `vkWaitForFences(UINT64_MAX)` blocks forever on GPU hang |
| 3 | HIGH | Vulkan result codes discarded silently (bind/bind fail ignored) |
| 4 | HIGH | Handshake failure "continues with defaults" — TCP stream desynced |
| 5 | HIGH | Guest config `load()` ignores batch tuning params from JSON |
| 6 | HIGH | `build-and-package.ps1` hardcodes VS 2026 paths |
| 7 | HIGH | `CMakePresets.json` release-x86 inherits Clang tools |
| 8 | HIGH | Renderer + MultiGpuRenderer dead code (~500 lines) |
| 9 | MEDIUM | No TCP keepalive — 300s dead conn detection |
| 10 | MEDIUM | No reconnection support (std::call_once) |
| 11 | MEDIUM | Guest shutdown socket close before thread join |
| 12 | MEDIUM | Host config path CWD-relative |
| 13 | MEDIUM | `diagnose.sh` wrong port (50051 vs 9443) |
| 14 | MEDIUM | `ssh_test.py` referenced but doesn't exist |
| 15 | MEDIUM | `test_host.cpp` + `test_network.cpp` placeholders |
| 16 | MEDIUM | No CI/CD, no automated E2E tests |
| 17 | MEDIUM | Guest config JSON defaults mismatch code |
| 18 | MEDIUM | `jpeg_quality` + `max_batch_bytes` missing from config JSONs |
| 19 | MEDIUM | HW encoding broken for VAAPI/QSV/D3D11/CUDA |
| 20 | MEDIUM | MF NV12→RGBA per-pixel float, no SIMD |
| 21 | LOW | Win32 display window no double-buffering, no DPI |
| 22 | LOW | `uninstall.bat` removes vulkan-1.dll from System32 |
| 23 | LOW | Hardcoded Vulkan SDK path in CMakeLists.txt |
| 24 | LOW | CPack configured but unused |
| 25 | LOW | `memorySizes_` not cleared on ResourceMapper cleanup |

---

## ✅ VERIFIED SAFE (không phải bug):
- `write_raw(NULL, size)` — an toàn (fill zero bytes)
- pAllocator NULL trong auto-gen — không crash
- `vkCreateSemaphore` serializer — đúng (manual serializer)
- `vkCreateBuffer`/`vkCreateImage` pQueueFamilyIndices — serialize đúng
- `vkQueueSubmit`/`vkQueueSubmit2` — count đúng
- All memory leaks (pName, pSampleMask, etc.) — đã fix
- All handle remapping (swapchain, bufferView, immutableSampler) — đã fix
- All pNext clearing — đã fix
