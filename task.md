# Báo Cáo Phân Tích Dự Án OmniGPU

> **Phase 1-4: 60+ lỗi đã fix. Phase 5: 8 lỗi mới phát hiện (chưa fix).**

## ✅ TỔNG KẾT CÁC PHASE ĐÃ FIX

### Phase 1 — P0 Critical (10 bugs)
| Bug | Fix |
|-----|-----|
| **C0** | vkCmdBeginRendering depth/stencil attachment remap (imageView + resolveImageView) |
| **C1** | vkCmdClearAttachments imageView remap (field-by-field đọc + mapper) |
| **C2** | next_fake_handle_id() start 0x10000→0x20000000 |
| **C3** | vkAcquireNextImage2KHR manual hook (delegate) |
| **C4** | vkCmdBlitImage2 manual hook + host handler (regions data) |
| **C5** | vkDestroySwapchainKHR đọc stream đúng (offscreen no-op) |
| **C6** | vkCreateSamplerYcbcrConversion skip (ko có guest hook) |

### Phase 2 — P1 Severe (15 bugs)
| Bug | Fix |
|-----|------|
| **S1** | Schema thêm offset field (cần regenerate FlatBuffers) |
| **S2** | Track map_offset + adjust flush pointers (s_memory_map_offsets) |
| **S3** | VK_WHOLE_SIZE actual_size==0 → VK_ERROR_MEMORY_MAP_FAILED |
| **S4** | vkResetQueryPool dùng firstQuery/queryCount từ stream |
| **S5** | vkCmdBeginQuery dùng flags từ stream |
| **S6** | vkCmdWriteTimestamp dùng pipelineStage từ stream |
| **S7** | vkCmdBindDescriptorSets null pipeline layout check |
| **S8** | vkCreateDescriptorSetLayout remap immutable sampler handles |
| **S9** | Null pipeline sub-structs preserved (set =null nếu is_present=false) |
| **S10** | vkCmdCopyImage dùng srcLayout/dstLayout từ stream |
| **S11** | VkClearValue zero-init (cv{}) |
| **S12** | teardown_framebuffer reset handles = VK_NULL_HANDLE |
| **S13** | running_ flag std::atomic\<bool\> |
| **S14** | Unknown sync query fallthrough default→continue |
| **S15** | Guest receive xoá spurious set_sync_response from non-DataType_Unknown |

### Phase 3 — P2 Moderate (16 bugs)
| Bug | Fix |
|-----|------|
| **M1** | vkCmdClearDepthStencilImage implement |
| **M2** | vkCmdResolveImage implement |
| **M3** | vkCreateBufferView + vkDestroyBufferView implement + store/remove mapper |
| **M4-M12** | Các STUB còn lại skip (ko có guest hook) |
| **M13** | GPU caps populate 8 field từ VkPhysicalDeviceVulkan11/12Properties |
| **M14** | GPU scoring tính VRAM (+10/4GB, max+50) |
| **M15** | initialize_guest failure check + log |
| **M16** | Receive thread ưu tiên DataMessage trước VideoFrame |

### Phase 4 — P3 Low (18 bugs)
| Bug | Fix |
|-----|------|
| **L1-L18** | Dead code removal, null checks, log fixes, operator[] safety, thread safety, schema cleanup |

---

## ⚠️ PHASE 5 — LỖI MỚI PHÁT HIỆN (Chưa fix)

### 🔴 CRITICAL

### [N0] `handshake.cpp` — Fields OFF-BY-ONE do thiếu `max_samples`
**File:** `handshake.cpp:275-283`

Schema `.fbs` field ID order: `sample_counts(50)` → `max_samples(51)` → `max_tessellation_factor(52)` → `framebuffer_color_sample_counts(53)`.
Nhưng `CreateCapabilitiesResponse()` call **bỏ qua** `max_samples`:
```cpp
caps.sample_counts,                    // → id 50 ✓
caps.max_tessellation_factor,          // → id 51 (LẼ RA: max_samples) ✗
caps.framebuffer_color_sample_counts,  // → id 52 (LẼ RA: max_tessellation_factor) ✗
caps.compute_queue_count,              // → id 53 (LẼ RA: framebuffer_color_sample_counts) ✗
```
**Tất cả field từ đây trở đi bị lệch 1 position.** Guest nhận sai: tessellation_factor = framebuffer sample counts, sample counts = compute_queue, etc.

**Fix:** Thêm `1 /* max_samples */` vào giữa `sample_counts` và `max_tessellation_factor`.

---

### 🔴 HIGH

### [N1] `ManualHookRegistrar` — thiếu `vkCmdBlitImage2KHR`
**File:** `vk_intercept.cpp` lines 2535-2540

5 Copy2 KHR aliases đã registered, `vkCmdBlitImage2KHR` **bị thiếu**.
App gọi `vkCmdBlitImage2KHR` → fallback `omnigpu_generic_stub` → blit silently skipped.

**Fix:** Thêm `register_manual_hook("vkCmdBlitImage2KHR", reinterpret_cast<void*>(vkCmdBlitImage2_hook));`

---

### [N2] `ResourceMapper::store_*` — silent overwrite → potential GPU memory leak
**File:** `command_dispatcher.h:33-84`

Tất cả `store_*` methods dùng `map[key] = value` — nếu key đã tồn tại, old resource bị ghi đè không destroy.
Thông thường handles unique, nhưng nếu lỗi app gọi create 2 lần cho cùng handle → leak.

**Fix:** Check `find()`, log warning hoặc destroy old handle trước khi overwrite.

---

### [N3] `vkGetDeviceProcAddr` — không cache → 13 calls/frame
**File:** `command_dispatcher.cpp` (13 handlers)

`vkGetDeviceProcAddr` gọi mỗi lần dispatch cho: `vkCmdPipelineBarrier2, vkCmdCopy*2(x5), vkQueueSubmit2, vkCmdSetVertexInputEXT, vkBind*Memory2(x2), vkCmdWaitEvents2, vkCmdBlitImage2, vkGetSemaphoreCounterValue`.

**Fix:** Cache function pointers trong `CommandDispatcher::set_device()`.

---

### [N4] `handshake.cpp` — Auth token hoàn toàn broken
**File:** `guest_init.cpp:59` + `handshake.cpp:22-31`

Guest `CreateCapabilitiesRequest` không bao giờ gửi `auth_token`. Nếu host config có token → luôn auth fail → handshake fail.

**Fix:** Guest đọc token từ config hoặc thêm param vào `CreateCapabilitiesRequest`.

---

### 🟡 MODERATE

### [N5] `handshake.cpp` — Response loss = deadlock host-guest
**File:** `handshake.cpp:286-300`

Host gửi response xong vào thẳng main loop. Guest đang blocking `receive_data()`. Nếu response bị mất (TCP issue) → host chờ command, guest chờ caps → **deadlock vĩnh viễn**. Không có timeout bên host.

**Fix:** Thêm timeout trên host sau gửi response (nếu không nhận CommandMessage trong N giây → disconnect).

---

### [N6] `vkGetPhysicalDeviceProperties_hook` — đọc caps KHÔNG check `valid()`
**File:** `vk_intercept.cpp:484-486`

`vendorID`, `deviceID`, `deviceType` đọc từ caps không check `caps.valid()`. Khi handshake fail → guest trả RTX 4090 default thay vì fallback.

**Fix:** Check `caps.valid()` cho mọi field.

---

### [N7] `gpu_caps_store.cpp` — data race trên `get()/store()`
**File:** `gpu_caps_store.cpp:7-17`

`std::string gpu_name` được gán non-atomic trong `store()`. `get()` trả reference → UB nếu gọi đồng thời.

**Fix:** `std::atomic` hoặc mutex.
