#include "multi_gpu_renderer.h"
#include <cstring>
#include <spdlog/spdlog.h>

namespace omnigpu {

MultiGpuRenderer::~MultiGpuRenderer() { shutdown(); }

bool MultiGpuRenderer::init(GpuManager& gpuMgr,
                             const std::vector<int>& gpuIndices,
                             uint32_t width, uint32_t height) {
    if (gpuIndices.empty()) {
        SPDLOG_ERROR("MultiGpuRenderer: no GPUs provided");
        return false;
    }

    gpuMgr_ = &gpuMgr;
    gpuIndices_ = gpuIndices;
    totalWidth_ = width;
    totalHeight_ = height;

    // Split height evenly across GPUs
    uint32_t baseHeight = height / static_cast<uint32_t>(gpuIndices.size());
    uint32_t remainder = height % static_cast<uint32_t>(gpuIndices.size());

    for (size_t i = 0; i < gpuIndices.size(); ++i) {
        GpuUnit unit;
        unit.gpuIndex = gpuIndices[i];
        unit.renderer = std::make_unique<Renderer>();
        unit.offsetY = static_cast<uint32_t>(i) * baseHeight;
        unit.regionHeight = baseHeight + (i == gpuIndices.size() - 1 ? remainder : 0);

        VkPhysicalDevice physDevice = gpuMgr.gpu_device(unit.gpuIndex);

        if (!unit.renderer->init(physDevice, width, unit.regionHeight)) {
            SPDLOG_ERROR("MultiGpuRenderer: GPU {} renderer init failed",
                         unit.gpuIndex);
            return false;
        }

        units_.push_back(std::move(unit));
    }

    SPDLOG_INFO("MultiGpuRenderer: {} GPU(s) initialized, resolution {}x{} "
                "split into {} strips",
                units_.size(), width, height, units_.size());
    return true;
}

void MultiGpuRenderer::shutdown() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (shutdown_) return;
    shutdown_ = true;

    for (auto& unit : units_) {
        if (unit.renderer) {
            unit.renderer->shutdown();
        }
    }
    units_.clear();

    // Release all GPUs back to manager (only once)
    if (gpuMgr_ && !gpuIndices_.empty()) {
        gpuMgr_->release_gpu_team(gpuIndices_);
        gpuIndices_.clear();
    }
}

bool MultiGpuRenderer::begin_frame() {
    // Begin frame on all GPUs in parallel
    // Since we're single-threaded, we do them sequentially but submit
    // without waiting so GPU work can overlap
    for (auto& unit : units_) {
        if (!unit.renderer) continue;
        VkCommandBuffer cmd = unit.renderer->begin_frame();
        if (cmd == VK_NULL_HANDLE) {
            SPDLOG_ERROR("MultiGpuRenderer: GPU {} begin_frame failed",
                         unit.gpuIndex);
            return false;
        }
    }
    return true;
}

bool MultiGpuRenderer::submit_and_readback(std::vector<uint8_t>& out_pixels) {
    out_pixels.resize(totalWidth_ * totalHeight_ * 4);

    // Read back from each GPU and assemble the final image
    for (auto& unit : units_) {
        if (!unit.renderer) continue;

        std::vector<uint8_t> stripPixels;
        if (!unit.renderer->submit_and_readback(stripPixels)) {
            SPDLOG_ERROR("MultiGpuRenderer: GPU {} readback failed",
                         unit.gpuIndex);
            return false;
        }

        // Copy strip into the correct position in the full image
        uint32_t rowSize = totalWidth_ * 4;
        uint32_t stripRowSize = totalWidth_ * 4;
        uint32_t destOffset = unit.offsetY * rowSize;

        if (stripPixels.size() == stripRowSize * unit.regionHeight) {
            std::memcpy(out_pixels.data() + destOffset,
                        stripPixels.data(),
                        stripRowSize * unit.regionHeight);
        } else {
            SPDLOG_WARN("MultiGpuRenderer: GPU {} strip size mismatch "
                        "(expected {}, got {})",
                        unit.gpuIndex,
                        stripRowSize * unit.regionHeight,
                        stripPixels.size());
            return false;
        }
    }

    return true;
}

VkDevice MultiGpuRenderer::first_device() const {
    if (units_.empty() || !units_[0].renderer) return VK_NULL_HANDLE;
    return units_[0].renderer->device();
}
VkQueue MultiGpuRenderer::first_queue() const {
    if (units_.empty() || !units_[0].renderer) return VK_NULL_HANDLE;
    return units_[0].renderer->queue();
}
uint32_t MultiGpuRenderer::first_queue_family() const {
    if (units_.empty() || !units_[0].renderer) return 0;
    return units_[0].renderer->queue_family_index();
}
VkCommandPool MultiGpuRenderer::first_command_pool() const {
    if (units_.empty() || !units_[0].renderer) return VK_NULL_HANDLE;
    return units_[0].renderer->command_pool();
}
VkPhysicalDevice MultiGpuRenderer::first_physical_device() const {
    if (units_.empty() || !units_[0].renderer) return VK_NULL_HANDLE;
    return units_[0].renderer->physical_device();
}

} // namespace omnigpu
