#pragma once

#include <vulkan/vulkan.h>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace omnigpu {

class GpuManager;

struct ComputeUnit {
    int gpuIndex = -1;
    VkPhysicalDevice physDevice = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VkQueue queue = VK_NULL_HANDLE;
    uint32_t queueFamily = 0;
    VkCommandPool cmdPool = VK_NULL_HANDLE;
    int64_t dedicatedMemory = 0; // bytes
    int64_t usedMemory = 0;
    float loadFactor = 0.0f; // 0.0 = idle, 1.0 = fully loaded
};

struct WorkRange {
    uint32_t offset = 0; // element offset
    uint32_t count = 0;  // element count
    int gpuIndex = -1;
};

class MultiGpuCompute {
public:
    MultiGpuCompute() = default;
    ~MultiGpuCompute();

    MultiGpuCompute(const MultiGpuCompute&) = delete;
    MultiGpuCompute& operator=(const MultiGpuCompute&) = delete;

    bool init(GpuManager& gpuMgr, const std::vector<int>& gpuIndices);
    void shutdown();

    size_t unit_count() const { return units_.size(); }
    const ComputeUnit& unit(size_t i) const { return units_[i]; }
    const ComputeUnit& primary() const { return units_[0]; }

    // Split N work items across GPUs balancing by load factor
    std::vector<WorkRange> split_work(uint32_t totalWork, uint32_t workgroupSize) const;

    // Track memory usage per GPU
    void track_allocation(int gpuIndex, int64_t bytes);
    void track_free(int gpuIndex, int64_t bytes);

    // Get best GPU for a new allocation
    int best_gpu_for_allocation(int64_t bytes) const;

    // Load balance: update load factors based on pending work
    void update_load_factors(const std::vector<float>& gpuLoads);

    // Stats
    std::string summary() const;

private:
    std::vector<ComputeUnit> units_;
    mutable std::mutex mutex_;
    bool ensure_device_per_gpu(GpuManager& gpuMgr, ComputeUnit& unit, int gpuIndex);
};

} // namespace omnigpu
