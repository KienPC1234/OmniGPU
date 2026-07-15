#include "handshake.h"
#include "common/flatbuffers_utils.h"
#include "common/network_utils.h"
#include "omnigpu_protocol_generated.h"
#include <spdlog/spdlog.h>
#include <vulkan/vulkan.h>
#include <chrono>
#include <cstring>
#include <vector>

namespace omnigpu::handshake {

caps::GpuCapabilities query_host_gpu_caps() {
    caps::GpuCapabilities caps;

    VkInstance instance = VK_NULL_HANDLE;
    VkPhysicalDevice physDevice = VK_NULL_HANDLE;

    VkApplicationInfo appInfo{};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = "OmniGPU Host";
    appInfo.apiVersion = VK_API_VERSION_1_3;

    VkInstanceCreateInfo instInfo{};
    instInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    instInfo.pApplicationInfo = &appInfo;

    if (vkCreateInstance(&instInfo, nullptr, &instance) == VK_SUCCESS) {
        uint32_t devCount = 1;
        VkResult enumRes = vkEnumeratePhysicalDevices(instance, &devCount, &physDevice);
        if (enumRes != VK_SUCCESS && enumRes != VK_INCOMPLETE) {
            devCount = 0;
            physDevice = VK_NULL_HANDLE;
        }

        if (physDevice != VK_NULL_HANDLE) {
            VkPhysicalDeviceProperties props;
            vkGetPhysicalDeviceProperties(physDevice, &props);

            caps.gpu_name = props.deviceName;
            caps.driver_version = props.driverVersion;
            caps.api_version = props.apiVersion;
            caps.max_memory_allocation = props.limits.maxMemoryAllocationCount;
            caps.max_push_constants_size = props.limits.maxPushConstantsSize;
            caps.max_bound_descriptor_sets = props.limits.maxBoundDescriptorSets;
            caps.max_per_stage_resources = props.limits.maxPerStageResources;
            caps.max_image_dimension_2d = props.limits.maxImageDimension2D;
            caps.vendor_id = props.vendorID;
            caps.device_id = props.deviceID;
            caps.device_type = static_cast<uint32_t>(props.deviceType);
            caps.max_framebuffer_width = props.limits.maxFramebufferWidth;
            caps.max_framebuffer_height = props.limits.maxFramebufferHeight;
            caps.max_framebuffer_layers = props.limits.maxFramebufferLayers;
            caps.max_sampler_anisotropy = props.limits.maxSamplerAnisotropy;
            caps.max_color_attachments = props.limits.maxColorAttachments;
            caps.timestamp_period = props.limits.timestampPeriod;
            caps.max_viewports = props.limits.maxViewports;
            caps.max_viewport_dimensions_w = props.limits.maxViewportDimensions[0];
            caps.max_viewport_dimensions_h = props.limits.maxViewportDimensions[1];
            caps.max_fragment_output_attachments = props.limits.maxFragmentOutputAttachments;
            caps.min_uniform_buffer_offset_alignment = props.limits.minUniformBufferOffsetAlignment;
            caps.min_storage_buffer_offset_alignment = props.limits.minStorageBufferOffsetAlignment;
            caps.max_uniform_buffer_range = props.limits.maxUniformBufferRange;
            caps.max_storage_buffer_range = props.limits.maxStorageBufferRange;
            caps.non_coherent_atom_size = props.limits.nonCoherentAtomSize;
            caps.buffer_image_granularity = props.limits.bufferImageGranularity;
            caps.max_compute_work_group_count_x = props.limits.maxComputeWorkGroupCount[0];
            caps.max_compute_work_group_count_y = props.limits.maxComputeWorkGroupCount[1];
            caps.max_compute_work_group_count_z = props.limits.maxComputeWorkGroupCount[2];
            caps.max_compute_work_group_invocations = props.limits.maxComputeWorkGroupInvocations;
            caps.max_compute_shared_memory_size = props.limits.maxComputeSharedMemorySize;
            caps.max_clip_distances = props.limits.maxClipDistances;
            caps.max_cull_distances = props.limits.maxCullDistances;
            caps.max_combined_clip_and_cull_distances = props.limits.maxCombinedClipAndCullDistances;
            caps.max_tessellation_factor = props.limits.maxTessellationGenerationLevel;
            caps.sample_counts = static_cast<uint32_t>(props.limits.framebufferColorSampleCounts);
            caps.framebuffer_color_sample_counts = static_cast<uint32_t>(props.limits.framebufferColorSampleCounts);

            // Memory properties (query independently)
            VkPhysicalDeviceMemoryProperties memProps;
            vkGetPhysicalDeviceMemoryProperties(physDevice, &memProps);
            caps.max_memory_heaps = memProps.memoryHeapCount;
            if (memProps.memoryHeapCount > 0) {
                caps.memory_heap_size_0 = memProps.memoryHeaps[0].size;
                caps.heap_0_flags = static_cast<uint32_t>(memProps.memoryHeaps[0].flags);
            }
            if (memProps.memoryHeapCount > 1) {
                caps.memory_heap_size_1 = memProps.memoryHeaps[1].size;
                caps.heap_1_flags = static_cast<uint32_t>(memProps.memoryHeaps[1].flags);
            }
            caps.memory_type_count = memProps.memoryTypeCount;
        }

        vkDestroyInstance(instance, nullptr);
    }

    caps.timestamp = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch())
            .count());

    SPDLOG_INFO("Host GPU capabilities: {} (vendor=0x{:04X}, device=0x{:04X})",
                caps.gpu_name, caps.vendor_id, caps.device_id);
    return caps;
}

bool handle_capabilities_request(SOCKET client_fd) {
    auto caps = query_host_gpu_caps();

    flatbuffers::FlatBufferBuilder builder;

    auto gpu_name = builder.CreateString(caps.gpu_name);

    auto resp = fbs::CreateCapabilitiesResponse(
        builder, gpu_name,
        caps.driver_version,
        caps.api_version,
        caps.max_memory_allocation,
        caps.max_push_constants_size,
        caps.max_bound_descriptor_sets,
        caps.max_per_stage_resources,
        caps.max_image_dimension_2d,
        caps.timestamp,
        caps.vendor_id,
        caps.device_id,
        caps.device_type,
        caps.max_framebuffer_width,
        caps.max_framebuffer_height,
        caps.max_framebuffer_layers,
        caps.max_memory_heaps,
        caps.memory_heap_size_0,
        caps.memory_heap_size_1,
        caps.heap_0_flags,
        caps.heap_1_flags,
        caps.memory_type_count,
        caps.max_sampler_anisotropy,
        caps.max_color_attachments,
        caps.max_bound_descriptor_sets_ext,
        caps.max_per_stage_descriptor_samplers,
        caps.max_per_stage_descriptor_uniform_buffers,
        caps.max_per_stage_descriptor_storage_buffers,
        caps.max_per_stage_descriptor_sampled_images,
        caps.max_per_stage_descriptor_storage_images,
        caps.max_per_stage_resources_ext,
        caps.subgroup_size,
        caps.timestamp_period,
        caps.max_viewports,
        caps.max_viewport_dimensions_w,
        caps.max_viewport_dimensions_h,
        caps.max_fragment_output_attachments,
        caps.min_uniform_buffer_offset_alignment,
        caps.min_storage_buffer_offset_alignment,
        caps.max_uniform_buffer_range,
        caps.max_storage_buffer_range,
        caps.non_coherent_atom_size,
        caps.buffer_image_granularity,
        caps.max_compute_work_group_count_x,
        caps.max_compute_work_group_count_y,
        caps.max_compute_work_group_count_z,
        caps.max_compute_work_group_invocations,
        caps.max_compute_shared_memory_size,
        caps.max_clip_distances,
        caps.max_cull_distances,
        caps.max_combined_clip_and_cull_distances,
        caps.sample_counts,
        caps.max_tessellation_factor,
        caps.framebuffer_color_sample_counts);

    auto msg = fbs::CreateMessage(
        builder, fbs::MessagePayload_CapabilitiesResponse, resp.Union());

    builder.Finish(msg);

    auto span = builder.GetBufferSpan();
    uint32_t net_size = static_cast<uint32_t>(span.size());
    net_size = htonl(net_size);

    if (!tcp::send_all(client_fd,
                       reinterpret_cast<const uint8_t*>(&net_size),
                       sizeof(net_size))) {
        return false;
    }

    return tcp::send_all(client_fd, span.data(), span.size());
}

} // namespace omnigpu::handshake
