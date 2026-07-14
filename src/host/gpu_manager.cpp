#include "gpu_manager.h"
#include <cstring>
#include <spdlog/spdlog.h>

namespace omnigpu {

GpuManager::~GpuManager() {
    if (instance_ != VK_NULL_HANDLE) {
        vkDestroyInstance(instance_, nullptr);
    }
}

bool GpuManager::init() {
    VkApplicationInfo appInfo{};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = "OmniGPU GPU Manager";
    appInfo.applicationVersion = VK_MAKE_VERSION(0, 1, 0);
    appInfo.apiVersion = VK_API_VERSION_1_3;

    const char* ext = VK_KHR_SURFACE_EXTENSION_NAME;

    VkInstanceCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    info.pApplicationInfo = &appInfo;
    info.enabledExtensionCount = 1;
    info.ppEnabledExtensionNames = &ext;

    if (vkCreateInstance(&info, nullptr, &instance_) != VK_SUCCESS) {
        SPDLOG_ERROR("GpuManager: Failed to create Vulkan instance");
        return false;
    }

    uint32_t count = 0;
    vkEnumeratePhysicalDevices(instance_, &count, nullptr);
    if (count == 0) {
        SPDLOG_ERROR("GpuManager: No Vulkan devices found");
        return false;
    }

    std::vector<VkPhysicalDevice> devices(count);
    vkEnumeratePhysicalDevices(instance_, &count, devices.data());

    for (uint32_t i = 0; i < count; ++i) {
        GpuInfo info;
        info.device = devices[i];
        vkGetPhysicalDeviceProperties(devices[i], &info.props);
        info.name = info.props.deviceName;
        gpus_.push_back(info);

        SPDLOG_INFO("GpuManager: GPU {} = {} (dedicated mem {} MB)",
                    i, info.name,
                    info.props.limits.maxMemoryAllocationCount / (1024 * 1024));
    }

    SPDLOG_INFO("GpuManager: {} GPU(s) available", count);
    return true;
}

int GpuManager::acquire_gpu() {
    if (gpus_.empty()) return -1;

    int bestIdx = 0;
    int minLoad = gpus_[0].sessionCount;

    for (int i = 1; i < static_cast<int>(gpus_.size()); ++i) {
        if (gpus_[i].sessionCount < minLoad) {
            minLoad = gpus_[i].sessionCount;
            bestIdx = i;
        }
    }

    gpus_[bestIdx].sessionCount++;
    SPDLOG_INFO("GpuManager: assigned GPU {} ({}) to new session (load={})",
                bestIdx, gpus_[bestIdx].name, minLoad + 1);
    return bestIdx;
}

void GpuManager::release_gpu(int index) {
    if (index >= 0 && index < static_cast<int>(gpus_.size())) {
        int prev = gpus_[index].sessionCount;
        gpus_[index].sessionCount = prev > 0 ? prev - 1 : 0;
        SPDLOG_INFO("GpuManager: released GPU {} (load was {})", index, prev);
    }
}

} // namespace omnigpu
