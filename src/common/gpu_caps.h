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

    // Extended Vulkan 1.3 properties
    uint32_t vendor_id = 0x10DE;
    uint32_t device_id = 0x2684;  // RTX 4090
    uint32_t device_type = 2;     // VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU
    uint32_t max_framebuffer_width = 16384;
    uint32_t max_framebuffer_height = 16384;
    uint32_t max_framebuffer_layers = 256;
    uint32_t max_memory_heaps = 2;
    uint64_t memory_heap_size_0 = 24ULL * 1024 * 1024 * 1024;  // 24 GB
    uint64_t memory_heap_size_1 = 16ULL * 1024 * 1024 * 1024;  // 16 GB
    uint32_t heap_0_flags = 1;  // VK_MEMORY_HEAP_DEVICE_LOCAL_BIT
    uint32_t heap_1_flags = 0;  // HOST_VISIBLE_BIT
    uint32_t memory_type_count = 4;
    float max_sampler_anisotropy = 16.0f;
    uint32_t max_color_attachments = 8;
    uint32_t max_bound_descriptor_sets_ext = 32;
    uint32_t max_per_stage_descriptor_samplers = 2048;
    uint32_t max_per_stage_descriptor_uniform_buffers = 2048;
    uint32_t max_per_stage_descriptor_storage_buffers = 2048;
    uint32_t max_per_stage_descriptor_sampled_images = 2048;
    uint32_t max_per_stage_descriptor_storage_images = 2048;
    uint32_t max_per_stage_resources_ext = 1000000;
    uint32_t subgroup_size = 32;
    float timestamp_period = 1.0f;
    uint32_t max_viewports = 16;
    float max_viewport_dimensions_w = 16384.0f;
    float max_viewport_dimensions_h = 16384.0f;
    uint32_t max_fragment_output_attachments = 8;
    uint64_t min_uniform_buffer_offset_alignment = 256;
    uint64_t min_storage_buffer_offset_alignment = 256;
    uint64_t max_uniform_buffer_range = 65536;
    uint64_t max_storage_buffer_range = 1ULL << 30;
    uint32_t non_coherent_atom_size = 256;
    uint64_t buffer_image_granularity = 1024;
    uint32_t max_compute_work_group_count_x = 65535;
    uint32_t max_compute_work_group_count_y = 65535;
    uint32_t max_compute_work_group_count_z = 65535;
    uint32_t max_compute_work_group_invocations = 1024;
    uint32_t max_compute_shared_memory_size = 49152;
    uint32_t max_clip_distances = 8;
    uint32_t max_cull_distances = 8;
    uint32_t max_combined_clip_and_cull_distances = 8;
    uint32_t sample_counts = 15;
    uint32_t max_samples = 1;
    uint32_t max_tessellation_factor = 64;
    uint32_t framebuffer_color_sample_counts = 15;

    // Compute-specific capabilities
    uint32_t compute_queue_count = 1;
    uint32_t supported_subgroup_operations = 0;

    bool valid() const { return !gpu_name.empty(); }
};

} // namespace omnigpu::caps
