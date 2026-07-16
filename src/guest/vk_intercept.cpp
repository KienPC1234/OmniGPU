// Manual Vulkan intercept implementations
// Auto-generated hooks are in vk_intercept_gen.cpp
// ICD entrypoints are in icd_entrypoints.cpp

#include "vk_intercept.h"
#include "client.h"
#include "vulkan_serializer.h"
#include "common/gpu_caps_store.h"
#include "guest_init.h"
#include <algorithm>
#include <atomic>
#include <cstring>
#include <spdlog/spdlog.h>
#include <vulkan/vulkan.h>

namespace omnigpu::intercept {

// ---------------------------------------------------------------------------
// Dispatchable handle structures (needed so the Vulkan Loader can write its magic/dispatch table pointer without crashing)
// ---------------------------------------------------------------------------
struct FakeInstance {
    uintptr_t loader_magic;
};
struct FakePhysicalDevice {
    uintptr_t loader_magic;
};
struct FakeDevice {
    uintptr_t loader_magic;
};
struct FakeQueue {
    uintptr_t loader_magic;
};

static VkInstance make_fake_instance() {
    FakeInstance* inst = new FakeInstance{0x01CDC0DE};
    return reinterpret_cast<VkInstance>(inst);
}
static VkPhysicalDevice make_fake_phys_device() {
    FakePhysicalDevice* pd = new FakePhysicalDevice{0x01CDC0DE};
    return reinterpret_cast<VkPhysicalDevice>(pd);
}
static VkDevice make_fake_device() {
    FakeDevice* dev = new FakeDevice{0x01CDC0DE};
    return reinterpret_cast<VkDevice>(dev);
}
static VkQueue make_fake_queue() {
    FakeQueue* q = new FakeQueue{0x01CDC0DE};
    return reinterpret_cast<VkQueue>(q);
}

// ---------------------------------------------------------------------------
// vkCreateInstance — create a fake instance handle
// ---------------------------------------------------------------------------
// Check if an extension name is in the supported list (mirrors host GPU)
static bool is_extension_supported(const char* name) {
    static const char* supported[] = {
        VK_KHR_SURFACE_EXTENSION_NAME,
        "VK_KHR_win32_surface",
        "VK_KHR_get_physical_device_properties2",
        "VK_KHR_get_surface_capabilities2",
        "VK_KHR_surface_protected_capabilities",
        "VK_KHR_surface_maintenance1",
        "VK_KHR_device_group_creation",
        "VK_KHR_external_fence_capabilities",
        "VK_KHR_external_memory_capabilities",
        "VK_KHR_external_semaphore_capabilities",
        "VK_KHR_display",
        "VK_KHR_get_display_properties2",
        "VK_KHR_portability_enumeration",
        "VK_EXT_surface_maintenance1",
        "VK_EXT_swapchain_colorspace",
        "VK_EXT_debug_report",
        "VK_EXT_debug_utils",
        "VK_EXT_direct_mode_display",
        "VK_LUNARG_direct_driver_loading",
        "VK_NV_external_memory_capabilities",
    };
    for (auto& ext : supported) {
        if (strcmp(name, ext) == 0) return true;
    }
    return false;
}

VkResult VKAPI_PTR vkCreateInstance_hook(
    const VkInstanceCreateInfo* pCreateInfo,
    const VkAllocationCallbacks* pAllocator,
    VkInstance* pInstance)
{
    SPDLOG_TRACE("Intercepted: vkCreateInstance");

    // Establish connection to host/daemon on instance creation
    omnigpu::init::connect_to_host();

    // Validate requested extensions against our supported list
    if (pCreateInfo && pCreateInfo->enabledExtensionCount > 0 && pCreateInfo->ppEnabledExtensionNames) {
        for (uint32_t i = 0; i < pCreateInfo->enabledExtensionCount; i++) {
            if (!is_extension_supported(pCreateInfo->ppEnabledExtensionNames[i])) {
                SPDLOG_WARN("Unsupported instance extension: {}",
                            pCreateInfo->ppEnabledExtensionNames[i]);
                return VK_ERROR_EXTENSION_NOT_PRESENT;
            }
        }
    }

    if (pInstance) *pInstance = make_fake_instance();
    return VK_SUCCESS;
}

// ---------------------------------------------------------------------------
// vkDestroyInstance — no-op
// ---------------------------------------------------------------------------
void VKAPI_PTR vkDestroyInstance_hook(
    VkInstance instance,
    const VkAllocationCallbacks* pAllocator)
{
    SPDLOG_TRACE("Intercepted: vkDestroyInstance({})",
                 reinterpret_cast<void*>(instance));
    delete reinterpret_cast<FakeInstance*>(instance);
}

// ---------------------------------------------------------------------------
// vkEnumeratePhysicalDevices — return 1 fake device
// ---------------------------------------------------------------------------
VkResult VKAPI_PTR vkEnumeratePhysicalDevices_hook(
    VkInstance instance,
    uint32_t* pPhysicalDeviceCount,
    VkPhysicalDevice* pPhysicalDevices)
{
    SPDLOG_TRACE("Intercepted: vkEnumeratePhysicalDevices");
    if (!pPhysicalDeviceCount) return VK_INCOMPLETE;

    if (!pPhysicalDevices) {
        *pPhysicalDeviceCount = 1;
        return VK_SUCCESS;
    }

    if (*pPhysicalDeviceCount < 1) {
        *pPhysicalDeviceCount = 1;
        return VK_INCOMPLETE;
    }

    pPhysicalDevices[0] = make_fake_phys_device();
    *pPhysicalDeviceCount = 1;
    return VK_SUCCESS;
}

// ---------------------------------------------------------------------------
// vkCreateDevice — create a fake device handle
// ---------------------------------------------------------------------------
VkResult VKAPI_PTR vkCreateDevice_hook(
    VkPhysicalDevice physicalDevice,
    const VkDeviceCreateInfo* pCreateInfo,
    const VkAllocationCallbacks* pAllocator,
    VkDevice* pDevice)
{
    SPDLOG_TRACE("Intercepted: vkCreateDevice");
    if (pDevice) *pDevice = make_fake_device();
    return VK_SUCCESS;
}

// ---------------------------------------------------------------------------
// vkDestroyDevice — no-op
// ---------------------------------------------------------------------------
void VKAPI_PTR vkDestroyDevice_hook(
    VkDevice device,
    const VkAllocationCallbacks* pAllocator)
{
    SPDLOG_TRACE("Intercepted: vkDestroyDevice({})",
                 reinterpret_cast<void*>(device));
    delete reinterpret_cast<FakeDevice*>(device);
}

// ---------------------------------------------------------------------------
// vkGetDeviceQueue — return a fake queue
// ---------------------------------------------------------------------------
void VKAPI_PTR vkGetDeviceQueue_hook(
    VkDevice device,
    uint32_t queueFamilyIndex,
    uint32_t queueIndex,
    VkQueue* pQueue)
{
    SPDLOG_TRACE("Intercepted: vkGetDeviceQueue");
    if (pQueue) *pQueue = make_fake_queue();
}

// ---------------------------------------------------------------------------
// vkGetDeviceQueue2 — Vulkan 1.1: return a fake queue
// ---------------------------------------------------------------------------
void VKAPI_PTR vkGetDeviceQueue2_hook(
    VkDevice device,
    const VkDeviceQueueInfo2* pQueueInfo,
    VkQueue* pQueue)
{
    SPDLOG_TRACE("Intercepted: vkGetDeviceQueue2");
    if (pQueue) *pQueue = make_fake_queue();
}

// ---------------------------------------------------------------------------
// Proc addr hooks
// ---------------------------------------------------------------------------

extern "C" PFN_vkVoidFunction VKAPI_CALL vk_icdGetInstanceProcAddr(VkInstance instance, const char* pName);

PFN_vkVoidFunction VKAPI_PTR vkGetInstanceProcAddr_hook(VkInstance instance, const char* pName) {
    return vk_icdGetInstanceProcAddr(instance, pName);
}

PFN_vkVoidFunction VKAPI_PTR vkGetDeviceProcAddr_hook(VkDevice device, const char* pName) {
    return vk_icdGetInstanceProcAddr(nullptr, pName);
}

// ---------------------------------------------------------------------------
// Enumeration hooks
// ---------------------------------------------------------------------------

VkResult VKAPI_PTR vkEnumerateInstanceExtensionProperties_hook(
    const char* pLayerName,
    uint32_t* pPropertyCount,
    VkExtensionProperties* pProperties)
{
    SPDLOG_TRACE("Intercepted: vkEnumerateInstanceExtensionProperties");
    if (!pPropertyCount) return VK_ERROR_INITIALIZATION_FAILED;

    static const VkExtensionProperties instance_extensions[] = {
        {VK_KHR_SURFACE_EXTENSION_NAME, VK_KHR_SURFACE_SPEC_VERSION},
        {"VK_KHR_win32_surface", 6},
        {"VK_KHR_get_physical_device_properties2", 2},
        {"VK_KHR_get_surface_capabilities2", 1},
        {"VK_KHR_surface_protected_capabilities", 1},
        {"VK_KHR_surface_maintenance1", 1},
        {"VK_KHR_device_group_creation", 1},
        {"VK_KHR_external_fence_capabilities", 1},
        {"VK_KHR_external_memory_capabilities", 1},
        {"VK_KHR_external_semaphore_capabilities", 1},
        {"VK_KHR_display", 23},
        {"VK_KHR_get_display_properties2", 1},
        {"VK_KHR_portability_enumeration", 1},
        {"VK_EXT_surface_maintenance1", 1},
        {"VK_EXT_swapchain_colorspace", 5},
        {"VK_EXT_debug_report", 10},
        {"VK_EXT_debug_utils", 2},
        {"VK_EXT_direct_mode_display", 1},
        {"VK_LUNARG_direct_driver_loading", 1},
        {"VK_NV_external_memory_capabilities", 1},
    };
    uint32_t count = static_cast<uint32_t>(
        sizeof(instance_extensions) / sizeof(instance_extensions[0]));

    if (!pProperties) {
        *pPropertyCount = count;
        return VK_SUCCESS;
    }

    uint32_t to_copy = std::min(*pPropertyCount, count);
    std::memcpy(pProperties, instance_extensions, to_copy * sizeof(VkExtensionProperties));
    *pPropertyCount = to_copy;

    if (to_copy < count) return VK_INCOMPLETE;
    return VK_SUCCESS;
}

VkResult VKAPI_PTR vkEnumerateDeviceExtensionProperties_hook(
    VkPhysicalDevice physicalDevice,
    const char* pLayerName,
    uint32_t* pPropertyCount,
    VkExtensionProperties* pProperties)
{
    SPDLOG_TRACE("Intercepted: vkEnumerateDeviceExtensionProperties");
    if (!pPropertyCount) return VK_ERROR_INITIALIZATION_FAILED;

    static const VkExtensionProperties device_extensions[] = {
        {VK_KHR_SWAPCHAIN_EXTENSION_NAME, VK_KHR_SWAPCHAIN_SPEC_VERSION},
        {"VK_KHR_maintenance1", 2},
        {"VK_KHR_maintenance2", 1},
        {"VK_KHR_maintenance3", 1},
        {"VK_KHR_maintenance4", 2},
        {"VK_KHR_shader_draw_parameters", 1},
        {"VK_KHR_storage_buffer_storage_class", 1},
        {"VK_KHR_16bit_storage", 1},
        {"VK_KHR_8bit_storage", 1},
        {"VK_KHR_descriptor_update_template", 1},
        {"VK_KHR_sampler_ycbcr_conversion", 14},
        {"VK_KHR_multiview", 1},
        {"VK_KHR_get_memory_requirements2", 1},
        {"VK_KHR_bind_memory2", 1},
        {"VK_KHR_dedicated_allocation", 3},
        {"VK_KHR_driver_properties", 1},
        {"VK_KHR_timeline_semaphore", 2},
        {"VK_KHR_vulkan_memory_model", 3},
        {"VK_KHR_uniform_buffer_standard_layout", 1},
        {"VK_KHR_imageless_framebuffer", 1},
        {"VK_KHR_spirv_1_4_extension", 1},
        {"VK_KHR_separate_depth_stencil_layouts", 1},
        {"VK_KHR_shader_subgroup_extended_types", 1},
        {"VK_KHR_create_renderpass2", 1},
        {"VK_KHR_depth_stencil_resolve", 1},
        {"VK_EXT_vertex_input_dynamic_state", 2},
        {"VK_EXT_private_data", 1},
        {"VK_EXT_extended_dynamic_state", 1},
        {"VK_EXT_extended_dynamic_state2", 1},
        {"VK_EXT_tooling_info", 1},
    };
    uint32_t count = static_cast<uint32_t>(
        sizeof(device_extensions) / sizeof(device_extensions[0]));

    if (!pProperties) {
        *pPropertyCount = count;
        return VK_SUCCESS;
    }

    uint32_t to_copy = std::min(*pPropertyCount, count);
    std::memcpy(pProperties, device_extensions, to_copy * sizeof(VkExtensionProperties));
    *pPropertyCount = to_copy;

    if (to_copy < count) return VK_INCOMPLETE;
    return VK_SUCCESS;
}

VkResult VKAPI_PTR vkEnumerateInstanceLayerProperties_hook(
    uint32_t* pPropertyCount,
    VkLayerProperties* pProperties)
{
    SPDLOG_TRACE("Intercepted: vkEnumerateInstanceLayerProperties");
    if (!pPropertyCount) return VK_ERROR_INITIALIZATION_FAILED;
    if (!pProperties) {
        *pPropertyCount = 0;
        return VK_SUCCESS;
    }
    *pPropertyCount = 0;
    return VK_SUCCESS;
}

VkResult VKAPI_PTR vkEnumerateDeviceLayerProperties_hook(
    VkPhysicalDevice physicalDevice,
    uint32_t* pPropertyCount,
    VkLayerProperties* pProperties)
{
    SPDLOG_TRACE("Intercepted: vkEnumerateDeviceLayerProperties");
    if (!pPropertyCount) return VK_ERROR_INITIALIZATION_FAILED;
    if (!pProperties) {
        *pPropertyCount = 0;
        return VK_SUCCESS;
    }
    *pPropertyCount = 0;
    return VK_SUCCESS;
}

VkResult VKAPI_PTR vkEnumerateInstanceVersion_hook(
    uint32_t* pApiVersion)
{
    SPDLOG_TRACE("Intercepted: vkEnumerateInstanceVersion");
    if (pApiVersion) {
        *pApiVersion = VK_API_VERSION_1_3;
    }
    return VK_SUCCESS;
}

// ---------------------------------------------------------------------------
// Physical device query hooks
// ---------------------------------------------------------------------------

void VKAPI_PTR vkGetPhysicalDeviceFormatProperties_hook(
    VkPhysicalDevice physicalDevice,
    VkFormat format,
    VkFormatProperties* pFormatProperties)
{
    SPDLOG_TRACE("Intercepted: vkGetPhysicalDeviceFormatProperties");
    std::memset(pFormatProperties, 0, sizeof(*pFormatProperties));
    pFormatProperties->linearTilingFeatures = VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT;
    pFormatProperties->optimalTilingFeatures = VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT | VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT;
    pFormatProperties->bufferFeatures = VK_FORMAT_FEATURE_VERTEX_BUFFER_BIT;
}

void VKAPI_PTR vkGetPhysicalDeviceFormatProperties2_hook(
    VkPhysicalDevice physicalDevice,
    VkFormat format,
    VkFormatProperties2* pFormatProperties)
{
    SPDLOG_TRACE("Intercepted: vkGetPhysicalDeviceFormatProperties2");
    if (!pFormatProperties) return;
    vkGetPhysicalDeviceFormatProperties_hook(physicalDevice, format, &pFormatProperties->formatProperties);
}

VkResult VKAPI_PTR vkGetPhysicalDeviceImageFormatProperties_hook(
    VkPhysicalDevice physicalDevice,
    VkFormat format,
    VkImageType type,
    VkImageTiling tiling,
    VkImageUsageFlags usage,
    VkImageCreateFlags flags,
    VkImageFormatProperties* pImageFormatProperties)
{
    SPDLOG_TRACE("Intercepted: vkGetPhysicalDeviceImageFormatProperties");
    std::memset(pImageFormatProperties, 0, sizeof(*pImageFormatProperties));
    pImageFormatProperties->maxExtent = {8192, 8192, 1};
    pImageFormatProperties->maxMipLevels = 1;
    pImageFormatProperties->maxArrayLayers = 1;
    pImageFormatProperties->sampleCounts = VK_SAMPLE_COUNT_1_BIT;
    pImageFormatProperties->maxResourceSize = 1024 * 1024 * 1024;
    return VK_SUCCESS;
}

VkResult VKAPI_PTR vkGetPhysicalDeviceImageFormatProperties2_hook(
    VkPhysicalDevice physicalDevice,
    const VkPhysicalDeviceImageFormatInfo2* pImageFormatInfo,
    VkImageFormatProperties2* pImageFormatProperties)
{
    SPDLOG_TRACE("Intercepted: vkGetPhysicalDeviceImageFormatProperties2");
    if (!pImageFormatInfo || !pImageFormatProperties) return VK_ERROR_FORMAT_NOT_SUPPORTED;
    std::memset(&pImageFormatProperties->imageFormatProperties, 0, sizeof(VkImageFormatProperties));
    pImageFormatProperties->imageFormatProperties.maxExtent = {8192, 8192, 1};
    pImageFormatProperties->imageFormatProperties.maxMipLevels = 1;
    pImageFormatProperties->imageFormatProperties.maxArrayLayers = 1;
    pImageFormatProperties->imageFormatProperties.sampleCounts = VK_SAMPLE_COUNT_1_BIT;
    pImageFormatProperties->imageFormatProperties.maxResourceSize = 1024 * 1024 * 1024;
    return VK_SUCCESS;
}

void VKAPI_PTR vkGetPhysicalDeviceSparseImageFormatProperties_hook(
    VkPhysicalDevice physicalDevice,
    VkFormat format,
    VkImageType type,
    VkSampleCountFlagBits samples,
    VkImageUsageFlags usage,
    VkImageTiling tiling,
    uint32_t* pPropertyCount,
    VkSparseImageFormatProperties* pProperties)
{
    SPDLOG_TRACE("Intercepted: vkGetPhysicalDeviceSparseImageFormatProperties");
    if (pPropertyCount) *pPropertyCount = 0;
}

void VKAPI_PTR vkGetPhysicalDeviceSparseImageFormatProperties2_hook(
    VkPhysicalDevice physicalDevice,
    const VkPhysicalDeviceSparseImageFormatInfo2* pFormatInfo,
    uint32_t* pPropertyCount,
    VkSparseImageFormatProperties2* pProperties)
{
    SPDLOG_TRACE("Intercepted: vkGetPhysicalDeviceSparseImageFormatProperties2");
    if (pPropertyCount) *pPropertyCount = 0;
}

void VKAPI_PTR vkGetPhysicalDeviceProperties_hook(
    VkPhysicalDevice physicalDevice,
    VkPhysicalDeviceProperties* pProperties)
{
    SPDLOG_TRACE("Intercepted: vkGetPhysicalDeviceProperties");

    auto& caps = caps::get();
    std::memset(pProperties, 0, sizeof(*pProperties));

    strncpy_s(pProperties->deviceName, VK_MAX_PHYSICAL_DEVICE_NAME_SIZE,
              caps.valid() ? caps.gpu_name.c_str() : "OmniGPU Virtual Device",
              _TRUNCATE);
    pProperties->apiVersion = caps.valid() ? caps.api_version : VK_API_VERSION_1_3;
    pProperties->driverVersion = caps.valid() ? caps.driver_version
                                               : VK_MAKE_API_VERSION(0, 1, 3, 0);
    pProperties->vendorID = caps.vendor_id;
    pProperties->deviceID = caps.device_id;
    pProperties->deviceType = static_cast<VkPhysicalDeviceType>(caps.device_type);
    std::memcpy(pProperties->pipelineCacheUUID, "\x01\x02\x03\x04\x05\x06\x07\x08\x09\x0a\x0b\x0c\x0d\x0e\x0f\x10", 16);

    pProperties->limits.maxImageDimension1D = caps.max_image_dimension_2d;
    pProperties->limits.maxImageDimension2D = caps.max_image_dimension_2d;
    pProperties->limits.maxImageDimension3D = caps.max_image_dimension_2d;
    pProperties->limits.maxImageDimensionCube = caps.max_image_dimension_2d;
    pProperties->limits.maxImageArrayLayers = 256;
    pProperties->limits.maxTexelBufferElements = 0x10000000;
    pProperties->limits.maxUniformBufferRange = static_cast<uint32_t>(caps.max_uniform_buffer_range);
    pProperties->limits.maxStorageBufferRange = static_cast<uint32_t>(caps.max_storage_buffer_range);
    pProperties->limits.maxPushConstantsSize = caps.max_push_constants_size;
    pProperties->limits.maxMemoryAllocationCount = static_cast<uint32_t>(caps.max_memory_allocation);
    pProperties->limits.maxSamplerAllocationCount = 4096;
    pProperties->limits.bufferImageGranularity = caps.buffer_image_granularity;
    pProperties->limits.sparseAddressSpaceSize = 0;
    pProperties->limits.maxBoundDescriptorSets = caps.max_bound_descriptor_sets;
    pProperties->limits.maxPerStageDescriptorSamplers = caps.max_per_stage_descriptor_samplers;
    pProperties->limits.maxPerStageDescriptorUniformBuffers = caps.max_per_stage_descriptor_uniform_buffers;
    pProperties->limits.maxPerStageDescriptorStorageBuffers = caps.max_per_stage_descriptor_storage_buffers;
    pProperties->limits.maxPerStageDescriptorSampledImages = caps.max_per_stage_descriptor_sampled_images;
    pProperties->limits.maxPerStageDescriptorStorageImages = caps.max_per_stage_descriptor_storage_images;
    pProperties->limits.maxPerStageDescriptorInputAttachments = caps.max_color_attachments;
    pProperties->limits.maxPerStageResources = caps.max_per_stage_resources;
    pProperties->limits.maxDescriptorSetSamplers = caps.max_per_stage_descriptor_samplers * 4;
    pProperties->limits.maxDescriptorSetUniformBuffers = caps.max_per_stage_descriptor_uniform_buffers * 4;
    pProperties->limits.maxDescriptorSetUniformBuffersDynamic = 16;
    pProperties->limits.maxDescriptorSetStorageBuffers = caps.max_per_stage_descriptor_storage_buffers * 4;
    pProperties->limits.maxDescriptorSetStorageBuffersDynamic = 16;
    pProperties->limits.maxDescriptorSetSampledImages = caps.max_per_stage_descriptor_sampled_images * 4;
    pProperties->limits.maxDescriptorSetStorageImages = caps.max_per_stage_descriptor_storage_images * 4;
    pProperties->limits.maxDescriptorSetInputAttachments = caps.max_color_attachments * 4;
    pProperties->limits.maxVertexInputAttributes = 32;
    pProperties->limits.maxVertexInputBindings = 32;
    pProperties->limits.maxVertexInputAttributeOffset = 2047;
    pProperties->limits.maxVertexInputBindingStride = 2048;
    pProperties->limits.maxVertexOutputComponents = 128;
    pProperties->limits.maxTessellationGenerationLevel = static_cast<uint32_t>(caps.max_tessellation_factor);
    pProperties->limits.maxTessellationPatchSize = 32;
    pProperties->limits.maxTessellationControlPerVertexInputComponents = 128;
    pProperties->limits.maxTessellationControlPerVertexOutputComponents = 128;
    pProperties->limits.maxTessellationControlPerPatchOutputComponents = 128;
    pProperties->limits.maxTessellationControlTotalOutputComponents = 4096;
    pProperties->limits.maxTessellationEvaluationInputComponents = 128;
    pProperties->limits.maxTessellationEvaluationOutputComponents = 128;
    pProperties->limits.maxGeometryShaderInvocations = 128;
    pProperties->limits.maxGeometryInputComponents = 128;
    pProperties->limits.maxGeometryOutputComponents = 128;
    pProperties->limits.maxGeometryOutputVertices = 1024;
    pProperties->limits.maxGeometryTotalOutputComponents = 1024;
    pProperties->limits.maxFragmentInputComponents = 128;
    pProperties->limits.maxFragmentOutputAttachments = caps.max_fragment_output_attachments;
    pProperties->limits.maxFragmentDualSrcAttachments = 1;
    pProperties->limits.maxFragmentCombinedOutputResources = caps.max_fragment_output_attachments + caps.max_per_stage_descriptor_storage_images;
    pProperties->limits.maxComputeSharedMemorySize = caps.max_compute_shared_memory_size;
    pProperties->limits.maxComputeWorkGroupCount[0] = caps.max_compute_work_group_count_x;
    pProperties->limits.maxComputeWorkGroupCount[1] = caps.max_compute_work_group_count_y;
    pProperties->limits.maxComputeWorkGroupCount[2] = caps.max_compute_work_group_count_z;
    pProperties->limits.maxComputeWorkGroupInvocations = caps.max_compute_work_group_invocations;
    pProperties->limits.maxComputeWorkGroupSize[0] = 1024;
    pProperties->limits.maxComputeWorkGroupSize[1] = 1024;
    pProperties->limits.maxComputeWorkGroupSize[2] = 64;
    pProperties->limits.subPixelInterpolationOffsetBits = 4;
    pProperties->limits.maxFramebufferWidth = caps.max_framebuffer_width;
    pProperties->limits.maxFramebufferHeight = caps.max_framebuffer_height;
    pProperties->limits.maxFramebufferLayers = caps.max_framebuffer_layers;
    pProperties->limits.framebufferColorSampleCounts = static_cast<VkSampleCountFlags>(caps.framebuffer_color_sample_counts);
    pProperties->limits.framebufferDepthSampleCounts = static_cast<VkSampleCountFlags>(caps.sample_counts);
    pProperties->limits.framebufferStencilSampleCounts = static_cast<VkSampleCountFlags>(caps.sample_counts);
    pProperties->limits.framebufferNoAttachmentsSampleCounts = static_cast<VkSampleCountFlags>(caps.sample_counts);
    pProperties->limits.maxColorAttachments = caps.max_color_attachments;
    pProperties->limits.sampledImageColorSampleCounts = static_cast<VkSampleCountFlags>(caps.sample_counts);
    pProperties->limits.sampledImageIntegerSampleCounts = VK_SAMPLE_COUNT_1_BIT;
    pProperties->limits.sampledImageDepthSampleCounts = static_cast<VkSampleCountFlags>(caps.sample_counts);
    pProperties->limits.sampledImageStencilSampleCounts = static_cast<VkSampleCountFlags>(caps.sample_counts);
    pProperties->limits.storageImageSampleCounts = static_cast<VkSampleCountFlags>(caps.sample_counts);
    pProperties->limits.maxSampleMaskWords = 1;
    pProperties->limits.timestampComputeAndGraphics = VK_TRUE;
    pProperties->limits.timestampPeriod = caps.timestamp_period;
    pProperties->limits.discreteQueuePriorities = 2;
    pProperties->limits.pointSizeGranularity = 1.0f;
    pProperties->limits.lineWidthGranularity = 1.0f;
    pProperties->limits.pointSizeRange[0] = 1.0f;
    pProperties->limits.pointSizeRange[1] = 256.0f;
    pProperties->limits.lineWidthRange[0] = 1.0f;
    pProperties->limits.lineWidthRange[1] = 8.0f;
    pProperties->limits.strictLines = VK_TRUE;
    pProperties->limits.standardSampleLocations = VK_TRUE;
    pProperties->limits.optimalBufferCopyOffsetAlignment = caps.min_uniform_buffer_offset_alignment;
    pProperties->limits.optimalBufferCopyRowPitchAlignment = 256;
    pProperties->limits.nonCoherentAtomSize = caps.non_coherent_atom_size;

    pProperties->sparseProperties.residencyAlignedMipSize = VK_TRUE;
    pProperties->sparseProperties.residencyNonResidentStrict = VK_TRUE;
    pProperties->sparseProperties.residencyStandard2DBlockShape = VK_TRUE;
    pProperties->sparseProperties.residencyStandard2DMultisampleBlockShape = VK_TRUE;
    pProperties->sparseProperties.residencyStandard3DBlockShape = VK_TRUE;
}

void VKAPI_PTR vkGetPhysicalDeviceProperties2_hook(
    VkPhysicalDevice physicalDevice,
    VkPhysicalDeviceProperties2* pProperties)
{
    SPDLOG_TRACE("Intercepted: vkGetPhysicalDeviceProperties2");
    if (!pProperties) return;

    vkGetPhysicalDeviceProperties_hook(physicalDevice, &pProperties->properties);

    VkBaseOutStructure* ext = reinterpret_cast<VkBaseOutStructure*>(pProperties->pNext);
    while (ext) {
        switch (ext->sType) {
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_PROPERTIES: {
            auto* p11 = reinterpret_cast<VkPhysicalDeviceVulkan11Properties*>(ext);
            p11->maxMultiviewViewCount = 6;
            p11->maxMultiviewInstanceIndex = (1u << 27) - 1;
            p11->subgroupSize = 32;
            p11->subgroupSupportedOperations = VK_SHADER_STAGE_ALL;
            p11->subgroupSupportedStages = VK_SHADER_STAGE_ALL;
            break;
        }
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_PROPERTIES: {
            auto* p12 = reinterpret_cast<VkPhysicalDeviceVulkan12Properties*>(ext);
            p12->maxDescriptorSetUpdateAfterBindInputAttachments = 8;
            p12->maxDescriptorSetUpdateAfterBindSampledImages = 16;
            p12->maxDescriptorSetUpdateAfterBindStorageBuffers = 8;
            p12->maxDescriptorSetUpdateAfterBindStorageBuffersDynamic = 4;
            p12->maxDescriptorSetUpdateAfterBindStorageImages = 8;
            p12->shaderSignedZeroInfNanPreserveFloat16 = VK_TRUE;
            p12->shaderSignedZeroInfNanPreserveFloat32 = VK_TRUE;
            break;
        }
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_PROPERTIES: {
            auto* p13 = reinterpret_cast<VkPhysicalDeviceVulkan13Properties*>(ext);
            p13->maxInlineUniformTotalSize = 256;
            p13->maxPerStageDescriptorInlineUniformBlocks = 4;
            p13->maxPerStageDescriptorUpdateAfterBindInlineUniformBlocks = 4;
            p13->maxDescriptorSetInlineUniformBlocks = 4;
            p13->maxDescriptorSetUpdateAfterBindInlineUniformBlocks = 4;
            break;
        }
        default:
            break;
        }
        ext = ext->pNext;
    }
}

void VKAPI_PTR vkGetPhysicalDeviceFeatures_hook(
    VkPhysicalDevice physicalDevice,
    VkPhysicalDeviceFeatures* pFeatures)
{
    SPDLOG_TRACE("Intercepted: vkGetPhysicalDeviceFeatures");
    std::memset(pFeatures, 0, sizeof(*pFeatures));

    pFeatures->robustBufferAccess = VK_TRUE;
    pFeatures->geometryShader = VK_TRUE;
    pFeatures->tessellationShader = VK_TRUE;
    pFeatures->multiDrawIndirect = VK_TRUE;
    pFeatures->drawIndirectFirstInstance = VK_TRUE;
    pFeatures->fillModeNonSolid = VK_TRUE;
    pFeatures->samplerAnisotropy = VK_TRUE;
    pFeatures->shaderClipDistance = VK_TRUE;
    pFeatures->shaderCullDistance = VK_TRUE;
    pFeatures->imageCubeArray = VK_TRUE;
    pFeatures->independentBlend = VK_TRUE;
    pFeatures->depthClamp = VK_TRUE;
    pFeatures->largePoints = VK_TRUE;
    pFeatures->occlusionQueryPrecise = VK_TRUE;
    pFeatures->pipelineStatisticsQuery = VK_TRUE;
    pFeatures->vertexPipelineStoresAndAtomics = VK_TRUE;
    pFeatures->fragmentStoresAndAtomics = VK_TRUE;
    pFeatures->shaderStorageImageExtendedFormats = VK_TRUE;
    pFeatures->shaderStorageImageReadWithoutFormat = VK_TRUE;
    pFeatures->shaderStorageImageWriteWithoutFormat = VK_TRUE;
    pFeatures->shaderFloat64 = VK_TRUE;
    pFeatures->shaderInt64 = VK_TRUE;
    pFeatures->shaderInt16 = VK_TRUE;
    pFeatures->textureCompressionBC = VK_TRUE;
}

void VKAPI_PTR vkGetPhysicalDeviceFeatures2_hook(
    VkPhysicalDevice physicalDevice,
    VkPhysicalDeviceFeatures2* pFeatures)
{
    SPDLOG_TRACE("Intercepted: vkGetPhysicalDeviceFeatures2");
    if (!pFeatures) return;
    vkGetPhysicalDeviceFeatures_hook(physicalDevice, &pFeatures->features);

    VkBaseOutStructure* ext = reinterpret_cast<VkBaseOutStructure*>(pFeatures->pNext);
    while (ext) {
        switch (ext->sType) {
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES: {
            auto* f11 = reinterpret_cast<VkPhysicalDeviceVulkan11Features*>(ext);
            f11->multiview = VK_TRUE;
            f11->samplerYcbcrConversion = VK_TRUE;
            break;
        }
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES: {
            auto* f12 = reinterpret_cast<VkPhysicalDeviceVulkan12Features*>(ext);
            f12->samplerMirrorClampToEdge = VK_TRUE;
            f12->drawIndirectCount = VK_TRUE;
            f12->descriptorIndexing = VK_TRUE;
            f12->shaderSampledImageArrayNonUniformIndexing = VK_TRUE;
            f12->shaderStorageBufferArrayNonUniformIndexing = VK_TRUE;
            f12->descriptorBindingUniformBufferUpdateAfterBind = VK_TRUE;
            f12->descriptorBindingSampledImageUpdateAfterBind = VK_TRUE;
            f12->descriptorBindingStorageBufferUpdateAfterBind = VK_TRUE;
            f12->descriptorBindingStorageImageUpdateAfterBind = VK_TRUE;
            f12->descriptorBindingUpdateUnusedWhilePending = VK_TRUE;
            f12->descriptorBindingPartiallyBound = VK_TRUE;
            f12->descriptorBindingVariableDescriptorCount = VK_TRUE;
            f12->runtimeDescriptorArray = VK_TRUE;
            f12->timelineSemaphore = VK_TRUE;
            f12->scalarBlockLayout = VK_TRUE;
            f12->imagelessFramebuffer = VK_TRUE;
            f12->vulkanMemoryModel = VK_TRUE;
            f12->vulkanMemoryModelDeviceScope = VK_TRUE;
            f12->shaderOutputViewportIndex = VK_TRUE;
            f12->shaderOutputLayer = VK_TRUE;
            f12->uniformBufferStandardLayout = VK_TRUE;
            f12->subgroupBroadcastDynamicId = VK_TRUE;
            f12->bufferDeviceAddress = VK_TRUE;
            f12->hostQueryReset = VK_TRUE;
            break;
        }
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES: {
            auto* f13 = reinterpret_cast<VkPhysicalDeviceVulkan13Features*>(ext);
            f13->synchronization2 = VK_TRUE;
            f13->dynamicRendering = VK_TRUE;
            f13->maintenance4 = VK_TRUE;
            f13->inlineUniformBlock = VK_TRUE;
            f13->pipelineCreationCacheControl = VK_TRUE;
            f13->privateData = VK_TRUE;
            f13->subgroupSizeControl = VK_TRUE;
            f13->computeFullSubgroups = VK_TRUE;
            f13->shaderDemoteToHelperInvocation = VK_TRUE;
            f13->shaderIntegerDotProduct = VK_TRUE;
            f13->shaderTerminateInvocation = VK_TRUE;
            f13->shaderZeroInitializeWorkgroupMemory = VK_TRUE;
            f13->textureCompressionASTC_HDR = VK_TRUE;
            break;
        }
        default:
            break;
        }
        ext = ext->pNext;
    }
}

void VKAPI_PTR vkGetPhysicalDeviceMemoryProperties_hook(
    VkPhysicalDevice physicalDevice,
    VkPhysicalDeviceMemoryProperties* pMemoryProperties)
{
    SPDLOG_TRACE("Intercepted: vkGetPhysicalDeviceMemoryProperties");
    auto& caps = caps::get();
    std::memset(pMemoryProperties, 0, sizeof(*pMemoryProperties));

    if (caps.valid() && caps.max_memory_heaps > 0) {
        pMemoryProperties->memoryHeapCount = caps.max_memory_heaps;
        pMemoryProperties->memoryHeaps[0].size = caps.memory_heap_size_0;
        pMemoryProperties->memoryHeaps[0].flags = static_cast<VkMemoryHeapFlags>(caps.heap_0_flags);
        if (caps.max_memory_heaps > 1) {
            pMemoryProperties->memoryHeaps[1].size = caps.memory_heap_size_1;
            pMemoryProperties->memoryHeaps[1].flags = static_cast<VkMemoryHeapFlags>(caps.heap_1_flags);
        }
        pMemoryProperties->memoryTypeCount = std::min(caps.memory_type_count, 32u);
        for (uint32_t i = 0; i < pMemoryProperties->memoryTypeCount; i++) {
            pMemoryProperties->memoryTypes[i].heapIndex = (i < 2) ? 0 : 1;
            pMemoryProperties->memoryTypes[i].propertyFlags = (i == 0)
                ? VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
                : (i == 1)
                  ? VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
                  : VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
        }
    } else {
        pMemoryProperties->memoryHeapCount = 2;
        pMemoryProperties->memoryHeaps[0].size = 24ULL * 1024 * 1024 * 1024;
        pMemoryProperties->memoryHeaps[0].flags = VK_MEMORY_HEAP_DEVICE_LOCAL_BIT;
        pMemoryProperties->memoryHeaps[1].size = 16ULL * 1024 * 1024 * 1024;
        pMemoryProperties->memoryHeaps[1].flags = 0;

        pMemoryProperties->memoryTypeCount = 4;
        pMemoryProperties->memoryTypes[0].heapIndex = 0;
        pMemoryProperties->memoryTypes[0].propertyFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
        pMemoryProperties->memoryTypes[1].heapIndex = 0;
        pMemoryProperties->memoryTypes[1].propertyFlags =
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
        pMemoryProperties->memoryTypes[2].heapIndex = 0;
        pMemoryProperties->memoryTypes[2].propertyFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT;
        pMemoryProperties->memoryTypes[3].heapIndex = 1;
        pMemoryProperties->memoryTypes[3].propertyFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    }
}

void VKAPI_PTR vkGetPhysicalDeviceMemoryProperties2_hook(
    VkPhysicalDevice physicalDevice,
    VkPhysicalDeviceMemoryProperties2* pMemoryProperties)
{
    SPDLOG_TRACE("Intercepted: vkGetPhysicalDeviceMemoryProperties2");
    if (!pMemoryProperties) return;
    vkGetPhysicalDeviceMemoryProperties_hook(physicalDevice, &pMemoryProperties->memoryProperties);
}

void VKAPI_PTR vkGetPhysicalDeviceQueueFamilyProperties_hook(
    VkPhysicalDevice physicalDevice,
    uint32_t* pQueueFamilyPropertyCount,
    VkQueueFamilyProperties* pQueueFamilyProperties)
{
    SPDLOG_TRACE("Intercepted: vkGetPhysicalDeviceQueueFamilyProperties");
    if (!pQueueFamilyProperties) {
        if (pQueueFamilyPropertyCount) *pQueueFamilyPropertyCount = 1;
        return;
    }
    if (*pQueueFamilyPropertyCount < 1) {
        *pQueueFamilyPropertyCount = 1;
        return;
    }
    std::memset(pQueueFamilyProperties, 0, sizeof(VkQueueFamilyProperties));
    pQueueFamilyProperties->queueCount = 1;
    pQueueFamilyProperties->queueFlags =
        VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT | VK_QUEUE_TRANSFER_BIT;
    pQueueFamilyProperties->timestampValidBits = 64;
    pQueueFamilyProperties->minImageTransferGranularity = {1, 1, 1};
    *pQueueFamilyPropertyCount = 1;
}

void VKAPI_PTR vkGetPhysicalDeviceQueueFamilyProperties2_hook(
    VkPhysicalDevice physicalDevice,
    uint32_t* pQueueFamilyPropertyCount,
    VkQueueFamilyProperties2* pQueueFamilyProperties)
{
    SPDLOG_TRACE("Intercepted: vkGetPhysicalDeviceQueueFamilyProperties2");
    if (!pQueueFamilyProperties) {
        if (pQueueFamilyPropertyCount) *pQueueFamilyPropertyCount = 1;
        return;
    }
    if (*pQueueFamilyPropertyCount < 1) {
        *pQueueFamilyPropertyCount = 1;
        return;
    }
    std::memset(&pQueueFamilyProperties[0], 0, sizeof(VkQueueFamilyProperties2));
    pQueueFamilyProperties[0].sType = VK_STRUCTURE_TYPE_QUEUE_FAMILY_PROPERTIES_2;
    pQueueFamilyProperties[0].queueFamilyProperties.queueCount = 1;
    pQueueFamilyProperties[0].queueFamilyProperties.queueFlags =
        VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT | VK_QUEUE_TRANSFER_BIT;
    pQueueFamilyProperties[0].queueFamilyProperties.timestampValidBits = 64;
    pQueueFamilyProperties[0].queueFamilyProperties.minImageTransferGranularity = {1, 1, 1};
    *pQueueFamilyPropertyCount = 1;
}

// ---------------------------------------------------------------------------
// External properties (Vulkan 1.1) — zero-fill, meaning not externally shareable
// ---------------------------------------------------------------------------
void VKAPI_PTR vkGetPhysicalDeviceExternalBufferProperties_hook(
    VkPhysicalDevice physicalDevice,
    const VkPhysicalDeviceExternalBufferInfo* pExternalBufferInfo,
    VkExternalBufferProperties* pExternalBufferProperties)
{
    SPDLOG_TRACE("Intercepted: vkGetPhysicalDeviceExternalBufferProperties");
    if (!pExternalBufferProperties) return;
    std::memset(pExternalBufferProperties, 0, sizeof(*pExternalBufferProperties));
    pExternalBufferProperties->sType = VK_STRUCTURE_TYPE_EXTERNAL_BUFFER_PROPERTIES;
}

void VKAPI_PTR vkGetPhysicalDeviceExternalFenceProperties_hook(
    VkPhysicalDevice physicalDevice,
    const VkPhysicalDeviceExternalFenceInfo* pExternalFenceInfo,
    VkExternalFenceProperties* pExternalFenceProperties)
{
    SPDLOG_TRACE("Intercepted: vkGetPhysicalDeviceExternalFenceProperties");
    if (!pExternalFenceProperties) return;
    std::memset(pExternalFenceProperties, 0, sizeof(*pExternalFenceProperties));
    pExternalFenceProperties->sType = VK_STRUCTURE_TYPE_EXTERNAL_FENCE_PROPERTIES;
}

void VKAPI_PTR vkGetPhysicalDeviceExternalSemaphoreProperties_hook(
    VkPhysicalDevice physicalDevice,
    const VkPhysicalDeviceExternalSemaphoreInfo* pExternalSemaphoreInfo,
    VkExternalSemaphoreProperties* pExternalSemaphoreProperties)
{
    SPDLOG_TRACE("Intercepted: vkGetPhysicalDeviceExternalSemaphoreProperties");
    if (!pExternalSemaphoreProperties) return;
    std::memset(pExternalSemaphoreProperties, 0, sizeof(*pExternalSemaphoreProperties));
    pExternalSemaphoreProperties->sType = VK_STRUCTURE_TYPE_EXTERNAL_SEMAPHORE_PROPERTIES;
}

// ---------------------------------------------------------------------------
// Misc queries
// ---------------------------------------------------------------------------
void VKAPI_PTR vkGetDescriptorSetLayoutSupport_hook(
    VkDevice device,
    const VkDescriptorSetLayoutCreateInfo* pCreateInfo,
    VkDescriptorSetLayoutSupport* pSupport)
{
    SPDLOG_TRACE("Intercepted: vkGetDescriptorSetLayoutSupport");
    if (!pSupport) return;
    std::memset(pSupport, 0, sizeof(*pSupport));
    pSupport->sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_SUPPORT;
    pSupport->supported = VK_TRUE;
}

uint64_t VKAPI_PTR vkGetBufferDeviceAddress_hook(
    VkDevice device,
    const VkBufferDeviceAddressInfo* pInfo)
{
    SPDLOG_TRACE("Intercepted: vkGetBufferDeviceAddress");
    if (!pInfo) return 0;
    auto* cl = init::get_client();
    if (cl && cl->socket() != INVALID_SOCKET) {
        uint64_t buffer_handle = handle_to_u64(pInfo->buffer);
        return cl->sync_query(0x80, buffer_handle);
    }
    return handle_to_u64(pInfo->buffer);
}

uint64_t VKAPI_PTR vkGetBufferOpaqueCaptureAddress_hook(
    VkDevice device,
    const VkBufferDeviceAddressInfo* pInfo)
{
    SPDLOG_TRACE("Intercepted: vkGetBufferOpaqueCaptureAddress");
    if (!pInfo) return 0;
    auto* cl = init::get_client();
    if (cl && cl->socket() != INVALID_SOCKET)
        return cl->sync_query(0x81, handle_to_u64(pInfo->buffer));
    return 0;
}

uint64_t VKAPI_PTR vkGetDeviceMemoryOpaqueCaptureAddress_hook(
    VkDevice device,
    const VkDeviceMemoryOpaqueCaptureAddressInfo* pInfo)
{
    SPDLOG_TRACE("Intercepted: vkGetDeviceMemoryOpaqueCaptureAddress");
    if (!pInfo) return 0;
    auto* cl = init::get_client();
    if (cl && cl->socket() != INVALID_SOCKET)
        return cl->sync_query(0x82, handle_to_u64(pInfo->memory));
    return 0;
}

void VKAPI_PTR vkGetRenderAreaGranularity_hook(
    VkDevice device,
    VkRenderPass renderPass,
    VkExtent2D* pGranularity)
{
    SPDLOG_TRACE("Intercepted: vkGetRenderAreaGranularity");
    if (pGranularity) {
        pGranularity->width = 1;
        pGranularity->height = 1;
    }
}

void VKAPI_PTR vkGetImageSubresourceLayout_hook(
    VkDevice device,
    VkImage image,
    const VkImageSubresource* pSubresource,
    VkSubresourceLayout* pLayout)
{
    SPDLOG_TRACE("Intercepted: vkGetImageSubresourceLayout");
    if (pLayout) std::memset(pLayout, 0, sizeof(*pLayout));
}

VkResult VKAPI_PTR vkGetQueryPoolResults_hook(
    VkDevice device,
    VkQueryPool queryPool,
    uint32_t firstQuery,
    uint32_t queryCount,
    size_t dataSize,
    void* pData,
    VkDeviceSize stride,
    VkQueryResultFlags flags)
{
    SPDLOG_TRACE("Intercepted: vkGetQueryPoolResults");
    if (pData && dataSize > 0) std::memset(pData, 0, dataSize);
    return VK_SUCCESS;
}

VkResult VKAPI_PTR vkGetPipelineCacheData_hook(
    VkDevice device,
    VkPipelineCache pipelineCache,
    size_t* pDataSize,
    void* pData)
{
    SPDLOG_TRACE("Intercepted: vkGetPipelineCacheData");
    if (!pDataSize) return VK_INCOMPLETE;
    if (!pData) {
        *pDataSize = 0;
        return VK_SUCCESS;
    }
    *pDataSize = 0;
    return VK_SUCCESS;
}

// ---------------------------------------------------------------------------
// Fence / Event status queries
// ---------------------------------------------------------------------------
VkResult VKAPI_PTR vkGetFenceStatus_hook(VkDevice device, VkFence fence) {
    SPDLOG_TRACE("Intercepted: vkGetFenceStatus");
    return VK_SUCCESS;
}

VkResult VKAPI_PTR vkGetEventStatus_hook(VkDevice device, VkEvent event) {
    SPDLOG_TRACE("Intercepted: vkGetEventStatus");
    return VK_EVENT_SET;
}

VkResult VKAPI_PTR vkGetPrivateData_hook(
    VkDevice device,
    VkObjectType objectType,
    uint64_t objectHandle,
    VkPrivateDataSlot privateDataSlot,
    uint64_t* pData)
{
    SPDLOG_TRACE("Intercepted: vkGetPrivateData");
    if (pData) *pData = 0;
    return VK_SUCCESS;
}

// ---------------------------------------------------------------------------
// Surface support
// ---------------------------------------------------------------------------
VkResult VKAPI_PTR vkGetPhysicalDeviceSurfaceSupportKHR_hook(
    VkPhysicalDevice physicalDevice,
    uint32_t queueFamilyIndex,
    VkSurfaceKHR surface,
    VkBool32* pSupported)
{
    SPDLOG_TRACE("Intercepted: vkGetPhysicalDeviceSurfaceSupportKHR");
    if (pSupported) *pSupported = VK_TRUE;
    return VK_SUCCESS;
}

// ---------------------------------------------------------------------------
// vkEnumeratePhysicalDeviceGroups — Vulkan 1.1: return 1 fake group
// ---------------------------------------------------------------------------
VkResult VKAPI_PTR vkEnumeratePhysicalDeviceGroups_hook(
    VkInstance instance,
    uint32_t* pPhysicalDeviceGroupCount,
    VkPhysicalDeviceGroupProperties* pPhysicalDeviceGroupProperties)
{
    SPDLOG_TRACE("Intercepted: vkEnumeratePhysicalDeviceGroups");
    if (!pPhysicalDeviceGroupCount) return VK_INCOMPLETE;

    if (!pPhysicalDeviceGroupProperties) {
        *pPhysicalDeviceGroupCount = 1;
        return VK_SUCCESS;
    }

    if (*pPhysicalDeviceGroupCount < 1) {
        *pPhysicalDeviceGroupCount = 1;
        return VK_INCOMPLETE;
    }

    std::memset(pPhysicalDeviceGroupProperties, 0, sizeof(VkPhysicalDeviceGroupProperties));
    pPhysicalDeviceGroupProperties->sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_GROUP_PROPERTIES;
    pPhysicalDeviceGroupProperties->physicalDeviceCount = 1;
    pPhysicalDeviceGroupProperties->physicalDevices[0] = make_fake_phys_device();
    pPhysicalDeviceGroupProperties->subsetAllocation = VK_FALSE;
    *pPhysicalDeviceGroupCount = 1;
    return VK_SUCCESS;
}

// ---------------------------------------------------------------------------
// Tool properties (Vulkan 1.3)
// ---------------------------------------------------------------------------
VkResult VKAPI_PTR vkGetPhysicalDeviceToolPropertiesEXT_hook(
    VkPhysicalDevice physicalDevice,
    uint32_t* pToolCount,
    VkPhysicalDeviceToolProperties* pToolProperties)
{
    SPDLOG_TRACE("Intercepted: vkGetPhysicalDeviceToolPropertiesEXT");
    if (!pToolCount) return VK_ERROR_INITIALIZATION_FAILED;
    if (!pToolProperties) {
        *pToolCount = 0;
        return VK_SUCCESS;
    }
    *pToolCount = 0;
    return VK_SUCCESS;
}

} // namespace omnigpu::intercept

// Static initializer: register manual hooks into the hook map
// Runs during CRT initialization (before DllMain), safe for map insertion.
namespace {
struct ManualHookRegistrar {
    ManualHookRegistrar() {
        using namespace omnigpu::intercept;

        // Instance / Device lifecycle
        register_manual_hook("vkCreateInstance", reinterpret_cast<void*>(vkCreateInstance_hook));
        register_manual_hook("vkDestroyInstance", reinterpret_cast<void*>(vkDestroyInstance_hook));
        register_manual_hook("vkEnumeratePhysicalDevices", reinterpret_cast<void*>(vkEnumeratePhysicalDevices_hook));
        register_manual_hook("vkCreateDevice", reinterpret_cast<void*>(vkCreateDevice_hook));
        register_manual_hook("vkDestroyDevice", reinterpret_cast<void*>(vkDestroyDevice_hook));

        // Queue
        register_manual_hook("vkGetDeviceQueue", reinterpret_cast<void*>(vkGetDeviceQueue_hook));
        register_manual_hook("vkGetDeviceQueue2", reinterpret_cast<void*>(vkGetDeviceQueue2_hook));

        // Proc addr
        register_manual_hook("vkGetInstanceProcAddr", reinterpret_cast<void*>(vkGetInstanceProcAddr_hook));
        register_manual_hook("vkGetDeviceProcAddr", reinterpret_cast<void*>(vkGetDeviceProcAddr_hook));

        // Enumeration
        register_manual_hook("vkEnumerateInstanceExtensionProperties", reinterpret_cast<void*>(vkEnumerateInstanceExtensionProperties_hook));
        register_manual_hook("vkEnumerateDeviceExtensionProperties", reinterpret_cast<void*>(vkEnumerateDeviceExtensionProperties_hook));
        register_manual_hook("vkEnumerateInstanceLayerProperties", reinterpret_cast<void*>(vkEnumerateInstanceLayerProperties_hook));
        register_manual_hook("vkEnumerateDeviceLayerProperties", reinterpret_cast<void*>(vkEnumerateDeviceLayerProperties_hook));
        register_manual_hook("vkEnumerateInstanceVersion", reinterpret_cast<void*>(vkEnumerateInstanceVersion_hook));
        register_manual_hook("vkEnumeratePhysicalDeviceGroups", reinterpret_cast<void*>(vkEnumeratePhysicalDeviceGroups_hook));

        // Physical device properties / features / memory
        register_manual_hook("vkGetPhysicalDeviceProperties", reinterpret_cast<void*>(vkGetPhysicalDeviceProperties_hook));
        register_manual_hook("vkGetPhysicalDeviceProperties2", reinterpret_cast<void*>(vkGetPhysicalDeviceProperties2_hook));
        register_manual_hook("vkGetPhysicalDeviceFeatures", reinterpret_cast<void*>(vkGetPhysicalDeviceFeatures_hook));
        register_manual_hook("vkGetPhysicalDeviceFeatures2", reinterpret_cast<void*>(vkGetPhysicalDeviceFeatures2_hook));
        register_manual_hook("vkGetPhysicalDeviceMemoryProperties", reinterpret_cast<void*>(vkGetPhysicalDeviceMemoryProperties_hook));
        register_manual_hook("vkGetPhysicalDeviceMemoryProperties2", reinterpret_cast<void*>(vkGetPhysicalDeviceMemoryProperties2_hook));
        register_manual_hook("vkGetPhysicalDeviceQueueFamilyProperties", reinterpret_cast<void*>(vkGetPhysicalDeviceQueueFamilyProperties_hook));
        register_manual_hook("vkGetPhysicalDeviceQueueFamilyProperties2", reinterpret_cast<void*>(vkGetPhysicalDeviceQueueFamilyProperties2_hook));
        register_manual_hook("vkGetPhysicalDeviceFormatProperties", reinterpret_cast<void*>(vkGetPhysicalDeviceFormatProperties_hook));
        register_manual_hook("vkGetPhysicalDeviceFormatProperties2", reinterpret_cast<void*>(vkGetPhysicalDeviceFormatProperties2_hook));
        register_manual_hook("vkGetPhysicalDeviceImageFormatProperties", reinterpret_cast<void*>(vkGetPhysicalDeviceImageFormatProperties_hook));
        register_manual_hook("vkGetPhysicalDeviceImageFormatProperties2", reinterpret_cast<void*>(vkGetPhysicalDeviceImageFormatProperties2_hook));
        register_manual_hook("vkGetPhysicalDeviceSparseImageFormatProperties", reinterpret_cast<void*>(vkGetPhysicalDeviceSparseImageFormatProperties_hook));
        register_manual_hook("vkGetPhysicalDeviceSparseImageFormatProperties2", reinterpret_cast<void*>(vkGetPhysicalDeviceSparseImageFormatProperties2_hook));

        // External properties
        register_manual_hook("vkGetPhysicalDeviceExternalBufferProperties", reinterpret_cast<void*>(vkGetPhysicalDeviceExternalBufferProperties_hook));
        register_manual_hook("vkGetPhysicalDeviceExternalFenceProperties", reinterpret_cast<void*>(vkGetPhysicalDeviceExternalFenceProperties_hook));
        register_manual_hook("vkGetPhysicalDeviceExternalSemaphoreProperties", reinterpret_cast<void*>(vkGetPhysicalDeviceExternalSemaphoreProperties_hook));

        // Misc queries
        register_manual_hook("vkGetDescriptorSetLayoutSupport", reinterpret_cast<void*>(vkGetDescriptorSetLayoutSupport_hook));
        register_manual_hook("vkGetBufferDeviceAddress", reinterpret_cast<void*>(vkGetBufferDeviceAddress_hook));
        register_manual_hook("vkGetBufferOpaqueCaptureAddress", reinterpret_cast<void*>(vkGetBufferOpaqueCaptureAddress_hook));
        register_manual_hook("vkGetDeviceMemoryOpaqueCaptureAddress", reinterpret_cast<void*>(vkGetDeviceMemoryOpaqueCaptureAddress_hook));
        register_manual_hook("vkGetFenceStatus", reinterpret_cast<void*>(vkGetFenceStatus_hook));
        register_manual_hook("vkGetEventStatus", reinterpret_cast<void*>(vkGetEventStatus_hook));
        register_manual_hook("vkGetPrivateData", reinterpret_cast<void*>(vkGetPrivateData_hook));
        register_manual_hook("vkGetRenderAreaGranularity", reinterpret_cast<void*>(vkGetRenderAreaGranularity_hook));
        register_manual_hook("vkGetImageSubresourceLayout", reinterpret_cast<void*>(vkGetImageSubresourceLayout_hook));
        register_manual_hook("vkGetQueryPoolResults", reinterpret_cast<void*>(vkGetQueryPoolResults_hook));
        register_manual_hook("vkGetPipelineCacheData", reinterpret_cast<void*>(vkGetPipelineCacheData_hook));

        // KHR
        register_manual_hook("vkGetPhysicalDeviceSurfaceSupportKHR", reinterpret_cast<void*>(vkGetPhysicalDeviceSurfaceSupportKHR_hook));

        // Tool properties
        register_manual_hook("vkGetPhysicalDeviceToolPropertiesEXT", reinterpret_cast<void*>(vkGetPhysicalDeviceToolPropertiesEXT_hook));
    }
} s_manual_registrar;
}
