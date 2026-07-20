#include "buffer_manager.h"
#include <algorithm>
#include <chrono>
#include <cstring>
#include <spdlog/spdlog.h>

namespace omnigpu::host {

BufferManager::~BufferManager() { cleanup(); }

uint64_t BufferManager::now_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

uint32_t BufferManager::find_memory_type(uint32_t typeBits, VkMemoryPropertyFlags props) const {
    if (physDevice_ == VK_NULL_HANDLE) {
        // No physical device set — return first compatible type from bits
        for (uint32_t i = 0; i < 32; i++) {
            if ((typeBits & (1U << i)) != 0U) {
                return i;
            }
        }
        return 0;
    }
    VkPhysicalDeviceMemoryProperties mp;
    vkGetPhysicalDeviceMemoryProperties(physDevice_, &mp);
    for (uint32_t i = 0; i < mp.memoryTypeCount; i++) {
        if (((typeBits & (1U << i)) != 0U) &&
            (mp.memoryTypes[i].propertyFlags & props) == props) {
            return i;
        }
    }
    // Fallback: first compatible type
    for (uint32_t i = 0; i < mp.memoryTypeCount; i++) {
        if ((typeBits & (1U << i)) != 0U) {
            return i;
        }
    }
    SPDLOG_WARN("BufferManager: no memory type found for typeBits={:#x}, props={:#x}",
                typeBits, props);
    return 0;
}

bool BufferManager::create_staging(GpuBuffer& buf) {
    VkBufferCreateInfo bci{};
    bci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bci.size = buf.size;
    bci.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateBuffer(device_, &bci, nullptr, &buf.stagingBuffer) != VK_SUCCESS)
        return false;

    VkMemoryRequirements mr;
    vkGetBufferMemoryRequirements(device_, buf.stagingBuffer, &mr);

    VkMemoryAllocateInfo mai{};
    mai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    mai.allocationSize = mr.size;
    mai.memoryTypeIndex = find_memory_type(mr.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    if (vkAllocateMemory(device_, &mai, nullptr, &buf.stagingMemory) != VK_SUCCESS) {
        vkDestroyBuffer(device_, buf.stagingBuffer, nullptr);
        buf.stagingBuffer = VK_NULL_HANDLE;
        return false;
    }

    vkBindBufferMemory(device_, buf.stagingBuffer, buf.stagingMemory, 0);
    return true;
}

uint64_t BufferManager::create_buffer(VkDeviceSize size, VkBufferUsageFlags usage,
                                       int gpuIndex, bool persistent) {
    if (!device_) return 0;

    std::lock_guard<std::mutex> lock(mutex_);

    // For simplicity, create on the primary device.
    // Multi-GPU support would create on the specified gpuIndex device.
    VkBufferCreateInfo bci{};
    bci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bci.size = size;
    bci.usage = usage | VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VkBuffer buffer;
    if (vkCreateBuffer(device_, &bci, nullptr, &buffer) != VK_SUCCESS) {
        SPDLOG_ERROR("BufferManager: failed to create buffer (size={})", size);
        return 0;
    }

    VkMemoryRequirements mr;
    vkGetBufferMemoryRequirements(device_, buffer, &mr);

    // Prefer DEVICE_LOCAL for persistent, HOST_VISIBLE for non-persistent
    VkMemoryPropertyFlags memProps = persistent
        ? VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
        : (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    VkMemoryAllocateInfo mai{};
    mai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    mai.allocationSize = mr.size;
    mai.memoryTypeIndex = find_memory_type(mr.memoryTypeBits, memProps);

    VkDeviceMemory memory;
    if (vkAllocateMemory(device_, &mai, nullptr, &memory) != VK_SUCCESS) {
        vkDestroyBuffer(device_, buffer, nullptr);
        SPDLOG_ERROR("BufferManager: failed to allocate memory (size={})", size);
        return 0;
    }

    vkBindBufferMemory(device_, buffer, memory, 0);

    uint64_t id = nextId_++;
    GpuBuffer gpuBuf;
    gpuBuf.id = id;
    gpuBuf.buffer = buffer;
    gpuBuf.memory = memory;
    gpuBuf.size = size;
    gpuBuf.usage = usage;
    gpuBuf.memoryTypeIndex = mai.memoryTypeIndex;
    gpuBuf.gpuIndex = gpuIndex;
    gpuBuf.device = device_;
    gpuBuf.persistent = persistent;
    gpuBuf.lastAccess = now_ms();

    // Create staging for upload/download if persistent
    if (persistent && !create_staging(gpuBuf)) {
        vkFreeMemory(device_, memory, nullptr);
        vkDestroyBuffer(device_, buffer, nullptr);
        return 0;
    }

    buffers_[id] = std::move(gpuBuf);
    totalVramUsed_ += mr.size;

    SPDLOG_DEBUG("BufferManager: created buffer {} (size={}, persistent={})",
                 id, size, persistent);
    return id;
}

bool BufferManager::upload(uint64_t bufferId, const void* data,
                            VkDeviceSize size, VkDeviceSize offset) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = buffers_.find(bufferId);
    if (it == buffers_.end()) return false;

    auto& buf = it->second;
    buf.lastAccess = now_ms();

    if (buf.persistent && buf.stagingMemory) {
        // Upload via staging: map staging buffer → copy → GPU copy
        void* mapped;
        VkResult res = vkMapMemory(device_, buf.stagingMemory, offset, size, 0, &mapped);
        if (res != VK_SUCCESS) return false;
        std::memcpy(mapped, data, static_cast<size_t>(size));
        vkUnmapMemory(device_, buf.stagingMemory);

        // Record copy command: staging → device-local buffer
        VkCommandBufferAllocateInfo ai{};
        ai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        ai.commandPool = cmdPool_;
        ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        ai.commandBufferCount = 1;

        VkCommandBuffer cmd;
        if (vkAllocateCommandBuffers(device_, &ai, &cmd) != VK_SUCCESS) return false;

        VkCommandBufferBeginInfo bi{};
        bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(cmd, &bi);

        VkBufferCopy region{};
        region.srcOffset = offset;
        region.dstOffset = offset;
        region.size = size;
        vkCmdCopyBuffer(cmd, buf.stagingBuffer, buf.buffer, 1, &region);
        vkEndCommandBuffer(cmd);

        VkSubmitInfo si{};
        si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        si.commandBufferCount = 1;
        si.pCommandBuffers = &cmd;

        VkFence fence;
        VkFenceCreateInfo fci{};
        fci.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        vkCreateFence(device_, &fci, nullptr, &fence);
        vkQueueSubmit(queue_, 1, &si, fence);
        vkWaitForFences(device_, 1, &fence, VK_TRUE, UINT64_MAX);

        vkDestroyFence(device_, fence, nullptr);
        vkFreeCommandBuffers(device_, cmdPool_, 1, &cmd);
    } else {
        // Direct upload to host-visible memory
        void* mapped;
        VkResult res = vkMapMemory(device_, buf.memory, offset, size, 0, &mapped);
        if (res != VK_SUCCESS) return false;
        std::memcpy(mapped, data, static_cast<size_t>(size));
        vkUnmapMemory(device_, buf.memory);
    }

    buf.dirty = false;
    return true;
}

bool BufferManager::download(uint64_t bufferId, void* outData,
                              VkDeviceSize size, VkDeviceSize offset) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = buffers_.find(bufferId);
    if (it == buffers_.end()) return false;

    auto& buf = it->second;
    buf.lastAccess = now_ms();

    if (buf.persistent && buf.stagingMemory) {
        // Copy device → staging → read
        VkCommandBufferAllocateInfo ai{};
        ai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        ai.commandPool = cmdPool_;
        ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        ai.commandBufferCount = 1;

        VkCommandBuffer cmd;
        if (vkAllocateCommandBuffers(device_, &ai, &cmd) != VK_SUCCESS) return false;

        VkCommandBufferBeginInfo bi{};
        bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(cmd, &bi);

        VkBufferCopy region{};
        region.srcOffset = offset;
        region.dstOffset = offset;
        region.size = size;
        vkCmdCopyBuffer(cmd, buf.buffer, buf.stagingBuffer, 1, &region);
        vkEndCommandBuffer(cmd);

        VkSubmitInfo si{};
        si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        si.commandBufferCount = 1;
        si.pCommandBuffers = &cmd;

        VkFence fence;
        VkFenceCreateInfo fci{};
        fci.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        vkCreateFence(device_, &fci, nullptr, &fence);
        vkQueueSubmit(queue_, 1, &si, fence);
        vkWaitForFences(device_, 1, &fence, VK_TRUE, UINT64_MAX);

        void* mapped;
        if (vkMapMemory(device_, buf.stagingMemory, offset, size, 0, &mapped) == VK_SUCCESS) {
            std::memcpy(outData, mapped, static_cast<size_t>(size));
            vkUnmapMemory(device_, buf.stagingMemory);
        }

        vkDestroyFence(device_, fence, nullptr);
        vkFreeCommandBuffers(device_, cmdPool_, 1, &cmd);
        return true;
    }

    // Direct read from host-visible memory
    void* mapped;
    if (vkMapMemory(device_, buf.memory, offset, size, 0, &mapped) != VK_SUCCESS)
        return false;
    std::memcpy(outData, mapped, static_cast<size_t>(size));
    vkUnmapMemory(device_, buf.memory);
    return true;
}

VkBuffer BufferManager::get_handle(uint64_t bufferId) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = buffers_.find(bufferId);
    return it != buffers_.end() ? it->second.buffer : VK_NULL_HANDLE;
}

VkDeviceMemory BufferManager::get_memory(uint64_t bufferId) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = buffers_.find(bufferId);
    return it != buffers_.end() ? it->second.memory : VK_NULL_HANDLE;
}

bool BufferManager::destroy_buffer(uint64_t bufferId) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = buffers_.find(bufferId);
    if (it == buffers_.end()) {
        return false;
    }

    auto& buf = it->second;
    if (buf.stagingBuffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(device_, buf.stagingBuffer, nullptr);
    }
    if (buf.stagingMemory != VK_NULL_HANDLE) {
        vkFreeMemory(device_, buf.stagingMemory, nullptr);
    }
    if (buf.buffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(device_, buf.buffer, nullptr);
    }
    if (buf.memory != VK_NULL_HANDLE) {
        vkFreeMemory(device_, buf.memory, nullptr);
    }

    totalVramUsed_ -= buf.size;
    buffers_.erase(it);
    return true;
}

uint64_t BufferManager::evict_lru(VkDeviceSize neededBytes) {
    std::lock_guard<std::mutex> lock(mutex_);
    uint64_t freed = 0;

    std::vector<std::pair<uint64_t, uint64_t>> sorted;
    sorted.reserve(buffers_.size());
    for (const auto& [id, buf] : buffers_) {
        sorted.emplace_back(buf.lastAccess, id);
    }
    std::ranges::sort(sorted);

    for (auto& [time, id] : sorted) {
        if (freed >= neededBytes) {
            break;
        }
        auto it = buffers_.find(id);
        if (it == buffers_.end()) {
            continue;
        }
        if (!it->second.persistent) {
            freed += it->second.size;
            auto& buf = it->second;
            if (buf.stagingBuffer != VK_NULL_HANDLE) {
                vkDestroyBuffer(device_, buf.stagingBuffer, nullptr);
            }
            if (buf.stagingMemory != VK_NULL_HANDLE) {
                vkFreeMemory(device_, buf.stagingMemory, nullptr);
            }
            if (buf.buffer != VK_NULL_HANDLE) {
                vkDestroyBuffer(device_, buf.buffer, nullptr);
            }
            if (buf.memory != VK_NULL_HANDLE) {
                vkFreeMemory(device_, buf.memory, nullptr);
            }
            totalVramUsed_ -= buf.size;
            buffers_.erase(it);
        }
    }
    return freed;
}

std::string BufferManager::stats() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return fmt::format("{} buffers, {} MB VRAM used", buffers_.size(), totalVramUsed_ / (1024ULL * 1024ULL));
}

void BufferManager::cleanup() {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& [id, buf] : buffers_) {
        if (buf.stagingBuffer != VK_NULL_HANDLE) {
            vkDestroyBuffer(device_, buf.stagingBuffer, nullptr);
        }
        if (buf.stagingMemory != VK_NULL_HANDLE) {
            vkFreeMemory(device_, buf.stagingMemory, nullptr);
        }
        if (buf.buffer != VK_NULL_HANDLE) {
            vkDestroyBuffer(device_, buf.buffer, nullptr);
        }
        if (buf.memory != VK_NULL_HANDLE) {
            vkFreeMemory(device_, buf.memory, nullptr);
        }
    }
    buffers_.clear();
    totalVramUsed_ = 0;
}

} // namespace omnigpu::host
