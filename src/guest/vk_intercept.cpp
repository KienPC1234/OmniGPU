// Manual Vulkan intercept implementations
// Auto-generated hooks are in vk_intercept_gen.cpp
// ICD entrypoints are in icd_entrypoints.cpp

#include "vk_intercept.h"
#include "client.h"
#include "command_batch.h"
#include "vulkan_serializer.h"
#include "vulkan_struct_serializer.h"
#include "common/gpu_caps_store.h"
#include "common/flatbuffers_utils.h"
#include "guest_init.h"
#include "omnigpu_protocol_generated.h"
#include <algorithm>
#include <vulkan/vk_icd.h>
#include <atomic>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <unordered_map>
#include <vector>
#include <spdlog/spdlog.h>

#ifdef _WIN32
#define VK_USE_PLATFORM_WIN32_KHR
#include <vulkan/vulkan_win32.h>
#endif
#include <vulkan/vulkan.h>

#ifdef _WIN32
#include <malloc.h>
#define omni_aligned_alloc(alignment, size) _aligned_malloc((size), (alignment))
#define omni_aligned_free(ptr) _aligned_free(ptr)
#else
#include <stdlib.h>
#define omni_aligned_alloc(alignment, size) aligned_alloc((alignment), (size))
#define omni_aligned_free(ptr) free(ptr)
#endif

namespace omnigpu::intercept {

// Forward declarations

// Global state for memory mapping / flush tracking
static std::mutex s_map_mutex;
static std::unordered_map<uint64_t, void*> s_mapped_ptrs;
static std::unordered_map<uint64_t, void*> s_unmapped_ptrs;  // retained shadows across unmap
static std::unordered_map<uint64_t, VkDeviceSize> s_memory_sizes;
static std::unordered_map<uint64_t, VkDevice> s_memory_devices;
static std::unordered_map<uint64_t, VkDeviceSize> s_memory_map_offsets;
static std::unordered_map<uint64_t, bool> s_memory_dirty;
static std::unordered_map<uint64_t, bool> s_memory_coherent;
struct PendingFlush { VkDeviceSize offset; VkDeviceSize size; };
static std::unordered_map<uint64_t, std::vector<struct PendingFlush>> s_pending_flushes;
struct QueryResultDest { void* ptr; size_t size; };
static std::unordered_map<uint64_t, QueryResultDest> s_pending_query_results;
struct LayoutResultDest { VkSubresourceLayout* ptr; };
static std::unordered_map<uint64_t, LayoutResultDest> s_pending_layouts;
static std::atomic<uint32_t> g_next_fake_id{0x10000};

// Buffer binding tracking: buffer_handle -> {memory_handle, offset}
static std::unordered_map<uint64_t, std::pair<uint64_t, VkDeviceSize>> s_buffer_bindings;
// BDA cache: buffer_handle -> device_address (invalidated on bind)
static std::unordered_map<uint64_t, uint64_t> s_buffer_addresses;

uint64_t next_fake_handle() {
    return g_next_fake_id.fetch_add(1, std::memory_order_relaxed);
}

void track_buffer_binding(uint64_t buffer_handle, uint64_t memory_handle, VkDeviceSize offset) {
    std::lock_guard<std::mutex> lock(s_map_mutex);
    s_buffer_bindings[buffer_handle] = {memory_handle, offset};
    // Invalidate BDA cache — next vkGetBufferDeviceAddress will re-query with bind processed
    s_buffer_addresses.erase(buffer_handle);
    SPDLOG_INFO("track_buffer_binding: buf={:#x} mem={:#x} off={}", buffer_handle, memory_handle, offset);
}

// ---------------------------------------------------------------------------
// Dispatchable handle structures (needed so the Vulkan Loader can write its magic/dispatch table pointer without crashing)
// ---------------------------------------------------------------------------
// The Vulkan Loader writes ICD_LOADER_MAGIC + dispatch table pointers at offset 0.
// Minimum storage needed: 2 * sizeof(void*) = 16 bytes on x64.
struct alignas(void*) FakeInstance {
    void* loader_storage[8];  // 64 bytes — enough for loader dispatch + our data
    VkPhysicalDevice physical_device = VK_NULL_HANDLE;
    std::mutex mutex;
};
struct alignas(void*) FakePhysicalDevice {
    void* loader_storage[8];
};
struct alignas(void*) FakeDevice {
    void* loader_storage[8];
};
struct alignas(void*) FakeQueue {
    void* loader_storage[8];
};

static VkInstance make_fake_instance() {
    auto* inst = new FakeInstance{};
    set_loader_magic_value(inst);
    return reinterpret_cast<VkInstance>(inst);
}
static VkPhysicalDevice make_fake_phys_device() {
    auto* pd = new FakePhysicalDevice{};
    set_loader_magic_value(pd);
    return reinterpret_cast<VkPhysicalDevice>(pd);
}
static VkPhysicalDevice get_or_create_phys_device(VkInstance instance) {
    auto* inst = reinterpret_cast<FakeInstance*>(instance);
    if (!inst) return VK_NULL_HANDLE;
    std::lock_guard<std::mutex> lock(inst->mutex);
    if (inst->physical_device == VK_NULL_HANDLE) {
        inst->physical_device = make_fake_phys_device();
    }
    return inst->physical_device;
}
static VkDevice make_fake_device() {
    auto* dev = new FakeDevice{};
    set_loader_magic_value(dev);
    return reinterpret_cast<VkDevice>(dev);
}
static VkQueue make_fake_queue() {
    auto* q = new FakeQueue{};
    set_loader_magic_value(q);
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
    auto* inst = reinterpret_cast<FakeInstance*>(instance);
    if (inst) {
        if (inst->physical_device) {
            delete reinterpret_cast<FakePhysicalDevice*>(inst->physical_device);
        }
        delete inst;
    }
    SPDLOG_DEBUG("vkDestroyInstance: freed instance");
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

    pPhysicalDevices[0] = get_or_create_phys_device(instance);
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

    static std::vector<VkExtensionProperties> instance_extensions;
    if (instance_extensions.empty()) {
        auto add = [&](const char* name, uint32_t ver) {
            VkExtensionProperties p{};
            strncpy_s(p.extensionName, sizeof(p.extensionName), name, _TRUNCATE);
            p.specVersion = ver;
            instance_extensions.push_back(p);
        };
        add(VK_KHR_SURFACE_EXTENSION_NAME, VK_KHR_SURFACE_SPEC_VERSION);
        add("VK_KHR_win32_surface", 6);
        add("VK_KHR_get_physical_device_properties2", 2);
        add("VK_KHR_get_surface_capabilities2", 1);
        add("VK_KHR_surface_protected_capabilities", 1);
        add("VK_KHR_surface_maintenance1", 1);
        add("VK_KHR_device_group_creation", 1);
        add("VK_KHR_external_fence_capabilities", 1);
        add("VK_KHR_external_memory_capabilities", 1);
        add("VK_KHR_external_semaphore_capabilities", 1);
        add("VK_KHR_display", 23);
        add("VK_KHR_get_display_properties2", 1);
        add("VK_KHR_portability_enumeration", 1);
        add("VK_KHR_driver_properties", 1);
        add("VK_EXT_surface_maintenance1", 1);
        add("VK_EXT_swapchain_colorspace", 5);
        add("VK_EXT_debug_report", 10);
        add("VK_EXT_debug_utils", 2);
        add("VK_EXT_direct_mode_display", 1);
        add("VK_LUNARG_direct_driver_loading", 1);
        add("VK_NV_external_memory_capabilities", 1);
    }
    uint32_t count = static_cast<uint32_t>(instance_extensions.size());

    if (!pProperties) {
        *pPropertyCount = count;
        return VK_SUCCESS;
    }
    uint32_t to_copy = std::min(*pPropertyCount, count);
    std::memcpy(pProperties, instance_extensions.data(), to_copy * sizeof(VkExtensionProperties));
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

    static std::vector<VkExtensionProperties> device_extensions;
    if (device_extensions.empty()) {
        auto add = [&](const char* name, uint32_t ver) {
            VkExtensionProperties p{};
            strncpy_s(p.extensionName, sizeof(p.extensionName), name, _TRUNCATE);
            p.specVersion = ver;
            device_extensions.push_back(p);
        };
        add(VK_KHR_SWAPCHAIN_EXTENSION_NAME, VK_KHR_SWAPCHAIN_SPEC_VERSION);
        add("VK_KHR_maintenance1", 2);
        add("VK_KHR_maintenance2", 1);
        add("VK_KHR_maintenance3", 1);
        add("VK_KHR_maintenance4", 2);
        add("VK_KHR_shader_draw_parameters", 1);
        add("VK_KHR_storage_buffer_storage_class", 1);
        add("VK_KHR_16bit_storage", 1);
        add("VK_KHR_8bit_storage", 1);
        add("VK_KHR_shader_float16_int8", 1);
        add("VK_KHR_cooperative_matrix", 1);
        add("VK_KHR_shader_integer_dot_product", 1);
        add("VK_KHR_descriptor_update_template", 1);
        add("VK_KHR_sampler_ycbcr_conversion", 14);
        add("VK_KHR_multiview", 1);
        add("VK_KHR_get_memory_requirements2", 1);
        add("VK_KHR_bind_memory2", 1);
        add("VK_KHR_dedicated_allocation", 3);
        add("VK_KHR_driver_properties", 1);
        add("VK_KHR_timeline_semaphore", 2);
        add("VK_KHR_vulkan_memory_model", 3);
        add("VK_KHR_uniform_buffer_standard_layout", 1);
        add("VK_KHR_imageless_framebuffer", 1);
        add("VK_KHR_spirv_1_4_extension", 1);
        add("VK_KHR_separate_depth_stencil_layouts", 1);
        add("VK_KHR_shader_subgroup_extended_types", 1);
        add(VK_EXT_SUBGROUP_SIZE_CONTROL_EXTENSION_NAME, 1);
        add("VK_KHR_create_renderpass2", 1);
        add("VK_KHR_depth_stencil_resolve", 1);
        add("VK_EXT_vertex_input_dynamic_state", 2);
        add("VK_EXT_private_data", 1);
        add("VK_EXT_extended_dynamic_state", 1);
        add("VK_EXT_extended_dynamic_state2", 1);
        add("VK_EXT_tooling_info", 1);
        add("VK_KHR_buffer_device_address", 1);
        add("VK_KHR_push_descriptor", 2);
        add("VK_KHR_maintenance5", 1);
        add("VK_KHR_maintenance6", 1);
    }
    uint32_t count = static_cast<uint32_t>(device_extensions.size());

    if (!pProperties) {
        *pPropertyCount = count;
        return VK_SUCCESS;
    }

    uint32_t to_copy = std::min(*pPropertyCount, count);
    std::memcpy(pProperties, device_extensions.data(), to_copy * sizeof(VkExtensionProperties));
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
    pProperties->vendorID = caps.valid() ? caps.vendor_id : 0x10DE;
    pProperties->deviceID = caps.valid() ? caps.device_id : 0x2684;
    pProperties->deviceType = static_cast<VkPhysicalDeviceType>(
        caps.valid() ? caps.device_type : 2);
    std::memcpy(pProperties->pipelineCacheUUID, "\x01\x02\x03\x04\x05\x06\x07\x08\x09\x0a\x0b\x0c\x0d\x0e\x0f\x10", 16);

    pProperties->limits.maxImageDimension1D = caps.max_image_dimension_2d;
    pProperties->limits.maxImageDimension2D = caps.max_image_dimension_2d;
    pProperties->limits.maxImageDimension3D = caps.max_image_dimension_2d;
    pProperties->limits.maxImageDimensionCube = caps.max_image_dimension_2d;
    pProperties->limits.maxImageArrayLayers = 256;
    pProperties->limits.maxTexelBufferElements = 0x10000000;
    pProperties->limits.maxUniformBufferRange = static_cast<uint32_t>(caps.max_uniform_buffer_range);
    pProperties->limits.maxStorageBufferRange = 0xFFFFFFFF;  // Allow large buffer ranges (NVIDIA supports this)
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
    pProperties->limits.optimalBufferCopyOffsetAlignment = 1; // Match NVIDIA
    pProperties->limits.optimalBufferCopyRowPitchAlignment = 1; // Match NVIDIA
    pProperties->limits.nonCoherentAtomSize = caps.non_coherent_atom_size ? caps.non_coherent_atom_size : 64; // Match NVIDIA (0x40)
    pProperties->limits.minTexelBufferOffsetAlignment = 256;
    pProperties->limits.minUniformBufferOffsetAlignment = caps.min_uniform_buffer_offset_alignment ? caps.min_uniform_buffer_offset_alignment : 64; // Match NVIDIA (0x40)
    pProperties->limits.minStorageBufferOffsetAlignment = caps.min_storage_buffer_offset_alignment ? caps.min_storage_buffer_offset_alignment : 16; // Match NVIDIA (0x10)

    pProperties->sparseProperties.residencyAlignedMipSize = VK_TRUE;
    pProperties->sparseProperties.residencyNonResidentStrict = VK_TRUE;
    pProperties->sparseProperties.residencyStandard2DBlockShape = VK_TRUE;
    pProperties->sparseProperties.residencyStandard2DMultisampleBlockShape = VK_TRUE;
    pProperties->sparseProperties.residencyStandard3DBlockShape = VK_TRUE;

    // Critical: minMemoryMapAlignment — used by host buffer allocator as divisor
    pProperties->limits.minMemoryMapAlignment = 64;

    // Store minStorageBufferOffsetAlignment for diagnostic access
    static VkDeviceSize s_last_min_storage_align = 0;
    s_last_min_storage_align = pProperties->limits.minStorageBufferOffsetAlignment;

    // Diagnostic: log key alignment/divisor properties
    static bool diag_logged = false;
    if (!diag_logged) {
        diag_logged = true;
        FILE* diag = fopen("C:\\Users\\test\\AppData\\Local\\Temp\\omnigpu_diag.txt", "a");
        if (diag) {
            fprintf(diag, "--- Device Limits ---\n");
            fprintf(diag, "minStorageBufferOffsetAlignment: %llu\n",
                    (unsigned long long)pProperties->limits.minStorageBufferOffsetAlignment);
            fprintf(diag, "minUniformBufferOffsetAlignment: %llu\n",
                    (unsigned long long)pProperties->limits.minUniformBufferOffsetAlignment);
            fprintf(diag, "nonCoherentAtomSize: %llu\n",
                    (unsigned long long)pProperties->limits.nonCoherentAtomSize);
            fprintf(diag, "optimalBufferCopyOffsetAlignment: %llu\n",
                    (unsigned long long)pProperties->limits.optimalBufferCopyOffsetAlignment);
            fprintf(diag, "bufferImageGranularity: %llu\n",
                    (unsigned long long)pProperties->limits.bufferImageGranularity);
            fprintf(diag, "maxComputeWorkGroupInvocations: %u\n",
                    pProperties->limits.maxComputeWorkGroupInvocations);
            fprintf(diag, "maxComputeSharedMemorySize: %u\n",
                    pProperties->limits.maxComputeSharedMemorySize);
            fprintf(diag, "maxComputeWorkGroupCount: [%u, %u, %u]\n",
                    pProperties->limits.maxComputeWorkGroupCount[0],
                    pProperties->limits.maxComputeWorkGroupCount[1],
                    pProperties->limits.maxComputeWorkGroupCount[2]);
            fprintf(diag, "maxComputeWorkGroupSize: [%u, %u, %u]\n",
                    pProperties->limits.maxComputeWorkGroupSize[0],
                    pProperties->limits.maxComputeWorkGroupSize[1],
                    pProperties->limits.maxComputeWorkGroupSize[2]);
            fprintf(diag, "maxPushConstantsSize: %u\n",
                    pProperties->limits.maxPushConstantsSize);
            fprintf(diag, "maxBoundDescriptorSets: %u\n",
                    pProperties->limits.maxBoundDescriptorSets);
            fprintf(diag, "minTexelBufferOffsetAlignment: %llu\n",
                    (unsigned long long)pProperties->limits.minTexelBufferOffsetAlignment);
            fprintf(diag, "timestampPeriod: %f\n",
                    pProperties->limits.timestampPeriod);
            fclose(diag);
        }
    }
}

void VKAPI_PTR vkGetPhysicalDeviceProperties2_hook(
    VkPhysicalDevice physicalDevice,
    VkPhysicalDeviceProperties2* pProperties)
{
    SPDLOG_TRACE("Intercepted: vkGetPhysicalDeviceProperties2");
    if (!pProperties) return;

    vkGetPhysicalDeviceProperties_hook(physicalDevice, &pProperties->properties);

    auto& caps = caps::get();

    // Traverse pNext chain and fill known structs
    VkBaseOutStructure* prev = reinterpret_cast<VkBaseOutStructure*>(pProperties);
    VkBaseOutStructure* ext = reinterpret_cast<VkBaseOutStructure*>(pProperties->pNext);
    bool has_driver_props = false;
    while (ext) {
        switch (ext->sType) {
        // In modern Vulkan headers (1.4+):
        //   VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_PROPERTIES = 1000094000
        //   VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_PROPERTIES = 50
        // In old Vulkan headers:
        //   VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_PROPERTIES = 1000094000 (same as modern SUBGROUP_PROPERTIES!)
        // Handle both: SUBGROUP_PROPERTIES (modern 1000094000) catches old VK11_PROPERTIES too
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_PROPERTIES: {
            auto* sg = reinterpret_cast<VkPhysicalDeviceSubgroupProperties*>(ext);
            sg->subgroupSize = caps.valid() ? caps.subgroup_size : 32;
            sg->supportedStages = VK_SHADER_STAGE_ALL;
            sg->supportedOperations = static_cast<VkSubgroupFeatureFlags>(
                VK_SUBGROUP_FEATURE_BASIC_BIT |
                VK_SUBGROUP_FEATURE_VOTE_BIT |
                VK_SUBGROUP_FEATURE_ARITHMETIC_BIT |
                VK_SUBGROUP_FEATURE_BALLOT_BIT |
                VK_SUBGROUP_FEATURE_SHUFFLE_BIT |
                VK_SUBGROUP_FEATURE_SHUFFLE_RELATIVE_BIT |
                VK_SUBGROUP_FEATURE_CLUSTERED_BIT |
                VK_SUBGROUP_FEATURE_QUAD_BIT);
            sg->quadOperationsInAllStages = VK_TRUE;
            break;
        }
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_PROPERTIES: {
            auto* p11 = reinterpret_cast<VkPhysicalDeviceVulkan11Properties*>(ext);
            p11->maxMultiviewViewCount = 6;
            p11->maxMultiviewInstanceIndex = (1u << 27) - 1;
            p11->subgroupSize = caps.valid() ? caps.subgroup_size : 32;
            p11->subgroupSupportedOperations = static_cast<VkSubgroupFeatureFlags>(
                caps.valid() ? caps.supported_subgroup_operations :
                (VK_SUBGROUP_FEATURE_BASIC_BIT | VK_SUBGROUP_FEATURE_VOTE_BIT |
                 VK_SUBGROUP_FEATURE_ARITHMETIC_BIT | VK_SUBGROUP_FEATURE_BALLOT_BIT |
                 VK_SUBGROUP_FEATURE_SHUFFLE_BIT | VK_SUBGROUP_FEATURE_SHUFFLE_RELATIVE_BIT |
                 VK_SUBGROUP_FEATURE_CLUSTERED_BIT | VK_SUBGROUP_FEATURE_QUAD_BIT));
            p11->subgroupSupportedStages = VK_SHADER_STAGE_ALL;
            // Fill UUIDs from cached host GPU caps
            {
                auto& caps = caps::get();
                uint8_t dev_uuid[VK_UUID_SIZE]{};
                uint8_t drv_uuid[VK_UUID_SIZE]{};
                if (caps.valid()) {
                    for (int i = 0; i < 4; i++) {
                        dev_uuid[i]   = static_cast<uint8_t>((caps.vendor_id >> (i * 8)) & 0xFF);
                        dev_uuid[4+i] = static_cast<uint8_t>((caps.device_id >> (i * 8)) & 0xFF);
                        dev_uuid[8+i] = static_cast<uint8_t>((caps.driver_version >> (i * 8)) & 0xFF);
                        drv_uuid[i]   = static_cast<uint8_t>((caps.driver_version >> (i * 8)) & 0xFF);
                        drv_uuid[4+i] = static_cast<uint8_t>((caps.vendor_id >> (i * 8)) & 0xFF);
                    }
                } else {
                    dev_uuid[0] = 0x01; dev_uuid[1] = 0x02; dev_uuid[2] = 0x03; dev_uuid[3] = 0x04;
                    dev_uuid[4] = 0x05; dev_uuid[5] = 0x06; dev_uuid[6] = 0x07; dev_uuid[7] = 0x08;
                    dev_uuid[8] = 0x09; dev_uuid[9] = 0x0a; dev_uuid[10]= 0x0b; dev_uuid[11]= 0x0c;
                    dev_uuid[12]= 0x0d; dev_uuid[13]= 0x0e; dev_uuid[14]= 0x0f; dev_uuid[15]= 0x10;
                    std::memcpy(drv_uuid, dev_uuid, VK_UUID_SIZE);
                }
                std::memcpy(p11->deviceUUID, dev_uuid, VK_UUID_SIZE);
                std::memcpy(p11->driverUUID, drv_uuid, VK_UUID_SIZE);
            }
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
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_SIZE_CONTROL_PROPERTIES: {
            auto* scp = reinterpret_cast<VkPhysicalDeviceSubgroupSizeControlProperties*>(ext);
            scp->minSubgroupSize = 32;
            scp->maxSubgroupSize = 32;
            scp->maxComputeWorkgroupSubgroups = 0xFFFFFFFF;
            scp->requiredSubgroupSizeStages = VK_SHADER_STAGE_ALL;
            break;
        }
        case 1000095000: // old VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_PROPERTIES (pre-renumbering)
        {
            auto* sg = reinterpret_cast<VkPhysicalDeviceSubgroupProperties*>(ext);
            sg->subgroupSize = caps.valid() ? caps.subgroup_size : 32;
            sg->supportedStages = VK_SHADER_STAGE_ALL;
            // Set all subgroup operations supported (NVIDIA GPUs support all)
            sg->supportedOperations = static_cast<VkSubgroupFeatureFlags>(
                VK_SUBGROUP_FEATURE_BASIC_BIT |
                VK_SUBGROUP_FEATURE_VOTE_BIT |
                VK_SUBGROUP_FEATURE_ARITHMETIC_BIT |
                VK_SUBGROUP_FEATURE_BALLOT_BIT |
                VK_SUBGROUP_FEATURE_SHUFFLE_BIT |
                VK_SUBGROUP_FEATURE_SHUFFLE_RELATIVE_BIT |
                VK_SUBGROUP_FEATURE_CLUSTERED_BIT |
                VK_SUBGROUP_FEATURE_QUAD_BIT);
            sg->quadOperationsInAllStages = VK_TRUE;
            break;
        }
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAINTENANCE_4_PROPERTIES: {
            auto* m4 = reinterpret_cast<VkPhysicalDeviceMaintenance4Properties*>(ext);
            m4->maxBufferSize = 8ULL * 1024 * 1024 * 1024;
            break;
        }
        case 1000168000: { // VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAINTENANCE_3_PROPERTIES
            auto* m3 = reinterpret_cast<VkPhysicalDeviceMaintenance3Properties*>(ext);
            m3->maxMemoryAllocationSize = caps.valid() ? caps.max_memory_allocation_size : (8ULL * 1024 * 1024 * 1024);
            m3->maxPerSetDescriptors = 1024;
            break;
        }
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES: {
            auto* drv = reinterpret_cast<VkPhysicalDeviceDriverProperties*>(ext);
            auto& caps = caps::get();
            if (caps.valid()) {
                strncpy_s(drv->driverName, VK_MAX_DRIVER_NAME_SIZE,
                          caps.gpu_name.c_str(), _TRUNCATE);
                strncpy_s(drv->driverInfo, VK_MAX_DRIVER_INFO_SIZE,
                          ("OmniGPU forwarding to " + caps.gpu_name).c_str(), _TRUNCATE);
                drv->driverID = VK_DRIVER_ID_NVIDIA_PROPRIETARY;
                drv->conformanceVersion = {1, 3, 6, 0};
            } else {
                drv->driverID = VK_DRIVER_ID_AMD_PROPRIETARY;
                drv->conformanceVersion = {1, 3, 0, 0};
                strncpy_s(drv->driverName, VK_MAX_DRIVER_NAME_SIZE,
                          "OmniGPU Virtual", _TRUNCATE);
            }
            has_driver_props = true;
            SPDLOG_INFO("vkGetPhysicalDeviceProperties2: set driverID={}", static_cast<int>(drv->driverID));
            break;
        }
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES: {
            auto* id = reinterpret_cast<VkPhysicalDeviceIDProperties*>(ext);
            std::memcpy(id->deviceUUID, "\x01\x02\x03\x04\x05\x06\x07\x08\x09\x0a\x0b\x0c\x0d\x0e\x0f\x10", VK_UUID_SIZE);
            std::memcpy(id->driverUUID, "\x10\x0f\x0e\x0d\x0c\x0b\x0a\x09\x08\x07\x06\x05\x04\x03\x02\x01", VK_UUID_SIZE);
            id->deviceLUIDValid = VK_FALSE;
            SPDLOG_INFO("vkGetPhysicalDeviceProperties2: handled VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES");
            break;
        }
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_INTEGER_DOT_PRODUCT_PROPERTIES: {
            auto* p = reinterpret_cast<VkPhysicalDeviceShaderIntegerDotProductProperties*>(ext);
            bool ok = caps.valid() && caps.supports_integer_dot_product;
            p->integerDotProduct8BitUnsignedAccelerated = ok;
            p->integerDotProduct8BitSignedAccelerated = ok;
            p->integerDotProduct8BitMixedSignednessAccelerated = ok;
            p->integerDotProduct4x8BitPackedUnsignedAccelerated = ok;
            p->integerDotProduct4x8BitPackedSignedAccelerated = ok;
            p->integerDotProduct4x8BitPackedMixedSignednessAccelerated = ok;
            p->integerDotProduct16BitUnsignedAccelerated = ok;
            p->integerDotProduct16BitSignedAccelerated = ok;
            p->integerDotProduct16BitMixedSignednessAccelerated = ok;
            p->integerDotProduct32BitUnsignedAccelerated = ok;
            p->integerDotProduct32BitSignedAccelerated = ok;
            p->integerDotProduct32BitMixedSignednessAccelerated = ok;
            p->integerDotProduct64BitUnsignedAccelerated = ok;
            p->integerDotProduct64BitSignedAccelerated = ok;
            p->integerDotProduct64BitMixedSignednessAccelerated = ok;
            p->integerDotProductAccumulatingSaturating8BitUnsignedAccelerated = ok;
            p->integerDotProductAccumulatingSaturating8BitSignedAccelerated = ok;
            p->integerDotProductAccumulatingSaturating8BitMixedSignednessAccelerated = ok;
            p->integerDotProductAccumulatingSaturating4x8BitPackedUnsignedAccelerated = ok;
            p->integerDotProductAccumulatingSaturating4x8BitPackedSignedAccelerated = ok;
            p->integerDotProductAccumulatingSaturating4x8BitPackedMixedSignednessAccelerated = ok;
            p->integerDotProductAccumulatingSaturating16BitUnsignedAccelerated = ok;
            p->integerDotProductAccumulatingSaturating16BitSignedAccelerated = ok;
            p->integerDotProductAccumulatingSaturating16BitMixedSignednessAccelerated = ok;
            p->integerDotProductAccumulatingSaturating32BitUnsignedAccelerated = ok;
            p->integerDotProductAccumulatingSaturating32BitSignedAccelerated = ok;
            p->integerDotProductAccumulatingSaturating32BitMixedSignednessAccelerated = ok;
            p->integerDotProductAccumulatingSaturating64BitUnsignedAccelerated = ok;
            p->integerDotProductAccumulatingSaturating64BitSignedAccelerated = ok;
            p->integerDotProductAccumulatingSaturating64BitMixedSignednessAccelerated = ok;
            SPDLOG_INFO("vkGetPhysicalDeviceProperties2: handled VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_INTEGER_DOT_PRODUCT_PROPERTIES");
            break;
        }
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TEXEL_BUFFER_ALIGNMENT_PROPERTIES: {
            auto* p = reinterpret_cast<VkPhysicalDeviceTexelBufferAlignmentProperties*>(ext);
            p->storageTexelBufferOffsetAlignmentBytes = 256;
            p->storageTexelBufferOffsetSingleTexelAlignment = VK_FALSE;
            p->uniformTexelBufferOffsetAlignmentBytes = 256;
            p->uniformTexelBufferOffsetSingleTexelAlignment = VK_FALSE;
            SPDLOG_INFO("vkGetPhysicalDeviceProperties2: handled VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TEXEL_BUFFER_ALIGNMENT_PROPERTIES");
            break;
        }
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_4_PROPERTIES: {
            auto* p14 = reinterpret_cast<VkPhysicalDeviceVulkan14Properties*>(ext);
            p14->lineSubPixelPrecisionBits = 8;
            p14->maxVertexAttribDivisor = 0xFFFFFFFF;
            p14->supportsNonZeroFirstInstance = VK_TRUE;
            p14->maxPushDescriptors = 32;
            p14->dynamicRenderingLocalReadDepthStencilAttachments = VK_TRUE;
            p14->dynamicRenderingLocalReadMultisampledAttachments = VK_TRUE;
            p14->earlyFragmentMultisampleCoverageAfterSampleCounting = VK_TRUE;
            p14->earlyFragmentSampleMaskTestBeforeSampleCounting = VK_TRUE;
            p14->depthStencilSwizzleOneSupport = VK_TRUE;
            p14->polygonModePointSize = VK_TRUE;
            p14->nonStrictSinglePixelWideLinesUseParallelogram = VK_TRUE;
            break;
        }
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PUSH_DESCRIPTOR_PROPERTIES_KHR: {
            auto* pd = reinterpret_cast<VkPhysicalDevicePushDescriptorPropertiesKHR*>(ext);
            pd->maxPushDescriptors = 32;
            break;
        }
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAINTENANCE_5_PROPERTIES_KHR: {
            auto* m5 = reinterpret_cast<VkPhysicalDeviceMaintenance5PropertiesKHR*>(ext);
            m5->earlyFragmentMultisampleCoverageAfterSampleCounting = VK_TRUE;
            m5->earlyFragmentSampleMaskTestBeforeSampleCounting = VK_TRUE;
            m5->depthStencilSwizzleOneSupport = VK_TRUE;
            m5->polygonModePointSize = VK_TRUE;
            m5->nonStrictSinglePixelWideLinesUseParallelogram = VK_TRUE;
            break;
        }
        default:
            SPDLOG_INFO("vkGetPhysicalDeviceProperties2: unhandled sType={}", static_cast<int>(ext->sType));
            break;
        }
        prev = ext;
        ext = ext->pNext;
    }

    // If vulkaninfo didn't request driver properties, inject them into the chain
    if (!has_driver_props && caps.valid()) {
        static VkPhysicalDeviceDriverProperties fallback_drv{};
        if (fallback_drv.sType == 0) {
            fallback_drv.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES;
            fallback_drv.driverID = VK_DRIVER_ID_NVIDIA_PROPRIETARY;
            strncpy_s(fallback_drv.driverName, VK_MAX_DRIVER_NAME_SIZE,
                      caps.gpu_name.c_str(), _TRUNCATE);
            strncpy_s(fallback_drv.driverInfo, VK_MAX_DRIVER_INFO_SIZE,
                      ("OmniGPU forwarding to " + caps.gpu_name).c_str(), _TRUNCATE);
            fallback_drv.conformanceVersion = {1, 3, 6, 0};
        }
        fallback_drv.pNext = nullptr;
        prev->pNext = reinterpret_cast<VkBaseOutStructure*>(&fallback_drv);
        SPDLOG_INFO("vkGetPhysicalDeviceProperties2: injected driver properties");
    }
}

void VKAPI_PTR vkGetPhysicalDeviceFeatures_hook(
    VkPhysicalDevice physicalDevice,
    VkPhysicalDeviceFeatures* pFeatures)
{
    SPDLOG_TRACE("Intercepted: vkGetPhysicalDeviceFeatures");
    std::memset(pFeatures, 0, sizeof(*pFeatures));

    // Compute-critical features — enable only what compute workloads actually need
    pFeatures->robustBufferAccess = VK_TRUE;
    pFeatures->shaderFloat64 = VK_TRUE;
    pFeatures->shaderInt64 = VK_TRUE;
    pFeatures->shaderInt16 = VK_TRUE;
    pFeatures->vertexPipelineStoresAndAtomics = VK_TRUE;
    pFeatures->fragmentStoresAndAtomics = VK_TRUE;
    pFeatures->shaderStorageImageExtendedFormats = VK_TRUE;
    pFeatures->shaderStorageImageReadWithoutFormat = VK_TRUE;
    pFeatures->shaderStorageImageWriteWithoutFormat = VK_TRUE;
    pFeatures->multiDrawIndirect = VK_TRUE;
    pFeatures->drawIndirectFirstInstance = VK_TRUE;
    pFeatures->pipelineStatisticsQuery = VK_TRUE;
    pFeatures->occlusionQueryPrecise = VK_TRUE;

    // Rendering features — OFF by default (apps must check before enabling)
    // geometryShader, tessellationShader, fillModeNonSolid, samplerAnisotropy,
    // shaderClipDistance, shaderCullDistance, imageCubeArray, independentBlend,
    // depthClamp, largePoints, textureCompressionBC = VK_FALSE
}

void VKAPI_PTR vkGetPhysicalDeviceFeatures2_hook(
    VkPhysicalDevice physicalDevice,
    VkPhysicalDeviceFeatures2* pFeatures)
{
    SPDLOG_TRACE("Intercepted: vkGetPhysicalDeviceFeatures2");
    if (!pFeatures) return;
    vkGetPhysicalDeviceFeatures_hook(physicalDevice, &pFeatures->features);

    auto& caps = caps::get();

    VkBaseOutStructure* ext = reinterpret_cast<VkBaseOutStructure*>(pFeatures->pNext);
    while (ext) {
        switch (ext->sType) {
        case 1000094001: // old VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES: {
            auto* f11 = reinterpret_cast<VkPhysicalDeviceVulkan11Features*>(ext);
            // Compute-relevant 1.1 features
            f11->multiview = VK_FALSE;
            f11->samplerYcbcrConversion = VK_TRUE;
            // 16-bit storage promoted to Vulkan 1.1
            f11->storageBuffer16BitAccess = caps.valid() ? caps.supports_16bit_storage : VK_FALSE;
            f11->uniformAndStorageBuffer16BitAccess = caps.valid() ? caps.supports_16bit_storage : VK_FALSE;
            break;
        }
        case 1000094003: // old VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES: {
            auto* f12 = reinterpret_cast<VkPhysicalDeviceVulkan12Features*>(ext);
            // Compute-critical 1.2 features
            f12->bufferDeviceAddress = VK_TRUE;
            f12->descriptorIndexing = VK_TRUE;
            f12->shaderStorageBufferArrayNonUniformIndexing = VK_TRUE;
            f12->descriptorBindingStorageBufferUpdateAfterBind = VK_TRUE;
            f12->descriptorBindingStorageImageUpdateAfterBind = VK_TRUE;
            f12->descriptorBindingPartiallyBound = VK_TRUE;
            f12->descriptorBindingVariableDescriptorCount = VK_TRUE;
            f12->runtimeDescriptorArray = VK_TRUE;
            f12->scalarBlockLayout = VK_TRUE;
            f12->timelineSemaphore = VK_TRUE;
            f12->uniformBufferStandardLayout = VK_TRUE;
            f12->hostQueryReset = VK_TRUE;
            f12->vulkanMemoryModel = VK_TRUE;
            f12->vulkanMemoryModelDeviceScope = VK_TRUE;
            f12->drawIndirectCount = VK_TRUE;
            // ML support — enable only if host GPU supports them
            f12->shaderFloat16 = caps.valid() ? caps.supports_float16_int8 : VK_FALSE;
            f12->shaderInt8   = caps.valid() ? caps.supports_float16_int8 : VK_FALSE;
            // 8-bit storage promoted to Vulkan 1.2
            f12->storageBuffer8BitAccess = caps.valid() ? caps.supports_8bit_storage : VK_FALSE;
            f12->uniformAndStorageBuffer8BitAccess = caps.valid() ? caps.supports_8bit_storage : VK_FALSE;
            // Non-essential for compute
            f12->samplerMirrorClampToEdge = VK_FALSE;
            f12->shaderSampledImageArrayNonUniformIndexing = VK_FALSE;
            f12->descriptorBindingUniformBufferUpdateAfterBind = VK_FALSE;
            f12->descriptorBindingSampledImageUpdateAfterBind = VK_FALSE;
            f12->descriptorBindingUpdateUnusedWhilePending = VK_FALSE;
            f12->imagelessFramebuffer = VK_FALSE;
            f12->shaderOutputViewportIndex = VK_FALSE;
            f12->shaderOutputLayer = VK_FALSE;
            f12->subgroupBroadcastDynamicId = VK_FALSE;
            break;
        }
        case 1000094005: // old VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES: {
            auto* f13 = reinterpret_cast<VkPhysicalDeviceVulkan13Features*>(ext);
            // Compute-critical 1.3 features
            f13->synchronization2 = VK_TRUE;
            f13->maintenance4 = VK_TRUE;
            f13->pipelineCreationCacheControl = VK_TRUE;
            f13->subgroupSizeControl = VK_TRUE;
            f13->computeFullSubgroups = VK_TRUE;
            f13->shaderZeroInitializeWorkgroupMemory = VK_TRUE;
            f13->shaderIntegerDotProduct = caps.valid() ? caps.supports_integer_dot_product : VK_FALSE;
            // Non-essential for compute
            f13->dynamicRendering = VK_FALSE;
            f13->inlineUniformBlock = VK_FALSE;
            f13->privateData = VK_FALSE;
            f13->shaderDemoteToHelperInvocation = VK_FALSE;
            f13->shaderTerminateInvocation = VK_FALSE;
            f13->textureCompressionASTC_HDR = VK_FALSE;
            break;
        }
        case 1000168000: // old VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_16BIT_STORAGE_FEATURES (KHR extension numbering)
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_16BIT_STORAGE_FEATURES: {
            auto* f16s = reinterpret_cast<VkPhysicalDevice16BitStorageFeatures*>(ext);
            bool ok = caps.valid() && caps.supports_16bit_storage;
            f16s->storageBuffer16BitAccess = ok;
            f16s->uniformAndStorageBuffer16BitAccess = ok;
            f16s->storagePushConstant16 = VK_FALSE;
            f16s->storageInputOutput16 = VK_FALSE;
            break;
        }
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_8BIT_STORAGE_FEATURES: {
            auto* f8s = reinterpret_cast<VkPhysicalDevice8BitStorageFeatures*>(ext);
            bool ok = caps.valid() && caps.supports_8bit_storage;
            f8s->storageBuffer8BitAccess = ok;
            f8s->uniformAndStorageBuffer8BitAccess = ok;
            f8s->storagePushConstant8 = VK_FALSE;
            break;
        }
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_FLOAT16_INT8_FEATURES: {
            auto* ffi = reinterpret_cast<VkPhysicalDeviceShaderFloat16Int8Features*>(ext);
            bool ok = caps.valid() && caps.supports_float16_int8;
            ffi->shaderFloat16 = ok;
            ffi->shaderInt8 = ok;
            break;
        }
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_COOPERATIVE_MATRIX_FEATURES_KHR: {
            auto* f = reinterpret_cast<VkPhysicalDeviceCooperativeMatrixFeaturesKHR*>(ext);
            bool ok = caps.valid() && caps.supports_cooperative_matrix;
            f->cooperativeMatrix = ok;
            break;
        }
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAINTENANCE_4_FEATURES: {
            auto* f4 = reinterpret_cast<VkPhysicalDeviceMaintenance4Features*>(ext);
            f4->maintenance4 = VK_TRUE;
            break;
        }
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_INTEGER_DOT_PRODUCT_FEATURES_KHR: {
            auto* f = reinterpret_cast<VkPhysicalDeviceShaderIntegerDotProductFeaturesKHR*>(ext);
            bool ok = caps.valid() && caps.supports_integer_dot_product;
            f->shaderIntegerDotProduct = ok;
            break;
        }
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_4_FEATURES: {
            auto* f14 = reinterpret_cast<VkPhysicalDeviceVulkan14Features*>(ext);
            f14->globalPriorityQuery = VK_FALSE;
            f14->shaderSubgroupRotate = VK_TRUE;
            f14->shaderExpectAssume = VK_TRUE;
            f14->shaderFloatControls2 = VK_TRUE;
            f14->shaderSubgroupRotateClustered = VK_TRUE;
            f14->pushDescriptor = VK_TRUE;
            f14->dynamicRenderingLocalRead = VK_TRUE;
            f14->maintenance5 = VK_TRUE;
            f14->pipelineProtectedAccess = VK_FALSE;
            f14->pipelineRobustness = VK_TRUE;
            break;
        }
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAINTENANCE_5_FEATURES_KHR: {
            auto* f5 = reinterpret_cast<VkPhysicalDeviceMaintenance5FeaturesKHR*>(ext);
            f5->maintenance5 = VK_TRUE;
            break;
        }
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES: {
            auto* fbda = reinterpret_cast<VkPhysicalDeviceBufferDeviceAddressFeatures*>(ext);
            fbda->bufferDeviceAddress = VK_TRUE;
            fbda->bufferDeviceAddressCaptureReplay = VK_FALSE;
            fbda->bufferDeviceAddressMultiDevice = VK_FALSE;
            break;
        }
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES_EXT: {
            auto* fbda = reinterpret_cast<VkPhysicalDeviceBufferDeviceAddressFeatures*>(ext);
            fbda->bufferDeviceAddress = VK_TRUE;
            fbda->bufferDeviceAddressCaptureReplay = VK_FALSE;
            fbda->bufferDeviceAddressMultiDevice = VK_FALSE;
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

    uint64_t heap0size = 24ULL * 1024 * 1024 * 1024;
    uint32_t heap0flags = VK_MEMORY_HEAP_DEVICE_LOCAL_BIT;
    uint64_t heap1size = 16ULL * 1024 * 1024 * 1024;
    uint32_t heap1flags = 0;

    if (caps.valid()) {
        heap0size = caps.memory_heap_size_0 > 0 ? caps.memory_heap_size_0 : heap0size;
        heap0flags = caps.heap_0_flags;
        heap1size = caps.memory_heap_size_1 > 0 ? caps.memory_heap_size_1 : heap1size;
        heap1flags = caps.heap_1_flags;
    }

    pMemoryProperties->memoryHeapCount = 2;
    pMemoryProperties->memoryHeaps[0].size = heap0size;
    pMemoryProperties->memoryHeaps[0].flags = static_cast<VkMemoryHeapFlags>(heap0flags);
    pMemoryProperties->memoryHeaps[1].size = heap1size;
    pMemoryProperties->memoryHeaps[1].flags = static_cast<VkMemoryHeapFlags>(heap1flags);

    // 5 memory types:
    // Type 0: DEVICE_LOCAL only
    // Type 1: DEVICE_LOCAL | HOST_VISIBLE | HOST_COHERENT — BAR/rebar
    // Type 2: HOST_VISIBLE | HOST_COHERENT — staging from system RAM
    // Type 3: HOST_VISIBLE | HOST_CACHED — cached staging
    // Type 4: DEVICE_LOCAL | HOST_VISIBLE | HOST_CACHED — cached unified
    pMemoryProperties->memoryTypeCount = 5;
    pMemoryProperties->memoryTypes[0].heapIndex = 0;
    pMemoryProperties->memoryTypes[0].propertyFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;

    pMemoryProperties->memoryTypes[1].heapIndex = 0;
    pMemoryProperties->memoryTypes[1].propertyFlags =
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT |
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
        VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;

    pMemoryProperties->memoryTypes[2].heapIndex = 1;
    pMemoryProperties->memoryTypes[2].propertyFlags =
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
        VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;

    pMemoryProperties->memoryTypes[3].heapIndex = 1;
    pMemoryProperties->memoryTypes[3].propertyFlags =
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
        VK_MEMORY_PROPERTY_HOST_CACHED_BIT;

    pMemoryProperties->memoryTypes[4].heapIndex = 0;
    pMemoryProperties->memoryTypes[4].propertyFlags =
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT |
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
        VK_MEMORY_PROPERTY_HOST_CACHED_BIT;
}

void VKAPI_PTR vkGetPhysicalDeviceMemoryProperties2_hook(
    VkPhysicalDevice physicalDevice,
    VkPhysicalDeviceMemoryProperties2* pMemoryProperties)
{
    SPDLOG_TRACE("Intercepted: vkGetPhysicalDeviceMemoryProperties2");
    if (!pMemoryProperties) return;
    vkGetPhysicalDeviceMemoryProperties_hook(physicalDevice, &pMemoryProperties->memoryProperties);

    VkBaseOutStructure* ext = reinterpret_cast<VkBaseOutStructure*>(pMemoryProperties->pNext);
    while (ext) {
        if (ext->sType == VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_BUDGET_PROPERTIES_EXT) {
            auto* mb = reinterpret_cast<VkPhysicalDeviceMemoryBudgetPropertiesEXT*>(ext);
            mb->heapBudget[0] = pMemoryProperties->memoryProperties.memoryHeaps[0].size;
            mb->heapUsage[0] = 0;
            mb->heapBudget[1] = pMemoryProperties->memoryProperties.memoryHeaps[1].size;
            mb->heapUsage[1] = 0;
            for (uint32_t i = 2; i < VK_MAX_MEMORY_HEAPS; i++) {
                mb->heapBudget[i] = 0;
                mb->heapUsage[i] = 0;
            }
            SPDLOG_INFO("vkGetPhysicalDeviceMemoryProperties2: handled VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_BUDGET_PROPERTIES_EXT");
        }
        ext = ext->pNext;
    }
}

void VKAPI_PTR vkGetPhysicalDeviceQueueFamilyProperties_hook(
    VkPhysicalDevice physicalDevice,
    uint32_t* pQueueFamilyPropertyCount,
    VkQueueFamilyProperties* pQueueFamilyProperties)
{
    SPDLOG_TRACE("Intercepted: vkGetPhysicalDeviceQueueFamilyProperties");
    static constexpr uint32_t kQueueFamilyCount = 2;
    if (!pQueueFamilyProperties) {
        if (pQueueFamilyPropertyCount) *pQueueFamilyPropertyCount = kQueueFamilyCount;
        return;
    }
    uint32_t toCopy = std::min(*pQueueFamilyPropertyCount, kQueueFamilyCount);

    VkQueueFamilyProperties props[kQueueFamilyCount] = {};
    // QF 0: graphics + compute + transfer (general purpose)
    props[0].queueCount = 1;
    props[0].queueFlags = VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT | VK_QUEUE_TRANSFER_BIT;
    props[0].timestampValidBits = 64;
    props[0].minImageTransferGranularity = {1, 1, 1};
    // QF 1: compute + transfer dedicated (for async compute workloads)
    props[1].queueCount = 1;
    props[1].queueFlags = VK_QUEUE_COMPUTE_BIT | VK_QUEUE_TRANSFER_BIT;
    props[1].timestampValidBits = 64;
    props[1].minImageTransferGranularity = {1, 1, 1};

    std::memcpy(pQueueFamilyProperties, props, toCopy * sizeof(VkQueueFamilyProperties));
    *pQueueFamilyPropertyCount = toCopy;
}

void VKAPI_PTR vkGetPhysicalDeviceQueueFamilyProperties2_hook(
    VkPhysicalDevice physicalDevice,
    uint32_t* pQueueFamilyPropertyCount,
    VkQueueFamilyProperties2* pQueueFamilyProperties)
{
    SPDLOG_TRACE("Intercepted: vkGetPhysicalDeviceQueueFamilyProperties2");
    static constexpr uint32_t kQueueFamilyCount = 2;
    if (!pQueueFamilyProperties) {
        if (pQueueFamilyPropertyCount) *pQueueFamilyPropertyCount = kQueueFamilyCount;
        return;
    }
    uint32_t toCopy = std::min(*pQueueFamilyPropertyCount, kQueueFamilyCount);

    std::vector<VkQueueFamilyProperties2> props(kQueueFamilyCount);
    for (uint32_t i = 0; i < kQueueFamilyCount; i++) {
        props[i].sType = VK_STRUCTURE_TYPE_QUEUE_FAMILY_PROPERTIES_2;
    }
    // QF 0: graphics + compute + transfer
    props[0].queueFamilyProperties.queueCount = 1;
    props[0].queueFamilyProperties.queueFlags =
        VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT | VK_QUEUE_TRANSFER_BIT;
    props[0].queueFamilyProperties.timestampValidBits = 64;
    props[0].queueFamilyProperties.minImageTransferGranularity = {1, 1, 1};
    // QF 1: compute + transfer dedicated
    props[1].queueFamilyProperties.queueCount = 1;
    props[1].queueFamilyProperties.queueFlags =
        VK_QUEUE_COMPUTE_BIT | VK_QUEUE_TRANSFER_BIT;
    props[1].queueFamilyProperties.timestampValidBits = 64;
    props[1].queueFamilyProperties.minImageTransferGranularity = {1, 1, 1};

    std::memcpy(pQueueFamilyProperties, props.data(), toCopy * sizeof(VkQueueFamilyProperties2));
    *pQueueFamilyPropertyCount = toCopy;
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
    if (!pInfo) return 0;
    uint64_t buffer_handle = handle_to_u64(pInfo->buffer);
    SPDLOG_INFO("vkGetBufferDeviceAddress_hook entry: buffer={:#x}", buffer_handle);

    // Check cache first (only if buffer is bound — avoids stale tentative addresses)
    {
        std::lock_guard<std::mutex> lock(s_map_mutex);
        auto cache_it = s_buffer_addresses.find(buffer_handle);
        if (cache_it != s_buffer_addresses.end()) {
            SPDLOG_INFO("vkGetBufferDeviceAddress_hook exit (cached): address={:#x}", cache_it->second);
            return cache_it->second;
        }
        // Check if buffer has a pending binding — if so, flush batch to ensure
        // host processes vkBindBufferMemory before we query BDA
        auto bind_it = s_buffer_bindings.find(buffer_handle);
        if (bind_it != s_buffer_bindings.end()) {
            SPDLOG_INFO("vkGetBufferDeviceAddress_hook: buffer has pending bind, flushing batch");
        }
    }

    auto* batch = get_batch();
    if (batch) {
        batch->flush();
    }
    auto* cl = init::get_client();
    uint64_t addr = 0;
    if (cl && cl->socket() != INVALID_SOCKET) {
        addr = cl->sync_query(0x80, buffer_handle);
    } else {
        addr = buffer_handle;
    }
    SPDLOG_INFO("vkGetBufferDeviceAddress_hook exit: address={:#x}", addr);

    // Cache the address only if the buffer is bound (avoids caching stale tentative addrs)
    {
        std::lock_guard<std::mutex> lock(s_map_mutex);
        auto bind_it = s_buffer_bindings.find(buffer_handle);
        if (bind_it != s_buffer_bindings.end()) {
            s_buffer_addresses[buffer_handle] = addr;
        }
    }

    return addr;
}

uint64_t VKAPI_PTR vkGetBufferOpaqueCaptureAddress_hook(
    VkDevice device,
    const VkBufferDeviceAddressInfo* pInfo)
{
    if (!pInfo) return 0;
    SPDLOG_INFO("vkGetBufferOpaqueCaptureAddress_hook entry: buffer={:#x}", handle_to_u64(pInfo->buffer));
    auto* batch = get_batch();
    if (batch) {
        batch->flush();
    }
    auto* cl = init::get_client();
    uint64_t addr = 0;
    if (cl && cl->socket() != INVALID_SOCKET) {
        addr = cl->sync_query(0x81, handle_to_u64(pInfo->buffer));
    }
    SPDLOG_INFO("vkGetBufferOpaqueCaptureAddress_hook exit: address={:#x}", addr);
    return addr;
}

uint64_t VKAPI_PTR vkGetDeviceMemoryOpaqueCaptureAddress_hook(
    VkDevice device,
    const VkDeviceMemoryOpaqueCaptureAddressInfo* pInfo)
{
    if (!pInfo) return 0;
    SPDLOG_INFO("vkGetDeviceMemoryOpaqueCaptureAddress_hook entry: memory={:#x}", handle_to_u64(pInfo->memory));
    auto* batch = get_batch();
    if (batch) {
        batch->flush();
    }
    auto* cl = init::get_client();
    uint64_t addr = 0;
    if (cl && cl->socket() != INVALID_SOCKET) {
        addr = cl->sync_query(0x82, handle_to_u64(pInfo->memory));
    }
    SPDLOG_INFO("vkGetDeviceMemoryOpaqueCaptureAddress_hook exit: address={:#x}", addr);
    return addr;
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
    if (!pLayout) return;

    uint64_t image_key = handle_to_u64(image);
    {
        std::lock_guard<std::mutex> lock(s_map_mutex);
        s_pending_layouts[image_key] = { pLayout };
    }

    serializer::VulkanSerializer ser;
    ser.write_handle((uint64_t)device);
    ser.write_handle(image_key);
    ser.write_raw(pSubresource, sizeof(*pSubresource));

    auto* batch = get_batch();
    if (batch) {
        static std::atomic<uint32_t> req_id{0xB2000000};
        auto builder = protocol::build_command(
            fbs::FunctionId_vkGetImageSubresourceLayout,
            req_id.fetch_add(1),
            ser.data(), ser.size()
        );
        batch->append(builder);
        batch->flush();
    }

    auto* client = init::get_client();
    if (client) {
        client->sync_query(0x8c, image_key);
    }

    {
        std::lock_guard<std::mutex> lock(s_map_mutex);
        s_pending_layouts.erase(image_key);
    }
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
    if (!pData || dataSize == 0) return VK_SUCCESS;

    uint64_t pool_key = handle_to_u64(queryPool);
    {
        std::lock_guard<std::mutex> lock(s_map_mutex);
        s_pending_query_results[pool_key] = { pData, dataSize };
    }

    serializer::VulkanSerializer ser;
    ser.write_handle((uint64_t)device);
    ser.write_handle(pool_key);
    ser.write_u32(firstQuery);
    ser.write_u32(queryCount);
    ser.write_u64(static_cast<uint64_t>(dataSize));
    ser.write_u64(static_cast<uint64_t>(stride));
    ser.write_u32(static_cast<uint32_t>(flags));

    auto* batch = get_batch();
    if (batch) {
        static std::atomic<uint32_t> req_id{0xB1000000};
        auto builder = protocol::build_command(
            fbs::FunctionId_vkGetQueryPoolResults,
            req_id.fetch_add(1),
            ser.data(), ser.size()
        );
        batch->append(builder);
        batch->flush();
    }

    auto* client = init::get_client();
    if (client) {
        client->sync_query(0x8b, pool_key);
    }

    {
        std::lock_guard<std::mutex> lock(s_map_mutex);
        s_pending_query_results.erase(pool_key);
    }

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
    auto* batch = get_batch();
    if (batch) {
        batch->flush();
    }
    auto* client = init::get_client();
    if (client) {
        uint64_t res = client->sync_query(0x87, handle_to_u64(fence));
        return static_cast<VkResult>(res);
    }
    return VK_SUCCESS;
}

VkResult VKAPI_PTR vkGetEventStatus_hook(VkDevice device, VkEvent event) {
    SPDLOG_TRACE("Intercepted: vkGetEventStatus");
    auto* batch = get_batch();
    if (batch) batch->flush();
    auto* client = init::get_client();
    if (client) {
        uint64_t res = client->sync_query(0x8e, handle_to_u64(event));
        return static_cast<VkResult>(res);
    }
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
// vkAllocateCommandBuffers / vkFreeCommandBuffers (dispatchable - heap alloc)
// ---------------------------------------------------------------------------
struct alignas(void*) FakeCommandBuffer {
    void* loader_storage[8];
};

VkResult VKAPI_PTR vkAllocateCommandBuffers_hook(
    VkDevice device,
    const VkCommandBufferAllocateInfo* pAllocateInfo,
    VkCommandBuffer* pCommandBuffers)
{
    SPDLOG_TRACE("Intercepted: vkAllocateCommandBuffers count={}",
                 pAllocateInfo ? pAllocateInfo->commandBufferCount : 1);
    uint32_t count = pAllocateInfo ? pAllocateInfo->commandBufferCount : 1;
    for (uint32_t i = 0; i < count; i++) {
        auto* cb = new FakeCommandBuffer{};
        pCommandBuffers[i] = reinterpret_cast<VkCommandBuffer>(cb);
    }

    // Serialize and forward to host for real allocation
    serializer::VulkanSerializer ser;
    ser.write_handle((uint64_t)(device));
    ser.write_raw(pAllocateInfo, sizeof(*pAllocateInfo));
    ser.write_u32(count);
    for (uint32_t i = 0; i < count; i++)
        ser.write_handle((uint64_t)(pCommandBuffers[i]));

    auto* batch = intercept::get_batch();
    if (batch) {
        static std::atomic<uint32_t> req_id{0xA0000000};
        auto builder = protocol::build_command(
            fbs::FunctionId_vkAllocateCommandBuffers,
            req_id.fetch_add(1),
            ser.data(), ser.size()
        );
        batch->append(builder);
    }

    return VK_SUCCESS;
}

void VKAPI_PTR vkFreeCommandBuffers_hook(
    VkDevice device, VkCommandPool commandPool,
    uint32_t commandBufferCount, const VkCommandBuffer* pCommandBuffers)
{
    SPDLOG_TRACE("Intercepted: vkFreeCommandBuffers");
    for (uint32_t i = 0; i < commandBufferCount; i++) {
        delete reinterpret_cast<FakeCommandBuffer*>(pCommandBuffers[i]);
    }

    serializer::VulkanSerializer ser;
    ser.write_handle((uint64_t)(device));
    ser.write_handle((uint64_t)(commandPool));
    ser.write_u32(commandBufferCount);
    for (uint32_t i = 0; i < commandBufferCount; i++)
        ser.write_handle((uint64_t)(pCommandBuffers[i]));

    auto* batch = intercept::get_batch();
    if (batch) {
        static std::atomic<uint32_t> req_id{0xA1000000};
        auto builder = protocol::build_command(
            fbs::FunctionId_vkFreeCommandBuffers,
            req_id.fetch_add(1),
            ser.data(), ser.size()
        );
        batch->append(builder);
    }
}

VkResult VKAPI_PTR vkBeginCommandBuffer_hook(
    VkCommandBuffer commandBuffer, const VkCommandBufferBeginInfo* pBeginInfo)
{
    SPDLOG_TRACE("Intercepted: vkBeginCommandBuffer");

    serializer::VulkanSerializer ser;
    ser.write_handle((uint64_t)(commandBuffer));
    
    // Manual serialization of VkCommandBufferBeginInfo
    ser.write_u32(pBeginInfo->sType);
    // Serialize pNext chain (only VkDeviceGroupCommandBufferBeginInfo supported)
    if (pBeginInfo->pNext) {
        auto* pNext = pBeginInfo->pNext;
        VkStructureType st = *reinterpret_cast<const VkStructureType*>(pNext);
        if (st == VK_STRUCTURE_TYPE_DEVICE_GROUP_COMMAND_BUFFER_BEGIN_INFO) {
            ser.write_u32(static_cast<uint32_t>(st));
            auto* dg = static_cast<const VkDeviceGroupCommandBufferBeginInfo*>(pNext);
            ser.write_u32(dg->deviceMask);
        } else {
            ser.write_u32(0);
        }
    } else {
        ser.write_u32(0);
    }
    ser.write_u32(pBeginInfo->flags);
    ser.write_bool(pBeginInfo->pInheritanceInfo != nullptr);
    if (pBeginInfo->pInheritanceInfo) {
        const auto* pInherit = pBeginInfo->pInheritanceInfo;
        ser.write_u32(pInherit->sType);
        // InheritanceInfo pNext — write type + size for skipping
        ser.write_u32(pInherit->pNext
            ? static_cast<uint32_t>(*reinterpret_cast<const VkStructureType*>(pInherit->pNext))
            : 0U);
        ser.write_handle(handle_to_u64(pInherit->renderPass));
        ser.write_u32(pInherit->subpass);
        ser.write_handle(handle_to_u64(pInherit->framebuffer));
        ser.write_bool(pInherit->occlusionQueryEnable);
        ser.write_u32(pInherit->queryFlags);
        ser.write_u32(pInherit->pipelineStatistics);
    }

    auto* batch = intercept::get_batch();
    if (batch) {
        static std::atomic<uint32_t> req_id{0xA2000000};
        auto builder = protocol::build_command(
            fbs::FunctionId_vkBeginCommandBuffer,
            req_id.fetch_add(1),
            ser.data(), ser.size()
        );
        batch->append(builder);
    }

    return VK_SUCCESS;
}

VkResult VKAPI_PTR vkEndCommandBuffer_hook(VkCommandBuffer commandBuffer)
{
    SPDLOG_TRACE("Intercepted: vkEndCommandBuffer");

    serializer::VulkanSerializer ser;
    ser.write_handle((uint64_t)(commandBuffer));

    auto* batch = intercept::get_batch();
    if (batch) {
        static std::atomic<uint32_t> req_id{0xA3000000};
        auto builder = protocol::build_command(
            fbs::FunctionId_vkEndCommandBuffer,
            req_id.fetch_add(1),
            ser.data(), ser.size()
        );
        batch->append(builder);
    }

    return VK_SUCCESS;
}

VkResult VKAPI_PTR vkResetCommandBuffer_hook(
    VkCommandBuffer commandBuffer, VkCommandBufferResetFlags flags)
{
    SPDLOG_TRACE("Intercepted: vkResetCommandBuffer");

    serializer::VulkanSerializer ser;
    ser.write_handle((uint64_t)(commandBuffer));
    ser.write_u32(static_cast<uint32_t>(flags));

    auto* batch = intercept::get_batch();
    if (batch) {
        static std::atomic<uint32_t> req_id{0xA4000000};
        auto builder = protocol::build_command(
            fbs::FunctionId_vkResetCommandBuffer,
            req_id.fetch_add(1),
            ser.data(), ser.size()
        );
        batch->append(builder);
    }

    return VK_SUCCESS;
}

// ---------------------------------------------------------------------------
// WSI Surface and Swapchain hooks (needed by vkcube)
// ---------------------------------------------------------------------------
static std::mutex g_swapchain_mutex;
static std::unordered_map<uint64_t, std::vector<VkImage>> g_swapchain_images;
static std::atomic<uint32_t> s_current_image{0};

static uint64_t next_fake_handle_id() {
    static std::atomic<uint64_t> counter{0x20000000};
    return counter.fetch_add(1, std::memory_order_relaxed);
}

VkResult VKAPI_PTR vkCreateWin32SurfaceKHR_hook(
    VkInstance instance,
    const VkWin32SurfaceCreateInfoKHR* pCreateInfo,
    const VkAllocationCallbacks* pAllocator,
    VkSurfaceKHR* pSurface)
{
    SPDLOG_TRACE("Intercepted: vkCreateWin32SurfaceKHR");
    if (pSurface) *pSurface = handle_from_u64<VkSurfaceKHR>(next_fake_handle_id());
    return VK_SUCCESS;
}

void VKAPI_PTR vkDestroySurfaceKHR_hook(
    VkInstance instance, VkSurfaceKHR surface, const VkAllocationCallbacks* pAllocator)
{
    SPDLOG_TRACE("Intercepted: vkDestroySurfaceKHR");
}

VkResult VKAPI_PTR vkCreateSwapchainKHR_hook(
    VkDevice device,
    const VkSwapchainCreateInfoKHR* pCreateInfo,
    const VkAllocationCallbacks* pAllocator,
    VkSwapchainKHR* pSwapchain)
{
    SPDLOG_TRACE("Intercepted: vkCreateSwapchainKHR {}x{}",
                 pCreateInfo ? pCreateInfo->imageExtent.width  : 0,
                 pCreateInfo ? pCreateInfo->imageExtent.height : 0);
    uint64_t sc_id = next_fake_handle_id();
    VkSwapchainKHR sc = handle_from_u64<VkSwapchainKHR>(sc_id);
    if (pSwapchain) *pSwapchain = sc;
    uint32_t img_count = pCreateInfo ? std::max(pCreateInfo->minImageCount, 2u) : 2;
    std::vector<VkImage> imgs;
    imgs.reserve(img_count);
    for (uint32_t i = 0; i < img_count; i++) {
        imgs.push_back(handle_from_u64<VkImage>(next_fake_handle_id()));
    }
    std::lock_guard<std::mutex> lock(g_swapchain_mutex);
    g_swapchain_images[sc_id] = std::move(imgs);
    return VK_SUCCESS;
}

VkResult VKAPI_PTR vkGetSwapchainImagesKHR_hook(
    VkDevice device,
    VkSwapchainKHR swapchain,
    uint32_t* pSwapchainImageCount,
    VkImage* pSwapchainImages)
{
    SPDLOG_TRACE("Intercepted: vkGetSwapchainImagesKHR");
    if (!pSwapchainImageCount) return VK_ERROR_INITIALIZATION_FAILED;
    uint64_t sc_id = handle_to_u64(swapchain);
    std::lock_guard<std::mutex> lock(g_swapchain_mutex);
    auto it = g_swapchain_images.find(sc_id);
    uint32_t count = (it != g_swapchain_images.end()) ? static_cast<uint32_t>(it->second.size()) : 2;
    if (!pSwapchainImages) {
        *pSwapchainImageCount = count;
        return VK_SUCCESS;
    }
    uint32_t to_copy = std::min(*pSwapchainImageCount, count);
    if (it != g_swapchain_images.end()) {
        for (uint32_t i = 0; i < to_copy; i++) pSwapchainImages[i] = it->second[i];
    }
    *pSwapchainImageCount = to_copy;
    return (to_copy < count) ? VK_INCOMPLETE : VK_SUCCESS;
}

void VKAPI_PTR vkDestroySwapchainKHR_hook(
    VkDevice device, VkSwapchainKHR swapchain, const VkAllocationCallbacks* pAllocator)
{
    SPDLOG_TRACE("Intercepted: vkDestroySwapchainKHR");
    uint64_t sc_id = handle_to_u64(swapchain);
    std::lock_guard<std::mutex> lock(g_swapchain_mutex);
    g_swapchain_images.erase(sc_id);
}

VkResult VKAPI_PTR vkAcquireNextImageKHR_hook(
    VkDevice device, VkSwapchainKHR swapchain, uint64_t timeout,
    VkSemaphore semaphore, VkFence fence, uint32_t* pImageIndex)
{
    SPDLOG_TRACE("Intercepted: vkAcquireNextImageKHR");
    if (!pImageIndex) return VK_ERROR_INITIALIZATION_FAILED;
    uint64_t sc_id = handle_to_u64(swapchain);
    uint32_t count = 2;
    {
        std::lock_guard<std::mutex> lock(g_swapchain_mutex);
        auto it = g_swapchain_images.find(sc_id);
        if (it != g_swapchain_images.end()) count = static_cast<uint32_t>(it->second.size());
    }
    *pImageIndex = s_current_image.fetch_add(1, std::memory_order_relaxed) % count;
    return VK_SUCCESS;
}

VkResult VKAPI_PTR vkQueuePresentKHR_hook(
    VkQueue queue, const VkPresentInfoKHR* pPresentInfo)
{
    SPDLOG_TRACE("Intercepted: vkQueuePresentKHR");
    auto* batch = intercept::get_batch();
    if (batch) {
        batch->on_present();
        batch->force_flush();
    }
    return VK_SUCCESS;
}

VkResult VKAPI_PTR vkAcquireNextImage2KHR_hook(
    VkDevice device, const VkAcquireNextImageInfoKHR* pAcquireInfo, uint32_t* pImageIndex)
{
    SPDLOG_TRACE("Intercepted: vkAcquireNextImage2KHR");
    if (!pAcquireInfo || !pImageIndex) return VK_ERROR_INITIALIZATION_FAILED;
    return vkAcquireNextImageKHR_hook(device, pAcquireInfo->swapchain,
                                       pAcquireInfo->timeout,
                                       pAcquireInfo->semaphore,
                                       pAcquireInfo->fence, pImageIndex);
}

VkResult VKAPI_PTR vkGetDeviceGroupPresentCapabilitiesKHR_hook(
    VkDevice device,
    VkDeviceGroupPresentCapabilitiesKHR* pDeviceGroupPresentCapabilities)
{
    SPDLOG_TRACE("Intercepted: vkGetDeviceGroupPresentCapabilitiesKHR");
    if (!pDeviceGroupPresentCapabilities) return VK_ERROR_INITIALIZATION_FAILED;
    std::memset(pDeviceGroupPresentCapabilities, 0, sizeof(*pDeviceGroupPresentCapabilities));
    pDeviceGroupPresentCapabilities->sType = VK_STRUCTURE_TYPE_DEVICE_GROUP_PRESENT_CAPABILITIES_KHR;
    pDeviceGroupPresentCapabilities->presentMask[0] = 1;
    pDeviceGroupPresentCapabilities->modes = VK_DEVICE_GROUP_PRESENT_MODE_LOCAL_BIT_KHR;
    return VK_SUCCESS;
}

// ============ MEMORY REQUIREMENT QUERIES ============
void VKAPI_PTR vkGetBufferMemoryRequirements_hook(
    VkDevice device, VkBuffer buffer, VkMemoryRequirements* pMemoryRequirements)
{
    SPDLOG_TRACE("Intercepted: vkGetBufferMemoryRequirements");
    if (pMemoryRequirements) {
        // Flush batch first so host has created the buffer
        auto* batch = get_batch();
        if (batch) {
            batch->flush();
        }
        // Sync from host for full memory requirements
        auto* cl = init::get_client();
        if (cl && cl->socket() != INVALID_SOCKET) {
            VkMemoryRequirements mr{};
            if (cl->sync_query_buf(0x83, handle_to_u64(buffer),
                                   reinterpret_cast<uint8_t*>(&mr), sizeof(mr)) &&
                mr.size != 0) {
                if (mr.alignment == 0) {
                    SPDLOG_ERROR("vkGetBufferMemoryRequirements: host returned alignment=0 for buffer={:#x}, size={}, using fallback 256",
                                 handle_to_u64(buffer), mr.size);
                    mr.alignment = 256;
                }
                pMemoryRequirements->size = mr.size;
                pMemoryRequirements->alignment = mr.alignment;
                pMemoryRequirements->memoryTypeBits = 0x1F; // guest's own type bits
                SPDLOG_INFO("vkGetBufferMemoryRequirements: buffer={:#x}, size={}, alignment={}",
                            handle_to_u64(buffer), mr.size, mr.alignment);
                return;
            }
        }
        // Fallback: assume buffer needs at least 64KB
        pMemoryRequirements->size = 65536;
        pMemoryRequirements->alignment = 256;
        pMemoryRequirements->memoryTypeBits = 0x1F; // bits 0-4
    }
}

void VKAPI_PTR vkGetBufferMemoryRequirements2_hook(
    VkDevice device, const VkBufferMemoryRequirementsInfo2* pInfo,
    VkMemoryRequirements2* pMemoryRequirements)
{
    SPDLOG_TRACE("Intercepted: vkGetBufferMemoryRequirements2");
    if (pMemoryRequirements && pInfo) {
        vkGetBufferMemoryRequirements_hook(device, pInfo->buffer, &pMemoryRequirements->memoryRequirements);
    }
}

void VKAPI_PTR vkGetImageMemoryRequirements_hook(
    VkDevice device, VkImage image, VkMemoryRequirements* pMemoryRequirements)
{
    SPDLOG_TRACE("Intercepted: vkGetImageMemoryRequirements");
    if (pMemoryRequirements) {
        // Flush batch first so host has created the image
        auto* batch = get_batch();
        if (batch) {
            batch->flush();
        }
        auto* cl = init::get_client();
        if (cl && cl->socket() != INVALID_SOCKET) {
            uint64_t result = cl->sync_query(0x84, handle_to_u64(image));
            if (result != 0) {
                pMemoryRequirements->size = result;
                pMemoryRequirements->alignment = 65536;
                pMemoryRequirements->memoryTypeBits = 0x1F;
                return;
            }
        }
        // Fallback
        pMemoryRequirements->size = 1024 * 1024;
        pMemoryRequirements->alignment = 65536;
        pMemoryRequirements->memoryTypeBits = 0x1F;
    }
}

void VKAPI_PTR vkGetImageMemoryRequirements2_hook(
    VkDevice device, const VkImageMemoryRequirementsInfo2* pInfo,
    VkMemoryRequirements2* pMemoryRequirements)
{
    SPDLOG_TRACE("Intercepted: vkGetImageMemoryRequirements2");
    if (pMemoryRequirements && pInfo) {
        vkGetImageMemoryRequirements_hook(device, pInfo->image, &pMemoryRequirements->memoryRequirements);
    }
}

void VKAPI_PTR vkGetImageSparseMemoryRequirements_hook(
    VkDevice device, VkImage image, uint32_t* pSparseMemoryRequirementCount,
    VkSparseImageMemoryRequirements* pSparseMemoryRequirements)
{
    SPDLOG_TRACE("Intercepted: vkGetImageSparseMemoryRequirements");
    if (pSparseMemoryRequirementCount) *pSparseMemoryRequirementCount = 0;
}

void VKAPI_PTR vkGetImageSparseMemoryRequirements2_hook(
    VkDevice device, const VkImageSparseMemoryRequirementsInfo2* pInfo,
    uint32_t* pSparseMemoryRequirementCount,
    VkSparseImageMemoryRequirements2* pSparseMemoryRequirements)
{
    SPDLOG_TRACE("Intercepted: vkGetImageSparseMemoryRequirements2");
    if (pSparseMemoryRequirementCount) *pSparseMemoryRequirementCount = 0;
}

void VKAPI_PTR vkGetDeviceBufferMemoryRequirements_hook(
    VkDevice device, const VkDeviceBufferMemoryRequirements* pInfo,
    VkMemoryRequirements2* pMemoryRequirements)
{
    SPDLOG_TRACE("Intercepted: vkGetDeviceBufferMemoryRequirements");
    if (pMemoryRequirements && pInfo && pInfo->pCreateInfo) {
        pMemoryRequirements->sType = VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2;

        // Create a temporary buffer so the host can report real requirements
        VkBuffer tempBuf = VK_NULL_HANDLE;
        VkResult cres = vkCreateBuffer_hook(device, pInfo->pCreateInfo, nullptr, &tempBuf);
        if (cres != VK_SUCCESS || tempBuf == VK_NULL_HANDLE) {
            // Fallback: use create info values directly
            pMemoryRequirements->memoryRequirements.size = pInfo->pCreateInfo->size;
            pMemoryRequirements->memoryRequirements.alignment = 256;
            pMemoryRequirements->memoryRequirements.memoryTypeBits = 0x1F;
            return;
        }

        // Flush so host creates the buffer
        auto* batch = get_batch();
        if (batch) batch->flush();

        // Query host for full memory requirements
        auto* cl = init::get_client();
        bool queried = false;
        if (cl && cl->socket() != INVALID_SOCKET) {
            VkMemoryRequirements mr{};
            if (cl->sync_query_buf(0x83, handle_to_u64(tempBuf),
                                   reinterpret_cast<uint8_t*>(&mr), sizeof(mr)) &&
                mr.size != 0) {
                pMemoryRequirements->memoryRequirements.size = mr.size;
                pMemoryRequirements->memoryRequirements.alignment = mr.alignment;
                pMemoryRequirements->memoryRequirements.memoryTypeBits = 0x1F;
                queried = true;
            }
        }

        if (!queried) {
            pMemoryRequirements->memoryRequirements.size = pInfo->pCreateInfo->size;
            pMemoryRequirements->memoryRequirements.alignment = 256;
            pMemoryRequirements->memoryRequirements.memoryTypeBits = 0x1F;
        }

        // Destroy the temporary buffer
        vkDestroyBuffer_hook(device, tempBuf, nullptr);
        // Flush so host destroys the temp buffer
        if (batch) batch->flush();
    }
}

void VKAPI_PTR vkGetDeviceImageMemoryRequirements_hook(
    VkDevice device, const VkDeviceImageMemoryRequirements* pInfo,
    VkMemoryRequirements2* pMemoryRequirements)
{
    SPDLOG_TRACE("Intercepted: vkGetDeviceImageMemoryRequirements");
    if (pMemoryRequirements && pInfo && pInfo->pCreateInfo) {
        pMemoryRequirements->sType = VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2;
        pMemoryRequirements->memoryRequirements.size = 262144;
        pMemoryRequirements->memoryRequirements.alignment = 65536;
        pMemoryRequirements->memoryRequirements.memoryTypeBits = 0xFFFFFFFF;
    }
}

// ---------------------------------------------------------------------------
// Surface query hooks (needed by vkcube)
// ---------------------------------------------------------------------------
VkResult VKAPI_PTR vkGetPhysicalDeviceSurfaceCapabilitiesKHR_hook(
    VkPhysicalDevice physicalDevice, VkSurfaceKHR surface,
    VkSurfaceCapabilitiesKHR* pSurfaceCapabilities)
{
    SPDLOG_TRACE("Intercepted: vkGetPhysicalDeviceSurfaceCapabilitiesKHR");
    if (!pSurfaceCapabilities) return VK_ERROR_INITIALIZATION_FAILED;
    auto& caps = caps::get();
    uint32_t w = caps.valid() ? caps.max_framebuffer_width  : 3840;
    uint32_t h = caps.valid() ? caps.max_framebuffer_height : 2160;
    std::memset(pSurfaceCapabilities, 0, sizeof(*pSurfaceCapabilities));
    pSurfaceCapabilities->minImageCount = 2;
    pSurfaceCapabilities->maxImageCount = 8;
    pSurfaceCapabilities->currentExtent  = {w, h};
    pSurfaceCapabilities->minImageExtent = {1, 1};
    pSurfaceCapabilities->maxImageExtent = {w, h};
    pSurfaceCapabilities->maxImageArrayLayers = 1;
    pSurfaceCapabilities->supportedTransforms = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
    pSurfaceCapabilities->currentTransform    = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
    pSurfaceCapabilities->supportedCompositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    pSurfaceCapabilities->supportedUsageFlags = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
        VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    return VK_SUCCESS;
}

VkResult VKAPI_PTR vkGetPhysicalDeviceSurfaceFormatsKHR_hook(
    VkPhysicalDevice physicalDevice, VkSurfaceKHR surface,
    uint32_t* pSurfaceFormatCount, VkSurfaceFormatKHR* pSurfaceFormats)
{
    SPDLOG_TRACE("Intercepted: vkGetPhysicalDeviceSurfaceFormatsKHR");
    if (!pSurfaceFormatCount) return VK_ERROR_INITIALIZATION_FAILED;
    static const VkSurfaceFormatKHR formats[] = {
        {VK_FORMAT_B8G8R8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR},
        {VK_FORMAT_R8G8B8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR},
    };
    uint32_t count = static_cast<uint32_t>(sizeof(formats) / sizeof(formats[0]));
    if (!pSurfaceFormats) {
        *pSurfaceFormatCount = count;
        return VK_SUCCESS;
    }
    uint32_t to_copy = std::min(*pSurfaceFormatCount, count);
    std::memcpy(pSurfaceFormats, formats, to_copy * sizeof(VkSurfaceFormatKHR));
    *pSurfaceFormatCount = to_copy;
    return (to_copy < count) ? VK_INCOMPLETE : VK_SUCCESS;
}

VkResult VKAPI_PTR vkGetPhysicalDeviceSurfacePresentModesKHR_hook(
    VkPhysicalDevice physicalDevice, VkSurfaceKHR surface,
    uint32_t* pPresentModeCount, VkPresentModeKHR* pPresentModes)
{
    SPDLOG_TRACE("Intercepted: vkGetPhysicalDeviceSurfacePresentModesKHR");
    if (!pPresentModeCount) return VK_ERROR_INITIALIZATION_FAILED;
    static const VkPresentModeKHR modes[] = {
        VK_PRESENT_MODE_FIFO_KHR, VK_PRESENT_MODE_MAILBOX_KHR, VK_PRESENT_MODE_IMMEDIATE_KHR,
    };
    uint32_t count = static_cast<uint32_t>(sizeof(modes) / sizeof(modes[0]));
    if (!pPresentModes) {
        *pPresentModeCount = count;
        return VK_SUCCESS;
    }
    uint32_t to_copy = std::min(*pPresentModeCount, count);
    std::memcpy(pPresentModes, modes, to_copy * sizeof(VkPresentModeKHR));
    *pPresentModeCount = to_copy;
    return (to_copy < count) ? VK_INCOMPLETE : VK_SUCCESS;
}

VkResult VKAPI_PTR vkGetPhysicalDeviceSurfaceSupportKHR_hook(
    VkPhysicalDevice physicalDevice, uint32_t queueFamilyIndex,
    VkSurfaceKHR surface, VkBool32* pSupported)
{
    SPDLOG_TRACE("Intercepted: vkGetPhysicalDeviceSurfaceSupportKHR");
    if (pSupported) *pSupported = VK_TRUE;
    return VK_SUCCESS;
}

VkResult VKAPI_PTR vkGetPhysicalDeviceSurfaceCapabilities2KHR_hook(
    VkPhysicalDevice physicalDevice,
    const VkPhysicalDeviceSurfaceInfo2KHR* pSurfaceInfo,
    VkSurfaceCapabilities2KHR* pSurfaceCapabilities)
{
    SPDLOG_TRACE("Intercepted: vkGetPhysicalDeviceSurfaceCapabilities2KHR");
    if (!pSurfaceCapabilities) return VK_ERROR_INITIALIZATION_FAILED;
    pSurfaceCapabilities->sType = VK_STRUCTURE_TYPE_SURFACE_CAPABILITIES_2_KHR;
    VkSurfaceKHR surf = pSurfaceInfo ? pSurfaceInfo->surface : VK_NULL_HANDLE;
    return vkGetPhysicalDeviceSurfaceCapabilitiesKHR_hook(
        physicalDevice, surf, &pSurfaceCapabilities->surfaceCapabilities);
}

VkResult VKAPI_PTR vkGetPhysicalDeviceSurfaceFormats2KHR_hook(
    VkPhysicalDevice physicalDevice,
    const VkPhysicalDeviceSurfaceInfo2KHR* pSurfaceInfo,
    uint32_t* pSurfaceFormatCount,
    VkSurfaceFormat2KHR* pSurfaceFormats)
{
    SPDLOG_TRACE("Intercepted: vkGetPhysicalDeviceSurfaceFormats2KHR");
    if (!pSurfaceFormatCount) return VK_ERROR_INITIALIZATION_FAILED;
    static const VkSurfaceFormatKHR fmts[] = {
        {VK_FORMAT_B8G8R8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR},
        {VK_FORMAT_R8G8B8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR},
    };
    uint32_t count = static_cast<uint32_t>(sizeof(fmts) / sizeof(fmts[0]));
    if (!pSurfaceFormats) {
        *pSurfaceFormatCount = count;
        return VK_SUCCESS;
    }
    uint32_t to_copy = std::min(*pSurfaceFormatCount, count);
    for (uint32_t i = 0; i < to_copy; i++) {
        pSurfaceFormats[i].sType = VK_STRUCTURE_TYPE_SURFACE_FORMAT_2_KHR;
        pSurfaceFormats[i].pNext = nullptr;
        pSurfaceFormats[i].surfaceFormat = fmts[i];
    }
    *pSurfaceFormatCount = to_copy;
    return (to_copy < count) ? VK_INCOMPLETE : VK_SUCCESS;
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
    pPhysicalDeviceGroupProperties->physicalDevices[0] = get_or_create_phys_device(instance);
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

// ---------------------------------------------------------------------------
// vkMapMemory / vkUnmapMemory — allocate host memory for guest writes
// ---------------------------------------------------------------------------
void write_query_results(uint64_t pool_key, const uint8_t* data, size_t size) {
    std::lock_guard<std::mutex> lock(s_map_mutex);
    auto it = s_pending_query_results.find(pool_key);
    if (it != s_pending_query_results.end()) {
        size_t to_copy = std::min(size, it->second.size);
        if (it->second.ptr && to_copy > 0) {
            std::memcpy(it->second.ptr, data, to_copy);
        }
    }
}

void write_layout_result(uint64_t image_key, const uint8_t* data, size_t size) {
    std::lock_guard<std::mutex> lock(s_map_mutex);
    auto it = s_pending_layouts.find(image_key);
    if (it != s_pending_layouts.end()) {
        if (it->second.ptr && size >= sizeof(VkSubresourceLayout)) {
            std::memcpy(it->second.ptr, data, sizeof(VkSubresourceLayout));
        }
    }
}

// Called by vkAllocateMemory hook to track allocation sizes
static void track_memory_allocation(VkDevice device, VkDeviceMemory memory, VkDeviceSize allocSize, uint32_t memoryTypeIndex) {
    uint64_t mem_key = handle_to_u64(memory);
    std::lock_guard<std::mutex> lock(s_map_mutex);
    s_memory_sizes[mem_key] = allocSize;
    s_memory_devices[mem_key] = device;
    s_memory_dirty[mem_key] = false;
    s_memory_coherent[mem_key] = (memoryTypeIndex == 1 || memoryTypeIndex == 2);
}
static void untrack_memory_allocation(VkDeviceMemory memory) {
    uint64_t mem_key = handle_to_u64(memory);
    std::lock_guard<std::mutex> lock(s_map_mutex);
    s_memory_sizes.erase(mem_key);
    s_memory_devices.erase(mem_key);
    s_memory_dirty.erase(mem_key);
    s_pending_flushes.erase(mem_key);
    s_memory_coherent.erase(mem_key);
    // Free retained shadow if any
    auto mapped_it = s_mapped_ptrs.find(mem_key);
    if (mapped_it != s_mapped_ptrs.end()) {
        omni_aligned_free(mapped_it->second);
        s_mapped_ptrs.erase(mapped_it);
        s_memory_map_offsets.erase(mem_key);
    }
    auto unmapped_it = s_unmapped_ptrs.find(mem_key);
    if (unmapped_it != s_unmapped_ptrs.end()) {
        omni_aligned_free(unmapped_it->second);
        s_unmapped_ptrs.erase(unmapped_it);
    }
}
static VkDeviceSize get_memory_size(VkDeviceMemory memory) {
    uint64_t mem_key = handle_to_u64(memory);
    std::lock_guard<std::mutex> lock(s_map_mutex);
    auto it = s_memory_sizes.find(mem_key);
    return (it != s_memory_sizes.end()) ? it->second : 0;
}

VkResult VKAPI_PTR vkAllocateMemory_hook(
    VkDevice device,
    const VkMemoryAllocateInfo* pAllocateInfo,
    const VkAllocationCallbacks* pAllocator,
    VkDeviceMemory* pMemory)
{
    uint64_t alloc_size = pAllocateInfo ? pAllocateInfo->allocationSize : 0;
    uint32_t alloc_type = pAllocateInfo ? pAllocateInfo->memoryTypeIndex : 99;
    SPDLOG_INFO("vkAllocateMemory_hook entry: size={} type={}", alloc_size, alloc_type);
    FILE* diag = fopen("C:\\Users\\test\\AppData\\Local\\Temp\\omnigpu_diag.txt", "a");
    if (diag) { fprintf(diag, "Alloc: size=%llu type=%u\n", (unsigned long long)alloc_size, (unsigned)alloc_type); fclose(diag); }

    // Create fake handle and track allocation size
    VkDeviceMemory fake_mem{};
    if (pMemory) {
        fake_mem = handle_from_u64<VkDeviceMemory>(next_fake_handle());
        *pMemory = fake_mem;
        if (pAllocateInfo) {
            track_memory_allocation(device, fake_mem, pAllocateInfo->allocationSize, pAllocateInfo->memoryTypeIndex);
        }
    }

    // Serialize and forward to host for real allocation
    serializer::VulkanSerializer ser;
    ser.write_handle((uint64_t)(device));
    serializer::write_VkMemoryAllocateInfo(ser, pAllocateInfo);
    // Write pAllocator as: bool (has_allocator) + struct_data (if has_allocator)
    ser.write_bool(pAllocator != nullptr ? VK_TRUE : VK_FALSE);
    if (pAllocator) {
        ser.write_raw(pAllocator, sizeof(VkAllocationCallbacks));
    }
    ser.write_handle((uint64_t)(fake_mem));

    auto* batch = intercept::get_batch();
    if (batch) {
        static std::atomic<uint32_t> req_id{1};
        auto builder = protocol::build_command(
            fbs::FunctionId_vkAllocateMemory,
            req_id.fetch_add(1),
            ser.data(),
            ser.size()
        );
        batch->append(builder);
    }

    SPDLOG_INFO("vkAllocateMemory_hook exit: handle={:#x}", handle_to_u64(fake_mem));
    return VK_SUCCESS;
}

void VKAPI_PTR vkFreeMemory_hook(
    VkDevice device, VkDeviceMemory memory,
    const VkAllocationCallbacks* pAllocator)
{
    SPDLOG_TRACE("Intercepted: vkFreeMemory memory={}", (void*)memory);
    untrack_memory_allocation(memory);
    {
        std::lock_guard<std::mutex> lock(s_map_mutex);
        auto it = s_mapped_ptrs.find(handle_to_u64(memory));
        if (it != s_mapped_ptrs.end()) {
            omni_aligned_free(it->second);
            s_mapped_ptrs.erase(it);
        }
    }

    serializer::VulkanSerializer ser;
    ser.write_handle((uint64_t)(device));
    ser.write_handle((uint64_t)(memory));
    ser.write_bool(pAllocator != nullptr ? VK_TRUE : VK_FALSE);
    if (pAllocator) {
        ser.write_raw(pAllocator, sizeof(VkAllocationCallbacks));
    }

    auto* batch = intercept::get_batch();
    if (batch) {
        static std::atomic<uint32_t> req_id{1};
        auto builder = protocol::build_command(
            fbs::FunctionId_vkFreeMemory,
            req_id.fetch_add(1),
            ser.data(),
            ser.size()
        );
        batch->append(builder);
    }
}

static VkDeviceSize resolve_map_size(VkDeviceMemory memory, VkDeviceSize size, VkDeviceSize offset) {
    if (size == VK_WHOLE_SIZE) {
        VkDeviceSize total = get_memory_size(memory);
        return (total > offset) ? (total - offset) : 0;
    }
    return size;
}

VkResult VKAPI_PTR vkMapMemory_hook(
    VkDevice device, VkDeviceMemory memory,
    VkDeviceSize offset, VkDeviceSize size,
    VkMemoryMapFlags flags, void** ppData)
{
    VkDeviceSize total_size = get_memory_size(memory);
    SPDLOG_TRACE("vkMapMemory_hook entry: device={} memory={} size={} total_size={}",
                (void*)device, (void*)memory, size, total_size);
    if (total_size == 0) {
        SPDLOG_ERROR("vkMapMemory: total_size is 0 (untracked memory allocation)");
        return VK_ERROR_MEMORY_MAP_FAILED;
    }
    if (!ppData) return VK_ERROR_INITIALIZATION_FAILED;

    uint64_t mem_key = handle_to_u64(memory);
    void* ptr = nullptr;
    bool is_remap = false;
    bool need_invalidate = false;
    {
        std::lock_guard<std::mutex> lock(s_map_mutex);
        auto reuse_it = s_unmapped_ptrs.find(mem_key);
        if (reuse_it != s_unmapped_ptrs.end()) {
            ptr = reuse_it->second;
            s_unmapped_ptrs.erase(reuse_it);
            is_remap = true;
            need_invalidate = true;  // was unmapped → need to fetch from host
            SPDLOG_INFO("vkMapMemory: REUSE unmapped shadow for mem={:#x}", mem_key);
        } else {
            auto already_it = s_mapped_ptrs.find(mem_key);
            if (already_it != s_mapped_ptrs.end()) {
                ptr = already_it->second;
                is_remap = true;  // already mapped, just update offset
                SPDLOG_TRACE("vkMapMemory: already mapped, reuse ptr for mem={:#x}", mem_key);
            }
        }
    }

    if (!ptr) {
        size_t aligned_size = (static_cast<size_t>(total_size) + 255) & ~255;
        ptr = omni_aligned_alloc(256, aligned_size);
        if (!ptr) {
            SPDLOG_ERROR("vkMapMemory: failed to allocate {} bytes", aligned_size);
            return VK_ERROR_OUT_OF_HOST_MEMORY;
        }
        std::memset(ptr, 0, aligned_size);
    }

    // If re-mapping after unmap, fetch latest data from host
    if (need_invalidate) {
        auto* batch = get_batch();
        auto* client = init::get_client();
        if (batch && client) {
            serializer::VulkanSerializer ser;
            ser.write_handle((uint64_t)(device));
            ser.write_u32(1);
            ser.write_handle(mem_key);
            ser.write_u64(offset);
            ser.write_u64(total_size);
            static std::atomic<uint32_t> inv_req{0x96000000};
            auto builder = protocol::build_command(
                fbs::FunctionId_vkInvalidateMappedMemoryRanges,
                inv_req.fetch_add(1), ser.data(), ser.size());
            batch->append(builder);
            batch->force_flush();
            client->sync_query(0x8d, mem_key);
        }
    }

    *ppData = static_cast<uint8_t*>(ptr) + offset;

    {
        std::lock_guard<std::mutex> lock(s_map_mutex);
        s_mapped_ptrs[mem_key] = ptr;
        s_memory_map_offsets[mem_key] = offset;
        // Always mark dirty on map — app will write data after mapping
        s_memory_dirty[mem_key] = true;
    }
    SPDLOG_TRACE("vkMapMemory_hook exit: ptr={} mem={:#x} (map_offset={}, remap={})", *ppData, mem_key, offset, is_remap);
    return VK_SUCCESS;
}

void VKAPI_PTR vkUnmapMemory_hook(VkDevice device, VkDeviceMemory memory)
{
    SPDLOG_INFO("vkUnmapMemory_hook: mem={:#x} uploading to host...", handle_to_u64(memory));
    uint64_t mem_key = handle_to_u64(memory);

    sync_all_mapped_memory_to_host();

    std::lock_guard<std::mutex> lock(s_map_mutex);
    auto it = s_mapped_ptrs.find(mem_key);
    if (it != s_mapped_ptrs.end()) {
        s_unmapped_ptrs[mem_key] = it->second;
        s_mapped_ptrs.erase(it);
        s_memory_map_offsets.erase(mem_key);
    }
    SPDLOG_INFO("vkUnmapMemory_hook: mem={:#x} shadow retained", mem_key);
}

// ---------------------------------------------------------------------------
// vkMapMemory2 / vkUnmapMemory2 — Vulkan 1.4 variants
// Used by newer apps (vkcube SDK 1.4+) instead of vkMapMemory
// ---------------------------------------------------------------------------
VkResult VKAPI_PTR vkMapMemory2_hook(
    VkDevice device,
    const VkMemoryMapInfo* pMemoryMapInfo,
    void** ppData)
{
    if (!pMemoryMapInfo || !ppData) return VK_ERROR_INITIALIZATION_FAILED;
    VkDeviceSize total_size = get_memory_size(pMemoryMapInfo->memory);
    SPDLOG_TRACE("vkMapMemory2_hook entry: device={} memory={} size={} total_size={}",
                (void*)device, (void*)pMemoryMapInfo->memory,
                pMemoryMapInfo->size, total_size);
    if (total_size == 0) {
        SPDLOG_ERROR("vkMapMemory2: total_size is 0 (untracked memory allocation)");
        return VK_ERROR_MEMORY_MAP_FAILED;
    }

    uint64_t mem_key = handle_to_u64(pMemoryMapInfo->memory);
    void* ptr = nullptr;
    bool is_remap = false;
    bool need_invalidate = false;
    {
        std::lock_guard<std::mutex> lock(s_map_mutex);
        auto reuse_it = s_unmapped_ptrs.find(mem_key);
        if (reuse_it != s_unmapped_ptrs.end()) {
            ptr = reuse_it->second;
            s_unmapped_ptrs.erase(reuse_it);
            is_remap = true;
            need_invalidate = true;
            SPDLOG_INFO("vkMapMemory2: REUSE unmapped shadow for mem={:#x}", mem_key);
        } else {
            auto already_it = s_mapped_ptrs.find(mem_key);
            if (already_it != s_mapped_ptrs.end()) {
                ptr = already_it->second;
                is_remap = true;
                SPDLOG_TRACE("vkMapMemory2: already mapped, reuse ptr for mem={:#x}", mem_key);
            }
        }
    }

    if (!ptr) {
        size_t aligned_size = (static_cast<size_t>(total_size) + 255) & ~255;
        ptr = omni_aligned_alloc(256, aligned_size);
        if (!ptr) {
            SPDLOG_ERROR("vkMapMemory2: failed to allocate {} bytes", aligned_size);
            return VK_ERROR_OUT_OF_HOST_MEMORY;
        }
        std::memset(ptr, 0, aligned_size);
    }

    if (need_invalidate) {
        auto* batch = get_batch();
        auto* client = init::get_client();
        if (batch && client) {
            serializer::VulkanSerializer ser;
            ser.write_handle((uint64_t)(device));
            ser.write_u32(1);
            ser.write_handle(mem_key);
            ser.write_u64(pMemoryMapInfo->offset);
            ser.write_u64(total_size);
            static std::atomic<uint32_t> inv_req{0x96000000};
            auto builder = protocol::build_command(
                fbs::FunctionId_vkInvalidateMappedMemoryRanges,
                inv_req.fetch_add(1), ser.data(), ser.size());
            batch->append(builder);
            batch->force_flush();
            client->sync_query(0x8d, mem_key);
        }
    }

    *ppData = static_cast<uint8_t*>(ptr) + pMemoryMapInfo->offset;

    {
        std::lock_guard<std::mutex> lock(s_map_mutex);
        s_mapped_ptrs[mem_key] = ptr;
        s_memory_map_offsets[mem_key] = pMemoryMapInfo->offset;
        s_memory_dirty[mem_key] = true;
    }
    SPDLOG_TRACE("vkMapMemory2_hook exit: ptr={} mem={:#x} (map_offset={})", *ppData, mem_key, pMemoryMapInfo->offset);
    return VK_SUCCESS;
}

void VKAPI_PTR vkUnmapMemory2_hook(
    VkDevice device,
    const VkMemoryUnmapInfo* pMemoryUnmapInfo)
{
    if (!pMemoryUnmapInfo) return;
    SPDLOG_INFO("vkUnmapMemory2_hook: mem={:#x} uploading to host...", handle_to_u64(pMemoryUnmapInfo->memory));
    uint64_t mem_key = handle_to_u64(pMemoryUnmapInfo->memory);

    sync_all_mapped_memory_to_host();

    std::lock_guard<std::mutex> lock(s_map_mutex);
    auto it = s_mapped_ptrs.find(mem_key);
    if (it != s_mapped_ptrs.end()) {
        s_unmapped_ptrs[mem_key] = it->second;
        s_mapped_ptrs.erase(it);
        s_memory_map_offsets.erase(mem_key);
    }
    SPDLOG_INFO("vkUnmapMemory2_hook: mem={:#x} shadow retained", mem_key);
}

void sync_all_mapped_memory_to_host() {
    std::lock_guard<std::mutex> lock(s_map_mutex);

    // Collect both mapped and retained-unmapped entries
    struct SyncEntry { uint64_t key; void* ptr; bool is_mapped; };
    std::vector<SyncEntry> entries;
    for (const auto& [key, ptr] : s_mapped_ptrs) entries.push_back({key, ptr, true});
    for (const auto& [key, ptr] : s_unmapped_ptrs) entries.push_back({key, ptr, false});
    if (entries.empty()) return;

    auto* batch = get_batch();
    if (!batch) return;

    for (const auto& [mem_key, guest_ptr, is_mapped] : entries) {
        bool is_coherent = false;
        {
            auto coh_it = s_memory_coherent.find(mem_key);
            if (coh_it != s_memory_coherent.end() && coh_it->second) {
                is_coherent = true;
            }
        }

        bool is_dirty = false;
        auto dirty_it = s_memory_dirty.find(mem_key);
        if (dirty_it != s_memory_dirty.end() && dirty_it->second) {
            is_dirty = true;
        }

        if (!is_coherent && !is_dirty) continue;
        if (is_coherent && !is_dirty) continue;
        SPDLOG_INFO("sync_all: syncing mem={:#x} mapped={} coherent={} dirty={}", mem_key, is_mapped, is_coherent, is_dirty);

        // Coherent memory: mark as dirty on first map (app will write after mapping)
        // Only sync if dirty or has explicit flush ranges

        VkDevice device = VK_NULL_HANDLE;
        {
            auto it = s_memory_devices.find(mem_key);
            if (it == s_memory_devices.end()) {
                SPDLOG_WARN("sync_all: skipping mem={:#x} — not tracked in s_memory_devices", mem_key);
                continue;
            }
            device = it->second;
        }
        // Use find() instead of operator[] to avoid creating entries for non-memory handles
        auto size_it = s_memory_sizes.find(mem_key);
        if (size_it == s_memory_sizes.end()) {
            SPDLOG_WARN("sync_all: skipping mem={:#x} — not tracked in s_memory_sizes", mem_key);
            continue;
        }
        VkDeviceSize total_size = size_it->second;
        if (total_size == 0 || !guest_ptr) {
            SPDLOG_WARN("sync_all: skipping mem={:#x} — invalid total_size={} guest_ptr={}",
                        mem_key, total_size, fmt::ptr(guest_ptr));
            continue;
        }

        // Check if we have tracked flush ranges — if so, only send those
        auto flush_it = s_pending_flushes.find(mem_key);
        if (flush_it != s_pending_flushes.end() && !flush_it->second.empty()) {
            for (const auto& pf : flush_it->second) {
                if (pf.offset + pf.size > total_size) continue;
                serializer::VulkanSerializer ser;
                ser.write_handle((uint64_t)(device));
                ser.write_u32(1);
                ser.write_handle(mem_key);
                ser.write_u64(pf.offset);
                ser.write_u64(pf.size);
                ser.write_raw(reinterpret_cast<const uint8_t*>(guest_ptr) + pf.offset,
                              static_cast<size_t>(pf.size));

                static std::atomic<uint32_t> req_id{0x90000000};
                auto builder = protocol::build_command(
                    fbs::FunctionId_vkFlushMappedMemoryRanges,
                    req_id.fetch_add(1, std::memory_order_relaxed),
                    ser.data(), ser.size()
                );
                batch->append(builder);
            }
            flush_it->second.clear();
            } else {
            // Chunk large uploads into 1 MB pieces
            static constexpr VkDeviceSize kChunkSize = 1024 * 1024;
            VkDeviceSize remaining = total_size;
            VkDeviceSize off = 0;
            uint32_t chunk_count = 0;
            while (remaining > 0) {
                VkDeviceSize chunk = std::min(remaining, kChunkSize);
                // Data integrity: log first 16B of model data before upload
                if (off == 0 && chunk >= 16 && total_size > 100ULL * 1024 * 1024) {
                    const uint8_t* p = reinterpret_cast<const uint8_t*>(guest_ptr);
                    SPDLOG_INFO("MODEL_UPLOAD_GUEST: mem={:#x} total={}MB first16={:02x} {:02x} {:02x} {:02x} {:02x} {:02x} {:02x} {:02x} {:02x} {:02x} {:02x} {:02x} {:02x} {:02x} {:02x} {:02x}",
                        mem_key, total_size/(1024*1024),
                        p[0], p[1], p[2], p[3], p[4], p[5], p[6], p[7],
                        p[8], p[9], p[10], p[11], p[12], p[13], p[14], p[15]);
                }
                serializer::VulkanSerializer ser;
                ser.write_handle((uint64_t)(device));
                ser.write_u32(1);
                ser.write_handle(mem_key);
                ser.write_u64(off);
                ser.write_u64(chunk);
                ser.write_raw(reinterpret_cast<const uint8_t*>(guest_ptr) + off,
                              static_cast<size_t>(chunk));

                static std::atomic<uint32_t> req_id{0x90000000};
                auto builder = protocol::build_command(
                    fbs::FunctionId_vkFlushMappedMemoryRanges,
                    req_id.fetch_add(1, std::memory_order_relaxed),
                    ser.data(), ser.size()
                );
                batch->append(builder);
                off += chunk;
                remaining -= chunk;
                chunk_count++;
            }
            SPDLOG_INFO("sync_all: mem={:#x} uploaded {} chunks ({}KB total)",
                        mem_key, chunk_count, total_size / 1024);
            }

        if (dirty_it != s_memory_dirty.end()) {
            dirty_it->second = false;
        }
    }
}

VkResult VKAPI_PTR vkFlushMappedMemoryRanges_hook(
    VkDevice device, uint32_t memoryRangeCount, const VkMappedMemoryRange* pMemoryRanges)
{
    SPDLOG_INFO("vkFlushMappedMemoryRanges_hook entry: count={}", memoryRangeCount);
    {
        std::lock_guard<std::mutex> lock(s_map_mutex);
        for (uint32_t i = 0; i < memoryRangeCount; i++) {
            const auto& range = pMemoryRanges[i];
            uint64_t mem_key = handle_to_u64(range.memory);
            VkDeviceSize offset = range.offset;
            VkDeviceSize size = resolve_map_size(range.memory, range.size, range.offset);

            void* guest_ptr = nullptr;
            auto it = s_mapped_ptrs.find(mem_key);
            if (it != s_mapped_ptrs.end()) {
                guest_ptr = it->second;
            }

            SPDLOG_INFO("  range {}: memory={:#x} offset={} size={} guest_ptr={}", i, mem_key, offset, size, guest_ptr);

            if (guest_ptr && size > 0) {
                // Mark this range as pending sync
                s_pending_flushes[mem_key].push_back({offset, size});
                s_memory_dirty[mem_key] = true;
            }
        }
    }

    SPDLOG_INFO("vkFlushMappedMemoryRanges_hook exit");
    return VK_SUCCESS;
}

VkResult VKAPI_PTR vkInvalidateMappedMemoryRanges_hook(
    VkDevice device, uint32_t memoryRangeCount, const VkMappedMemoryRange* pMemoryRanges)
{
    SPDLOG_INFO("vkInvalidateMappedMemoryRanges_hook entry: count={}", memoryRangeCount);
    auto* batch = get_batch();
    if (batch) {
        serializer::VulkanSerializer ser;
        ser.write_handle((uint64_t)(device));
        ser.write_u32(memoryRangeCount);
        for (uint32_t i = 0; i < memoryRangeCount; i++) {
            ser.write_handle(handle_to_u64(pMemoryRanges[i].memory));
            ser.write_u64(pMemoryRanges[i].offset);
            ser.write_u64(pMemoryRanges[i].size);
            SPDLOG_INFO("  range {}: memory={:#x} offset={} size={}", i, handle_to_u64(pMemoryRanges[i].memory), pMemoryRanges[i].offset, pMemoryRanges[i].size);
        }
        static std::atomic<uint32_t> req_id{0x95000000};
        auto builder = protocol::build_command(
            fbs::FunctionId_vkInvalidateMappedMemoryRanges,
            req_id.fetch_add(1, std::memory_order_relaxed),
            ser.data(), ser.size()
        );
        batch->append(builder);
        batch->flush();
    }

    auto* client = init::get_client();
    if (client) {
        for (uint32_t i = 0; i < memoryRangeCount; i++) {
            client->sync_query(0x8d, handle_to_u64(pMemoryRanges[i].memory));
        }
    }
    SPDLOG_INFO("vkInvalidateMappedMemoryRanges_hook exit");
    return VK_SUCCESS;
}

VkResult VKAPI_PTR vkQueueSubmit_hook(
    VkQueue queue, uint32_t submitCount, const VkSubmitInfo* pSubmits, VkFence fence)
{
    SPDLOG_TRACE("Intercepted: vkQueueSubmit");
    FILE* diag = fopen("C:\\Users\\test\\AppData\\Local\\Temp\\omnigpu_diag.txt", "a");
    if (diag) { fprintf(diag, "QueueSubmit called\n"); fclose(diag); }

    // Sync all mapped memory writes first
    sync_all_mapped_memory_to_host();

    auto* batch = get_batch();
    if (batch) {
        serializer::VulkanSerializer ser;
        ser.write_handle((uint64_t)(queue));
        ser.write_u32(static_cast<uint32_t>(submitCount));
        for (uint32_t i = 0; i < submitCount; i++) {
            serializer::write_VkSubmitInfo(ser, &pSubmits[i]);
        }
        ser.write_handle((uint64_t)(fence));

        static std::atomic<uint32_t> req_id{0x92000000};
        auto builder = protocol::build_command(
            fbs::FunctionId_vkQueueSubmit,
            req_id.fetch_add(1, std::memory_order_relaxed),
            ser.data(),
            ser.size()
        );
        batch->append(builder);
        // CRITICAL: force flush so host receives submit immediately.
        // Without this, compute workloads deadlock because vkWaitForFences
        // blocks the guest while the submit is still in the batch queue.
        batch->force_flush();
    }

    return VK_SUCCESS;
}

VkResult VKAPI_PTR vkQueueSubmit2_hook(
    VkQueue queue, uint32_t submitCount, const VkSubmitInfo2* pSubmits, VkFence fence)
{
    SPDLOG_TRACE("Intercepted: vkQueueSubmit2");

    // Sync all mapped memory writes first
    sync_all_mapped_memory_to_host();

    auto* batch = get_batch();
    if (batch) {
        serializer::VulkanSerializer ser;
        ser.write_handle((uint64_t)(queue));
        ser.write_u32(static_cast<uint32_t>(submitCount));
        for (uint32_t i = 0; i < submitCount; i++) {
            serializer::write_VkSubmitInfo2(ser, &pSubmits[i]);
        }
        ser.write_handle((uint64_t)(fence));

        static std::atomic<uint32_t> req_id{0x93000000};
        auto builder = protocol::build_command(
            fbs::FunctionId_vkQueueSubmit2,
            req_id.fetch_add(1, std::memory_order_relaxed),
            ser.data(),
            ser.size()
        );
        batch->append(builder);
        batch->force_flush();
    }

    return VK_SUCCESS;
}

// Called by receive thread when host sends back buffer data via DataMessage
// Copies the received data into the guest's shadow buffer for the given memory handle.
void update_shadow_buffer(uint64_t mem_key, const uint8_t* data, size_t size, VkDeviceSize offset) {
    std::lock_guard<std::mutex> lock(s_map_mutex);
    void* dest = nullptr;
    auto it = s_mapped_ptrs.find(mem_key);
    if (it != s_mapped_ptrs.end()) {
        dest = it->second;
    } else {
        auto uit = s_unmapped_ptrs.find(mem_key);
        if (uit != s_unmapped_ptrs.end()) {
            dest = uit->second;
        }
    }
    if (!dest || !data || size == 0) {
        SPDLOG_TRACE("update_shadow_buffer: skip mem={:#x}", mem_key);
        return;
    }
    auto size_it = s_memory_sizes.find(mem_key);
    VkDeviceSize total_size = (size_it != s_memory_sizes.end()) ? size_it->second : 0;
    if (offset + size <= total_size) {
        std::memcpy(static_cast<uint8_t*>(dest) + offset, data, size);
        SPDLOG_TRACE("update_shadow_buffer: mem={:#x} offset={} size={}", mem_key, offset, size);
    } else {
        SPDLOG_ERROR("update_shadow_buffer: write out of bounds! mem={:#x} offset={} size={} total_size={}",
                     mem_key, offset, size, total_size);
    }
}

} // namespace omnigpu::intercept

// Static initializer: register manual hooks into the hook map
// Runs during CRT initialization (before DllMain), safe for map insertion.
namespace {
using namespace omnigpu;
using namespace omnigpu::intercept;
VkResult VKAPI_PTR vkWaitForFences_hook(
    VkDevice device, uint32_t fenceCount, const VkFence* pFences,
    VkBool32 waitAll, uint64_t timeout)
{
    SPDLOG_TRACE("Intercepted vkWaitForFences: count={}", fenceCount);
    auto* batch = get_batch();
    if (batch) {
        batch->flush();
    }
    VkResult final_res = VK_SUCCESS;
    auto* client = init::get_client();
    if (client) {
        uint64_t extra[3];
        uint64_t waitAll64 = waitAll ? 1 : 0;
        for (uint32_t i = 0; i < fenceCount; i++) {
            extra[0] = handle_to_u64(pFences[i]);
            extra[1] = waitAll64;
            extra[2] = timeout;
            uint64_t res = client->sync_query_ext(0x85,
                reinterpret_cast<const uint8_t*>(extra), sizeof(extra));
            if (static_cast<VkResult>(res) != VK_SUCCESS) {
                final_res = static_cast<VkResult>(res);
            }
        }
    }
    return final_res;
}

VkResult VKAPI_PTR vkDeviceWaitIdle_hook(VkDevice device) {
    SPDLOG_TRACE("Intercepted vkDeviceWaitIdle");
    auto* batch = get_batch();
    if (batch) {
        batch->flush();
    }
    auto* client = init::get_client();
    if (client) {
        uint64_t res = client->sync_query(0x88, handle_to_u64(device));
        return static_cast<VkResult>(res);
    }
    return VK_SUCCESS;
}

VkResult VKAPI_PTR vkQueueWaitIdle_hook(VkQueue queue) {
    SPDLOG_TRACE("Intercepted vkQueueWaitIdle");
    auto* batch = get_batch();
    if (batch) {
        batch->flush();
    }
    auto* client = init::get_client();
    if (client) {
        uint64_t res = client->sync_query(0x89, handle_to_u64(queue));
        return static_cast<VkResult>(res);
    }
    return VK_SUCCESS;
}

VkResult VKAPI_PTR vkGetSemaphoreCounterValue_hook(
    VkDevice device, VkSemaphore semaphore, uint64_t* pValue)
{
    SPDLOG_TRACE("Intercepted vkGetSemaphoreCounterValue");
    auto* batch = get_batch();
    if (batch) {
        batch->flush();
    }
    auto* client = init::get_client();
    if (client && pValue) {
        uint64_t val = client->sync_query(0x8a, handle_to_u64(semaphore));
        *pValue = val;
        return VK_SUCCESS;
    }
    if (pValue) *pValue = 0;
    return VK_SUCCESS;
}

void VKAPI_PTR vkCmdCopyBuffer2_hook(VkCommandBuffer commandBuffer, const VkCopyBufferInfo2* pCopyBufferInfo) {
    SPDLOG_TRACE("Intercepted vkCmdCopyBuffer2");
    serializer::VulkanSerializer ser;
    ser.write_handle((uint64_t)commandBuffer);
    ser.write_handle(handle_to_u64(pCopyBufferInfo->srcBuffer));
    ser.write_handle(handle_to_u64(pCopyBufferInfo->dstBuffer));
    ser.write_u32(pCopyBufferInfo->regionCount);
    for (uint32_t i = 0; i < pCopyBufferInfo->regionCount; i++) {
        ser.write_raw(&pCopyBufferInfo->pRegions[i], sizeof(VkBufferCopy2));
    }

    auto* batch = get_batch();
    if (batch) {
        static std::atomic<uint32_t> req_id{0xC1000000};
        auto builder = protocol::build_command(
            fbs::FunctionId_vkCmdCopyBuffer2,
            req_id.fetch_add(1),
            ser.data(), ser.size()
        );
        batch->append(builder);
    }
}

void VKAPI_PTR vkCmdCopyImage2_hook(VkCommandBuffer commandBuffer, const VkCopyImageInfo2* pCopyImageInfo) {
    SPDLOG_TRACE("Intercepted vkCmdCopyImage2");
    serializer::VulkanSerializer ser;
    ser.write_handle((uint64_t)commandBuffer);
    ser.write_handle(handle_to_u64(pCopyImageInfo->srcImage));
    ser.write_u32(static_cast<uint32_t>(pCopyImageInfo->srcImageLayout));
    ser.write_handle(handle_to_u64(pCopyImageInfo->dstImage));
    ser.write_u32(static_cast<uint32_t>(pCopyImageInfo->dstImageLayout));
    ser.write_u32(pCopyImageInfo->regionCount);
    for (uint32_t i = 0; i < pCopyImageInfo->regionCount; i++) {
        ser.write_raw(&pCopyImageInfo->pRegions[i], sizeof(VkImageCopy2));
    }

    auto* batch = get_batch();
    if (batch) {
        static std::atomic<uint32_t> req_id{0xC2000000};
        auto builder = protocol::build_command(
            fbs::FunctionId_vkCmdCopyImage2,
            req_id.fetch_add(1),
            ser.data(), ser.size()
        );
        batch->append(builder);
    }
}

void VKAPI_PTR vkCmdCopyBufferToImage2_hook(VkCommandBuffer commandBuffer, const VkCopyBufferToImageInfo2* pCopyBufferToImageInfo) {
    SPDLOG_TRACE("Intercepted vkCmdCopyBufferToImage2");
    serializer::VulkanSerializer ser;
    ser.write_handle((uint64_t)commandBuffer);
    ser.write_handle(handle_to_u64(pCopyBufferToImageInfo->srcBuffer));
    ser.write_handle(handle_to_u64(pCopyBufferToImageInfo->dstImage));
    ser.write_u32(static_cast<uint32_t>(pCopyBufferToImageInfo->dstImageLayout));
    ser.write_u32(pCopyBufferToImageInfo->regionCount);
    for (uint32_t i = 0; i < pCopyBufferToImageInfo->regionCount; i++) {
        ser.write_raw(&pCopyBufferToImageInfo->pRegions[i], sizeof(VkBufferImageCopy2));
    }

    auto* batch = get_batch();
    if (batch) {
        static std::atomic<uint32_t> req_id{0xC3000000};
        auto builder = protocol::build_command(
            fbs::FunctionId_vkCmdCopyBufferToImage2,
            req_id.fetch_add(1),
            ser.data(), ser.size()
        );
        batch->append(builder);
    }
}

void VKAPI_PTR vkCmdCopyImageToBuffer2_hook(VkCommandBuffer commandBuffer, const VkCopyImageToBufferInfo2* pCopyImageToBufferInfo) {
    SPDLOG_TRACE("Intercepted vkCmdCopyImageToBuffer2");
    serializer::VulkanSerializer ser;
    ser.write_handle((uint64_t)commandBuffer);
    ser.write_handle(handle_to_u64(pCopyImageToBufferInfo->srcImage));
    ser.write_u32(static_cast<uint32_t>(pCopyImageToBufferInfo->srcImageLayout));
    ser.write_handle(handle_to_u64(pCopyImageToBufferInfo->dstBuffer));
    ser.write_u32(pCopyImageToBufferInfo->regionCount);
    for (uint32_t i = 0; i < pCopyImageToBufferInfo->regionCount; i++) {
        ser.write_raw(&pCopyImageToBufferInfo->pRegions[i], sizeof(VkBufferImageCopy2));
    }

    auto* batch = get_batch();
    if (batch) {
        static std::atomic<uint32_t> req_id{0xC4000000};
        auto builder = protocol::build_command(
            fbs::FunctionId_vkCmdCopyImageToBuffer2,
            req_id.fetch_add(1),
            ser.data(), ser.size()
        );
        batch->append(builder);
    }
}

void VKAPI_PTR vkCmdResolveImage2_hook(VkCommandBuffer commandBuffer, const VkResolveImageInfo2* pResolveImageInfo) {
    SPDLOG_TRACE("Intercepted vkCmdResolveImage2");
    serializer::VulkanSerializer ser;
    ser.write_handle((uint64_t)commandBuffer);
    ser.write_handle(handle_to_u64(pResolveImageInfo->srcImage));
    ser.write_u32(static_cast<uint32_t>(pResolveImageInfo->srcImageLayout));
    ser.write_handle(handle_to_u64(pResolveImageInfo->dstImage));
    ser.write_u32(static_cast<uint32_t>(pResolveImageInfo->dstImageLayout));
    ser.write_u32(pResolveImageInfo->regionCount);
    for (uint32_t i = 0; i < pResolveImageInfo->regionCount; i++) {
        ser.write_raw(&pResolveImageInfo->pRegions[i], sizeof(VkImageResolve2));
    }

    auto* batch = get_batch();
    if (batch) {
        static std::atomic<uint32_t> req_id{0xC5000000};
        auto builder = protocol::build_command(
            fbs::FunctionId_vkCmdResolveImage2,
            req_id.fetch_add(1),
            ser.data(), ser.size()
        );
        batch->append(builder);
    }
}

void VKAPI_PTR vkCmdBlitImage2_hook(VkCommandBuffer commandBuffer, const VkBlitImageInfo2* pBlitImageInfo) {
    SPDLOG_TRACE("Intercepted vkCmdBlitImage2");
    serializer::VulkanSerializer ser;
    ser.write_handle((uint64_t)commandBuffer);
    ser.write_handle(handle_to_u64(pBlitImageInfo->srcImage));
    ser.write_u32(static_cast<uint32_t>(pBlitImageInfo->srcImageLayout));
    ser.write_handle(handle_to_u64(pBlitImageInfo->dstImage));
    ser.write_u32(static_cast<uint32_t>(pBlitImageInfo->dstImageLayout));
    ser.write_u32(pBlitImageInfo->regionCount);
    for (uint32_t i = 0; i < pBlitImageInfo->regionCount; i++) {
        ser.write_raw(&pBlitImageInfo->pRegions[i], sizeof(VkImageBlit2));
    }
    ser.write_u32(static_cast<uint32_t>(pBlitImageInfo->filter));

    auto* batch = get_batch();
    if (batch) {
        static std::atomic<uint32_t> req_id{0xC6000000};
        auto builder = protocol::build_command(
            fbs::FunctionId_vkCmdBlitImage2,
            req_id.fetch_add(1),
            ser.data(), ser.size()
        );
        batch->append(builder);
    }
}

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

        // KHR aliases for query functions (promoted to core in 1.1).
        // Only register KHR variants of manual query functions that fill output data.
        register_manual_hook("vkGetPhysicalDeviceProperties2KHR", reinterpret_cast<void*>(vkGetPhysicalDeviceProperties2_hook));
        register_manual_hook("vkGetPhysicalDeviceFeatures2KHR", reinterpret_cast<void*>(vkGetPhysicalDeviceFeatures2_hook));
        register_manual_hook("vkGetPhysicalDeviceMemoryProperties2KHR", reinterpret_cast<void*>(vkGetPhysicalDeviceMemoryProperties2_hook));
        register_manual_hook("vkGetPhysicalDeviceQueueFamilyProperties2KHR", reinterpret_cast<void*>(vkGetPhysicalDeviceQueueFamilyProperties2_hook));
        register_manual_hook("vkGetPhysicalDeviceFormatProperties2KHR", reinterpret_cast<void*>(vkGetPhysicalDeviceFormatProperties2_hook));
        register_manual_hook("vkGetPhysicalDeviceImageFormatProperties2KHR", reinterpret_cast<void*>(vkGetPhysicalDeviceImageFormatProperties2_hook));
        register_manual_hook("vkGetPhysicalDeviceSparseImageFormatProperties2KHR", reinterpret_cast<void*>(vkGetPhysicalDeviceSparseImageFormatProperties2_hook));
        register_manual_hook("vkGetPhysicalDeviceExternalBufferPropertiesKHR", reinterpret_cast<void*>(vkGetPhysicalDeviceExternalBufferProperties_hook));
        register_manual_hook("vkGetPhysicalDeviceExternalFencePropertiesKHR", reinterpret_cast<void*>(vkGetPhysicalDeviceExternalFenceProperties_hook));
        register_manual_hook("vkGetPhysicalDeviceExternalSemaphorePropertiesKHR", reinterpret_cast<void*>(vkGetPhysicalDeviceExternalSemaphoreProperties_hook));
        register_manual_hook("vkEnumeratePhysicalDeviceGroupsKHR", reinterpret_cast<void*>(vkEnumeratePhysicalDeviceGroups_hook));

        // Tool properties
        register_manual_hook("vkGetPhysicalDeviceToolPropertiesEXT", reinterpret_cast<void*>(vkGetPhysicalDeviceToolPropertiesEXT_hook));

        // Memory requirement queries
        register_manual_hook("vkGetBufferMemoryRequirements", reinterpret_cast<void*>(vkGetBufferMemoryRequirements_hook));
        register_manual_hook("vkGetBufferMemoryRequirements2", reinterpret_cast<void*>(vkGetBufferMemoryRequirements2_hook));
        register_manual_hook("vkGetImageMemoryRequirements", reinterpret_cast<void*>(vkGetImageMemoryRequirements_hook));
        register_manual_hook("vkGetImageMemoryRequirements2", reinterpret_cast<void*>(vkGetImageMemoryRequirements2_hook));
        register_manual_hook("vkGetImageSparseMemoryRequirements", reinterpret_cast<void*>(vkGetImageSparseMemoryRequirements_hook));
        register_manual_hook("vkGetImageSparseMemoryRequirements2", reinterpret_cast<void*>(vkGetImageSparseMemoryRequirements2_hook));
        register_manual_hook("vkGetDeviceBufferMemoryRequirements", reinterpret_cast<void*>(vkGetDeviceBufferMemoryRequirements_hook));
        register_manual_hook("vkGetDeviceImageMemoryRequirements", reinterpret_cast<void*>(vkGetDeviceImageMemoryRequirements_hook));

        // Command buffer (dispatchable handles)
        register_manual_hook("vkAllocateCommandBuffers", reinterpret_cast<void*>(vkAllocateCommandBuffers_hook));
        register_manual_hook("vkFreeCommandBuffers", reinterpret_cast<void*>(vkFreeCommandBuffers_hook));
        register_manual_hook("vkBeginCommandBuffer", reinterpret_cast<void*>(vkBeginCommandBuffer_hook));
        register_manual_hook("vkEndCommandBuffer", reinterpret_cast<void*>(vkEndCommandBuffer_hook));
        register_manual_hook("vkResetCommandBuffer", reinterpret_cast<void*>(vkResetCommandBuffer_hook));

        // WSI Surface / Swapchain
        register_manual_hook("vkCreateWin32SurfaceKHR", reinterpret_cast<void*>(vkCreateWin32SurfaceKHR_hook));
        register_manual_hook("vkDestroySurfaceKHR", reinterpret_cast<void*>(vkDestroySurfaceKHR_hook));
        register_manual_hook("vkCreateSwapchainKHR", reinterpret_cast<void*>(vkCreateSwapchainKHR_hook));
        register_manual_hook("vkDestroySwapchainKHR", reinterpret_cast<void*>(vkDestroySwapchainKHR_hook));
        register_manual_hook("vkGetSwapchainImagesKHR", reinterpret_cast<void*>(vkGetSwapchainImagesKHR_hook));
        register_manual_hook("vkAcquireNextImageKHR", reinterpret_cast<void*>(vkAcquireNextImageKHR_hook));
        register_manual_hook("vkAcquireNextImage2KHR", reinterpret_cast<void*>(vkAcquireNextImage2KHR_hook));
        register_manual_hook("vkQueuePresentKHR", reinterpret_cast<void*>(vkQueuePresentKHR_hook));
        register_manual_hook("vkGetDeviceGroupPresentCapabilitiesKHR", reinterpret_cast<void*>(vkGetDeviceGroupPresentCapabilitiesKHR_hook));

        // Surface queries
        register_manual_hook("vkGetPhysicalDeviceSurfaceCapabilitiesKHR", reinterpret_cast<void*>(vkGetPhysicalDeviceSurfaceCapabilitiesKHR_hook));
        register_manual_hook("vkGetPhysicalDeviceSurfaceFormatsKHR", reinterpret_cast<void*>(vkGetPhysicalDeviceSurfaceFormatsKHR_hook));
        register_manual_hook("vkGetPhysicalDeviceSurfacePresentModesKHR", reinterpret_cast<void*>(vkGetPhysicalDeviceSurfacePresentModesKHR_hook));
        register_manual_hook("vkGetPhysicalDeviceSurfaceCapabilities2KHR", reinterpret_cast<void*>(vkGetPhysicalDeviceSurfaceCapabilities2KHR_hook));
        register_manual_hook("vkGetPhysicalDeviceSurfaceFormats2KHR", reinterpret_cast<void*>(vkGetPhysicalDeviceSurfaceFormats2KHR_hook));

        // Memory mapping (must allocate host memory for guest writes)
        register_manual_hook("vkAllocateMemory", reinterpret_cast<void*>(vkAllocateMemory_hook));
        register_manual_hook("vkFreeMemory", reinterpret_cast<void*>(vkFreeMemory_hook));
        register_manual_hook("vkMapMemory", reinterpret_cast<void*>(vkMapMemory_hook));
        register_manual_hook("vkUnmapMemory", reinterpret_cast<void*>(vkUnmapMemory_hook));
        // Vulkan 1.4 variants
        register_manual_hook("vkMapMemory2", reinterpret_cast<void*>(vkMapMemory2_hook));
        register_manual_hook("vkUnmapMemory2", reinterpret_cast<void*>(vkUnmapMemory2_hook));
        // KHR extension variants (same implementations)
        register_manual_hook("vkMapMemory2KHR", reinterpret_cast<void*>(vkMapMemory2_hook));
        register_manual_hook("vkUnmapMemory2KHR", reinterpret_cast<void*>(vkUnmapMemory2_hook));

        // Submit & Memory Flush manual overrides
        register_manual_hook("vkQueueSubmit", reinterpret_cast<void*>(vkQueueSubmit_hook));
        register_manual_hook("vkQueueSubmit2", reinterpret_cast<void*>(vkQueueSubmit2_hook));
        register_manual_hook("vkQueueSubmit2KHR", reinterpret_cast<void*>(vkQueueSubmit2_hook));
        register_manual_hook("vkFlushMappedMemoryRanges", reinterpret_cast<void*>(vkFlushMappedMemoryRanges_hook));
        register_manual_hook("vkInvalidateMappedMemoryRanges", reinterpret_cast<void*>(vkInvalidateMappedMemoryRanges_hook));

        register_manual_hook("vkWaitForFences", reinterpret_cast<void*>(vkWaitForFences_hook));
        register_manual_hook("vkDeviceWaitIdle", reinterpret_cast<void*>(vkDeviceWaitIdle_hook));
        register_manual_hook("vkQueueWaitIdle", reinterpret_cast<void*>(vkQueueWaitIdle_hook));
        register_manual_hook("vkGetSemaphoreCounterValue", reinterpret_cast<void*>(vkGetSemaphoreCounterValue_hook));
        register_manual_hook("vkCmdCopyBuffer2", reinterpret_cast<void*>(vkCmdCopyBuffer2_hook));
        register_manual_hook("vkCmdCopyImage2", reinterpret_cast<void*>(vkCmdCopyImage2_hook));
        register_manual_hook("vkCmdCopyBufferToImage2", reinterpret_cast<void*>(vkCmdCopyBufferToImage2_hook));
        register_manual_hook("vkCmdCopyImageToBuffer2", reinterpret_cast<void*>(vkCmdCopyImageToBuffer2_hook));
        register_manual_hook("vkCmdResolveImage2", reinterpret_cast<void*>(vkCmdResolveImage2_hook));
        register_manual_hook("vkCmdBlitImage2", reinterpret_cast<void*>(vkCmdBlitImage2_hook));
        register_manual_hook("vkCmdBlitImage2KHR", reinterpret_cast<void*>(vkCmdBlitImage2_hook));
        register_manual_hook("vkCmdCopyBuffer2KHR", reinterpret_cast<void*>(vkCmdCopyBuffer2_hook));
        register_manual_hook("vkCmdCopyImage2KHR", reinterpret_cast<void*>(vkCmdCopyImage2_hook));
        register_manual_hook("vkCmdCopyBufferToImage2KHR", reinterpret_cast<void*>(vkCmdCopyBufferToImage2_hook));
        register_manual_hook("vkCmdCopyImageToBuffer2KHR", reinterpret_cast<void*>(vkCmdCopyImageToBuffer2_hook));
        register_manual_hook("vkCmdResolveImage2KHR", reinterpret_cast<void*>(vkCmdResolveImage2_hook));
    }
} s_manual_registrar;
}
