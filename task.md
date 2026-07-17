# OmniGPU — Compute-First Task List

> **Định hướng:** Compute là tương lai. Render 3D qua TCP quá tốn kém.
> Compute workloads (ML, science, CI/CD) fit hoàn hảo với TCP forwarding.

---

## ✅ PHASE 1 — Core Pipeline (16/16)

- [x] **1.1** `vkQueueSubmit` — gọi submit thật với fence (was: chỉ log "recorded")
- [x] **1.2** `flush_and_readback()` — pipeline hoàn chỉnh (was: sai state)
- [x] **1.3** `renderTargetImage_` tracking
- [x] **1.4-1.5** `vkCmdPipelineBarrier`/`Barrier2` — forward barriers thật (was: rỗng)
- [x] **1.6** `ResourceMapper::cleanup()` — destroy đúng thứ tự
- [x] **1.7** `vkQueueSubmit2` — implementation đầy đủ (was: STUB)
- [x] **1.8-1.9** `vkCmdBeginRenderPass`/`2` — fix serializer mismatch
- [x] **1.10-1.12** `vkCmdWaitEvents`/`2`, `vkCmdSetVertexInputEXT` — fix từ STUB
- [x] **1.13** Dispatchable handle structs — đủ kích thước cho loader
- [x] **1.14-1.16** Command buffer lifecycle — forward hoàn chỉnh đến host

---

## ✅ PHASE 2 — Compute Path (17/17)

### Buffer Management
- [x] **2.1** `vkMapMemory` dirty page tracking — chỉ gửi ranges đã flush
- [x] **2.2** `sync_all_mapped_memory_to_host()` — optimize: chỉ gửi dirty ranges
- [x] **2.3** Staging buffer pattern — BufferManager với persistent + staging
- [x] **2.4** Async buffer readback — `vkInvalidateMappedMemoryRanges` gửi DataMessage về guest

### Compute Pipeline verified
- [x] **2.5** `vkCmdDispatch` E2E — compute test (vector add + matmul)
- [x] **2.6** `vkCmdDispatchIndirect` — buffer remap
- [x] **2.7** Compute pipeline creation + specialization constants
- [x] **2.8** `vkCmdPushConstants` cho compute
- [x] **2.9** Multiple descriptor sets + update
- [x] **2.10** `vkGetBufferDeviceAddress` sync query 0x80 round-trip

### Physical Device Info — compute-optimized
- [x] **2.11** `vkGetPhysicalDeviceFeatures` — chỉ enable compute-critical, không VK_TRUE hết
- [x] **2.12** `vkGetPhysicalDeviceMemoryProperties` — 5 types, compute-optimized
- [x] **2.13** `vkGetBufferMemoryRequirements` — sync từ host qua 0x83/0x84
- [x] **2.14** `vkGetPhysicalDeviceQueueFamilyProperties` — 2 queues (GFX+CMP, CMP only)

### Compute Test
- [x] **2.15** Test vector addition + matrix multiplication + buffer device address
- [x] **2.16-2.17** SPIR-V shaders compile tự động (compute_add, compute_mul)

---

## ✅ PHASE 3 — Compute Infrastructure (MỚI)

### Multi-GPU Compute Engine
- [x] **3.1** `MultiGpuCompute` — per-GPU device+queue+pool, work splitting, load balancing
- [x] **3.2** `BufferManager` — persistent GPU buffers, staging upload/download, LRU eviction
- [x] **3.3** Zero-copy VRAM: upload once → reference by ID → reuse across dispatches

### Protocol mở rộng
- [x] **3.4** `compute_queue_count`, `supports_buffer_device_address`, `auth_required`, `buffer_manager_capable`
- [x] **3.5** `compute_mode`, `large_buffers` flags trong CapabilitiesRequest

---

## ✅ PHASE 4 — Security (6/6)

- [x] **4.1** Auth token — `constant_time_compare()`, configurable
- [x] **4.2** Rate limiting — max 10 connections/second
- [x] **4.3** Max sessions — `max_sessions` (mặc định 16)
- [x] **4.4** Socket timeout — `session_timeout_s` (mặc định 300s)
- [x] **4.5** Message size limit — `max_msg_size_mb` (mặc định 16MB)
- [x] **4.6** VRAM budget — `per_session_memory_budget`

---

## ✅ PHASE 5 — Memory Audit (8/8)

- [x] **5.1** Null pAllocator crash — `write_raw(pAllocator, sizeof(*pAllocator))` đọc từ nullptr
- [x] **5.2** `vkFreeMemory` double-free — `remove_device_memory()` sau free
- [x] **5.3** `BufferManager::find_memory_type` — trả về 0 luôn, fix query memory properties thật
- [x] **5.4** VRAM budget enforcement — từ chối allocate nếu quá hạn mức
- [x] **5.5** `vkGetBufferDeviceAddress` sync query — round-trip 0x80
- [x] **5.6** Memory type selection đúng — HOST_VISIBLE|COHERENT, DEVICE_LOCAL, etc.
- [x] **5.7** Shadow buffer lifecycle — free+remove khi vkFreeMemory
- [x] **5.8** ResourceMapper cleanup — destroy đúng thứ tự phụ thuộc

---

## ✅ PHASE 6 — STUB Audit (16/16)

- [x] **6.1** `vkCmdCopyBufferToImage` — STUB → real với buffer+image remap
- [x] **6.2** `vkCmdCopyImageToBuffer` — STUB → real
- [x] **6.3** `vkCmdClearAttachments` — STUB → real
- [x] **6.4** `vkCmdExecuteCommands` — STUB → real (secondary CBs)
- [x] **6.5** `vkCmdCopyQueryPoolResults` — STUB → real (profiling)
- [x] **6.6-6.16** 11 Vulkan 1.3 "2" variants — STUB → best-effort (raw struct limitation)

---

## ✅ PHASE 7 — Batch System (5/5)

- [x] **7.1** **DEADLOCK FIX**: `vkQueueSubmit` force_flush ngay sau append (compute deadlock)
- [x] **7.2** `vkQueueSubmit2` + `vkQueuePresentKHR` force_flush
- [x] **7.3** Log sai: "0 commands" vì zeroed trước log — save count trước swap
- [x] **7.4** Data loss: send fail → batch cleared mất data — log error
- [x] **7.5** `on_present()` flush rỗng — check `empty()` trước

---

## ✅ PHASE 8 — Sync & Synchronization (10/10)

- [x] **8.1** `vkWaitForFences` — **CRITICAL**: skip fences, không wait → fix remap+wait thật
- [x] **8.2** `vkResetFences` — skip → fix remap+reset thật
- [x] **8.3** `vkWaitSemaphores` — custom serializer + remap
- [x] **8.4** `vkSignalSemaphore` — custom serializer
- [x] **8.5** `vkGetSemaphoreCounterValue` — remap + call thật
- [x] **8.6** Timeline semaphore support (VkSemaphoreTypeCreateInfo)
- [x] **8.7** `vkInvalidateMappedMemoryRanges` — readback từ host GPU → guest shadow buffer
- [x] **8.8** Thread-safe recv thread + DataMessage handler
- [x] **8.9** Guest `update_shadow_buffer()` — copy host data vào guest pointer
- [x] **8.10** Config JSON mẫu — host + guest với đầy đủ security fields

---

## 🟡 PHASE 9 — Còn Lại (Không ưu tiên)

### Stability
- [ ] **9.1** Static initializer order — `ManualHookRegistrar` race với DllMain
- [ ] **9.2** `shutdown_guest()` — delete batch trước disconnect
- [ ] **9.3** `connect_to_host()` — `std::call_once` không retry
- [ ] **9.4** Pool fences/command buffers — performance

### Compute Features (nice to have)
- [ ] **9.5** `vkCmdDispatchBase` — base group for multi-dispatch
- [ ] **9.6** Large buffer benchmark (1GB+)
- [ ] **9.7** Workload test: reduction, prefix sum, FFT

### Rendering (Secondary — không ưu tiên)
- [ ] **9.8** Video encoder pipeline cleanup
- [ ] **9.9** Swapchain backing store thật
- [ ] **9.10** Multi-GPU tiled rendering

### Testing
- [ ] **9.11** End-to-end compute test trên VM
- [ ] **9.12** Stress test + memory leak check
- [ ] **9.13** Protocol versioning

---

## PROGRESS

```
Phase 1 (Core Pipeline):     ████████████████████ 16/16 ✅
Phase 2 (Compute Path):      ████████████████████ 17/17 ✅
Phase 3 (Infrastructure):    ████████████████████  5/5  ✅ (MỚI)
Phase 4 (Security):          ████████████████████  6/6  ✅ (MỚI)
Phase 5 (Memory Audit):      ████████████████████  8/8  ✅ (MỚI)
Phase 6 (STUB Audit):        ████████████████████ 16/16 ✅ (MỚI)
Phase 7 (Batch System):      ████████████████████  5/5  ✅ (MỚI)
Phase 8 (Sync):              ████████████████████ 10/10 ✅ (MỚI)
Phase 9 (Remaining):         ░░░░░░░░░░░░░░░░░░░░  0/13
```

**Tổng: 83/96 tasks completed (86%) — 13 tasks low priority còn lại**
