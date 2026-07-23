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

// Constant-time string comparison to prevent timing attacks
static bool constant_time_compare(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    volatile uint8_t result = 0;
    for (size_t i = 0; i < a.size(); i++)
        result |= static_cast<uint8_t>(a[i]) ^ static_cast<uint8_t>(b[i]);
    return result == 0;
}

static bool validate_auth(const std::string& token, const std::string& expected) {
    if (expected.empty()) return true; // no auth required
    if (token.empty()) {
        SPDLOG_WARN("AUTH: client sent no token, expected one");
        return false;
    }
    bool ok = constant_time_compare(token, expected);
    if (!ok) SPDLOG_WARN("AUTH: invalid token from client (len={})", token.size());
    return ok;
}

static uint32_t query_compute_queue_count(VkPhysicalDevice physDev) {
    uint32_t qfCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(physDev, &qfCount, nullptr);
    std::vector<VkQueueFamilyProperties> qfProps(qfCount);
    vkGetPhysicalDeviceQueueFamilyProperties(physDev, &qfCount, qfProps.data());
    uint32_t count = 0;
    for (uint32_t i = 0; i < qfCount; i++)
        if (qfProps[i].queueFlags & VK_QUEUE_COMPUTE_BIT) count++;
    return count;
}

struct SubgroupInfo {
    uint32_t subgroupSize = 32;
    uint32_t supportedOperations = 0;
    uint32_t supportedStages = VK_SHADER_STAGE_ALL;
};

static SubgroupInfo query_subgroup_info(VkPhysicalDevice physDev) {
    SubgroupInfo info;
    VkPhysicalDeviceSubgroupProperties subProps{};
    subProps.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_PROPERTIES;
    VkPhysicalDeviceVulkan11Properties p11{};
    p11.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_PROPERTIES;
    p11.pNext = &subProps;

    VkPhysicalDeviceProperties2 props2{};
    props2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
    props2.pNext = &p11;
    vkGetPhysicalDeviceProperties2(physDev, &props2);

    info.subgroupSize = p11.subgroupSize ? p11.subgroupSize : (subProps.subgroupSize ? subProps.subgroupSize : 32);
    info.supportedOperations = p11.subgroupSupportedOperations ? static_cast<uint32_t>(p11.subgroupSupportedOperations) : static_cast<uint32_t>(subProps.supportedOperations);
    info.supportedStages = static_cast<uint32_t>(p11.subgroupSupportedStages ? p11.subgroupSupportedStages : subProps.supportedStages);

    uint32_t all_subgroup_ops = VK_SUBGROUP_FEATURE_BASIC_BIT |
                                VK_SUBGROUP_FEATURE_VOTE_BIT |
                                VK_SUBGROUP_FEATURE_ARITHMETIC_BIT |
                                VK_SUBGROUP_FEATURE_BALLOT_BIT |
                                VK_SUBGROUP_FEATURE_SHUFFLE_BIT |
                                VK_SUBGROUP_FEATURE_SHUFFLE_RELATIVE_BIT |
                                VK_SUBGROUP_FEATURE_CLUSTERED_BIT |
                                VK_SUBGROUP_FEATURE_QUAD_BIT;

    if ((info.supportedOperations & all_subgroup_ops) == 0) {
        info.supportedOperations = all_subgroup_ops;
    }
    return info;
}

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
        uint32_t devCount = 0;
        vkEnumeratePhysicalDevices(instance, &devCount, nullptr);
        if (devCount > 0) {
            std::vector<VkPhysicalDevice> devices(devCount);
            vkEnumeratePhysicalDevices(instance, &devCount, devices.data());

            // Pick device with highest VRAM (best for compute/rendering)
            VkPhysicalDevice bestDev = VK_NULL_HANDLE;
            uint64_t bestVram = 0;
            for (auto pd : devices) {
                VkPhysicalDeviceMemoryProperties memProps;
                vkGetPhysicalDeviceMemoryProperties(pd, &memProps);
                for (uint32_t i = 0; i < memProps.memoryHeapCount; i++) {
                    if ((memProps.memoryHeaps[i].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) &&
                        memProps.memoryHeaps[i].size > bestVram) {
                        bestVram = memProps.memoryHeaps[i].size;
                        bestDev = pd;
                    }
                }
            }
            if (bestDev == VK_NULL_HANDLE && devCount > 0)
                bestDev = devices[0];
            physDevice = bestDev;
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
            caps.max_viewport_dimensions_w = static_cast<float>(props.limits.maxViewportDimensions[0]);
            caps.max_viewport_dimensions_h = static_cast<float>(props.limits.maxViewportDimensions[1]);
            caps.max_fragment_output_attachments = props.limits.maxFragmentOutputAttachments;
            caps.min_uniform_buffer_offset_alignment = props.limits.minUniformBufferOffsetAlignment;
            caps.min_storage_buffer_offset_alignment = props.limits.minStorageBufferOffsetAlignment;
            caps.max_uniform_buffer_range = props.limits.maxUniformBufferRange;
            caps.max_storage_buffer_range = props.limits.maxStorageBufferRange;
            caps.non_coherent_atom_size = static_cast<uint32_t>(props.limits.nonCoherentAtomSize);
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
            caps.max_samples = static_cast<uint32_t>(VK_SAMPLE_COUNT_1_BIT);
            caps.framebuffer_color_sample_counts = static_cast<uint32_t>(props.limits.framebufferColorSampleCounts);

            // Maintenance3: maxMemoryAllocationSize (separate from maxMemoryAllocationCount)
            VkPhysicalDeviceMaintenance3Properties maint3{};
            maint3.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAINTENANCE_3_PROPERTIES;
            VkPhysicalDeviceProperties2 props2{};
            props2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
            props2.pNext = &maint3;
            vkGetPhysicalDeviceProperties2(physDevice, &props2);
            caps.max_memory_allocation_size = maint3.maxMemoryAllocationSize;
            caps.max_storage_buffer_range = props.limits.maxStorageBufferRange;

            auto subInfo = query_subgroup_info(physDevice);
            caps.compute_queue_count = query_compute_queue_count(physDevice);
            caps.supported_subgroup_operations = subInfo.supportedOperations;

            // ===== ML support: hardcode for NVIDIA RTX 40 series =====
            // Skip vkGetPhysicalDeviceFeatures2 pNext query (crashes nvoglv64.dll
            // on driver 32.0.16.1047 with integer divide by zero).
            caps.supports_16bit_storage = true;
            caps.supports_8bit_storage  = true;
            caps.supports_float16_int8  = true;
            caps.supports_cooperative_matrix = true;
            caps.coopmat_m = 16;
            caps.coopmat_n = 16;
            caps.coopmat_k = 16;
            caps.supports_integer_dot_product = true;

            // Query per-stage descriptor limits from VkPhysicalDeviceProperties (already in VkPhysicalDeviceLimits)
            caps.max_bound_descriptor_sets_ext = props.limits.maxBoundDescriptorSets;
            caps.max_per_stage_descriptor_samplers = props.limits.maxPerStageDescriptorSamplers;
            caps.max_per_stage_descriptor_uniform_buffers = props.limits.maxPerStageDescriptorUniformBuffers;
            caps.max_per_stage_descriptor_storage_buffers = props.limits.maxPerStageDescriptorStorageBuffers;
            caps.max_per_stage_descriptor_sampled_images = props.limits.maxPerStageDescriptorSampledImages;
            caps.max_per_stage_descriptor_storage_images = props.limits.maxPerStageDescriptorStorageImages;
            caps.max_per_stage_resources_ext = props.limits.maxPerStageResources;
            caps.subgroup_size = subInfo.subgroupSize;

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
            std::chrono::system_clock::now().time_since_epoch()).count());

    SPDLOG_INFO("Host GPU capabilities: {} (compute_queues={})",
                caps.gpu_name, caps.compute_queue_count);
    return caps;
}

HandshakeResult handle_capabilities_request(SOCKET client_fd,
    const std::string& expected_token) {
    HandshakeResult result;

    // Read the CapabilitiesRequest from guest
    uint32_t req_size = 0;
    if (!tcp::recv_all(client_fd, reinterpret_cast<uint8_t*>(&req_size), sizeof(req_size))) {
        result.error_msg = "Failed to read request size";
        return result;
    }
    req_size = ntohl(req_size);
    if (req_size > 1024 * 1024) { // 1MB max for handshake
        result.error_msg = "Handshake message too large";
        return result;
    }

    std::vector<uint8_t> req_buf(req_size);
    if (!tcp::recv_all(client_fd, req_buf.data(), req_size)) {
        result.error_msg = "Failed to read request";
        return result;
    }

    auto* msg = protocol::verify_root(req_buf.data(), req_buf.size());
    if (!msg || msg->payload_type() != fbs::MessagePayload_CapabilitiesRequest) {
        result.error_msg = "Expected CapabilitiesRequest";
        return result;
    }

    auto* req = msg->payload_as_CapabilitiesRequest();
    if (!req) {
        result.error_msg = "Invalid CapabilitiesRequest";
        return result;
    }

    // Validate auth token
    std::string client_token;
    if (req->auth_token()) client_token = req->auth_token()->str();
    if (!validate_auth(client_token, expected_token)) {
        result.error_msg = "Authentication failed";
        SPDLOG_WARN("AUTH: connection rejected — invalid token");
        return result;
    }

    result.client_version = req->client_version();
    result.compute_mode = req->compute_mode();
    result.large_buffers = req->large_buffers();
    result.ok = true;

    // Send back capabilities
    auto caps = query_host_gpu_caps();
    flatbuffers::FlatBufferBuilder builder;
    auto gpu_name = builder.CreateString(caps.gpu_name);

    bool auth_required = !expected_token.empty();

    auto resp = fbs::CreateCapabilitiesResponse(
        builder, gpu_name,
        caps.driver_version, caps.api_version,
        caps.max_memory_allocation, caps.max_push_constants_size,
        caps.max_bound_descriptor_sets, caps.max_per_stage_resources,
        caps.max_image_dimension_2d, caps.timestamp,
        caps.vendor_id, caps.device_id, caps.device_type,
        caps.max_framebuffer_width, caps.max_framebuffer_height,
        caps.max_framebuffer_layers,
        caps.max_memory_heaps, caps.memory_heap_size_0, caps.memory_heap_size_1,
        caps.heap_0_flags, caps.heap_1_flags, caps.memory_type_count,
        caps.max_sampler_anisotropy, caps.max_color_attachments,
        caps.max_bound_descriptor_sets_ext,
        caps.max_per_stage_descriptor_samplers,
        caps.max_per_stage_descriptor_uniform_buffers,
        caps.max_per_stage_descriptor_storage_buffers,
        caps.max_per_stage_descriptor_sampled_images,
        caps.max_per_stage_descriptor_storage_images,
        caps.max_per_stage_resources_ext,
        caps.subgroup_size, caps.timestamp_period,
        caps.max_viewports, caps.max_viewport_dimensions_w,
        caps.max_viewport_dimensions_h,
        caps.max_fragment_output_attachments,
        caps.min_uniform_buffer_offset_alignment,
        caps.min_storage_buffer_offset_alignment,
        caps.max_uniform_buffer_range, caps.max_storage_buffer_range,
        caps.non_coherent_atom_size, caps.buffer_image_granularity,
        caps.max_compute_work_group_count_x,
        caps.max_compute_work_group_count_y,
        caps.max_compute_work_group_count_z,
        caps.max_compute_work_group_invocations,
        caps.max_compute_shared_memory_size,
        caps.max_clip_distances, caps.max_cull_distances,
        caps.max_combined_clip_and_cull_distances,
        caps.sample_counts, caps.max_samples,
        caps.max_tessellation_factor, caps.framebuffer_color_sample_counts,
        caps.compute_queue_count,
        true, // supports_buffer_device_address
        caps.supported_subgroup_operations,
        caps.compute_queue_count > 1,
        auth_required,
        true,  // buffer_manager_capable
        caps.supports_16bit_storage,
        caps.supports_8bit_storage,
        caps.supports_float16_int8,
        caps.supports_cooperative_matrix,
        caps.coopmat_m,
        caps.coopmat_n,
        caps.coopmat_k,
        caps.supports_integer_dot_product
    );

    auto response_msg = fbs::CreateMessage(
        builder, fbs::MessagePayload_CapabilitiesResponse, resp.Union());
    builder.Finish(response_msg);

    auto span = builder.GetBufferSpan();
    uint32_t net_size = htonl(static_cast<uint32_t>(span.size()));
    if (!tcp::send_all(client_fd, reinterpret_cast<const uint8_t*>(&net_size), sizeof(net_size))) {
        result.error_msg = "Failed to send capabilities";
        result.ok = false;
        return result;
    }
    if (!tcp::send_all(client_fd, span.data(), span.size())) {
        result.error_msg = "Failed to send capabilities payload";
        result.ok = false;
        return result;
    }

    // Set 30-second receive timeout so host doesn't hang forever if guest
    // disconnects or handshake response is lost before CommandMessage arrives.
#ifdef _WIN32
    DWORD timeout_ms = 30000;
    setsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO,
               reinterpret_cast<const char*>(&timeout_ms), sizeof(timeout_ms));
#else
    struct timeval tv;
    tv.tv_sec = 30;
    tv.tv_usec = 0;
    setsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO,
               reinterpret_cast<const char*>(&tv), sizeof(tv));
#endif

    return result;
}

} // namespace omnigpu::handshake
