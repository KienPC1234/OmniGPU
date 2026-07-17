#pragma once

#include <vulkan/vulkan.h>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace omnigpu::host {

// A GPU-side persistent buffer that can be referenced across dispatches
// without re-uploading. This is the "zero-copy" primitive for compute.
struct GpuBuffer {
    uint64_t id = 0;
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkDeviceSize size = 0;
    VkBufferUsageFlags usage = 0;
    uint32_t memoryTypeIndex = 0;
    int gpuIndex = -1;
    VkDevice device = VK_NULL_HANDLE;
    bool dirty = false;       // needs upload from guest
    bool persistent = true;   // keep in VRAM after create
    uint64_t lastAccess = 0;

    // Staging: host-visible copy for upload/download
    VkBuffer stagingBuffer = VK_NULL_HANDLE;
    VkDeviceMemory stagingMemory = VK_NULL_HANDLE;
};

class BufferManager {
public:
    BufferManager() = default;
    ~BufferManager();

    BufferManager(const BufferManager&) = delete;
    BufferManager& operator=(const BufferManager&) = delete;

    // Set Vulkan device for the primary GPU
    void set_device(VkDevice dev, VkQueue q, uint32_t qf, VkCommandPool pool) {
        device_ = dev; queue_ = q; queueFamily_ = qf; cmdPool_ = pool;
    }
    void set_physical_device(VkPhysicalDevice pd) { physDevice_ = pd; }

    // Create a persistent GPU buffer, returns buffer ID
    uint64_t create_buffer(VkDeviceSize size, VkBufferUsageFlags usage,
                           int gpuIndex = 0, bool persistent = true);

    // Upload data to an existing buffer (uses staging)
    bool upload(uint64_t bufferId, const void* data, VkDeviceSize size, VkDeviceSize offset = 0);

    // Download data from a GPU buffer to host memory
    bool download(uint64_t bufferId, void* outData, VkDeviceSize size, VkDeviceSize offset = 0);

    // Get the Vulkan handle for a buffer ID
    VkBuffer get_handle(uint64_t bufferId) const;
    VkDeviceMemory get_memory(uint64_t bufferId) const;

    // Destroy a buffer
    bool destroy_buffer(uint64_t bufferId);

    // Evict least-recently-used buffers when VRAM is full
    uint64_t evict_lru(VkDeviceSize neededBytes);

    // Stats
    size_t count() const { return buffers_.size(); }
    VkDeviceSize total_vram_used() const { return totalVramUsed_; }
    std::string stats() const;

    void cleanup();

private:
    VkDevice device_ = VK_NULL_HANDLE;
    VkPhysicalDevice physDevice_ = VK_NULL_HANDLE;
    VkQueue queue_ = VK_NULL_HANDLE;
    uint32_t queueFamily_ = 0;
    VkCommandPool cmdPool_ = VK_NULL_HANDLE;

    mutable std::mutex mutex_;
    std::unordered_map<uint64_t, GpuBuffer> buffers_;
    VkDeviceSize totalVramUsed_ = 0;
    uint64_t nextId_ = 0x100000000; // high IDs to avoid collision with guest handles

    uint64_t now_ms() const;
    uint32_t find_memory_type(uint32_t typeBits, VkMemoryPropertyFlags props) const;
    bool create_staging(GpuBuffer& buf);
};

} // namespace omnigpu::host
