#pragma once

#include <vulkan/vulkan.h>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace omnigpu {

struct GpuInfo {
    VkPhysicalDevice device;
    VkPhysicalDeviceProperties props;
    std::string name;
    int sessionCount = 0;
    int performanceScore = 0;
};

class GpuManager {
public:
    GpuManager() = default;
    ~GpuManager();

    GpuManager(const GpuManager&) = delete;
    GpuManager& operator=(const GpuManager&) = delete;

    bool init();
    int acquire_gpu();
    std::vector<int> acquire_gpu_team(int count);
    void release_gpu(int index);
    void release_gpu_team(const std::vector<int>& indices);

    int gpu_count() const;
    GpuInfo gpu_info(int index) const;
    VkPhysicalDevice gpu_device(int index) const;
    int total_gpu_score() const;

private:
    VkInstance instance_ = VK_NULL_HANDLE;
    std::vector<GpuInfo> gpus_;
    mutable std::mutex mutex_;

    int compute_score(VkPhysicalDevice physDev) const;
};

} // namespace omnigpu
