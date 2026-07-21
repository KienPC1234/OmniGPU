#include "multi_gpu_compute.h"
#include "gpu_manager.h"
#include <algorithm>
#include <cstring>
#include <numeric>
#include <spdlog/spdlog.h>

namespace omnigpu {

MultiGpuCompute::~MultiGpuCompute() { shutdown(); }

bool MultiGpuCompute::ensure_device_per_gpu(GpuManager& gpuMgr, ComputeUnit& unit, int gpuIndex) {
    unit.gpuIndex = gpuIndex;
    unit.physDevice = gpuMgr.gpu_device(gpuIndex);
    if (unit.physDevice == VK_NULL_HANDLE) {
        SPDLOG_ERROR("MultiGpuCompute: GPU {} has no physical device", gpuIndex);
        return false;
    }

    VkPhysicalDeviceMemoryProperties memProps;
    vkGetPhysicalDeviceMemoryProperties(unit.physDevice, &memProps);
    for (uint32_t i = 0; i < memProps.memoryHeapCount; i++) {
        if (memProps.memoryHeaps[i].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT)
            unit.dedicatedMemory += static_cast<int64_t>(memProps.memoryHeaps[i].size);
    }

    SPDLOG_INFO("MultiGpuCompute: ensure_device_per_gpu GPU={}", gpuIndex);
    uint32_t qfCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(unit.physDevice, &qfCount, nullptr);
    SPDLOG_INFO("MultiGpuCompute: queue families={}", qfCount);
    std::vector<VkQueueFamilyProperties> qfProps(qfCount);
    vkGetPhysicalDeviceQueueFamilyProperties(unit.physDevice, &qfCount, qfProps.data());

    int computeQF = -1;
    for (uint32_t i = 0; i < qfCount; i++) {
        if (qfProps[i].queueFlags & VK_QUEUE_COMPUTE_BIT) {
            computeQF = static_cast<int>(i);
            if (qfProps[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) continue; // prefer dedicated compute
            break;
        }
    }
    if (computeQF < 0) {
        SPDLOG_ERROR("MultiGpuCompute: GPU {} has no compute queue", gpuIndex);
        return false;
    }
    unit.queueFamily = static_cast<uint32_t>(computeQF);
    SPDLOG_INFO("MultiGpuCompute: compute QF={}", unit.queueFamily);

    float priority = 1.0f;
    VkDeviceQueueCreateInfo qci{};
    qci.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    qci.queueFamilyIndex = unit.queueFamily;
    qci.queueCount = 1;
    qci.pQueuePriorities = &priority;

    VkPhysicalDeviceVulkan11Features features11{};
    features11.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES;
    features11.storageBuffer16BitAccess = VK_TRUE;
    features11.uniformAndStorageBuffer16BitAccess = VK_TRUE;

    VkPhysicalDeviceVulkan12Features features12{};
    features12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
    features12.bufferDeviceAddress = VK_TRUE;
    features12.descriptorIndexing = VK_TRUE;
    features12.scalarBlockLayout = VK_TRUE;
    features12.timelineSemaphore = VK_TRUE;
    features12.hostQueryReset = VK_TRUE;
    features12.shaderFloat16 = VK_TRUE;
    features12.shaderInt8 = VK_TRUE;
    features12.storageBuffer8BitAccess = VK_TRUE;
    features12.uniformAndStorageBuffer8BitAccess = VK_TRUE;

    VkPhysicalDeviceVulkan13Features features13{};
    features13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
    features13.synchronization2 = VK_TRUE;
    features13.maintenance4 = VK_TRUE;
    features13.shaderIntegerDotProduct = VK_TRUE;

    VkPhysicalDeviceCooperativeMatrixFeaturesKHR coopMat{};
    coopMat.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_COOPERATIVE_MATRIX_FEATURES_KHR;
    coopMat.cooperativeMatrix = VK_TRUE;

    VkPhysicalDeviceShaderIntegerDotProductFeaturesKHR intDot{};
    intDot.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_INTEGER_DOT_PRODUCT_FEATURES_KHR;
    intDot.shaderIntegerDotProduct = VK_TRUE;

    VkDeviceCreateInfo dci{};
    dci.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    dci.queueCreateInfoCount = 1;
    dci.pQueueCreateInfos = &qci;
    // Chain: dci → features13 → features12 → features11 → coopMat → intDot
    features11.pNext = &coopMat;
    coopMat.pNext = &intDot;
    features12.pNext = &features11;
    features13.pNext = &features12;
    dci.pNext = &features13;

    VkPhysicalDeviceFeatures features{};
    features.shaderFloat64 = VK_TRUE;
    features.shaderInt64 = VK_TRUE;
    features.shaderInt16 = VK_TRUE;
    features.robustBufferAccess = VK_TRUE;
    features.vertexPipelineStoresAndAtomics = VK_TRUE;
    features.shaderStorageImageExtendedFormats = VK_TRUE;
    dci.pEnabledFeatures = &features;

    SPDLOG_INFO("MultiGpuCompute: calling vkCreateDevice...");
    VkResult res = vkCreateDevice(unit.physDevice, &dci, nullptr, &unit.device);
    SPDLOG_INFO("MultiGpuCompute: vkCreateDevice done, res={}", static_cast<int>(res));
    if (res != VK_SUCCESS) {
        SPDLOG_ERROR("MultiGpuCompute: GPU {} device creation failed: {}", gpuIndex, static_cast<int>(res));
        return false;
    }

    SPDLOG_INFO("MultiGpuCompute: calling vkGetDeviceQueue...");
    vkGetDeviceQueue(unit.device, unit.queueFamily, 0, &unit.queue);

    VkCommandPoolCreateInfo cpci{};
    cpci.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    cpci.queueFamilyIndex = unit.queueFamily;
    cpci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    vkCreateCommandPool(unit.device, &cpci, nullptr, &unit.cmdPool);

    VkPhysicalDeviceProperties props{};
    vkGetPhysicalDeviceProperties(unit.physDevice, &props);
    SPDLOG_INFO("MultiGpuCompute: GPU {} device ready: {} (mem={}MB, qf={})",
                gpuIndex, props.deviceName,
                unit.dedicatedMemory / (1024 * 1024),
                unit.queueFamily);
    return true;
}

bool MultiGpuCompute::init(GpuManager& gpuMgr, const std::vector<int>& gpuIndices) {
    if (gpuIndices.empty()) {
        SPDLOG_ERROR("MultiGpuCompute: no GPU indices provided");
        return false;
    }

    for (int idx : gpuIndices) {
        ComputeUnit unit;
        if (!ensure_device_per_gpu(gpuMgr, unit, idx))
            return false;
        units_.push_back(std::move(unit));
    }

    SPDLOG_INFO("MultiGpuCompute: {} GPU(s) initialized, primary=GPU {}",
                units_.size(), units_[0].gpuIndex);
    return true;
}

void MultiGpuCompute::shutdown() {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& u : units_) {
        if (u.cmdPool) vkDestroyCommandPool(u.device, u.cmdPool, nullptr);
        if (u.device) vkDestroyDevice(u.device, nullptr);
    }
    units_.clear();
}

std::vector<WorkRange> MultiGpuCompute::split_work(uint32_t totalWork, uint32_t workgroupSize) const {
    if (units_.empty()) return {};

    if (units_.size() == 1) {
        return {{0, totalWork, units_[0].gpuIndex}};
    }

    // Weighted distribution based on load factors
    std::vector<float> weights(units_.size());
    float totalWeight = 0;
    for (size_t i = 0; i < units_.size(); i++) {
        float invLoad = 1.0f - std::min(units_[i].loadFactor, 0.9f);
        float memScore = 1.0f;
        if (units_[i].dedicatedMemory > 0) {
            float freeMem = static_cast<float>(units_[i].dedicatedMemory - units_[i].usedMemory);
            memScore = std::max(0.1f, freeMem / static_cast<float>(units_[i].dedicatedMemory));
        }
        weights[i] = (1.0f + invLoad) * (0.5f + memScore * 0.5f);
        totalWeight += weights[i];
    }

    // Normalize and assign work ranges (aligned to workgroupSize)
    uint32_t alignedTotal = (totalWork + workgroupSize - 1) / workgroupSize * workgroupSize;
    uint32_t assigned = 0;
    std::vector<WorkRange> ranges;

    for (size_t i = 0; i < units_.size(); i++) {
        WorkRange range;
        range.gpuIndex = units_[i].gpuIndex;
        range.offset = assigned;

        float fraction = weights[i] / totalWeight;
        uint32_t chunk = static_cast<uint32_t>(fraction * alignedTotal);
        chunk = (chunk + workgroupSize - 1) / workgroupSize * workgroupSize; // align
        chunk = std::min(chunk, alignedTotal - assigned);

        if (i == units_.size() - 1) {
            chunk = alignedTotal - assigned; // last GPU gets remainder
        }

        range.count = chunk;
        assigned += chunk;
        ranges.push_back(range);
    }

    return ranges;
}

void MultiGpuCompute::track_allocation(int gpuIndex, int64_t bytes) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& u : units_) {
        if (u.gpuIndex == gpuIndex) {
            u.usedMemory += bytes;
            return;
        }
    }
}

void MultiGpuCompute::track_free(int gpuIndex, int64_t bytes) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& u : units_) {
        if (u.gpuIndex == gpuIndex) {
            u.usedMemory = std::max(int64_t(0), u.usedMemory - bytes);
            return;
        }
    }
}

int MultiGpuCompute::best_gpu_for_allocation(int64_t bytes) const {
    std::lock_guard<std::mutex> lock(mutex_);
    int best = -1;
    float bestScore = -1.0f;
    for (size_t i = 0; i < units_.size(); i++) {
        int64_t free = units_[i].dedicatedMemory - units_[i].usedMemory;
        if (free < bytes) continue;
        float score = static_cast<float>(free) / (1.0f + units_[i].loadFactor);
        if (score > bestScore) {
            bestScore = score;
            best = static_cast<int>(i);
        }
    }
    return best;
}

void MultiGpuCompute::update_load_factors(const std::vector<float>& gpuLoads) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (size_t i = 0; i < units_.size() && i < gpuLoads.size(); i++)
        units_[i].loadFactor = gpuLoads[i];
}

std::string MultiGpuCompute::summary() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::string s;
    for (const auto& u : units_)
        s += fmt::format("GPU{}: mem={}MB used={}MB load={:.1f}%; ",
                         u.gpuIndex,
                         u.dedicatedMemory / (1024 * 1024),
                         u.usedMemory / (1024 * 1024),
                         u.loadFactor * 100.0f);
    return s;
}

} // namespace omnigpu
