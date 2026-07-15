#pragma once

#include "gpu_manager.h"
#include "renderer.h"
#include <cstdint>
#include <memory>
#include <vector>

namespace omnigpu {

class MultiGpuRenderer {
public:
    MultiGpuRenderer() = default;
    ~MultiGpuRenderer();

    MultiGpuRenderer(const MultiGpuRenderer&) = delete;
    MultiGpuRenderer& operator=(const MultiGpuRenderer&) = delete;

    bool init(GpuManager& gpuMgr, const std::vector<int>& gpuIndices,
              uint32_t width, uint32_t height);
    void shutdown();

    bool begin_frame();
    bool submit_and_readback(std::vector<uint8_t>& out_pixels);

    int gpu_count() const { return static_cast<int>(units_.size()); }

    // Access first renderer's device info for command dispatching
    VkDevice first_device() const;
    VkQueue first_queue() const;
    uint32_t first_queue_family() const;
    VkCommandPool first_command_pool() const;
    VkPhysicalDevice first_physical_device() const;

private:
    struct GpuUnit {
        std::unique_ptr<Renderer> renderer;
        int gpuIndex = -1;
        uint32_t offsetY = 0;
        uint32_t regionHeight = 0;
    };

    std::vector<GpuUnit> units_;
    GpuManager* gpuMgr_ = nullptr;
    mutable std::mutex mutex_;
    bool shutdown_ = false;
    std::vector<int> gpuIndices_;
    uint32_t totalWidth_ = 0;
    uint32_t totalHeight_ = 0;
};

} // namespace omnigpu
