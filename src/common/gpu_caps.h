#pragma once

#include <cstdint>
#include <string>

namespace omnigpu::caps {

struct GpuCapabilities {
    std::string gpu_name;
    uint32_t driver_version = 0;
    uint32_t api_version = 0;
    uint64_t max_memory_allocation = 0;
    uint32_t max_push_constants_size = 128;
    uint32_t max_bound_descriptor_sets = 4;
    uint32_t max_per_stage_resources = 32;
    uint32_t max_image_dimension_2d = 8192;
    uint64_t timestamp = 0;

    bool valid() const { return !gpu_name.empty(); }
};

} // namespace omnigpu::caps
