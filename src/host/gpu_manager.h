#pragma once

#include <vulkan/vulkan.h>
#include <cstdint>
#include <string>
#include <vector>

namespace omnigpu {

struct GpuInfo {
    VkPhysicalDevice device;
    VkPhysicalDeviceProperties props;
    std::string name;
    int sessionCount = 0;
};

class GpuManager {
public:
    GpuManager() = default;
    ~GpuManager();

    GpuManager(const GpuManager&) = delete;
    GpuManager& operator=(const GpuManager&) = delete;

    bool init();
    int acquire_gpu();
    void release_gpu(int index);

    int gpu_count() const { return static_cast<int>(gpus_.size()); }
    const GpuInfo& gpu_info(int index) const { return gpus_[index]; }
    VkPhysicalDevice gpu_device(int index) const { return gpus_[index].device; }

private:
    VkInstance instance_ = VK_NULL_HANDLE;
    std::vector<GpuInfo> gpus_;
    int nextGpu_ = 0;
};

} // namespace omnigpu
