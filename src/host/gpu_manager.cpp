#include "gpu_manager.h"
#include <algorithm>
#include <cstring>
#include <numeric>
#include <spdlog/spdlog.h>

namespace omnigpu {

GpuManager::~GpuManager() {
    if (instance_ != VK_NULL_HANDLE) {
        vkDestroyInstance(instance_, nullptr);
    }
}

int GpuManager::compute_score(VkPhysicalDevice physDev) const {
    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(physDev, &props);

    int score = 10;

    // Device type scoring
    switch (props.deviceType) {
    case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU:
        score = 100;
        break;
    case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU:
        score = 60;
        break;
    case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU:
        score = 40;
        break;
    case VK_PHYSICAL_DEVICE_TYPE_CPU:
        score = 20;
        break;
    default:
        break;
    }

    // Memory bonus (up to +50 based on heap size)
    VkPhysicalDeviceMemoryProperties memProps;
    vkGetPhysicalDeviceMemoryProperties(physDev, &memProps);
    uint64_t totalVram = 0;
    for (uint32_t i = 0; i < memProps.memoryHeapCount; i++) {
        if (memProps.memoryHeaps[i].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) {
            totalVram = memProps.memoryHeaps[i].size;
            break;
        }
    }
    // +10 per 4GB of VRAM, max +50
    score += std::min(50, static_cast<int>(totalVram / (4ULL * 1024 * 1024 * 1024)) * 10);

    // API version bonus
    if (props.apiVersion >= VK_API_VERSION_1_3) score += 20;
    else if (props.apiVersion >= VK_API_VERSION_1_2) score += 10;

    return score;
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

    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (uint32_t i = 0; i < count; ++i) {
            GpuInfo info;
            info.device = devices[i];
            vkGetPhysicalDeviceProperties(devices[i], &info.props);
            info.name = info.props.deviceName;
            info.performanceScore = compute_score(info.device);
            gpus_.push_back(info);

            VkPhysicalDeviceMemoryProperties memProps;
            vkGetPhysicalDeviceMemoryProperties(devices[i], &memProps);
            uint64_t vramBytes = 0;
            for (uint32_t h = 0; h < memProps.memoryHeapCount; h++)
                if (memProps.memoryHeaps[h].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT)
                    vramBytes = memProps.memoryHeaps[h].size;
            SPDLOG_INFO("GpuManager: GPU {} = {} (score={}, vram={} MB)",
                        i, info.name, info.performanceScore,
                        static_cast<uint32_t>(vramBytes / (1024 * 1024)));
        }

        // Sort by score descending so best GPU is first
        std::sort(gpus_.begin(), gpus_.end(),
                  [](const GpuInfo& a, const GpuInfo& b) {
                      return a.performanceScore > b.performanceScore;
                  });
    }

    SPDLOG_INFO("GpuManager: {} GPU(s) available, total score={}",
                count, total_gpu_score());
    return true;
}

int GpuManager::acquire_gpu() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (gpus_.empty()) return -1;

    // Weighted selection: prefer GPU with best score-to-load ratio
    int bestIdx = 0;
    double bestRatio = 0.0;

    for (int i = 0; i < static_cast<int>(gpus_.size()); ++i) {
        double load = static_cast<double>(gpus_[i].sessionCount) + 1.0;
        double ratio = static_cast<double>(gpus_[i].performanceScore) / load;
        if (ratio > bestRatio) {
            bestRatio = ratio;
            bestIdx = i;
        }
    }

    gpus_[bestIdx].sessionCount++;
    SPDLOG_INFO("GpuManager: assigned GPU {} ({}, score={}) to new session (load={})",
                bestIdx, gpus_[bestIdx].name, gpus_[bestIdx].performanceScore,
                gpus_[bestIdx].sessionCount);
    return bestIdx;
}

std::vector<int> GpuManager::acquire_gpu_team(int count) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<int> result;

    if (count <= 0 || count > static_cast<int>(gpus_.size())) return result;

    // Score-load ratio for weighted selection
    std::vector<std::pair<double, int>> scored;
    for (int i = 0; i < static_cast<int>(gpus_.size()); ++i) {
        double load = static_cast<double>(gpus_[i].sessionCount) + 1.0;
        double ratio = static_cast<double>(gpus_[i].performanceScore) / load;
        scored.push_back({ratio, i});
    }
    std::sort(scored.begin(), scored.end(),
              std::greater<std::pair<double, int>>());

    for (int j = 0; j < count && j < static_cast<int>(scored.size()); ++j) {
        int idx = scored[j].second;
        gpus_[idx].sessionCount++;
        result.push_back(idx);
        SPDLOG_INFO("GpuManager: team-assigned GPU {} ({}, score={})",
                    idx, gpus_[idx].name, gpus_[idx].performanceScore);
    }

    return result;
}

void GpuManager::release_gpu(int index) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (index >= 0 && index < static_cast<int>(gpus_.size())) {
        int prev = gpus_[index].sessionCount;
        gpus_[index].sessionCount = prev > 0 ? prev - 1 : 0;
        SPDLOG_INFO("GpuManager: released GPU {} (load was {})", index, prev);
    }
}

void GpuManager::release_gpu_team(const std::vector<int>& indices) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (int index : indices) {
        if (index >= 0 && index < static_cast<int>(gpus_.size())) {
            int prev = gpus_[index].sessionCount;
            gpus_[index].sessionCount = prev > 0 ? prev - 1 : 0;
            SPDLOG_INFO("GpuManager: team-released GPU {} (load was {})",
                        index, prev);
        }
    }
}

int GpuManager::gpu_count() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return static_cast<int>(gpus_.size());
}

VkPhysicalDevice GpuManager::gpu_device(int index) const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (index < 0 || static_cast<size_t>(index) >= gpus_.size())
        return VK_NULL_HANDLE;
    return gpus_[index].device;
}

GpuInfo GpuManager::gpu_info(int index) const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (index < 0 || static_cast<size_t>(index) >= gpus_.size())
        return {};
    return gpus_[index];
}

int GpuManager::total_gpu_score() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return std::accumulate(gpus_.begin(), gpus_.end(), 0,
                           [](int sum, const GpuInfo& g) {
                               return sum + g.performanceScore;
                           });
}

} // namespace omnigpu
