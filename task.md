# OmniGPU — Compute-First Task List

> **Định hướng:** Compute là tương lai. Render 3D qua TCP quá tốn kém (encode/decode video, latency). Compute workloads (ML, science, CI/CD) fit hoàn hảo với TCP forwarding vì: small commands, batch-friendly, no video needed.

---

## ✅ DONE (Phase 1 — Core Rendering Pipeline)

- [x] **1.1** Fix `vkQueueSubmit` handler — gọi submit thật với fence (was: chỉ log "recorded")
- [x] **1.2** Fix `flush_and_readback()` — proper pipeline: end render pass → barrier → copy → submit → wait → readback (was: dùng sai command buffer state)
- [x] **1.3** Fix `renderTargetImage_` tracking — theo dõi từ framebuffer attachment và dynamic rendering color attachment
- [x] **1.4** Fix `vkCmdPipelineBarrier` — forward barriers thật với remapped image/buffer handles (was: barrier rỗng)
- [x] **1.5** Fix `vkCmdPipelineBarrier2` — forward sync2 barriers thật (was: barrier rỗng)
- [x] **1.6** Fix `ResourceMapper::cleanup()` — destroy đúng thứ tự phụ thuộc (image views trước images, memory cuối cùng)
- [x] **1.7** Fix `vkQueueSubmit2` — implementation đầy đủ với remap handles (was: STUB)
- [x] **1.8** Fix `vkCmdBeginRenderPass` serializer mismatch — custom `read_VkRenderPassBeginInfo` thay vì `read_raw` (was: data corruption)
- [x] **1.9** Fix `vkCmdBeginRenderPass2` — proper read + forward (was: skip với wrong size)
- [x] **1.10** Fix `vkCmdWaitEvents` — forward barriers thật (was: null barriers)
- [x] **1.11** Fix `vkCmdWaitEvents2` — forward dependency infos (was: skip)
- [x] **1.12** Fix `vkCmdSetVertexInputEXT` — load function pointer via `vkGetDeviceProcAddr` (was: STUB)
- [x] **1.13** Fix dispatchable handle structs — đủ kích thước cho loader dispatch table (was: heap corruption, 8 bytes cho struct mà loader cần 16+)
- [x] **1.14** Fix `vkAllocateCommandBuffers` + `vkFreeCommandBuffers` — forward đến host (was: chỉ tạo fake handle local)
- [x] **1.15** Fix `vkBeginCommandBuffer` + `vkEndCommandBuffer` + `vkResetCommandBuffer` — forward đến host (was: no-op)
- [x] **1.16** Fix host `vkAllocateCommandBuffers` handler — store ALL command buffer mappings (was: chỉ store cái đầu tiên)

---

## 🚨 PHASE 2 — Compute Path (CRITICAL — ĐANG LÀM)

### Buffer Management (tối ưu cho compute workloads)

- [ ] **2.1** Fix `vkMapMemory` — tracking dirty pages để chỉ gửi thay đổi qua TCP (hiện tại gửi toàn bộ)
- [ ] **2.2** `sync_all_mapped_memory_to_host()` — optimize: chỉ gửi ranges đã flush, không gửi tất cả mapped memory mỗi submit
- [ ] **2.3** Staging buffer pattern — nếu app dùng HOST_VISIBLE + HOST_COHERENT, gửi data qua TCP rồi host copy sang DEVICE_LOCAL
- [ ] **2.4** Async buffer readback — flag để host gửi kết quả compute về mà không cần render pass

### Compute Pipeline (kiểm tra + sửa)

- [ ] **2.5** Verify `vkCmdDispatch` hoạt động end-to-end (guest → host → GPU → readback). Test: `vk_compute_test`
- [ ] **2.6** Verify `vkCmdDispatchIndirect` (cần indirect buffer sync)
- [ ] **2.7** Verify compute pipeline creation + specialization constants
- [ ] **2.8** Verify `vkCmdPushConstants` cho compute shaders
- [ ] **2.9** Verify multiple descriptor sets + update descriptor sets
- [ ] **2.10** Verify `vkGetBufferDeviceAddress` sync query rounds trip correctly

### Physical Device Info Fix (compute-critical)

- [ ] **2.11** `vkGetPhysicalDeviceFeatures` — KHÔNG trả về VK_TRUE cho features GPU thật không support. App sẽ enable feature không có → host device creation fail
- [ ] **2.12** `vkGetPhysicalDeviceMemoryProperties` — fake memory types không khớp GPU thật. Compute apps cần biết memory types chính xác
- [ ] **2.13** `vkGetBufferMemoryRequirements` / `vkGetImageMemoryRequirements` — hardcode sai kích thước. Cần sync từ host
- [ ] **2.14** `vkGetPhysicalDeviceQueueFamilyProperties` — chỉ trả về 1 queue family. Compute có thể cần compute-dedicated queue riêng

### Compute Test nâng cao

- [ ] **2.15** Thêm benchmark vào `vk_compute_test`: throughput measurement (GFLOPS), latency percentiles
- [ ] **2.16** Thêm test: large buffer (1GB+) upload + dispatch + readback để test memory bandwidth
- [ ] **2.17** Thêm workload test: reduction, prefix sum, FFT (compute shaders)

---

## 🟡 PHASE 3 — Stability & Performance

### Crash Fixes

- [ ] **3.1** Static initializer order — `ManualHookRegistrar` chạy trong CRT init, có thể race với DllMain
- [ ] **3.2** Thread safety — `g_swapchain_images`, `s_mapped_ptrs`, `s_memory_sizes` dùng mutex riêng → potential deadlock nếu nested
- [ ] **3.3** `shutdown_guest()` — delete batch trước khi disconnect client → pending data mất
- [ ] **3.4** `connect_to_host()` — `std::call_once` không cho phép retry nếu host down
- [ ] **3.5** Host include `../guest/vulkan_serializer.h` — dependency ngược, cần refactor

### Performance

- [ ] **3.6** Pool fences/command buffers — tránh tạo/destroy mỗi frame (hiện tại tạo fence mới mỗi lần submit)
- [ ] **3.7** Batch flush optimization — không gửi từng command riêng lẻ, gửi batch
- [ ] **3.8** TCP buffer tuning — tăng send/receive buffer sizes cho compute workloads
- [ ] **3.9** Compression cho buffer upload — nếu data compressible (model weights), dùng LZ4

---

## 🟢 PHASE 4 — Compute Features

### MUST HAVE

- [ ] **4.1** `vkCmdCopyBuffer` — verify hoạt động (cần thiết cho buffer management)
- [ ] **4.2** `vkCmdCopyBufferToImage` / `vkCmdCopyImageToBuffer` — fix từ STUB thành real (cho ML tensor ops)
- [ ] **4.3** `vkCmdFillBuffer` / `vkCmdUpdateBuffer` — verify
- [ ] **4.4** `vkCmdCopyQueryPoolResults` — fix từ STUB (cần cho profiling)
- [ ] **4.5** `vkCmdWriteTimestamp` / `vkCmdWriteTimestamp2` — verify (cần cho GPU timing)

### NICE TO HAVE

- [ ] **4.6** `vkCmdExecuteCommands` — secondary command buffers cho multi-dispatch
- [ ] **4.7** `VK_KHR_buffer_device_address` — full test và verify
- [ ] **4.8** Timeline semaphores — cho async compute

---

## 🔵 PHASE 5 — Rendering (Secondary Path)

> Render 3D qua TCP không phải ưu tiên. Chỉ làm khi compute ổn định.

- [ ] **5.1** Video encoder pipeline cleanup — hiện tại nhiều code path (FFmpeg, fallback JPEG)
- [ ] **5.2** Swapchain rework — fake swapchain images cần backing store thật trên host
- [ ] **5.3** Multi-GPU tiled rendering — hiện tại sequential, không true parallel
- [ ] **5.4** Remove redundant renderer code — `CommandDispatcher::setup_framebuffer()` duplicate với `Renderer`

---

## 🟣 PHASE 6 — Testing & Quality

- [ ] **6.1** End-to-end compute test: guest process → ICD → host → GPU → readback → verify
- [ ] **6.2** Benchmark suite: latency histograms, throughput by dispatch size, buffer transfer speed
- [ ] **6.3** Stress test: 10000+ dispatches, large buffers (1GB+), many pipelines
- [ ] **6.4** Memory leak check: create/destroy resources in loop, verify no leak
- [ ] **6.5** Protocol versioning — nếu guest và host build khác nhau, detect và báo lỗi

---

## PROGRESS

```
Phase 1 (Core Pipeline):   ████████████████░░░░  16/16 ✅
Phase 2 (Compute Path):    ██░░░░░░░░░░░░░░░░░░  1/17
Phase 3 (Stability):       ░░░░░░░░░░░░░░░░░░░░  0/9
Phase 4 (Features):        ░░░░░░░░░░░░░░░░░░░░  0/8
Phase 5 (Rendering):       ░░░░░░░░░░░░░░░░░░░░  0/0
Phase 6 (Testing):         ░░░░░░░░░░░░░░░░░░░░  0/5
```

**Tổng: 16/55 tasks completed (29%)**
