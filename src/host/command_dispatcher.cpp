#include "command_dispatcher.h"
#include "vulkan_struct_deserializer.h"
#include "omnigpu_protocol_generated.h"
#include "../guest/vulkan_serializer.h"
#include <atomic>
#include <spdlog/spdlog.h>
#include <cstring>

namespace omnigpu::host {

// ---------------------------------------------------------------------------
// CommandDispatcher
// ---------------------------------------------------------------------------

CommandDispatcher::CommandDispatcher() {
    // Handlers are registered in the constructor
    #define REGISTER(id, fn) handlers_[static_cast<int>(id)] = fn

    // --- Queue ---
    REGISTER(fbs::FunctionId_vkQueueSubmit, [](auto& d, auto& r) {
        VkDevice dev = d.mapper_.device();
        VkQueue q = d.mapper_.queue();
        r.read_handle(); // queue handle (ignored, use ours)
        uint32_t count = r.read_u32();

        std::vector<VkSubmitInfo> submits(count);
        for (uint32_t i = 0; i < count; i++) {
            if (!read_VkSubmitInfo(r, &submits[i])) break;
            auto& si = submits[i];
            for (uint32_t j = 0; j < si.commandBufferCount; j++)
                const_cast<VkCommandBuffer*>(si.pCommandBuffers)[j] =
                    d.mapper_.get_command_buffer(handle_to_u64(si.pCommandBuffers[j]));
            for (uint32_t j = 0; j < si.waitSemaphoreCount; j++)
                const_cast<VkSemaphore*>(si.pWaitSemaphores)[j] =
                    d.mapper_.get_semaphore(handle_to_u64(si.pWaitSemaphores[j]));
            for (uint32_t j = 0; j < si.signalSemaphoreCount; j++)
                const_cast<VkSemaphore*>(si.pSignalSemaphores)[j] =
                    d.mapper_.get_semaphore(handle_to_u64(si.pSignalSemaphores[j]));
        }

        uint64_t guestFence = r.read_handle();
        VkFence fence = d.mapper_.get_fence(guestFence);

        if (d.isComputeMode_) {
            // Compute mode: guest manages fences via sync queries — skip host fence
            if (fence == VK_NULL_HANDLE)
                fence = VK_NULL_HANDLE;  // no host fence needed
            d.hasPendingSubmit_ = false;

            // Flush GPU caches after staging uploads (aliased buffer writes).
            // Submit a global memory barrier to transition from TRANSFER to
            // COMPUTE_SHADER access, ensuring all staging writes are visible
            // to subsequent shader dispatches.
            if (d.needsQueueDrain_) {
                VkCommandBufferAllocateInfo cbai{};
                cbai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
                cbai.commandPool = d.mapper_.command_pool();
                cbai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
                cbai.commandBufferCount = 1;
                VkCommandBuffer barrierCB = VK_NULL_HANDLE;
                if (vkAllocateCommandBuffers(dev, &cbai, &barrierCB) == VK_SUCCESS) {
                    VkCommandBufferBeginInfo bi{};
                    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
                    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
                    vkBeginCommandBuffer(barrierCB, &bi);
                    VkMemoryBarrier mb{};
                    mb.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
                    mb.srcAccessMask = VK_ACCESS_HOST_WRITE_BIT;
                    mb.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_MEMORY_READ_BIT;
                    vkCmdPipelineBarrier(barrierCB,
                        VK_PIPELINE_STAGE_HOST_BIT,
                        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                        0, 1, &mb, 0, nullptr, 0, nullptr);
                    vkEndCommandBuffer(barrierCB);
                    VkSubmitInfo si2{};
                    si2.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
                    si2.commandBufferCount = 1;
                    si2.pCommandBuffers = &barrierCB;
                    VkFence barrierFence = VK_NULL_HANDLE;
                    VkFenceCreateInfo fci2{};
                    fci2.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
                    vkCreateFence(dev, &fci2, nullptr, &barrierFence);
                    vkQueueSubmit(q, 1, &si2, barrierFence);
                    vkWaitForFences(dev, 1, &barrierFence, VK_TRUE, UINT64_MAX);
                    vkDestroyFence(dev, barrierFence, nullptr);
                    vkFreeCommandBuffers(dev, d.mapper_.command_pool(), 1, &barrierCB);
                    SPDLOG_INFO("vkQueueSubmit: GPU barrier submitted for staging uploads");
                }
                d.needsQueueDrain_ = false;
            }
        } else {
            if (fence == VK_NULL_HANDLE) {
                VkFenceCreateInfo fci{};
                fci.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
                vkCreateFence(dev, &fci, nullptr, &fence);
            }
        }

        VkResult res = vkQueueSubmit(q, count, submits.data(), fence);
        if (res == VK_SUCCESS) {
            if (!d.isComputeMode_) {
                d.pendingSubmitFence_ = fence;
                d.pendingSubmitFenceGuestHandle_ = guestFence;
                d.hasPendingSubmit_ = true;
            }
            SPDLOG_DEBUG("  vkQueueSubmit ({} submits) -> submitted, fence={}",
                         count, (void*)fence);
        } else {
            SPDLOG_ERROR("  vkQueueSubmit failed: {}", static_cast<int>(res));
            if (!d.isComputeMode_ && fence != VK_NULL_HANDLE)
                vkDestroyFence(dev, fence, nullptr);
        }

        for (uint32_t i = 0; i < count; i++)
            free_VkSubmitInfo(&submits[i]);
    });
    REGISTER(fbs::FunctionId_vkDeviceWaitIdle, [](auto& d, auto& r) {
        r.read_handle(); // device
        vkDeviceWaitIdle(d.mapper_.device());
    });
    REGISTER(fbs::FunctionId_vkQueueWaitIdle, [](auto& d, auto& r) {
        r.read_handle(); // queue
        vkQueueWaitIdle(d.mapper_.queue());
    });

    // --- Command Pool ---
    REGISTER(fbs::FunctionId_vkCreateCommandPool, [](auto& d, auto& r) {
        r.read_handle(); // device
        VkCommandPoolCreateInfo ci{};
        read_VkCommandPoolCreateInfo(r, &ci);
        r.skip(sizeof(VkAllocationCallbacks));
        uint64_t pPool = r.read_handle();
        VkCommandPool pool;
        if (vkCreateCommandPool(d.mapper_.device(), &ci, nullptr, &pool) == VK_SUCCESS) {
            d.mapper_.store_command_pool(pPool, pool);
        }
    });
    REGISTER(fbs::FunctionId_vkDestroyCommandPool, [](auto& d, auto& r) {
        r.read_handle(); // device
        uint64_t pool = r.read_handle();
        r.skip(sizeof(VkAllocationCallbacks));
        auto p = d.mapper_.get_command_pool(pool);
        if (p) vkDestroyCommandPool(d.mapper_.device(), p, nullptr);
        d.mapper_.remove_command_pool(pool);
    });
    REGISTER(fbs::FunctionId_vkResetCommandPool, [](auto& d, auto& r) {
        r.read_handle(); // device
        uint64_t pool = r.read_handle();
        auto flags = r.read_u32();
        auto p = d.mapper_.get_command_pool(pool);
        if (p) vkResetCommandPool(d.mapper_.device(), p, static_cast<VkCommandPoolResetFlags>(flags));
    });
    REGISTER(fbs::FunctionId_vkAllocateCommandBuffers, [](auto& d, auto& r) {
        r.read_handle(); // device
        VkCommandBufferAllocateInfo ai{};
        r.read_raw(&ai, sizeof(ai));
        ai.pNext = nullptr;
        ai.commandPool = d.mapper_.get_command_pool(handle_to_u64(ai.commandPool));
        uint32_t count = r.read_u32();
        std::vector<VkCommandBuffer> cbs(count);
        if (count > 0 && vkAllocateCommandBuffers(d.mapper_.device(), &ai, cbs.data()) == VK_SUCCESS) {
            for (uint32_t i = 0; i < count; i++) {
                uint64_t guestHandle = r.read_handle();
                d.mapper_.store_command_buffer(guestHandle, cbs[i]);
            }
        } else {
            for (uint32_t i = 0; i < count; i++) r.read_handle();
        }
    });
    REGISTER(fbs::FunctionId_vkFreeCommandBuffers, [](auto& d, auto& r) {
        r.read_handle(); // device
        uint64_t pool = r.read_handle();
        uint32_t count = r.read_u32();
        auto poolH = d.mapper_.get_command_pool(pool);
        std::vector<VkCommandBuffer> cbs;
        cbs.reserve(count);
        for (uint32_t i = 0; i < count; i++) {
            uint64_t gcb = r.read_handle();
            auto cb = d.mapper_.get_command_buffer(gcb);
            if (cb) cbs.push_back(cb);
            d.mapper_.remove_command_buffer(gcb);
        }
        if (!cbs.empty() && poolH)
            vkFreeCommandBuffers(d.mapper_.device(), poolH, static_cast<uint32_t>(cbs.size()), cbs.data());
    });
    REGISTER(fbs::FunctionId_vkBeginCommandBuffer, [](auto& d, auto& r) {
        uint64_t cb = r.read_handle();
        VkCommandBufferBeginInfo bi{};
        bi.sType = static_cast<VkStructureType>(r.read_u32());
        bi.pNext = nullptr;
        bi.flags = static_cast<VkCommandBufferUsageFlags>(r.read_u32());
        
        bool has_inherit = r.read_bool();
        VkCommandBufferInheritanceInfo inheritInfo{};
        if (has_inherit) {
            inheritInfo.sType = static_cast<VkStructureType>(r.read_u32());
            inheritInfo.pNext = nullptr;
            inheritInfo.subpass = r.read_u32();
            r.read_handle();
            inheritInfo.occlusionQueryEnable = r.read_bool();
            inheritInfo.queryFlags = static_cast<VkQueryControlFlags>(r.read_u32());
            inheritInfo.pipelineStatistics = static_cast<VkQueryPipelineStatisticFlags>(r.read_u32());
            bi.pInheritanceInfo = &inheritInfo;
        } else {
            bi.pInheritanceInfo = nullptr;
        }

        d.mapper_.set_active_cmd(d.mapper_.get_command_buffer(cb));
        auto cmd = d.mapper_.active_cmd();
        if (cmd) vkBeginCommandBuffer(cmd, &bi);
    });
    REGISTER(fbs::FunctionId_vkEndCommandBuffer, [](auto& d, auto& r) {
        r.read_handle(); // command buffer
        auto cmd = d.mapper_.active_cmd();
        if (cmd) vkEndCommandBuffer(cmd);
    });
    REGISTER(fbs::FunctionId_vkResetCommandBuffer, [](auto& d, auto& r) {
        uint64_t cb = r.read_handle();
        auto flags = r.read_u32();
        auto cmd = d.mapper_.get_command_buffer(cb);
        if (cmd) vkResetCommandBuffer(cmd, static_cast<VkCommandBufferResetFlags>(flags));
    });

    // --- Memory ---
    REGISTER(fbs::FunctionId_vkAllocateMemory, [](auto& d, auto& r) {
        r.read_handle(); // device
        VkMemoryAllocateInfo ai{};
        read_VkMemoryAllocateInfo(r, &ai);
        if (r.read_bool()) { r.skip(sizeof(VkAllocationCallbacks)); }
        uint64_t pMem = r.read_handle();
        uint32_t guestType = ai.memoryTypeIndex;

        // Dump all real GPU memory types on first allocation
        static bool s_dumped_mem_types = false;
        VkPhysicalDeviceMemoryProperties memProps{};
        vkGetPhysicalDeviceMemoryProperties(d.phys_device(), &memProps);
        if (!s_dumped_mem_types) {
            s_dumped_mem_types = true;
            SPDLOG_INFO("=== Host GPU memory types ({}) ===", memProps.memoryTypeCount);
            for (uint32_t j = 0; j < memProps.memoryTypeCount; j++) {
                auto f = memProps.memoryTypes[j].propertyFlags;
                SPDLOG_INFO("  type[{}]: heap={} flags=0x{:x} {} {} {} {} {}",
                    j, memProps.memoryTypes[j].heapIndex, f,
                    (f & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) ? "DEVICE_LOCAL" : "",
                    (f & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) ? "HOST_VISIBLE" : "",
                    (f & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) ? "HOST_COHERENT" : "",
                    (f & VK_MEMORY_PROPERTY_HOST_CACHED_BIT) ? "HOST_CACHED" : "",
                    (f & VK_MEMORY_PROPERTY_LAZILY_ALLOCATED_BIT) ? "LAZY" : "");
            }
            for (uint32_t j = 0; j < memProps.memoryHeapCount; j++) {
                SPDLOG_INFO("  heap[{}]: size={}MB flags=0x{:x} {}",
                    j, memProps.memoryHeaps[j].size / (1024*1024),
                    memProps.memoryHeaps[j].flags,
                    (memProps.memoryHeaps[j].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) ? "DEVICE_LOCAL" : "");
            }
        }

        // Dynamic memory type remapping based on property flags
        VkMemoryPropertyFlags desiredFlags = 0;
        if (guestType == 0) {
            desiredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
        } else if (guestType == 1) {
            desiredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
        } else if (guestType == 2) {
            desiredFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
        } else if (guestType == 3) {
            desiredFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT;
        } else if (guestType == 4) {
            desiredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT;
        }

        std::vector<uint32_t> candidateTypes;
        // FORCE HOST_VISIBLE for all buffers to avoid VRAM staging/aliasing bugs
        bool preferHostVisible = true; // (ai.allocationSize <= 256ULL * 1024 * 1024);

        if (preferHostVisible) {
            // Small buffer: prefer types with HOST_VISIBLE
            for (uint32_t t = 0; t < memProps.memoryTypeCount; t++) {
                if ((memProps.memoryTypes[t].propertyFlags & desiredFlags) == desiredFlags &&
                    (memProps.memoryTypes[t].propertyFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)) {
                    candidateTypes.push_back(t);
                }
            }
            // Then try non-host-visible exact matches
            for (uint32_t t = 0; t < memProps.memoryTypeCount; t++) {
                if ((memProps.memoryTypes[t].propertyFlags & desiredFlags) == desiredFlags &&
                    !(memProps.memoryTypes[t].propertyFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)) {
                    candidateTypes.push_back(t);
                }
            }
        } else {
            for (uint32_t t = 0; t < memProps.memoryTypeCount; t++) {
                if ((memProps.memoryTypes[t].propertyFlags & desiredFlags) == desiredFlags) {
                    candidateTypes.push_back(t);
                }
            }
        }
        if (desiredFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) {
            // Try HOST_VISIBLE types first (system RAM) for large buffers
            // before falling back to pure VRAM (non-host-visible)
            if (preferHostVisible) {
                for (uint32_t t = 0; t < memProps.memoryTypeCount; t++) {
                    if (!(memProps.memoryTypes[t].propertyFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)) continue;
                    if (std::find(candidateTypes.begin(), candidateTypes.end(), t) == candidateTypes.end())
                        candidateTypes.push_back(t);
                }
            }
            for (uint32_t t = 0; t < memProps.memoryTypeCount; t++) {
                if (!(memProps.memoryTypes[t].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)) continue;
                if (std::find(candidateTypes.begin(), candidateTypes.end(), t) == candidateTypes.end())
                    candidateTypes.push_back(t);
            }
        }
        for (uint32_t t = 0; t < memProps.memoryTypeCount; t++) {
            if (!(memProps.memoryTypes[t].propertyFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)) continue;
            if (std::find(candidateTypes.begin(), candidateTypes.end(), t) == candidateTypes.end())
                candidateTypes.push_back(t);
        }

        VkDeviceMemory mem = VK_NULL_HANDLE;
        VkResult res = VK_ERROR_OUT_OF_DEVICE_MEMORY;
        for (auto& mt : candidateTypes) {
            ai.memoryTypeIndex = mt;
            VkDeviceMemory trialMem = VK_NULL_HANDLE;
            res = vkAllocateMemory(d.mapper_.device(), &ai, nullptr, &trialMem);
            if (res == VK_SUCCESS) {
                mem = trialMem;
                break;
            }
            SPDLOG_WARN("vkAllocateMemory: type={} failed (size={}MB, res={})",
                        mt, ai.allocationSize / (1024*1024), static_cast<int>(res));
        }
        if (res == VK_SUCCESS) {
            d.mapper_.store_device_memory(pMem, mem);
            d.vramUsed_ += ai.allocationSize;
            d.memorySizes_[pMem] = ai.allocationSize;
        } else {
            SPDLOG_ERROR("vkAllocateMemory host failed: size={}MB guestType={} res={}",
                         ai.allocationSize / (1024*1024), guestType, static_cast<int>(res));
        }
    });
    REGISTER(fbs::FunctionId_vkFreeMemory, [](auto& d, auto& r) {
        VkDevice dev = d.mapper_.device();
        r.read_handle(); // device
        uint64_t gMem = r.read_handle();
        // Read pAllocator: bool + optional struct bytes
        if (r.read_bool()) {
            r.skip(sizeof(VkAllocationCallbacks));
        }
        VkDeviceMemory hostMem = d.mapper_.get_device_memory(gMem);
        if (hostMem != VK_NULL_HANDLE) {
            vkFreeMemory(dev, hostMem, nullptr);
            d.mapper_.remove_device_memory(gMem);
            auto it = d.memorySizes_.find(gMem);
            if (it != d.memorySizes_.end()) {
                uint64_t size = it->second;
                d.vramUsed_ = d.vramUsed_ >= size ? d.vramUsed_ - size : 0;
                d.memorySizes_.erase(it);
            }
        } else {
            SPDLOG_WARN("vkFreeMemory: handle {:#x} not found (double-free?)", gMem);
        }
    });
    REGISTER(fbs::FunctionId_vkMapMemory, [](auto& d, auto& r) {
        r.read_handle(); r.read_handle(); r.read_u64(); r.read_u64(); r.read_u32();
        r.read_handle(); // ppData (output pointer)
    });
    REGISTER(fbs::FunctionId_vkUnmapMemory, [](auto& d, auto& r) {
        r.read_handle(); r.read_handle();
    });
    REGISTER(fbs::FunctionId_vkFlushMappedMemoryRanges, [](auto& d, auto& r) {
        VkDevice dev = d.mapper_.device();
        r.read_handle(); // device
        uint32_t count = r.read_u32();
        for (uint32_t i = 0; i < count; i++) {
            uint64_t gMem = r.read_handle();
            uint64_t offset = r.read_u64();
            uint64_t size = r.read_u64();
            size_t sz = static_cast<size_t>(size);

            std::vector<uint8_t> data(sz);
            if (sz > 0) {
                r.read_raw(data.data(), sz);
            }

            VkDeviceMemory hostMem = d.mapper_.get_device_memory(gMem);
            if (hostMem != VK_NULL_HANDLE && size > 0) {
                void* mapped = nullptr;
                VkResult res = vkMapMemory(dev, hostMem, offset, size, 0, &mapped);
                if (res == VK_SUCCESS && mapped) {
                    std::memcpy(mapped, data.data(), sz);
                    // Explicitly flush CPU writes to make them visible to GPU
                    VkMappedMemoryRange flushRange{};
                    flushRange.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
                    flushRange.memory = hostMem;
                    flushRange.offset = offset;
                    flushRange.size = size;
                    vkFlushMappedMemoryRanges(dev, 1, &flushRange);
                    // Data integrity: log first 16B of first chunk for model weights
                    if (offset == 0 && size >= 16 && size > 512*1024) {
                        auto szIt = d.memorySizes_.find(gMem);
                        uint64_t total = szIt != d.memorySizes_.end() ? szIt->second : 0;
                        if (total > 100ULL * 1024 * 1024) {
                            SPDLOG_INFO("MODEL_UPLOAD chunk0: mem={:#x} total={}MB first16={:02x} {:02x} {:02x} {:02x} {:02x} {:02x} {:02x} {:02x} {:02x} {:02x} {:02x} {:02x} {:02x} {:02x} {:02x} {:02x}",
                                gMem, total/(1024*1024),
                                data[0], data[1], data[2], data[3], data[4], data[5], data[6], data[7],
                                data[8], data[9], data[10], data[11], data[12], data[13], data[14], data[15]);
                        }
                    }
                    vkUnmapMemory(dev, hostMem);
                } else {
                    // Non-host-visible memory (pure VRAM) — use memory aliasing.
                    if (!d.upload_to_device_memory(hostMem, offset, data.data(), sz)) {
                        SPDLOG_WARN("FlushMappedMemory: upload_to_device_memory failed for mem={:#x} off={}", gMem, offset);
                    } else {
                        d.needsQueueDrain_ = true;
                    }
                }
            }
        }
    });
    REGISTER(fbs::FunctionId_vkInvalidateMappedMemoryRanges, [](auto& d, auto& r) {
        VkDevice dev = d.mapper_.device();
        r.read_handle(); // device
        uint32_t count = r.read_u32();
        for (uint32_t i = 0; i < count; i++) {
            uint64_t gMem = r.read_handle();
            uint64_t offset = r.read_u64();
            uint64_t size = r.read_u64();
            d.invalidate_and_send_memory(gMem, offset, size);
        }
    });
    REGISTER(fbs::FunctionId_vkBindBufferMemory, [](auto& d, auto& r) {
        r.read_handle();
        auto buf = r.read_handle(); auto mem = r.read_handle(); auto off = r.read_u64();
        auto b = d.mapper_.get_buffer(buf);
        auto m = d.mapper_.get_device_memory(mem);
        if (b && m) {
            vkBindBufferMemory(d.mapper_.device(), b, m, off);
            // Track first buffer bound to this memory (for VRAM staging uploads)
            if (d.memoryToBuffer_.find(mem) == d.memoryToBuffer_.end()) {
                d.memoryToBuffer_[mem] = buf;
            }
            // Cache the real GPU address after binding
            VkBufferDeviceAddressInfo bdai{};
            bdai.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
            bdai.buffer = b;
            uint64_t addr = vkGetBufferDeviceAddress(d.mapper_.device(), &bdai);
            d.bufferAddresses_[buf] = addr;
            SPDLOG_DEBUG("vkBindBufferMemory: cached BDA guest={:#x} hostBuf={} addr={:#x}",
                buf, (void*)b, addr);
        }
    });
    REGISTER(fbs::FunctionId_vkBindImageMemory, [](auto& d, auto& r) {
        r.read_handle();
        auto img = r.read_handle(); auto mem = r.read_handle(); auto off = r.read_u64();
        auto i = d.mapper_.get_image(img);
        auto m = d.mapper_.get_device_memory(mem);
        if (i && m) vkBindImageMemory(d.mapper_.device(), i, m, off);
    });

    // --- Buffer ---
    REGISTER(fbs::FunctionId_vkCreateBuffer, [](auto& d, auto& r) {
        r.read_handle(); // device
        VkBufferCreateInfo ci{};
        read_VkBufferCreateInfo(r, &ci);
        // Add TRANSFER_DST to allow direct staging uploads via vkCmdCopyBuffer,
        // avoiding aliased-buffer GPU cache coherence issues
        VkBufferUsageFlags orig = ci.usage;
        ci.usage |= VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        if (ci.usage != orig) {
            SPDLOG_DEBUG("vkCreateBuffer: added TRANSFER_DST (orig=0x{:x} new=0x{:x})", orig, ci.usage);
        }
        r.skip(sizeof(VkAllocationCallbacks));
        uint64_t pBuf = r.read_handle();
        VkBuffer buf;
        if (vkCreateBuffer(d.mapper_.device(), &ci, nullptr, &buf) == VK_SUCCESS) {
            d.mapper_.store_buffer(pBuf, buf);
        } else {
            SPDLOG_ERROR("vkCreateBuffer failed on host: size={} usage=0x{:x}", ci.size, ci.usage);
        }
        delete[] ci.pQueueFamilyIndices;
    });
    REGISTER(fbs::FunctionId_vkDestroyBuffer, [](auto& d, auto& r) {
        r.read_handle(); auto buf = r.read_handle(); r.skip(sizeof(VkAllocationCallbacks));
        auto b = d.mapper_.get_buffer(buf);
        if (b) vkDestroyBuffer(d.mapper_.device(), b, nullptr);
        d.mapper_.remove_buffer(buf);
        d.bufferAddresses_.erase(buf);
    });
    REGISTER(fbs::FunctionId_vkCreateBufferView, [](auto& d, auto& r) {
        r.read_handle(); // device
        VkBufferViewCreateInfo ci{};
        r.read_raw(&ci, sizeof(ci));
        ci.pNext = nullptr;
        ci.buffer = d.mapper_.get_buffer(handle_to_u64(ci.buffer));
        if (r.read_bool()) r.skip(sizeof(VkAllocationCallbacks));
        uint64_t pView = r.read_handle();
        VkBufferView view;
        if (ci.buffer && vkCreateBufferView(d.mapper_.device(), &ci, nullptr, &view) == VK_SUCCESS) {
            d.mapper_.store_buffer_view(pView, view);
        }
    });
    REGISTER(fbs::FunctionId_vkDestroyBufferView, [](auto& d, auto& r) {
        r.read_handle(); uint64_t gView = r.read_handle(); r.skip(sizeof(VkAllocationCallbacks));
        auto view = d.mapper_.get_buffer_view(gView);
        if (view) vkDestroyBufferView(d.mapper_.device(), view, nullptr);
        d.mapper_.remove_buffer_view(gView);
    });

    // --- Image ---
    REGISTER(fbs::FunctionId_vkCreateImage, [](auto& d, auto& r) {
        r.read_handle();
        VkImageCreateInfo ci{};
        read_VkImageCreateInfo(r, &ci);
        r.skip(sizeof(VkAllocationCallbacks));
        uint64_t pImg = r.read_handle();
        VkImage img;
        // Skip images with DEPTH_STENCIL usage — format numbers differ between
        // guest Vulkan headers (1.3) and host (1.4), causing the host to
        // misinterpret the format and crash the NVIDIA driver (divide-by-zero).
        if (ci.usage & VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT) {
            SPDLOG_WARN("vkCreateImage: skipping DEPTH_STENCIL image (fmt={} usage={:#x}) - Vulkan version mismatch workaround",
                        static_cast<int>(ci.format), ci.usage);
        } else if (vkCreateImage(d.mapper_.device(), &ci, nullptr, &img) == VK_SUCCESS) {
            d.mapper_.store_image(pImg, img);
        }
        delete[] ci.pQueueFamilyIndices;
    });
    REGISTER(fbs::FunctionId_vkDestroyImage, [](auto& d, auto& r) {
        r.read_handle(); auto img = r.read_handle(); r.skip(sizeof(VkAllocationCallbacks));
        auto i = d.mapper_.get_image(img);
        if (i) vkDestroyImage(d.mapper_.device(), i, nullptr);
        d.mapper_.remove_image(img);
    });
    REGISTER(fbs::FunctionId_vkCreateImageView, [](auto& d, auto& r) {
        r.read_handle();
        VkImageViewCreateInfo ci{};
        read_VkImageViewCreateInfo(r, &ci);
        r.skip(sizeof(VkAllocationCallbacks));
        uint64_t pView = r.read_handle();
        uint64_t gImg = handle_to_u64(ci.image);
        VkImage hostImg = d.mapper_.get_image(gImg);
        if (hostImg == VK_NULL_HANDLE) {
            SPDLOG_WARN("vkCreateImageView: cannot resolve image handle {:#x}", gImg);
        }
        ci.image = hostImg;
        VkImageView view;
        if (vkCreateImageView(d.mapper_.device(), &ci, nullptr, &view) == VK_SUCCESS) {
            d.mapper_.store_image_view(pView, view);
            d.mapper_.store_view_image(pView, ci.image);
        }
    });
    REGISTER(fbs::FunctionId_vkDestroyImageView, [](auto& d, auto& r) {
        r.read_handle(); auto v = r.read_handle(); r.skip(sizeof(VkAllocationCallbacks));
        auto view = d.mapper_.get_image_view(v);
        if (view) vkDestroyImageView(d.mapper_.device(), view, nullptr);
        d.mapper_.remove_image_view(v);
    });

    // --- Sampler ---
    REGISTER(fbs::FunctionId_vkCreateSampler, [](auto& d, auto& r) {
        r.read_handle();
        VkSamplerCreateInfo ci{};
        read_VkSamplerCreateInfo(r, &ci);
        r.skip(sizeof(VkAllocationCallbacks));
        uint64_t pSamp = r.read_handle();
        VkSampler samp;
        if (vkCreateSampler(d.mapper_.device(), &ci, nullptr, &samp) == VK_SUCCESS) {
            d.mapper_.store_sampler(pSamp, samp);
        }
    });
    REGISTER(fbs::FunctionId_vkDestroySampler, [](auto& d, auto& r) {
        r.read_handle(); auto s = r.read_handle(); r.skip(sizeof(VkAllocationCallbacks));
        auto samp = d.mapper_.get_sampler(s);
        if (samp) vkDestroySampler(d.mapper_.device(), samp, nullptr);
        d.mapper_.remove_sampler(s);
    });

    // --- Shader Module ---
    REGISTER(fbs::FunctionId_vkCreateShaderModule, [](auto& d, auto& r) {
        r.read_handle();
        // Custom serialization: flags + codeSize + pCode data
        VkShaderModuleCreateInfo ci{};
        ci.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        ci.flags = r.read_u32();
        ci.codeSize = static_cast<size_t>(r.read_u64());
        std::vector<uint32_t> spirv;
        if (ci.codeSize > 0) {
            spirv.resize((ci.codeSize + 3) / 4, 0);
            r.read_raw(spirv.data(), static_cast<size_t>(ci.codeSize));
            ci.pCode = spirv.data();
        }
        r.skip(sizeof(VkAllocationCallbacks));
        uint64_t pSM = r.read_handle();
        VkShaderModule sm;
        VkResult smRes = vkCreateShaderModule(d.mapper_.device(), &ci, nullptr, &sm);
        if (smRes == VK_SUCCESS) {
            d.mapper_.store_shader_module(pSM, sm);
        } else {
            SPDLOG_ERROR("vkCreateShaderModule failed: codeSize={} res={}", ci.codeSize, static_cast<int>(smRes));
        }
    });
    REGISTER(fbs::FunctionId_vkDestroyShaderModule, [](auto& d, auto& r) {
        r.read_handle(); auto s = r.read_handle(); r.skip(sizeof(VkAllocationCallbacks));
        auto sm = d.mapper_.get_shader_module(s);
        if (sm) vkDestroyShaderModule(d.mapper_.device(), sm, nullptr);
        d.mapper_.remove_shader_module(s);
    });

    // --- Pipeline Layout ---
    REGISTER(fbs::FunctionId_vkCreatePipelineLayout, [](auto& d, auto& r) {
        r.read_handle();
        VkPipelineLayoutCreateInfo ci{};
        read_VkPipelineLayoutCreateInfo(r, &ci);
        // Remap descriptor set layout handles (guest → host)
        for (uint32_t i = 0; i < ci.setLayoutCount; i++)
            const_cast<VkDescriptorSetLayout*>(ci.pSetLayouts)[i] =
                d.mapper_.get_dsl(handle_to_u64(ci.pSetLayouts[i]));
        r.skip(sizeof(VkAllocationCallbacks));
        uint64_t pPL = r.read_handle();
        VkPipelineLayout pl;
        if (vkCreatePipelineLayout(d.mapper_.device(), &ci, nullptr, &pl) == VK_SUCCESS) {
            d.mapper_.store_pipeline_layout(pPL, pl);
        }
        free_VkPipelineLayoutCreateInfo(&ci);
    });
    REGISTER(fbs::FunctionId_vkDestroyPipelineLayout, [](auto& d, auto& r) {
        r.read_handle(); auto pl = r.read_handle(); r.skip(sizeof(VkAllocationCallbacks));
        auto p = d.mapper_.get_pipeline_layout(pl);
        if (p) vkDestroyPipelineLayout(d.mapper_.device(), p, nullptr);
        d.mapper_.remove_pipeline_layout(pl);
    });

    // --- Compute pipelines ---
    REGISTER(fbs::FunctionId_vkCreateComputePipelines, [](auto& d, auto& r) {
        auto dev = d.mapper_.device();
        r.read_handle(); uint64_t pCache = r.read_handle();
        VkPipelineCache cache = d.mapper_.get_pipeline_cache(pCache);
        uint32_t count = r.read_u32();

        std::vector<VkComputePipelineCreateInfo> infos(count);
        for (uint32_t i = 0; i < count; i++)
            read_VkComputePipelineCreateInfo(r, &infos[i]);

        // Remap guest handles to host handles
        for (uint32_t i = 0; i < count; i++) {
            auto guestModule = (uint64_t)infos[i].stage.module;
            infos[i].stage.module = d.mapper_.get_shader_module(guestModule);

            auto guestLayout = (uint64_t)infos[i].layout;
            infos[i].layout = d.mapper_.get_pipeline_layout(guestLayout);

            auto guestBasePipeline = (uint64_t)infos[i].basePipelineHandle;
            if (guestBasePipeline != 0) {
                infos[i].basePipelineHandle = d.mapper_.get_pipeline(guestBasePipeline);
            }
        }

        r.skip(sizeof(VkAllocationCallbacks));

        std::vector<VkPipeline> pipelines(count);
        VkResult res = vkCreateComputePipelines(dev, cache, count,
                                                 infos.data(), nullptr,
                                                 pipelines.data());
        if (res != VK_SUCCESS) {
            SPDLOG_ERROR("vkCreateComputePipelines failed: count={} res={}", count, static_cast<int>(res));
        }
        uint32_t guestCount2 = r.read_u32();
        for (uint32_t i = 0; i < guestCount2; i++) {
            uint64_t guestPipeline = r.read_handle();
            if (res == VK_SUCCESS && i < count) {
                d.mapper_.store_pipeline(guestPipeline, pipelines[i]);
            }
        }
        for (auto& info : infos)
            free_VkComputePipelineCreateInfo(&info);
        SPDLOG_DEBUG("  vkCreateComputePipelines ({} pipelines) result={}", count, static_cast<int>(res));
    });
    REGISTER(fbs::FunctionId_vkDestroyPipeline, [](auto& d, auto& r) {
        r.read_handle(); auto pp = r.read_handle(); r.skip(sizeof(VkAllocationCallbacks));
        auto p = d.mapper_.get_pipeline(pp);
        if (p) vkDestroyPipeline(d.mapper_.device(), p, nullptr);
        d.mapper_.remove_pipeline(pp);
    });
    REGISTER(fbs::FunctionId_vkCreatePipelineCache, [](auto& d, auto& r) {
        r.read_handle(); r.skip(sizeof(VkPipelineCacheCreateInfo));
        r.skip(sizeof(VkAllocationCallbacks));
        uint64_t pPC = r.read_handle();
        VkPipelineCacheCreateInfo ci{};
        ci.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
        VkPipelineCache pc;
        if (vkCreatePipelineCache(d.mapper_.device(), &ci, nullptr, &pc) == VK_SUCCESS) {
            d.mapper_.store_pipeline_cache(pPC, pc);
        }
    });
    REGISTER(fbs::FunctionId_vkDestroyPipelineCache, [](auto& d, auto& r) {
        r.read_handle(); auto pc = r.read_handle(); r.skip(sizeof(VkAllocationCallbacks));
        auto p = d.mapper_.get_pipeline_cache(pc);
        if (p) vkDestroyPipelineCache(d.mapper_.device(), p, nullptr);
        d.mapper_.remove_pipeline_cache(pc);
    });

    // --- Descriptor Set Layout ---
    REGISTER(fbs::FunctionId_vkCreateDescriptorSetLayout, [](auto& d, auto& r) {
        r.read_handle();
        VkDescriptorSetLayoutCreateInfo ci{};
        read_VkDescriptorSetLayoutCreateInfo(r, &ci);
        // Remap immutable sampler handles
        for (uint32_t b = 0; b < ci.bindingCount; b++) {
            auto* bind = const_cast<VkDescriptorSetLayoutBinding*>(&ci.pBindings[b]);
            if (bind->pImmutableSamplers) {
                for (uint32_t s = 0; s < bind->descriptorCount; s++)
                    const_cast<VkSampler*>(bind->pImmutableSamplers)[s] =
                        d.mapper_.get_sampler(handle_to_u64(bind->pImmutableSamplers[s]));
            }
        }
        r.skip(sizeof(VkAllocationCallbacks));
        uint64_t pDSL = r.read_handle();
        VkDescriptorSetLayout dsl;
        if (vkCreateDescriptorSetLayout(d.mapper_.device(), &ci, nullptr, &dsl) == VK_SUCCESS) {
            d.mapper_.store_descriptor_set_layout(pDSL, dsl);
        }
        free_VkDescriptorSetLayoutCreateInfo(&ci);
    });
    REGISTER(fbs::FunctionId_vkDestroyDescriptorSetLayout, [](auto& d, auto& r) {
        r.read_handle(); auto g = r.read_handle(); r.skip(sizeof(VkAllocationCallbacks));
        auto h = d.mapper_.get_dsl(g);
        if (h) vkDestroyDescriptorSetLayout(d.mapper_.device(), h, nullptr);
        d.mapper_.remove_dsl(g);
    });
    REGISTER(fbs::FunctionId_vkCreateDescriptorPool, [](auto& d, auto& r) {
        r.read_handle();
        VkDescriptorPoolCreateInfo ci{};
        read_VkDescriptorPoolCreateInfo(r, &ci);
        r.skip(sizeof(VkAllocationCallbacks));
        uint64_t pDP = r.read_handle();
        VkDescriptorPool dp;
        if (vkCreateDescriptorPool(d.mapper_.device(), &ci, nullptr, &dp) == VK_SUCCESS) {
            d.mapper_.store_descriptor_pool(pDP, dp);
        }
        delete[] ci.pPoolSizes;
    });
    REGISTER(fbs::FunctionId_vkDestroyDescriptorPool, [](auto& d, auto& r) {
        r.read_handle(); auto dp = r.read_handle(); r.skip(sizeof(VkAllocationCallbacks));
        auto p = d.mapper_.get_dp(dp);
        if (p) vkDestroyDescriptorPool(d.mapper_.device(), p, nullptr);
        d.mapper_.remove_dp(dp);
    });
    REGISTER(fbs::FunctionId_vkAllocateDescriptorSets, [](auto& d, auto& r) {
        r.read_handle();
        VkDescriptorSetAllocateInfo ai{};
        read_VkDescriptorSetAllocateInfo(r, &ai);
        // Remap pool and set layout handles (guest → host)
        ai.descriptorPool = d.mapper_.get_dp(handle_to_u64(ai.descriptorPool));
        for (uint32_t i = 0; i < ai.descriptorSetCount; i++)
            const_cast<VkDescriptorSetLayout*>(ai.pSetLayouts)[i] =
                d.mapper_.get_dsl(handle_to_u64(ai.pSetLayouts[i]));

        std::vector<VkDescriptorSet> ds(ai.descriptorSetCount);
        VkResult res = vkAllocateDescriptorSets(d.mapper_.device(), &ai, ds.data());
        if (res != VK_SUCCESS) {
            SPDLOG_ERROR("vkAllocateDescriptorSets host failed: count={} res={}", ai.descriptorSetCount, static_cast<int>(res));
        }

        uint32_t guestCount = r.read_u32();
        for (uint32_t i = 0; i < guestCount; i++) {
            uint64_t guestDS = r.read_handle();
            if (res == VK_SUCCESS && i < ai.descriptorSetCount) {
                d.mapper_.store_descriptor_set(guestDS, ds[i]);
            }
        }
        delete[] ai.pSetLayouts;
    });
    REGISTER(fbs::FunctionId_vkFreeDescriptorSets, [](auto& d, auto& r) {
        r.read_handle();
        auto dp = r.read_handle();
        uint32_t count = r.read_u32();
        auto v = r.template read_array<uint64_t>(count);
        auto pool = d.mapper_.get_dp(dp);
        for (auto& gds : v) {
            auto ds = d.mapper_.get_ds(gds);
            if (ds && pool) vkFreeDescriptorSets(d.mapper_.device(), pool, 1, &ds);
            d.mapper_.remove_ds(gds);
        }
    });
    REGISTER(fbs::FunctionId_vkUpdateDescriptorSets, [](auto& d, auto& r) {
        r.read_handle(); // device
        uint32_t wc = r.read_u32();
        SPDLOG_TRACE("vkUpdateDescriptorSets: wc={}", wc);
        std::vector<VkWriteDescriptorSet> writes(wc);
        std::vector<VkDescriptorImageInfo*> imgPtrs(wc, nullptr);
        std::vector<VkDescriptorBufferInfo*> bufPtrs(wc, nullptr);
        std::vector<VkBufferView*> viewPtrs(wc, nullptr);
        for (uint32_t i = 0; i < wc; i++) {
            SPDLOG_TRACE("  reading write {}", i);
            read_VkWriteDescriptorSet(r, &writes[i], &imgPtrs[i], &bufPtrs[i], &viewPtrs[i]);
            SPDLOG_TRACE("  write {} ok, dstSet={}, type={}, count={}", i,
                (uint64_t)writes[i].dstSet, (int)writes[i].descriptorType, writes[i].descriptorCount);
        }
        uint32_t cc = r.read_u32();
        SPDLOG_TRACE("vkUpdateDescriptorSets: cc={}", cc);
        std::vector<VkCopyDescriptorSet> copies(cc);
        for (uint32_t i = 0; i < cc; i++) {
            r.read_raw(&copies[i], sizeof(VkCopyDescriptorSet));
            copies[i].pNext = nullptr;
            copies[i].srcSet = d.mapper_.get_ds(handle_to_u64(copies[i].srcSet));
            copies[i].dstSet = d.mapper_.get_ds(handle_to_u64(copies[i].dstSet));
        }

        // Remap handles
        SPDLOG_TRACE("vkUpdateDescriptorSets: remapping handles");
        for (auto& w : writes) {
            uint64_t guestDS = handle_to_u64(w.dstSet);
            w.dstSet = d.mapper_.get_ds(guestDS);
            if (w.dstSet == VK_NULL_HANDLE && guestDS != 0) {
                SPDLOG_ERROR("vkUpdateDescriptorSets: NULL dstSet — guest handle {:#x} not found in mapper", guestDS);
            }
            if (w.pImageInfo) {
                for (uint32_t j = 0; j < w.descriptorCount; j++) {
                    auto* img = const_cast<VkDescriptorImageInfo*>(&w.pImageInfo[j]);
                    img->sampler = d.mapper_.get_sampler(handle_to_u64(img->sampler));
                    img->imageView = d.mapper_.get_image_view(handle_to_u64(img->imageView));
                }
            }
            if (w.pBufferInfo) {
                for (uint32_t j = 0; j < w.descriptorCount; j++) {
                    auto* buf = const_cast<VkDescriptorBufferInfo*>(&w.pBufferInfo[j]);
                    uint64_t guestBuf = handle_to_u64(buf->buffer);
                    buf->buffer = d.mapper_.get_buffer(guestBuf);
                    if (buf->buffer == VK_NULL_HANDLE && guestBuf != 0) {
                        SPDLOG_ERROR("vkUpdateDescriptorSets: NULL buffer in write — guest handle {:#x} not found in mapper (binding={})",
                            guestBuf, w.dstBinding);
                    }
                }
            }
            if (w.pTexelBufferView) {
                for (uint32_t j = 0; j < w.descriptorCount; j++) {
                    auto* view = const_cast<VkBufferView*>(&w.pTexelBufferView[j]);
                    *view = d.mapper_.get_buffer_view(handle_to_u64(*view));
                }
            }
        }
        SPDLOG_TRACE("vkUpdateDescriptorSets: calling driver");
        vkUpdateDescriptorSets(d.mapper_.device(), wc, writes.data(), cc, copies.data());
        SPDLOG_TRACE("vkUpdateDescriptorSets: done ({} writes, {} copies)", wc, cc);
        for (uint32_t i = 0; i < wc; i++) {
            delete[] imgPtrs[i]; delete[] bufPtrs[i]; delete[] viewPtrs[i];
        }
    });
    REGISTER(fbs::FunctionId_vkResetDescriptorPool, [](auto& d, auto& r) {
        r.read_handle(); auto dp = r.read_handle(); r.read_u32();
        auto p = d.mapper_.get_dp(dp);
        if (p) vkResetDescriptorPool(d.mapper_.device(), p, 0);
    });

    // --- Fence ---
    REGISTER(fbs::FunctionId_vkCreateFence, [](auto& d, auto& r) {
        r.read_handle();
        VkFenceCreateInfo ci{};
        r.read_raw(&ci, sizeof(ci));
        ci.pNext = nullptr;
        r.skip(sizeof(VkAllocationCallbacks));
        uint64_t pFence = r.read_handle();
        VkFence fence;
        if (vkCreateFence(d.mapper_.device(), &ci, nullptr, &fence) == VK_SUCCESS) {
            d.mapper_.store_fence(pFence, fence);
        }
    });
    REGISTER(fbs::FunctionId_vkDestroyFence, [](auto& d, auto& r) {
        r.read_handle(); auto f = r.read_handle(); r.skip(sizeof(VkAllocationCallbacks));
        auto fence = d.mapper_.get_fence(f);
        if (fence) vkDestroyFence(d.mapper_.device(), fence, nullptr);
        d.mapper_.remove_fence(f);
    });
    REGISTER(fbs::FunctionId_vkWaitForFences, [](auto& d, auto& r) {
        r.read_handle(); // device
        uint32_t count = r.read_u32();
        std::vector<VkFence> fences;
        fences.reserve(count);
        for (uint32_t i = 0; i < count; i++) {
            uint64_t gFence = r.read_handle();
            auto f = d.mapper_.get_fence(gFence);
            if (f) fences.push_back(f);  // skip NULL (destroyed/removed) fences
        }
        VkBool32 waitAll = r.read_bool();
        uint64_t timeout = r.read_u64();
        VkResult res = fences.empty() ? VK_SUCCESS :
            vkWaitForFences(d.mapper_.device(), (uint32_t)fences.size(), fences.data(), waitAll, timeout);
        if (res != VK_SUCCESS) {
            SPDLOG_WARN("vkWaitForFences: timeout or error (count={}, timeout={}ms, res={})",
                        count, timeout, static_cast<int>(res));
        }
    });
    REGISTER(fbs::FunctionId_vkResetFences, [](auto& d, auto& r) {
        r.read_handle(); // device
        uint32_t count = r.read_u32();
        std::vector<VkFence> fences;
        fences.reserve(count);
        for (uint32_t i = 0; i < count; i++) {
            uint64_t gFence = r.read_handle();
            auto f = d.mapper_.get_fence(gFence);
            if (f) fences.push_back(f);  // skip NULL (destroyed/removed) fences
        }
        if (!fences.empty())
            vkResetFences(d.mapper_.device(), (uint32_t)fences.size(), fences.data());
    });

    // --- Semaphore ---
    REGISTER(fbs::FunctionId_vkCreateSemaphore, [](auto& d, auto& r) {
        r.read_handle();
        VkSemaphoreCreateInfo ci{};
        ci.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        ci.flags = r.read_u32();
        // Check for timeline semaphore extension
        VkSemaphoreTypeCreateInfo ti{};
        ti.sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO;
        bool has_timeline = r.read_bool() != VK_FALSE;
        if (has_timeline) {
            ti.semaphoreType = static_cast<VkSemaphoreType>(r.read_u32());
            ti.initialValue = r.read_u64();
            ci.pNext = &ti;
        }
        r.skip(sizeof(VkAllocationCallbacks));
        uint64_t pSem = r.read_handle();
        VkSemaphore sem;
        if (vkCreateSemaphore(d.mapper_.device(), &ci, nullptr, &sem) == VK_SUCCESS) {
            d.mapper_.store_semaphore(pSem, sem);
        }
    });
    REGISTER(fbs::FunctionId_vkDestroySemaphore, [](auto& d, auto& r) {
        r.read_handle(); auto s = r.read_handle(); r.skip(sizeof(VkAllocationCallbacks));
        auto sem = d.mapper_.get_semaphore(s);
        if (sem) vkDestroySemaphore(d.mapper_.device(), sem, nullptr);
        d.mapper_.remove_semaphore(s);
    });

    // --- Event ---
    REGISTER(fbs::FunctionId_vkCreateEvent, [](auto& d, auto& r) {
        r.read_handle();
        VkEventCreateInfo ci{};
        r.read_raw(&ci, sizeof(ci));
        r.skip(sizeof(VkAllocationCallbacks));
        uint64_t pEv = r.read_handle();
        VkEvent ev;
        if (vkCreateEvent(d.mapper_.device(), &ci, nullptr, &ev) == VK_SUCCESS) {
            d.mapper_.store_event(pEv, ev);
        }
    });
    REGISTER(fbs::FunctionId_vkDestroyEvent, [](auto& d, auto& r) {
        r.read_handle(); auto e = r.read_handle(); r.skip(sizeof(VkAllocationCallbacks));
        auto ev = d.mapper_.get_event(e);
        if (ev) vkDestroyEvent(d.mapper_.device(), ev, nullptr);
        d.mapper_.remove_event(e);
    });
    REGISTER(fbs::FunctionId_vkSetEvent, [](auto& d, auto& r) {
        r.read_handle(); auto e = r.read_handle();
        auto ev = d.mapper_.get_event(e);
        if (ev) vkSetEvent(d.mapper_.device(), ev);
    });
    REGISTER(fbs::FunctionId_vkResetEvent, [](auto& d, auto& r) {
        r.read_handle(); auto e = r.read_handle();
        auto ev = d.mapper_.get_event(e);
        if (ev) vkResetEvent(d.mapper_.device(), ev);
    });

    // --- Query Pool ---
    REGISTER(fbs::FunctionId_vkCreateQueryPool, [](auto& d, auto& r) {
        r.read_handle();
        VkQueryPoolCreateInfo ci{};
        r.read_raw(&ci, sizeof(ci));
        r.skip(sizeof(VkAllocationCallbacks));
        uint64_t pQP = r.read_handle();
        VkQueryPool qp;
        if (vkCreateQueryPool(d.mapper_.device(), &ci, nullptr, &qp) == VK_SUCCESS) {
            d.mapper_.store_query_pool(pQP, qp);
        }
    });
    REGISTER(fbs::FunctionId_vkDestroyQueryPool, [](auto& d, auto& r) {
        r.read_handle(); auto qp = r.read_handle(); r.skip(sizeof(VkAllocationCallbacks));
        auto q = d.mapper_.get_query_pool(qp);
        if (q) vkDestroyQueryPool(d.mapper_.device(), q, nullptr);
        d.mapper_.remove_query_pool(qp);
    });

    // --- Command Recording ---
    REGISTER(fbs::FunctionId_vkCmdBindPipeline, [](auto& d, auto& r) {
        auto cb = d.mapper_.active_cmd(); r.read_handle(); // cb
        VkPipelineBindPoint bp = static_cast<VkPipelineBindPoint>(r.read_u32());
        uint64_t pp = r.read_handle();
        auto ppl = d.mapper_.get_pipeline(pp);
        if (!ppl && pp != 0) {
            SPDLOG_ERROR("vkCmdBindPipeline: pipeline {:#x} not found — bind skipped!", pp);
        }
        if (cb && ppl) vkCmdBindPipeline(cb, bp, ppl);
    });
    REGISTER(fbs::FunctionId_vkCmdBindDescriptorSets, [](auto& d, auto& r) {
        auto cb = d.mapper_.active_cmd(); r.read_handle();
        VkPipelineBindPoint bp = static_cast<VkPipelineBindPoint>(r.read_u32());
        uint64_t layout = r.read_handle();
        uint32_t firstSet = r.read_u32();
        uint32_t count = r.read_u32();
        auto desc_handles = r.template read_array<uint64_t>(count);
        uint32_t dynCount = r.read_u32();
        auto dynOffsets = r.template read_array<uint32_t>(dynCount);
        if (cb) {
            VkPipelineLayout pl = d.mapper_.get_pipeline_layout(layout);
            if (!pl) { SPDLOG_WARN("vkCmdBindDescriptorSets: null pipeline layout"); return; }
            std::vector<VkDescriptorSet> dss;
            dss.reserve(desc_handles.size());
            for (auto& g : desc_handles) {
                auto ds = d.mapper_.get_ds(g);
                if (ds == VK_NULL_HANDLE && g != 0) {
                    SPDLOG_WARN("vkCmdBindDescriptorSets: guest handle {:#x} not mapped", g);
                }
                dss.push_back(ds);
            }
            vkCmdBindDescriptorSets(cb, bp, pl, firstSet,
                                    static_cast<uint32_t>(dss.size()),
                                    dss.data(), dynCount, dynOffsets.data());
        }
    });
    REGISTER(fbs::FunctionId_vkCmdPushConstants, [](auto& d, auto& r) {
        auto cb = d.mapper_.active_cmd(); r.read_handle();
        uint64_t layout = r.read_handle();
        VkShaderStageFlags stages = r.read_u32();
        uint32_t offset = r.read_u32();
        uint32_t size = r.read_u32();
        auto data = r.template read_array<uint8_t>(size);
        VkPipelineLayout pl = d.mapper_.get_pipeline_layout(layout);
        // Dump push constants for debugging output BDA
        static std::atomic<int> pc_count{0};
        int n = pc_count.fetch_add(1);
        if (n < 3 && size >= 8 && d.isComputeMode_) {
            SPDLOG_INFO("PUSH_CONST[{}]: size={} bytes first16={:02x} {:02x} {:02x} {:02x} {:02x} {:02x} {:02x} {:02x} {:02x} {:02x} {:02x} {:02x} {:02x} {:02x} {:02x} {:02x}",
                n, size, data[0], data[1], data[2], data[3], data[4], data[5], data[6], data[7],
                data[8], data[9], data[10], data[11], data[12], data[13], data[14], data[15]);
        }
        if (cb && data.size() > 0)
            vkCmdPushConstants(cb, pl, stages, offset, static_cast<uint32_t>(data.size()), data.data());
    });

    // --- Compute dispatch ---
    REGISTER(fbs::FunctionId_vkCmdDispatch, [](auto& d, auto& r) {
        auto cb = d.mapper_.active_cmd(); r.read_handle();
        uint32_t x = r.read_u32(); uint32_t y = r.read_u32(); uint32_t z = r.read_u32();
        static std::atomic<int> dispatch_n{0};
        int dn = dispatch_n.fetch_add(1);
        if (dn < 5 && d.isComputeMode_) {
            SPDLOG_INFO("DISPATCH[{}]: groups=({},{},{})", dn, x, y, z);
        }
        if (cb) vkCmdDispatch(cb, x, y, z);
    });
    // --- Copy ---
    REGISTER(fbs::FunctionId_vkCmdCopyBuffer, [](auto& d, auto& r) {
        auto cb = d.mapper_.active_cmd(); r.read_handle();
        uint64_t src = r.read_handle(); uint64_t dst = r.read_handle();
        uint32_t count = r.read_u32();
        auto regions = r.template read_array<VkBufferCopy>(count);
        auto s = d.mapper_.get_buffer(src); auto d2 = d.mapper_.get_buffer(dst);
        if ((!s && src != 0) || (!d2 && dst != 0)) {
            SPDLOG_ERROR("vkCmdCopyBuffer: buffer not found — src={:#x} dst={:#x}", src, dst);
        }
        if (cb && s && d2)
            vkCmdCopyBuffer(cb, s, d2, static_cast<uint32_t>(regions.size()), regions.data());
    });
    REGISTER(fbs::FunctionId_vkCmdCopyImage, [](auto& d, auto& r) {
        auto cb = d.mapper_.active_cmd(); r.read_handle();
        uint64_t src = r.read_handle(); VkImageLayout srcLayout = static_cast<VkImageLayout>(r.read_u32());
        uint64_t dst = r.read_handle(); VkImageLayout dstLayout = static_cast<VkImageLayout>(r.read_u32());
        uint32_t count = r.read_u32();
        auto regions = r.template read_array<VkImageCopy>(count);
        auto s = d.mapper_.get_image(src); auto d2 = d.mapper_.get_image(dst);
        if (cb && s && d2) vkCmdCopyImage(cb, s, srcLayout, d2, dstLayout,
                                           static_cast<uint32_t>(regions.size()), regions.data());
    });
    REGISTER(fbs::FunctionId_vkCmdBlitImage, [](auto& d, auto& r) {
        auto cb = d.mapper_.active_cmd(); r.read_handle();
        uint64_t src = r.read_handle(); VkImageLayout srcLayout = static_cast<VkImageLayout>(r.read_u32());
        uint64_t dst = r.read_handle(); VkImageLayout dstLayout = static_cast<VkImageLayout>(r.read_u32());
        uint32_t count = r.read_u32();
        auto regions = r.template read_array<VkImageBlit>(count);
        VkFilter filter = static_cast<VkFilter>(r.read_u32());
        auto s = d.mapper_.get_image(src);
        auto d2 = d.mapper_.get_image(dst);
        if (cb && s && d2)
            vkCmdBlitImage(cb, s, srcLayout, d2, dstLayout,
                           static_cast<uint32_t>(regions.size()), regions.data(), filter);
    });
    REGISTER(fbs::FunctionId_vkCmdCopyBufferToImage, [](auto& d, auto& r) {
        auto cb = d.mapper_.active_cmd(); r.read_handle();
        uint64_t srcBuf = r.read_handle(); uint64_t dstImg = r.read_handle();
        VkImageLayout dstLayout = static_cast<VkImageLayout>(r.read_u32());
        uint32_t rc = r.read_u32();
        std::vector<VkBufferImageCopy> regions(rc);
        for (auto& reg : regions) r.read_raw(&reg, sizeof(reg));
        VkBuffer src = d.mapper_.get_buffer(srcBuf);
        VkImage dst = d.mapper_.get_image(dstImg);
        if (cb && src && dst)
            vkCmdCopyBufferToImage(cb, src, dst, dstLayout, rc, regions.data());
    });
    REGISTER(fbs::FunctionId_vkCmdCopyImageToBuffer, [](auto& d, auto& r) {
        auto cb = d.mapper_.active_cmd(); r.read_handle();
        uint64_t srcImg = r.read_handle(); VkImageLayout srcLayout = static_cast<VkImageLayout>(r.read_u32());
        uint64_t dstBuf = r.read_handle();
        uint32_t rc = r.read_u32();
        std::vector<VkBufferImageCopy> regions(rc);
        for (auto& reg : regions) r.read_raw(&reg, sizeof(reg));
        VkImage src = d.mapper_.get_image(srcImg);
        VkBuffer dst = d.mapper_.get_buffer(dstBuf);
        if (cb && src && dst)
            vkCmdCopyImageToBuffer(cb, src, srcLayout, dst, rc, regions.data());
    });
    REGISTER(fbs::FunctionId_vkCmdUpdateBuffer, [](auto& d, auto& r) {
        auto cb = d.mapper_.active_cmd(); r.read_handle();
        uint64_t dst = r.read_handle();
        uint64_t off = r.read_u64();
        uint64_t size = r.read_u64();
        auto data = r.template read_array<uint8_t>(static_cast<uint32_t>(size));
        auto b = d.mapper_.get_buffer(dst);
        if (cb && b && data.size() > 0)
            vkCmdUpdateBuffer(cb, b, off, data.size(), data.data());
    });
    REGISTER(fbs::FunctionId_vkCmdFillBuffer, [](auto& d, auto& r) {
        auto cb = d.mapper_.active_cmd(); r.read_handle();
        uint64_t dst = r.read_handle(); uint64_t off = r.read_u64();
        uint64_t sz = r.read_u64(); uint32_t val = r.read_u32();
        auto b = d.mapper_.get_buffer(dst);
        if (!b && dst != 0) {
            SPDLOG_ERROR("vkCmdFillBuffer: buffer {:#x} not found — fill skipped!", dst);
        }
        if (cb && b) vkCmdFillBuffer(cb, b, off, sz, val);
    });
    REGISTER(fbs::FunctionId_vkCmdClearColorImage, [](auto& d, auto& r) {
        auto cb = d.mapper_.active_cmd(); r.read_handle();
        uint64_t img = r.read_handle();
        VkImageLayout layout = static_cast<VkImageLayout>(r.read_u32());
        VkClearColorValue color;
        r.read_raw(&color, sizeof(VkClearColorValue));
        uint32_t count = r.read_u32();
        auto ranges = r.template read_array<VkImageSubresourceRange>(count);
        auto i = d.mapper_.get_image(img);
        if (cb && i)
            vkCmdClearColorImage(cb, i, layout, &color,
                                 static_cast<uint32_t>(ranges.size()), ranges.data());
    });
                REGISTER(fbs::FunctionId_vkCmdExecuteCommands, [](auto& d, auto& r) {
        auto cb = d.mapper_.active_cmd(); r.read_handle();
        uint32_t count = r.read_u32();
        std::vector<VkCommandBuffer> secondaryCbs(count);
        for (uint32_t i = 0; i < count; i++) {
            uint64_t gCB = r.read_handle();
            secondaryCbs[i] = d.mapper_.get_command_buffer(gCB);
        }
        if (cb) vkCmdExecuteCommands(cb, count, secondaryCbs.data());
    });

    // --- Barrier ---
    REGISTER(fbs::FunctionId_vkCmdPipelineBarrier, [](auto& d, auto& r) {
        auto cb = d.mapper_.active_cmd(); r.read_handle();
        VkPipelineStageFlags src = r.read_u32(); VkPipelineStageFlags dst = r.read_u32();
        VkDependencyFlags dep = r.read_u32();
        uint32_t mc = r.read_u32();
        std::vector<VkMemoryBarrier> memBarriers(mc);
        for (auto& mb : memBarriers) r.read_raw(&mb, sizeof(mb));

        uint32_t bc = r.read_u32();
        std::vector<VkBufferMemoryBarrier> bufBarriers(bc);
        for (auto& bb : bufBarriers) {
            r.read_raw(&bb, sizeof(bb));
            bb.buffer = d.mapper_.get_buffer(handle_to_u64(bb.buffer));
        }

        uint32_t ic = r.read_u32();
        std::vector<VkImageMemoryBarrier> imgBarriers(ic);
        for (auto& ib : imgBarriers) {
            r.read_raw(&ib, sizeof(ib));
            ib.image = d.mapper_.get_image(handle_to_u64(ib.image));
        }

        if (cb) vkCmdPipelineBarrier(cb, src, dst, dep,
            mc, memBarriers.data(),
            bc, bufBarriers.data(),
            ic, imgBarriers.data());
    });

    // --- Event commands ---
    REGISTER(fbs::FunctionId_vkCmdSetEvent, [](auto& d, auto& r) {
        auto cb = d.mapper_.active_cmd(); r.read_handle();
        uint64_t evt = r.read_handle(); auto sm = r.read_u32();
        auto e = d.mapper_.get_event(evt);
        if (cb && e) vkCmdSetEvent(cb, e, sm);
    });
    REGISTER(fbs::FunctionId_vkCmdResetEvent, [](auto& d, auto& r) {
        auto cb = d.mapper_.active_cmd(); r.read_handle();
        uint64_t evt = r.read_handle(); auto sm = r.read_u32();
        auto e = d.mapper_.get_event(evt);
        if (cb && e) vkCmdResetEvent(cb, e, sm);
    });
    REGISTER(fbs::FunctionId_vkCmdWaitEvents, [](auto& d, auto& r) {
        auto cb = d.mapper_.active_cmd(); r.read_handle();
        uint32_t ec = r.read_u32();
        std::vector<uint64_t> events(ec);
        for (auto& e : events) e = r.read_handle();
        VkPipelineStageFlags srcStage = r.read_u32();
        VkPipelineStageFlags dstStage = r.read_u32();

        uint32_t mc = r.read_u32();
        std::vector<VkMemoryBarrier> memBarriers(mc);
        for (auto& mb : memBarriers) r.read_raw(&mb, sizeof(mb));

        uint32_t bc = r.read_u32();
        std::vector<VkBufferMemoryBarrier> bufBarriers(bc);
        for (auto& bb : bufBarriers) {
            r.read_raw(&bb, sizeof(bb));
            bb.buffer = d.mapper_.get_buffer(handle_to_u64(bb.buffer));
        }

        uint32_t ic = r.read_u32();
        std::vector<VkImageMemoryBarrier> imgBarriers(ic);
        for (auto& ib : imgBarriers) {
            r.read_raw(&ib, sizeof(ib));
            ib.image = d.mapper_.get_image(handle_to_u64(ib.image));
        }

        if (cb) {
            std::vector<VkEvent> hostEvents(ec);
            for (uint32_t i = 0; i < ec; i++)
                hostEvents[i] = d.mapper_.get_event(events[i]);
            vkCmdWaitEvents(cb, ec, hostEvents.data(), srcStage, dstStage,
                mc, memBarriers.data(),
                bc, bufBarriers.data(),
                ic, imgBarriers.data());
        }
    });

    // --- Query commands ---
    REGISTER(fbs::FunctionId_vkCmdBeginQuery, [](auto& d, auto& r) {
        auto cb = d.mapper_.active_cmd(); r.read_handle();
        uint64_t qp = r.read_handle(); uint32_t q = r.read_u32();
        VkQueryControlFlags flags = static_cast<VkQueryControlFlags>(r.read_u32());
        auto qph = d.mapper_.get_query_pool(qp);
        if (cb && qph) vkCmdBeginQuery(cb, qph, q, flags);
    });
    REGISTER(fbs::FunctionId_vkCmdEndQuery, [](auto& d, auto& r) {
        auto cb = d.mapper_.active_cmd(); r.read_handle();
        uint64_t qp = r.read_handle(); uint32_t q = r.read_u32();
        auto qph = d.mapper_.get_query_pool(qp);
        if (cb && qph) vkCmdEndQuery(cb, qph, q);
    });
    REGISTER(fbs::FunctionId_vkCmdWriteTimestamp, [](auto& d, auto& r) {
        auto cb = d.mapper_.active_cmd(); r.read_handle();
        VkPipelineStageFlags stage = static_cast<VkPipelineStageFlags>(r.read_u32());
        uint64_t qp = r.read_handle(); uint32_t q = r.read_u32();
        auto qph = d.mapper_.get_query_pool(qp);
        if (cb && qph) vkCmdWriteTimestamp(cb, static_cast<VkPipelineStageFlagBits>(stage), qph, q);
    });
    REGISTER(fbs::FunctionId_vkCmdResetQueryPool, [](auto& d, auto& r) {
        auto cb = d.mapper_.active_cmd(); r.read_handle();
        uint64_t qp = r.read_handle(); uint32_t fq = r.read_u32(); uint32_t qc = r.read_u32();
        auto qph = d.mapper_.get_query_pool(qp);
        if (cb && qph) vkCmdResetQueryPool(cb, qph, fq, qc);
    });
    REGISTER(fbs::FunctionId_vkCmdCopyQueryPoolResults, [](auto& d, auto& r) {
        auto cb = d.mapper_.active_cmd(); r.read_handle();
        uint64_t gPool = r.read_handle();
        uint32_t firstQuery = r.read_u32();
        uint32_t queryCount = r.read_u32();
        uint64_t gDstBuf = r.read_handle();
        VkDeviceSize dstOffset = r.read_u64();
        VkDeviceSize stride = r.read_u64();
        VkQueryResultFlags flags = static_cast<VkQueryResultFlags>(r.read_u32());
        VkQueryPool pool = d.mapper_.get_query_pool(gPool);
        VkBuffer dstBuf = d.mapper_.get_buffer(gDstBuf);
        if (cb && pool && dstBuf)
            vkCmdCopyQueryPoolResults(cb, pool, firstQuery, queryCount,
                                       dstBuf, dstOffset, stride, flags);
    });
    REGISTER(fbs::FunctionId_vkGetQueryPoolResults, [](auto& d, auto& r) {
        VkDevice dev = d.mapper_.device();
        r.read_handle();
        uint64_t gPool = r.read_handle();
        uint32_t firstQuery = r.read_u32();
        uint32_t queryCount = r.read_u32();
        uint64_t dataSize = r.read_u64();
        uint64_t stride = r.read_u64();
        VkQueryResultFlags flags = static_cast<VkQueryResultFlags>(r.read_u32());

        VkQueryPool pool = d.mapper_.get_query_pool(gPool);
        std::vector<uint8_t> data(static_cast<size_t>(dataSize), 0);
        if (dev && pool && dataSize > 0) {
            VkResult res = vkGetQueryPoolResults(dev, pool, firstQuery, queryCount,
                                                 static_cast<size_t>(dataSize), data.data(),
                                                 static_cast<VkDeviceSize>(stride), flags);
            if (res == VK_SUCCESS && d.sendDataFn_) {
                d.sendDataFn_(gPool, data.data(), data.size(), 0);
            }
        }
    });
    REGISTER(fbs::FunctionId_vkGetImageSubresourceLayout, [](auto& d, auto& r) {
        VkDevice dev = d.mapper_.device();
        r.read_handle();
        uint64_t gImg = r.read_handle();
        VkImageSubresource sub{};
        r.read_raw(&sub, sizeof(sub));

        VkImage img = d.mapper_.get_image(gImg);
        VkSubresourceLayout layout{};
        if (dev && img) {
            vkGetImageSubresourceLayout(dev, img, &sub, &layout);
            if (d.sendDataFn_) {
                d.sendDataFn_(gImg, reinterpret_cast<const uint8_t*>(&layout), sizeof(layout), 0);
            }
        }
    });

    // --- Synchronization2 (Vulkan 1.3) ---
    REGISTER(fbs::FunctionId_vkCmdPipelineBarrier2, [](auto& d, auto& r) {
        auto cb = d.mapper_.active_cmd(); r.read_handle();
        VkDependencyFlags depFlags = static_cast<VkDependencyFlags>(r.read_u32());
        uint32_t mem_br = r.read_u32();
        std::vector<VkMemoryBarrier2> memBarriers2(mem_br);
        for (auto& mb : memBarriers2) {
            mb.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2;
            mb.pNext = nullptr;
            mb.srcStageMask = r.read_u64();
            mb.srcAccessMask = r.read_u64();
            mb.dstStageMask = r.read_u64();
            mb.dstAccessMask = r.read_u64();
        }

        uint32_t buf_br = r.read_u32();
        std::vector<VkBufferMemoryBarrier2> bufBarriers2(buf_br);
        for (auto& bb : bufBarriers2) {
            bb.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
            bb.pNext = nullptr;
            bb.srcStageMask = r.read_u64();
            bb.srcAccessMask = r.read_u64();
            bb.dstStageMask = r.read_u64();
            bb.dstAccessMask = r.read_u64();
            bb.srcQueueFamilyIndex = r.read_u32();
            bb.dstQueueFamilyIndex = r.read_u32();
            bb.buffer = d.mapper_.get_buffer(r.read_handle());
            bb.offset = r.read_u64();
            bb.size = r.read_u64();
        }

        uint32_t img_br = r.read_u32();
        std::vector<VkImageMemoryBarrier2> imgBarriers2(img_br);
        for (auto& ib : imgBarriers2) {
            ib.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
            ib.pNext = nullptr;
            ib.srcStageMask = r.read_u64();
            ib.srcAccessMask = r.read_u64();
            ib.dstStageMask = r.read_u64();
            ib.dstAccessMask = r.read_u64();
            ib.oldLayout = static_cast<VkImageLayout>(r.read_u32());
            ib.newLayout = static_cast<VkImageLayout>(r.read_u32());
            ib.srcQueueFamilyIndex = r.read_u32();
            ib.dstQueueFamilyIndex = r.read_u32();
            ib.image = d.mapper_.get_image(r.read_handle());
            r.read_raw(&ib.subresourceRange, sizeof(VkImageSubresourceRange));
        }

        VkDependencyInfo di{};
        di.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
        di.dependencyFlags = depFlags;
        di.memoryBarrierCount = mem_br;
        di.pMemoryBarriers = memBarriers2.data();
        di.bufferMemoryBarrierCount = buf_br;
        di.pBufferMemoryBarriers = bufBarriers2.data();
        di.imageMemoryBarrierCount = img_br;
        di.pImageMemoryBarriers = imgBarriers2.data();
        if (cb) {
            auto pfnCmdPipelineBarrier2 = reinterpret_cast<PFN_vkCmdPipelineBarrier2>(
                vkGetDeviceProcAddr(d.mapper_.device(), "vkCmdPipelineBarrier2"));
            if (pfnCmdPipelineBarrier2) {
                pfnCmdPipelineBarrier2(cb, &di);
            } else {
                VkPipelineStageFlags srcStages = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
                VkPipelineStageFlags dstStages = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
                std::vector<VkMemoryBarrier> legacyMem(mem_br);
                for (size_t k = 0; k < mem_br; k++) {
                    legacyMem[k].sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
                    legacyMem[k].pNext = nullptr;
                    legacyMem[k].srcAccessMask = static_cast<VkAccessFlags>(memBarriers2[k].srcAccessMask);
                    legacyMem[k].dstAccessMask = static_cast<VkAccessFlags>(memBarriers2[k].dstAccessMask);
                }
                std::vector<VkBufferMemoryBarrier> legacyBuf(buf_br);
                for (size_t k = 0; k < buf_br; k++) {
                    legacyBuf[k].sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
                    legacyBuf[k].pNext = nullptr;
                    legacyBuf[k].srcAccessMask = static_cast<VkAccessFlags>(bufBarriers2[k].srcAccessMask);
                    legacyBuf[k].dstAccessMask = static_cast<VkAccessFlags>(bufBarriers2[k].dstAccessMask);
                    legacyBuf[k].srcQueueFamilyIndex = bufBarriers2[k].srcQueueFamilyIndex;
                    legacyBuf[k].dstQueueFamilyIndex = bufBarriers2[k].dstQueueFamilyIndex;
                    legacyBuf[k].buffer = bufBarriers2[k].buffer;
                    legacyBuf[k].offset = bufBarriers2[k].offset;
                    legacyBuf[k].size = bufBarriers2[k].size;
                }
                std::vector<VkImageMemoryBarrier> legacyImg(img_br);
                for (size_t k = 0; k < img_br; k++) {
                    legacyImg[k].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
                    legacyImg[k].pNext = nullptr;
                    legacyImg[k].srcAccessMask = static_cast<VkAccessFlags>(imgBarriers2[k].srcAccessMask);
                    legacyImg[k].dstAccessMask = static_cast<VkAccessFlags>(imgBarriers2[k].dstAccessMask);
                    legacyImg[k].oldLayout = imgBarriers2[k].oldLayout;
                    legacyImg[k].newLayout = imgBarriers2[k].newLayout;
                    legacyImg[k].srcQueueFamilyIndex = imgBarriers2[k].srcQueueFamilyIndex;
                    legacyImg[k].dstQueueFamilyIndex = imgBarriers2[k].dstQueueFamilyIndex;
                    legacyImg[k].image = imgBarriers2[k].image;
                    legacyImg[k].subresourceRange = imgBarriers2[k].subresourceRange;
                }
                vkCmdPipelineBarrier(cb, srcStages, dstStages, depFlags,
                                     static_cast<uint32_t>(legacyMem.size()), legacyMem.data(),
                                     static_cast<uint32_t>(legacyBuf.size()), legacyBuf.data(),
                                     static_cast<uint32_t>(legacyImg.size()), legacyImg.data());
            }
        }
    });
    // --- Copy commands 2 (Vulkan 1.3) ---
    REGISTER(fbs::FunctionId_vkCmdCopyBuffer2, [](auto& d, auto& r) {
        auto cb = d.mapper_.active_cmd();
        r.read_handle(); // cmdBuffer
        VkBuffer src = d.mapper_.get_buffer(r.read_handle());
        VkBuffer dst = d.mapper_.get_buffer(r.read_handle());
        uint32_t count = r.read_u32();
        std::vector<VkBufferCopy2> regions(count);
        for (uint32_t i = 0; i < count; i++) {
            r.read_raw(&regions[i], sizeof(VkBufferCopy2));
            regions[i].pNext = nullptr;
        }

        VkCopyBufferInfo2 ci{};
        ci.sType = VK_STRUCTURE_TYPE_COPY_BUFFER_INFO_2;
        ci.pNext = nullptr;
        ci.srcBuffer = src;
        ci.dstBuffer = dst;
        ci.regionCount = count;
        ci.pRegions = regions.data();

        if (cb && src && dst) {
            auto pfnCmdCopyBuffer2 = reinterpret_cast<PFN_vkCmdCopyBuffer2>(
                vkGetDeviceProcAddr(d.mapper_.device(), "vkCmdCopyBuffer2"));
            if (pfnCmdCopyBuffer2) {
                pfnCmdCopyBuffer2(cb, &ci);
            }
        }
    });
    REGISTER(fbs::FunctionId_vkCmdCopyImage2, [](auto& d, auto& r) {
        auto cb = d.mapper_.active_cmd();
        r.read_handle();
        VkImage src = d.mapper_.get_image(r.read_handle());
        VkImageLayout srcLayout = static_cast<VkImageLayout>(r.read_u32());
        VkImage dst = d.mapper_.get_image(r.read_handle());
        VkImageLayout dstLayout = static_cast<VkImageLayout>(r.read_u32());
        uint32_t count = r.read_u32();
        std::vector<VkImageCopy2> regions(count);
        for (uint32_t i = 0; i < count; i++) {
            r.read_raw(&regions[i], sizeof(VkImageCopy2));
            regions[i].pNext = nullptr;
        }

        VkCopyImageInfo2 ci{};
        ci.sType = VK_STRUCTURE_TYPE_COPY_IMAGE_INFO_2;
        ci.pNext = nullptr;
        ci.srcImage = src;
        ci.srcImageLayout = srcLayout;
        ci.dstImage = dst;
        ci.dstImageLayout = dstLayout;
        ci.regionCount = count;
        ci.pRegions = regions.data();

        if (cb && src && dst) {
            auto pfnCmdCopyImage2 = reinterpret_cast<PFN_vkCmdCopyImage2>(
                vkGetDeviceProcAddr(d.mapper_.device(), "vkCmdCopyImage2"));
            if (pfnCmdCopyImage2) {
                pfnCmdCopyImage2(cb, &ci);
            }
        }
    });
    REGISTER(fbs::FunctionId_vkCmdCopyBufferToImage2, [](auto& d, auto& r) {
        auto cb = d.mapper_.active_cmd();
        r.read_handle();
        VkBuffer src = d.mapper_.get_buffer(r.read_handle());
        VkImage dst = d.mapper_.get_image(r.read_handle());
        VkImageLayout dstLayout = static_cast<VkImageLayout>(r.read_u32());
        uint32_t count = r.read_u32();
        std::vector<VkBufferImageCopy2> regions(count);
        for (uint32_t i = 0; i < count; i++) {
            r.read_raw(&regions[i], sizeof(VkBufferImageCopy2));
            regions[i].pNext = nullptr;
        }

        VkCopyBufferToImageInfo2 ci{};
        ci.sType = VK_STRUCTURE_TYPE_COPY_BUFFER_TO_IMAGE_INFO_2;
        ci.pNext = nullptr;
        ci.srcBuffer = src;
        ci.dstImage = dst;
        ci.dstImageLayout = dstLayout;
        ci.regionCount = count;
        ci.pRegions = regions.data();

        if (cb && src && dst) {
            auto pfnCmdCopyBufferToImage2 = reinterpret_cast<PFN_vkCmdCopyBufferToImage2>(
                vkGetDeviceProcAddr(d.mapper_.device(), "vkCmdCopyBufferToImage2"));
            if (pfnCmdCopyBufferToImage2) {
                pfnCmdCopyBufferToImage2(cb, &ci);
            }
        }
    });
    REGISTER(fbs::FunctionId_vkCmdCopyImageToBuffer2, [](auto& d, auto& r) {
        auto cb = d.mapper_.active_cmd();
        r.read_handle();
        VkImage src = d.mapper_.get_image(r.read_handle());
        VkImageLayout srcLayout = static_cast<VkImageLayout>(r.read_u32());
        VkBuffer dst = d.mapper_.get_buffer(r.read_handle());
        uint32_t count = r.read_u32();
        std::vector<VkBufferImageCopy2> regions(count);
        for (uint32_t i = 0; i < count; i++) {
            r.read_raw(&regions[i], sizeof(VkBufferImageCopy2));
            regions[i].pNext = nullptr;
        }

        VkCopyImageToBufferInfo2 ci{};
        ci.sType = VK_STRUCTURE_TYPE_COPY_IMAGE_TO_BUFFER_INFO_2;
        ci.pNext = nullptr;
        ci.srcImage = src;
        ci.srcImageLayout = srcLayout;
        ci.dstBuffer = dst;
        ci.regionCount = count;
        ci.pRegions = regions.data();

        if (cb && src && dst) {
            auto pfnCmdCopyImageToBuffer2 = reinterpret_cast<PFN_vkCmdCopyImageToBuffer2>(
                vkGetDeviceProcAddr(d.mapper_.device(), "vkCmdCopyImageToBuffer2"));
            if (pfnCmdCopyImageToBuffer2) {
                pfnCmdCopyImageToBuffer2(cb, &ci);
            }
        }
    });
        // --- QueueSubmit2 (Vulkan 1.3) ---
    REGISTER(fbs::FunctionId_vkQueueSubmit2, [](auto& d, auto& r) {
        VkDevice dev = d.mapper_.device();
        VkQueue q = d.mapper_.queue();
        r.read_handle();
        uint32_t count = r.read_u32();

        std::vector<VkSubmitInfo2> submits(count);
        for (uint32_t i = 0; i < count; i++) {
            if (!read_VkSubmitInfo2(r, &submits[i])) break;
            auto& si = submits[i];
            for (uint32_t j = 0; j < si.commandBufferInfoCount; j++) {
                auto& cmdInfo = const_cast<VkCommandBufferSubmitInfo&>(si.pCommandBufferInfos[j]);
                cmdInfo.pNext = nullptr;
                cmdInfo.commandBuffer = d.mapper_.get_command_buffer(
                    handle_to_u64(cmdInfo.commandBuffer));
            }
            for (uint32_t j = 0; j < si.waitSemaphoreInfoCount; j++) {
                auto& semInfo = const_cast<VkSemaphoreSubmitInfo&>(si.pWaitSemaphoreInfos[j]);
                semInfo.pNext = nullptr;
                semInfo.semaphore = d.mapper_.get_semaphore(
                    handle_to_u64(semInfo.semaphore));
            }
            for (uint32_t j = 0; j < si.signalSemaphoreInfoCount; j++) {
                auto& semInfo = const_cast<VkSemaphoreSubmitInfo&>(si.pSignalSemaphoreInfos[j]);
                semInfo.pNext = nullptr;
                semInfo.semaphore = d.mapper_.get_semaphore(
                    handle_to_u64(semInfo.semaphore));
            }
        }

        uint64_t guestFence = r.read_handle();
        VkFence fence = d.mapper_.get_fence(guestFence);
        VkFenceCreateInfo fci{};
        fci.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        bool ownFence = false;
        if (fence == VK_NULL_HANDLE) {
            if (vkCreateFence(dev, &fci, nullptr, &fence) == VK_SUCCESS)
                ownFence = true;
        }

        auto pfnQueueSubmit2 = reinterpret_cast<PFN_vkQueueSubmit2>(
            vkGetDeviceProcAddr(dev, "vkQueueSubmit2"));
        VkResult res = pfnQueueSubmit2
            ? pfnQueueSubmit2(q, count, submits.data(), fence)
            : VK_ERROR_EXTENSION_NOT_PRESENT;
        if (res == VK_SUCCESS) {
            d.pendingSubmitFence_ = fence;
            d.pendingSubmitFenceGuestHandle_ = guestFence;
            d.hasPendingSubmit_ = true;
            SPDLOG_DEBUG("  vkQueueSubmit2 ({} submits) -> submitted, fence={}",
                         count, (void*)fence);
        } else {
            SPDLOG_ERROR("  vkQueueSubmit2 failed: {}", static_cast<int>(res));
            if (ownFence) vkDestroyFence(dev, fence, nullptr);
        }

        for (uint32_t i = 0; i < count; i++)
            free_VkSubmitInfo2(&submits[i]);
    });

    // --- Private Data (Vulkan 1.3) ---
    REGISTER(fbs::FunctionId_vkCreatePrivateDataSlot, [](auto& d, auto& r) {
        r.read_handle();
        VkPrivateDataSlotCreateInfo ci{};
        r.read_raw(&ci, sizeof(ci));
        ci.pNext = nullptr;
        r.skip(sizeof(VkAllocationCallbacks));
        uint64_t pSlot = r.read_handle();
        VkPrivateDataSlot slot;
        if (vkCreatePrivateDataSlot(d.mapper_.device(), &ci, nullptr, &slot) == VK_SUCCESS) {
            d.mapper_.store_private_data_slot(pSlot, slot);
        }
    });
    REGISTER(fbs::FunctionId_vkDestroyPrivateDataSlot, [](auto& d, auto& r) {
        r.read_handle(); uint64_t gSlot = r.read_handle(); r.skip(sizeof(VkAllocationCallbacks));
        auto slot = d.mapper_.get_private_data_slot(gSlot);
        if (slot) vkDestroyPrivateDataSlot(d.mapper_.device(), slot, nullptr);
        d.mapper_.remove_private_data_slot(gSlot);
    });
    REGISTER(fbs::FunctionId_vkSetPrivateData, [](auto& d, auto& r) {
        r.read_handle(); r.read_u32(); r.read_u64(); r.read_handle(); r.read_u64();
    });

    // --- Descriptor Update Template (1.1 promoted) ---
    REGISTER(fbs::FunctionId_vkCreateDescriptorUpdateTemplate, [](auto& d, auto& r) {
        r.read_handle(); // device
        VkDescriptorUpdateTemplateCreateInfo ci{};
        ci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_UPDATE_TEMPLATE_CREATE_INFO;
        ci.flags = r.read_u32();
        ci.descriptorUpdateEntryCount = r.read_u32();
        std::vector<VkDescriptorUpdateTemplateEntry> entries(ci.descriptorUpdateEntryCount);
        for (uint32_t i = 0; i < ci.descriptorUpdateEntryCount; i++) {
            entries[i].dstBinding = r.read_u32();
            entries[i].dstArrayElement = r.read_u32();
            entries[i].descriptorCount = r.read_u32();
            entries[i].descriptorType = static_cast<VkDescriptorType>(r.read_u32());
            entries[i].offset = static_cast<size_t>(r.read_u64());
            entries[i].stride = static_cast<size_t>(r.read_u64());
        }
        ci.pDescriptorUpdateEntries = entries.data();
        ci.templateType = static_cast<VkDescriptorUpdateTemplateType>(r.read_u32());
        ci.descriptorSetLayout = d.mapper_.get_dsl(r.read_handle());
        ci.pipelineBindPoint = static_cast<VkPipelineBindPoint>(r.read_u32());
        ci.pipelineLayout = d.mapper_.get_pipeline_layout(r.read_handle());
        ci.set = r.read_u32();
        r.skip(sizeof(VkAllocationCallbacks));
        uint64_t pTpl = r.read_handle();
        VkDescriptorUpdateTemplate tpl;
        if (vkCreateDescriptorUpdateTemplate(d.mapper_.device(), &ci, nullptr, &tpl) == VK_SUCCESS) {
            d.mapper_.store_descriptor_update_template(pTpl, tpl);
        }
    });
    REGISTER(fbs::FunctionId_vkDestroyDescriptorUpdateTemplate, [](auto& d, auto& r) {
        r.read_handle(); auto t = r.read_handle(); r.skip(sizeof(VkAllocationCallbacks));
        auto tpl = d.mapper_.get_descriptor_update_template(t);
        if (tpl) vkDestroyDescriptorUpdateTemplate(d.mapper_.device(), tpl, nullptr);
        d.mapper_.remove_descriptor_update_template(t);
    });
    REGISTER(fbs::FunctionId_vkUpdateDescriptorSetWithTemplate, [](auto& d, auto& r) {
        auto dev = d.mapper_.device();
        r.read_handle(); // device
        uint64_t gDS = r.read_handle();
        uint64_t gTpl = r.read_handle();
        auto ds = d.mapper_.get_ds(gDS);
        auto tpl = d.mapper_.get_descriptor_update_template(gTpl);
        uint64_t remaining = r.remaining();
        std::vector<uint8_t> data(static_cast<size_t>(remaining));
        if (remaining > 0) r.read_raw(data.data(), static_cast<size_t>(remaining));
        if (dev && ds && tpl) {
            vkUpdateDescriptorSetWithTemplate(dev, ds, tpl, data.data());
        }
    });

    // --- Sampler YCbCr (1.1 promoted) ---
    REGISTER(fbs::FunctionId_vkCreateSamplerYcbcrConversion, [](auto& d, auto& r) {
        r.read_handle(); r.skip(sizeof(VkSamplerYcbcrConversionCreateInfo));
        r.skip(sizeof(VkAllocationCallbacks)); r.read_handle();
    });
    REGISTER(fbs::FunctionId_vkDestroySamplerYcbcrConversion, [](auto& d, auto& r) {
        r.read_handle(); r.read_handle(); r.skip(sizeof(VkAllocationCallbacks));
    });

    // --- Bind sparse ---
    REGISTER(fbs::FunctionId_vkQueueBindSparse, [](auto& d, auto& r) {
        r.read_handle(); uint32_t c = r.read_u32();
        r.skip(c * sizeof(VkBindSparseInfo)); r.read_handle();
    });

    // --- Device groups ---
    REGISTER(fbs::FunctionId_vkCmdSetDeviceMask, [](auto& d, auto& r) {
        auto cb = d.mapper_.active_cmd(); r.read_handle(); r.read_u32();
        if (cb) vkCmdSetDeviceMask(cb, 1);
    });
    REGISTER(fbs::FunctionId_vkGetDeviceGroupPeerMemoryFeatures, [](auto& d, auto& r) {
        r.read_handle(); r.read_u32(); r.read_u32(); r.read_u32(); r.read_handle();
    });

    // --- Dispatch base ---
    REGISTER(fbs::FunctionId_vkCmdDispatchBase, [](auto& d, auto& r) {
        auto cb = d.mapper_.active_cmd(); r.read_handle();
        r.read_u32(); r.read_u32(); r.read_u32(); // baseGroupX/Y/Z
        uint32_t x = r.read_u32(); uint32_t y = r.read_u32(); uint32_t z = r.read_u32();
        if (cb) vkCmdDispatchBase(cb, 0, 0, 0, x, y, z);
    });

    // --- Memory requirements (3) ---
    REGISTER(fbs::FunctionId_vkGetDeviceBufferMemoryRequirements, [](auto& d, auto& r) {
        r.read_handle(); r.skip(sizeof(VkDeviceBufferMemoryRequirements));
        r.skip(sizeof(VkMemoryRequirements2));
    });
    REGISTER(fbs::FunctionId_vkGetDeviceImageMemoryRequirements, [](auto& d, auto& r) {
        r.read_handle(); r.skip(sizeof(VkDeviceImageMemoryRequirements));
        r.skip(sizeof(VkMemoryRequirements2));
    });
    REGISTER(fbs::FunctionId_vkGetDeviceImageSparseMemoryRequirements, [](auto& d, auto& r) {
        r.read_handle(); r.skip(sizeof(VkDeviceImageMemoryRequirements));
        r.read_handle(); r.read_handle();
    });

    // --- Bind buffer/image 2 (1.1) ---
    REGISTER(fbs::FunctionId_vkBindBufferMemory2, [](auto& d, auto& r) {
        VkDevice dev = d.mapper_.device();
        r.read_handle(); // device
        uint32_t count = r.read_u32();
        std::vector<VkBindBufferMemoryInfo> infos(count);
        std::vector<uint64_t> guestBufs(count);
        for (uint32_t i = 0; i < count; i++) {
            r.read_raw(&infos[i], sizeof(VkBindBufferMemoryInfo));
            infos[i].pNext = nullptr;
            guestBufs[i] = handle_to_u64(infos[i].buffer);
            infos[i].buffer = d.mapper_.get_buffer(guestBufs[i]);
            infos[i].memory = d.mapper_.get_device_memory(handle_to_u64(infos[i].memory));
        }
        if (dev && count > 0) {
            auto pfnBindBufferMemory2 = reinterpret_cast<PFN_vkBindBufferMemory2>(
                vkGetDeviceProcAddr(dev, "vkBindBufferMemory2"));
            if (pfnBindBufferMemory2) {
                pfnBindBufferMemory2(dev, count, infos.data());
                // Track buffer bindings + cache the real GPU addresses
                for (uint32_t i = 0; i < count; i++) {
                    uint64_t guestMem = handle_to_u64(infos[i].memory);
                    if (d.memoryToBuffer_.find(guestMem) == d.memoryToBuffer_.end()) {
                        d.memoryToBuffer_[guestMem] = guestBufs[i];
                    }
                    VkBufferDeviceAddressInfo bdai{};
                    bdai.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
                    bdai.buffer = infos[i].buffer;
                    uint64_t addr = vkGetBufferDeviceAddress(dev, &bdai);
                    d.bufferAddresses_[guestBufs[i]] = addr;
                }
            }
        }
    });
    REGISTER(fbs::FunctionId_vkBindImageMemory2, [](auto& d, auto& r) {
        VkDevice dev = d.mapper_.device();
        r.read_handle(); // device
        uint32_t count = r.read_u32();
        std::vector<VkBindImageMemoryInfo> infos(count);
        for (uint32_t i = 0; i < count; i++) {
            r.read_raw(&infos[i], sizeof(VkBindImageMemoryInfo));
            infos[i].pNext = nullptr;
            infos[i].image = d.mapper_.get_image(handle_to_u64(infos[i].image));
            infos[i].memory = d.mapper_.get_device_memory(handle_to_u64(infos[i].memory));
        }
        if (dev && count > 0) {
            auto pfnBindImageMemory2 = reinterpret_cast<PFN_vkBindImageMemory2>(
                vkGetDeviceProcAddr(dev, "vkBindImageMemory2"));
            if (pfnBindImageMemory2) {
                pfnBindImageMemory2(dev, count, infos.data());
            }
        }
    });
    REGISTER(fbs::FunctionId_vkTrimCommandPool, [](auto& d, auto& r) {
        r.read_handle(); r.read_handle(); r.read_u32();
    });
    REGISTER(fbs::FunctionId_vkMergePipelineCaches, [](auto& d, auto& r) {
        r.read_handle(); r.read_handle(); uint32_t c = r.read_u32();
        r.skip(c * sizeof(uint64_t));
    });

    // --- Wait/Signal semaphore (1.2) ---
    REGISTER(fbs::FunctionId_vkWaitSemaphores, [](auto& d, auto& r) {
        VkDevice dev = d.mapper_.device();
        r.read_handle(); // device
        VkSemaphoreWaitInfo wi{};
        read_VkSemaphoreWaitInfo(r, &wi);
        uint64_t timeout = r.read_u64();
        // Remap semaphore handles
        for (uint32_t i = 0; i < wi.semaphoreCount; i++)
            const_cast<VkSemaphore*>(wi.pSemaphores)[i] =
                d.mapper_.get_semaphore(handle_to_u64(wi.pSemaphores[i]));
        VkResult res = vkWaitSemaphores(dev, &wi, timeout);
        if (res != VK_SUCCESS)
            SPDLOG_WARN("vkWaitSemaphores: result={}", static_cast<int>(res));
        free_VkSemaphoreWaitInfo(&wi);
    });
    REGISTER(fbs::FunctionId_vkSignalSemaphore, [](auto& d, auto& r) {
        VkDevice dev = d.mapper_.device();
        r.read_handle(); // device
        VkSemaphoreSignalInfo si{};
        read_VkSemaphoreSignalInfo(r, &si);
        si.semaphore = d.mapper_.get_semaphore(handle_to_u64(si.semaphore));
        vkSignalSemaphore(dev, &si);
    });
    REGISTER(fbs::FunctionId_vkGetSemaphoreCounterValue, [](auto& d, auto& r) {
        VkDevice dev = d.mapper_.device();
        r.read_handle(); // device
        VkSemaphore sem = d.mapper_.get_semaphore(r.read_handle());
        r.read_handle(); // pValue output pointer (ignore)
        uint64_t val = 0;
        vkGetSemaphoreCounterValue(dev, sem, &val);
    });

    // --- Reset query pool (1.2) ---
    REGISTER(fbs::FunctionId_vkResetQueryPool, [](auto& d, auto& r) {
        r.read_handle(); auto qp = r.read_handle();
        uint32_t firstQuery = r.read_u32(); uint32_t queryCount = r.read_u32();
        auto qph = d.mapper_.get_query_pool(qp);
        if (qph) vkResetQueryPool(d.mapper_.device(), qph, firstQuery, queryCount);
    });

    // --- Reset/Set event 2 (1.3) ---
    REGISTER(fbs::FunctionId_vkCmdResetEvent2, [](auto& d, auto& r) {
        auto cb = d.mapper_.active_cmd(); r.read_handle();
        uint64_t evt = r.read_handle(); r.read_u64(); // stageMask2
        auto e = d.mapper_.get_event(evt);
        if (cb && e) vkCmdResetEvent2(cb, e, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT);
    });
    REGISTER(fbs::FunctionId_vkCmdSetEvent2, [](auto& d, auto& r) {
        auto cb = d.mapper_.active_cmd(); r.read_handle();
        uint64_t evt = r.read_handle();
        auto e = d.mapper_.get_event(evt);
        VkDependencyInfo di{};
        read_VkDependencyInfo(r, &di);
        // Remap buffer and image handles
        for (uint32_t j = 0; j < di.bufferMemoryBarrierCount; j++)
            const_cast<VkBufferMemoryBarrier2*>(di.pBufferMemoryBarriers)[j].buffer =
                d.mapper_.get_buffer(handle_to_u64(di.pBufferMemoryBarriers[j].buffer));
        for (uint32_t j = 0; j < di.imageMemoryBarrierCount; j++)
            const_cast<VkImageMemoryBarrier2*>(di.pImageMemoryBarriers)[j].image =
                d.mapper_.get_image(handle_to_u64(di.pImageMemoryBarriers[j].image));
        if (cb && e) vkCmdSetEvent2(cb, e, &di);
        free_VkDependencyInfo(&di);
    });
    REGISTER(fbs::FunctionId_vkCmdWaitEvents2, [](auto& d, auto& r) {
        auto cb = d.mapper_.active_cmd(); r.read_handle();
        uint32_t ec = r.read_u32();
        std::vector<VkEvent> hostEvents(ec);
        for (uint32_t i = 0; i < ec; i++)
            hostEvents[i] = d.mapper_.get_event(r.read_handle());

        std::vector<VkDependencyInfo> depInfos(ec);
        for (uint32_t i = 0; i < ec; i++) {
            read_VkDependencyInfo(r, &depInfos[i]);
            for (uint32_t j = 0; j < depInfos[i].bufferMemoryBarrierCount; j++)
                const_cast<VkBufferMemoryBarrier2*>(depInfos[i].pBufferMemoryBarriers)[j].buffer =
                    d.mapper_.get_buffer(handle_to_u64(depInfos[i].pBufferMemoryBarriers[j].buffer));
            for (uint32_t j = 0; j < depInfos[i].imageMemoryBarrierCount; j++)
                const_cast<VkImageMemoryBarrier2*>(depInfos[i].pImageMemoryBarriers)[j].image =
                    d.mapper_.get_image(handle_to_u64(depInfos[i].pImageMemoryBarriers[j].image));
        }

        if (cb) {
            auto func = reinterpret_cast<PFN_vkCmdWaitEvents2>(
                vkGetDeviceProcAddr(d.mapper_.device(), "vkCmdWaitEvents2"));
            if (func)
                func(cb, ec, hostEvents.data(), depInfos.data());
        }
        for (auto& di : depInfos) free_VkDependencyInfo(&di);
    });
    REGISTER(fbs::FunctionId_vkCmdWriteTimestamp2, [](auto& d, auto& r) {
        auto cb = d.mapper_.active_cmd(); r.read_handle();
        r.read_u64(); uint64_t qp = r.read_handle(); uint32_t q = r.read_u32();
        auto qph = d.mapper_.get_query_pool(qp);
        if (cb && qph) vkCmdWriteTimestamp2(cb, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, qph, q);
    });

    // --- Blit / Resolve / CopyBufferToImage 2 (1.3) ---
    REGISTER(fbs::FunctionId_vkCmdBlitImage2, [](auto& d, auto& r) {
        auto cb = d.mapper_.active_cmd(); r.read_handle();
        VkImage src = d.mapper_.get_image(r.read_handle());
        VkImageLayout srcLayout = static_cast<VkImageLayout>(r.read_u32());
        VkImage dst = d.mapper_.get_image(r.read_handle());
        VkImageLayout dstLayout = static_cast<VkImageLayout>(r.read_u32());
        uint32_t regionCount = r.read_u32();
        std::vector<VkImageBlit2> regions(regionCount);
        for (uint32_t i = 0; i < regionCount; i++) {
            r.read_raw(&regions[i], sizeof(VkImageBlit2));
            regions[i].pNext = nullptr;
        }
        VkFilter filter = static_cast<VkFilter>(r.read_u32());

        VkBlitImageInfo2 ci{};
        ci.sType = VK_STRUCTURE_TYPE_BLIT_IMAGE_INFO_2;
        ci.pNext = nullptr;
        ci.srcImage = src;
        ci.srcImageLayout = srcLayout;
        ci.dstImage = dst;
        ci.dstImageLayout = dstLayout;
        ci.regionCount = regionCount;
        ci.pRegions = regions.data();
        ci.filter = filter;
        if (cb && src && dst) {
            auto pfn = reinterpret_cast<PFN_vkCmdBlitImage2>(
                vkGetDeviceProcAddr(d.mapper_.device(), "vkCmdBlitImage2"));
            if (pfn) pfn(cb, &ci);
        }
    });
    REGISTER(fbs::FunctionId_vkCmdDispatchIndirect, [](auto& d, auto& r) {
        auto cb = d.mapper_.active_cmd(); r.read_handle();
        uint64_t buf = r.read_handle(); uint64_t off = r.read_u64();
        auto b = d.mapper_.get_buffer(buf);
        if (!b && buf != 0) {
            SPDLOG_ERROR("vkCmdDispatchIndirect: buffer {:#x} not found — dispatch skipped!", buf);
        }
        if (cb && b) vkCmdDispatchIndirect(cb, b, off);
    });
    #undef REGISTER
}

CommandDispatcher::~CommandDispatcher() {
    mapper_.cleanup();
}

void CommandDispatcher::cleanup() {
    mapper_.cleanup();
    mapper_.set_device(VK_NULL_HANDLE, VK_NULL_HANDLE, 0, VK_NULL_HANDLE);
}

void CommandDispatcher::invalidate_and_send_memory(uint64_t guestMem, uint64_t offset, uint64_t size) {
    auto dev = mapper_.device();
    if (!dev || !sendDataFn_ || size == 0) return;

    VkDeviceMemory hostMem = mapper_.get_device_memory(guestMem);
    if (hostMem == VK_NULL_HANDLE) {
        SPDLOG_WARN("invalidate_and_send_memory: guest mem {:#x} not found", guestMem);
        return;
    }

    if (size == VK_WHOLE_SIZE) {
        auto it = memorySizes_.find(guestMem);
        size = (it != memorySizes_.end()) ? it->second : 0;
        if (size == 0) return;
    }

    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(physDev_, &props);
    uint64_t alignment = props.limits.minMemoryMapAlignment;
    if (alignment == 0) alignment = 64;

    uint64_t aligned_offset = (offset / alignment) * alignment;
    uint64_t alignment_diff = offset - aligned_offset;
    uint64_t aligned_size = size + alignment_diff;

    void* mapped = nullptr;
    VkResult res = vkMapMemory(dev, hostMem, aligned_offset, aligned_size, 0, &mapped);
    if (res == VK_SUCCESS && mapped) {
        const uint8_t* source = static_cast<const uint8_t*>(mapped) + alignment_diff;
        sendDataFn_(guestMem, source, static_cast<size_t>(size), offset);
        vkUnmapMemory(dev, hostMem);
        SPDLOG_INFO("invalidate: sent mem={:#x} offset={} size={}KB via vkMapMemory", guestMem, offset, size/1024);
    } else {
        std::vector<uint8_t> readback_buf(static_cast<size_t>(size));
        if (download_from_device_memory(hostMem, offset, readback_buf.data(), static_cast<size_t>(size))) {
            sendDataFn_(guestMem, readback_buf.data(), static_cast<size_t>(size), offset);
            SPDLOG_INFO("invalidate: sent mem={:#x} offset={} size={}KB via staging", guestMem, offset, size/1024);
        } else {
            SPDLOG_WARN("invalidate: download failed for mem={:#x}", guestMem);
        }
    }
}

void CommandDispatcher::readback_all_buffers() {
    auto dev = mapper_.device();
    if (!dev || !sendDataFn_) return;

    static constexpr size_t kMaxChunk = 1024 * 1024; // 1 MB chunks

    // In compute mode, only readback small buffers (<1MB) — typically output/state,
    // not large model weights which don't change between dispatches.
    static constexpr size_t kComputeMaxReadback = 1024 * 1024;

    static std::atomic<bool> s_checked_large{false};

    for (const auto& [guestHandle, allocSize] : memorySizes_) {
        VkDeviceMemory hostMem = mapper_.get_device_memory(guestHandle);
        if (hostMem == VK_NULL_HANDLE || allocSize == 0) continue;

        // Verify weight buffer integrity: read back first 1MB of large buffers ONCE
        if (isComputeMode_ && !weightVerified_ && allocSize > 100ULL * 1024 * 1024) {
            weightVerified_ = true;
            void* lmapped = nullptr;
            size_t checkSize = static_cast<size_t>(std::min<VkDeviceSize>(allocSize, 1024ULL * 1024));
            VkResult lres = vkMapMemory(dev, hostMem, 0, checkSize, 0, &lmapped);
            if (lres == VK_SUCCESS && lmapped) {
                const uint8_t* p = static_cast<const uint8_t*>(lmapped);
                SPDLOG_INFO("WEIGHT_VERIFY: mem={:#x} total={}MB first16={:02x} {:02x} {:02x} {:02x} {:02x} {:02x} {:02x} {:02x} {:02x} {:02x} {:02x} {:02x} {:02x} {:02x} {:02x} {:02x}",
                    guestHandle, allocSize / (1024*1024),
                    p[0], p[1], p[2], p[3], p[4], p[5], p[6], p[7],
                    p[8], p[9], p[10], p[11], p[12], p[13], p[14], p[15]);
                vkUnmapMemory(dev, hostMem);
            } else {
                // Pure VRAM: use staging download
                std::vector<uint8_t> buf(checkSize);
                if (download_from_device_memory(hostMem, 0, buf.data(), checkSize)) {
                    const uint8_t* p = buf.data();
                    SPDLOG_INFO("WEIGHT_VERIFY: mem={:#x} total={}MB (via staging) first16={:02x} {:02x} {:02x} {:02x} {:02x} {:02x} {:02x} {:02x} {:02x} {:02x} {:02x} {:02x} {:02x} {:02x} {:02x} {:02x}",
                        guestHandle, allocSize / (1024*1024),
                        p[0], p[1], p[2], p[3], p[4], p[5], p[6], p[7],
                        p[8], p[9], p[10], p[11], p[12], p[13], p[14], p[15]);
                } else {
                    SPDLOG_WARN("WEIGHT_VERIFY: staging download failed for mem={:#x}", guestHandle);
                }
            }
        }

        // Skip large buffers (model weights) — they don't change
        if (isComputeMode_ && allocSize > kComputeMaxReadback) continue;
        if (allocSize > 128 * 1024 * 1024) continue;

        void* mapped = nullptr;
        VkResult res = vkMapMemory(dev, hostMem, 0, allocSize, 0, &mapped);
        if (res != VK_SUCCESS || !mapped) {
            // Non-host-visible memory (pure VRAM): use staging buffer fallback
            if (allocSize <= kMaxChunk) {
                std::vector<uint8_t> readback_buf(static_cast<size_t>(allocSize));
                if (download_from_device_memory(hostMem, 0, readback_buf.data(), static_cast<size_t>(allocSize))) {
                    sendDataFn_(guestHandle, readback_buf.data(), static_cast<size_t>(allocSize), 0);
                    SPDLOG_INFO("readback: mem={:#x} size={}KB sent via staging", guestHandle, allocSize / 1024);
                } else {
                    SPDLOG_WARN("readback: staging download failed for mem={:#x} size={}KB", guestHandle, allocSize / 1024);
                }
            } else {
                SPDLOG_WARN("readback: mem={:#x} size={}KB not host-visible, too large for staging", guestHandle, allocSize / 1024);
            }
            continue;
        }

        // Dump first 16 bytes of readback data for small compute output buffers
        if (isComputeMode_ && allocSize <= 256 * 1024) {
            const uint8_t* p = static_cast<const uint8_t*>(mapped);
            SPDLOG_INFO("READBACK_DATA: mem={:#x} size={} first16={:02x} {:02x} {:02x} {:02x} {:02x} {:02x} {:02x} {:02x} {:02x} {:02x} {:02x} {:02x} {:02x} {:02x} {:02x} {:02x}",
                guestHandle, allocSize,
                p[0], p[1], p[2], p[3], p[4], p[5], p[6], p[7],
                p[8], p[9], p[10], p[11], p[12], p[13], p[14], p[15]);
        }

        // Chunk and send
        const uint8_t* src = static_cast<const uint8_t*>(mapped);
        size_t remaining = static_cast<size_t>(allocSize);
        VkDeviceSize offset = 0;
        while (remaining > 0) {
            size_t chunk = std::min(remaining, kMaxChunk);
            sendDataFn_(guestHandle, src + offset, chunk, offset);
            offset += chunk;
            remaining -= chunk;
        }
        vkUnmapMemory(dev, hostMem);

        SPDLOG_INFO("readback: mem={:#x} size={}KB sent to guest", guestHandle, allocSize / 1024);
    }
}

void CommandDispatcher::set_device(VkPhysicalDevice physDev, VkDevice device,
                                    VkQueue queue, uint32_t queueFamily,
                                    VkCommandPool cmdPool) {
    mapper_.set_device(device, queue, queueFamily, cmdPool);
    physDev_ = physDev;
    cache_device_procs(device);
}

void CommandDispatcher::cache_device_procs(VkDevice dev) {
    pfnCmdPipelineBarrier2_ = reinterpret_cast<void*>(vkGetDeviceProcAddr(dev, "vkCmdPipelineBarrier2"));
    pfnCmdCopyBuffer2_ = reinterpret_cast<void*>(vkGetDeviceProcAddr(dev, "vkCmdCopyBuffer2"));
    pfnCmdCopyImage2_ = reinterpret_cast<void*>(vkGetDeviceProcAddr(dev, "vkCmdCopyImage2"));
    pfnCmdCopyBufferToImage2_ = reinterpret_cast<void*>(vkGetDeviceProcAddr(dev, "vkCmdCopyBufferToImage2"));
    pfnCmdCopyImageToBuffer2_ = reinterpret_cast<void*>(vkGetDeviceProcAddr(dev, "vkCmdCopyImageToBuffer2"));
    pfnCmdBlitImage2_ = reinterpret_cast<void*>(vkGetDeviceProcAddr(dev, "vkCmdBlitImage2"));
    pfnQueueSubmit2_ = reinterpret_cast<void*>(vkGetDeviceProcAddr(dev, "vkQueueSubmit2"));
    pfnCmdWaitEvents2_ = reinterpret_cast<void*>(vkGetDeviceProcAddr(dev, "vkCmdWaitEvents2"));
    pfnBindBufferMemory2_ = reinterpret_cast<void*>(vkGetDeviceProcAddr(dev, "vkBindBufferMemory2"));
    pfnBindImageMemory2_ = reinterpret_cast<void*>(vkGetDeviceProcAddr(dev, "vkBindImageMemory2"));
    pfnGetSemaphoreCounterValue_ = reinterpret_cast<void*>(vkGetDeviceProcAddr(dev, "vkGetSemaphoreCounterValue"));
}

void CommandDispatcher::dispatch(fbs::FunctionId func_id,
                                  const uint8_t* args, size_t args_size) {
    auto it = handlers_.find(static_cast<int>(func_id));
    if (it == handlers_.end()) {
        SPDLOG_DEBUG("Host: no handler for {} (id={})",
                     fbs::EnumNameFunctionId(func_id), static_cast<int>(func_id));
        return;
    }

    VulkanDeserializer reader(args, args_size);
    it->second(*this, reader);
}

void ResourceMapper::cleanup() {
    VkDevice dev = device_;
    if (dev == VK_NULL_HANDLE) return;

    // Destroy order: dependent resources first, then their parents

    // 1. Pipeline layouts (may reference descriptor set layouts)
    for (auto& [_, v] : pipelineLayouts_) if (v) vkDestroyPipelineLayout(dev, v, nullptr);

    // 2. Pipelines (may reference render passes, shader modules, pipeline layouts)
    for (auto& [_, v] : pipelines_) if (v) vkDestroyPipeline(dev, v, nullptr);
    for (auto& [_, v] : pipelineCaches_) if (v) vkDestroyPipelineCache(dev, v, nullptr);

    // 3. Image views (reference images)
    for (auto& [_, v] : imageViews_) if (v) vkDestroyImageView(dev, v, nullptr);

    // 4. Images (backed by memory, must be destroyed before memory)
    for (auto& [_, v] : images_) if (v) vkDestroyImage(dev, v, nullptr);

    // 5. Buffers (backed by memory, must be destroyed before memory)
    for (auto& [_, v] : buffers_) if (v) vkDestroyBuffer(dev, v, nullptr);
    for (auto& [_, v] : samplers_) if (v) vkDestroySampler(dev, v, nullptr);
    for (auto& [_, v] : shaderModules_) if (v) vkDestroyShaderModule(dev, v, nullptr);

    // 6. Descriptor sets (must be freed before pool)
    dss_.clear();

    // 7. Descriptor pools
    for (auto& [_, v] : dps_) if (v) vkDestroyDescriptorPool(dev, v, nullptr);

    // 8. Descriptor set layouts
    for (auto& [_, v] : dsls_) if (v) vkDestroyDescriptorSetLayout(dev, v, nullptr);

    // 9. Descriptor update templates
    for (auto& [_, v] : duts_) if (v) vkDestroyDescriptorUpdateTemplate(dev, v, nullptr);

    // 10. Query pools
    for (auto& [_, v] : queryPools_) if (v) vkDestroyQueryPool(dev, v, nullptr);

    // 11. Events
    for (auto& [_, v] : events_) if (v) vkDestroyEvent(dev, v, nullptr);

    // 12. Semaphores
    for (auto& [_, v] : semaphores_) if (v) vkDestroySemaphore(dev, v, nullptr);

    // 13. Fences
    for (auto& [_, v] : fences_) if (v) vkDestroyFence(dev, v, nullptr);

    // 16. Command pools (destroys all command buffers allocated from them)
    for (auto& [_, v] : cmdPools_) if (v) vkDestroyCommandPool(dev, v, nullptr);
    cmdBufs_.clear();

    // 17. Private data slots
    for (auto& [_, v] : privateDataSlots_) if (v) vkDestroyPrivateDataSlot(dev, v, nullptr);

    // 18. Device memory (must be freed LAST, after all resources using it)
    for (auto& [_, v] : memories_) if (v) vkFreeMemory(dev, v, nullptr);
}

bool CommandDispatcher::upload_to_device_buffer(VkBuffer dst, VkDeviceSize dstOffset, const uint8_t* data, size_t size) {
    auto dev = mapper_.device();
    auto q = mapper_.queue();
    if (!dev || !q || !data || size == 0) return false;

    VkBuffer stagingBuf = VK_NULL_HANDLE;
    VkDeviceMemory stagingMem = VK_NULL_HANDLE;

    VkBufferCreateInfo bci{};
    bci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bci.size = size;
    bci.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    if (vkCreateBuffer(dev, &bci, nullptr, &stagingBuf) != VK_SUCCESS) return false;

    VkMemoryRequirements mr{};
    vkGetBufferMemoryRequirements(dev, stagingBuf, &mr);
    VkMemoryAllocateInfo mai{};
    mai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    mai.allocationSize = mr.size;

    VkPhysicalDeviceMemoryProperties mp{};
    vkGetPhysicalDeviceMemoryProperties(physDev_, &mp);
    bool found = false;
    for (uint32_t t = 0; t < mp.memoryTypeCount; t++) {
        if ((mr.memoryTypeBits & (1u << t)) &&
            (mp.memoryTypes[t].propertyFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)) {
            mai.memoryTypeIndex = t; found = true; break;
        }
    }
    if (!found || vkAllocateMemory(dev, &mai, nullptr, &stagingMem) != VK_SUCCESS ||
        vkBindBufferMemory(dev, stagingBuf, stagingMem, 0) != VK_SUCCESS) {
        vkDestroyBuffer(dev, stagingBuf, nullptr);
        return false;
    }

    void* sm = nullptr;
    if (vkMapMemory(dev, stagingMem, 0, size, 0, &sm) != VK_SUCCESS || !sm) {
        vkDestroyBuffer(dev, stagingBuf, nullptr);
        vkFreeMemory(dev, stagingMem, nullptr);
        return false;
    }
    std::memcpy(sm, data, size);
    vkUnmapMemory(dev, stagingMem);

    // Submit one-shot copy
    VkCommandBufferAllocateInfo cbai{};
    cbai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cbai.commandPool = mapper_.command_pool();
    cbai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cbai.commandBufferCount = 1;
    VkCommandBuffer cb = VK_NULL_HANDLE;
    if (vkAllocateCommandBuffers(dev, &cbai, &cb) != VK_SUCCESS) {
        vkDestroyBuffer(dev, stagingBuf, nullptr);
        vkFreeMemory(dev, stagingMem, nullptr);
        return false;
    }

    VkCommandBufferBeginInfo bi{};
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cb, &bi);

    VkBufferCopy region{};
    region.srcOffset = 0;
    region.dstOffset = dstOffset;
    region.size = size;
    vkCmdCopyBuffer(cb, stagingBuf, dst, 1, &region);

    vkEndCommandBuffer(cb);

    VkSubmitInfo si{};
    si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.commandBufferCount = 1;
    si.pCommandBuffers = &cb;
    VkFence fence = VK_NULL_HANDLE;
    VkFenceCreateInfo fci{};
    fci.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    vkCreateFence(dev, &fci, nullptr, &fence);
    vkQueueSubmit(q, 1, &si, fence);
    vkWaitForFences(dev, 1, &fence, VK_TRUE, UINT64_MAX);
    vkDestroyFence(dev, fence, nullptr);
    vkFreeCommandBuffers(dev, mapper_.command_pool(), 1, &cb);

    vkDestroyBuffer(dev, stagingBuf, nullptr);
    vkFreeMemory(dev, stagingMem, nullptr);
    return true;
}

bool CommandDispatcher::upload_to_device_memory(VkDeviceMemory dstMem, VkDeviceSize dstOffset, const uint8_t* data, size_t size) {
    auto dev = mapper_.device();
    auto q = mapper_.queue();
    if (!dev || !q || !dstMem || !data || size == 0) return false;

    // 1. Create Staging Buffer (HOST_VISIBLE)
    VkBuffer stagingBuf = VK_NULL_HANDLE;
    VkDeviceMemory stagingMem = VK_NULL_HANDLE;

    VkBufferCreateInfo bci{};
    bci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bci.size = size;
    bci.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    if (vkCreateBuffer(dev, &bci, nullptr, &stagingBuf) != VK_SUCCESS) return false;

    VkMemoryRequirements mr{};
    vkGetBufferMemoryRequirements(dev, stagingBuf, &mr);
    VkMemoryAllocateInfo mai{};
    mai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    mai.allocationSize = mr.size;

    VkPhysicalDeviceMemoryProperties mp{};
    vkGetPhysicalDeviceMemoryProperties(physDev_, &mp);
    bool found = false;
    for (uint32_t t = 0; t < mp.memoryTypeCount; t++) {
        if ((mr.memoryTypeBits & (1u << t)) &&
            (mp.memoryTypes[t].propertyFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)) {
            mai.memoryTypeIndex = t; found = true; break;
        }
    }
    if (!found || vkAllocateMemory(dev, &mai, nullptr, &stagingMem) != VK_SUCCESS ||
        vkBindBufferMemory(dev, stagingBuf, stagingMem, 0) != VK_SUCCESS) {
        if (stagingBuf) vkDestroyBuffer(dev, stagingBuf, nullptr);
        if (stagingMem) vkFreeMemory(dev, stagingMem, nullptr);
        return false;
    }

    void* sm = nullptr;
    if (vkMapMemory(dev, stagingMem, 0, size, 0, &sm) != VK_SUCCESS || !sm) {
        vkDestroyBuffer(dev, stagingBuf, nullptr);
        vkFreeMemory(dev, stagingMem, nullptr);
        return false;
    }
    std::memcpy(sm, data, size);
    vkUnmapMemory(dev, stagingMem);

    // Data integrity: log first chunk for large uploads
    if (dstOffset == 0 && size >= 16) {
        SPDLOG_INFO("UPLOAD_VRAM chunk: offset={} size={}KB first16={:02x} {:02x} {:02x} {:02x} {:02x} {:02x} {:02x} {:02x} {:02x} {:02x} {:02x} {:02x} {:02x} {:02x} {:02x} {:02x}",
            dstOffset, size/1024,
            data[0], data[1], data[2], data[3], data[4], data[5], data[6], data[7],
            data[8], data[9], data[10], data[11], data[12], data[13], data[14], data[15]);
    }

    // 2. Create Temporary Target Buffer bound to aligned dstMem offset
    VkBuffer targetBuf = VK_NULL_HANDLE;
    VkBufferCreateInfo targetBci{};
    targetBci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    targetBci.size = size;
    targetBci.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    if (vkCreateBuffer(dev, &targetBci, nullptr, &targetBuf) != VK_SUCCESS) {
        vkDestroyBuffer(dev, stagingBuf, nullptr);
        vkFreeMemory(dev, stagingMem, nullptr);
        return false;
    }

    VkMemoryRequirements targetMr{};
    vkGetBufferMemoryRequirements(dev, targetBuf, &targetMr);

    VkDeviceSize alignMask = targetMr.alignment > 0 ? targetMr.alignment : 1;
    VkDeviceSize alignedOffset = (dstOffset / alignMask) * alignMask;
    VkDeviceSize offsetDiff = dstOffset - alignedOffset;
    VkDeviceSize requiredSize = size + offsetDiff;

    if (requiredSize > targetMr.size) {
        vkDestroyBuffer(dev, targetBuf, nullptr);
        targetBci.size = requiredSize;
        if (vkCreateBuffer(dev, &targetBci, nullptr, &targetBuf) != VK_SUCCESS) {
            vkDestroyBuffer(dev, stagingBuf, nullptr);
            vkFreeMemory(dev, stagingMem, nullptr);
            return false;
        }
    }

    if (vkBindBufferMemory(dev, targetBuf, dstMem, alignedOffset) != VK_SUCCESS) {
        vkDestroyBuffer(dev, targetBuf, nullptr);
        vkDestroyBuffer(dev, stagingBuf, nullptr);
        vkFreeMemory(dev, stagingMem, nullptr);
        return false;
    }

    // 3. One-shot command buffer copy
    VkCommandBufferAllocateInfo cbai{};
    cbai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cbai.commandPool = mapper_.command_pool();
    cbai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cbai.commandBufferCount = 1;
    VkCommandBuffer cb = VK_NULL_HANDLE;
    if (vkAllocateCommandBuffers(dev, &cbai, &cb) != VK_SUCCESS) {
        vkDestroyBuffer(dev, targetBuf, nullptr);
        vkDestroyBuffer(dev, stagingBuf, nullptr);
        vkFreeMemory(dev, stagingMem, nullptr);
        return false;
    }

    VkCommandBufferBeginInfo bi{};
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cb, &bi);

    VkBufferCopy region{};
    region.srcOffset = 0;
    region.dstOffset = offsetDiff;
    region.size = size;
    vkCmdCopyBuffer(cb, stagingBuf, targetBuf, 1, &region);
    vkEndCommandBuffer(cb);

    VkSubmitInfo si{};
    si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.commandBufferCount = 1;
    si.pCommandBuffers = &cb;
    VkFence fence = VK_NULL_HANDLE;
    VkFenceCreateInfo fci{};
    fci.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    vkCreateFence(dev, &fci, nullptr, &fence);
    vkQueueSubmit(q, 1, &si, fence);
    vkWaitForFences(dev, 1, &fence, VK_TRUE, UINT64_MAX);
    vkDestroyFence(dev, fence, nullptr);
    vkFreeCommandBuffers(dev, mapper_.command_pool(), 1, &cb);

    vkDestroyBuffer(dev, targetBuf, nullptr);
    vkDestroyBuffer(dev, stagingBuf, nullptr);
    vkFreeMemory(dev, stagingMem, nullptr);
    return true;
}

bool CommandDispatcher::download_from_device_memory(VkDeviceMemory srcMem, VkDeviceSize srcOffset, uint8_t* outData, size_t size) {
    auto dev = mapper_.device();
    auto q = mapper_.queue();
    if (!dev || !q || !srcMem || !outData || size == 0) return false;

    // 1. Create Staging Buffer (HOST_VISIBLE | HOST_COHERENT)
    VkBuffer stagingBuf = VK_NULL_HANDLE;
    VkDeviceMemory stagingMem = VK_NULL_HANDLE;

    VkBufferCreateInfo bci{};
    bci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bci.size = size;
    bci.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    if (vkCreateBuffer(dev, &bci, nullptr, &stagingBuf) != VK_SUCCESS) return false;

    VkMemoryRequirements mr{};
    vkGetBufferMemoryRequirements(dev, stagingBuf, &mr);
    VkMemoryAllocateInfo mai{};
    mai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    mai.allocationSize = mr.size;

    VkPhysicalDeviceMemoryProperties mp{};
    vkGetPhysicalDeviceMemoryProperties(physDev_, &mp);
    bool found = false;
    for (uint32_t t = 0; t < mp.memoryTypeCount; t++) {
        if ((mr.memoryTypeBits & (1u << t)) &&
            (mp.memoryTypes[t].propertyFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)) {
            mai.memoryTypeIndex = t; found = true; break;
        }
    }
    if (!found || vkAllocateMemory(dev, &mai, nullptr, &stagingMem) != VK_SUCCESS ||
        vkBindBufferMemory(dev, stagingBuf, stagingMem, 0) != VK_SUCCESS) {
        if (stagingBuf) vkDestroyBuffer(dev, stagingBuf, nullptr);
        if (stagingMem) vkFreeMemory(dev, stagingMem, nullptr);
        return false;
    }

    // 2. Create Temporary Source Buffer bound to aligned srcMem offset
    VkBuffer srcBuf = VK_NULL_HANDLE;
    VkBufferCreateInfo srcBci{};
    srcBci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    srcBci.size = size;
    srcBci.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    if (vkCreateBuffer(dev, &srcBci, nullptr, &srcBuf) != VK_SUCCESS) {
        vkDestroyBuffer(dev, stagingBuf, nullptr);
        vkFreeMemory(dev, stagingMem, nullptr);
        return false;
    }

    VkMemoryRequirements srcMr{};
    vkGetBufferMemoryRequirements(dev, srcBuf, &srcMr);

    VkDeviceSize alignMask = srcMr.alignment > 0 ? srcMr.alignment : 1;
    VkDeviceSize alignedOffset = (srcOffset / alignMask) * alignMask;
    VkDeviceSize offsetDiff = srcOffset - alignedOffset;
    VkDeviceSize requiredSize = size + offsetDiff;

    if (requiredSize > srcMr.size) {
        vkDestroyBuffer(dev, srcBuf, nullptr);
        srcBci.size = requiredSize;
        if (vkCreateBuffer(dev, &srcBci, nullptr, &srcBuf) != VK_SUCCESS) {
            vkDestroyBuffer(dev, stagingBuf, nullptr);
            vkFreeMemory(dev, stagingMem, nullptr);
            return false;
        }
    }

    if (vkBindBufferMemory(dev, srcBuf, srcMem, alignedOffset) != VK_SUCCESS) {
        vkDestroyBuffer(dev, srcBuf, nullptr);
        vkDestroyBuffer(dev, stagingBuf, nullptr);
        vkFreeMemory(dev, stagingMem, nullptr);
        return false;
    }

    // 3. One-shot command buffer copy
    VkCommandBufferAllocateInfo cbai{};
    cbai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cbai.commandPool = mapper_.command_pool();
    cbai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cbai.commandBufferCount = 1;
    VkCommandBuffer cb = VK_NULL_HANDLE;
    if (vkAllocateCommandBuffers(dev, &cbai, &cb) != VK_SUCCESS) {
        vkDestroyBuffer(dev, srcBuf, nullptr);
        vkDestroyBuffer(dev, stagingBuf, nullptr);
        vkFreeMemory(dev, stagingMem, nullptr);
        return false;
    }

    VkCommandBufferBeginInfo bi{};
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cb, &bi);

    VkBufferCopy region{};
    region.srcOffset = offsetDiff;
    region.dstOffset = 0;
    region.size = size;
    vkCmdCopyBuffer(cb, srcBuf, stagingBuf, 1, &region);
    vkEndCommandBuffer(cb);

    VkSubmitInfo si{};
    si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.commandBufferCount = 1;
    si.pCommandBuffers = &cb;
    VkFence fence = VK_NULL_HANDLE;
    VkFenceCreateInfo fci{};
    fci.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    vkCreateFence(dev, &fci, nullptr, &fence);
    vkQueueSubmit(q, 1, &si, fence);
    vkWaitForFences(dev, 1, &fence, VK_TRUE, UINT64_MAX);
    vkDestroyFence(dev, fence, nullptr);

    void* sm = nullptr;
    if (vkMapMemory(dev, stagingMem, 0, size, 0, &sm) == VK_SUCCESS && sm) {
        std::memcpy(outData, sm, size);
        vkUnmapMemory(dev, stagingMem);
    }

    vkFreeCommandBuffers(dev, mapper_.command_pool(), 1, &cb);
    vkDestroyBuffer(dev, srcBuf, nullptr);
    vkDestroyBuffer(dev, stagingBuf, nullptr);
    vkFreeMemory(dev, stagingMem, nullptr);
    return true;
}

} // namespace omnigpu::host
