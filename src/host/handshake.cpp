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

    // Try to query the first physical device
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
        vkEnumeratePhysicalDevices(instance, &devCount, &physDevice);

        if (physDevice != VK_NULL_HANDLE) {
            VkPhysicalDeviceProperties props;
            vkGetPhysicalDeviceProperties(physDevice, &props);

            caps.gpu_name = props.deviceName;
            caps.driver_version = props.driverVersion;
            caps.api_version = props.apiVersion;
            caps.max_memory_allocation =
                props.limits.maxMemoryAllocationCount;
            caps.max_push_constants_size =
                props.limits.maxPushConstantsSize;
            caps.max_bound_descriptor_sets =
                props.limits.maxBoundDescriptorSets;
            caps.max_per_stage_resources =
                props.limits.maxPerStageResources;
            caps.max_image_dimension_2d =
                props.limits.maxImageDimension2D;
        }

        vkDestroyInstance(instance, nullptr);
    }

    caps.timestamp = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch())
            .count());

    SPDLOG_INFO("Host GPU capabilities: {}", caps.gpu_name);
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
        caps.timestamp);

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
