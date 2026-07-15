#define _CRT_SECURE_NO_WARNINGS

#include "vk_intercept.h"
#include "guest_init.h"
#include <spdlog/spdlog.h>

#ifdef _WIN32
#define ICD_EXPORT __declspec(dllexport)

// Linker directives to export undecorated function names
#if defined(_M_IX86) || defined(__i386__)
// 32-bit stdcall mangles names with _ prefix and @N suffix
#pragma comment(linker, "/export:vkGetInstanceProcAddr=_vkGetInstanceProcAddr@8")
#pragma comment(linker, "/export:vk_icdGetInstanceProcAddr=_vk_icdGetInstanceProcAddr@8")
#pragma comment(linker, "/export:vk_icdGetPhysicalDeviceProcAddr=_vk_icdGetPhysicalDeviceProcAddr@8")
#pragma comment(linker, "/export:vk_icdNegotiateLoaderICDInterfaceVersion=_vk_icdNegotiateLoaderICDInterfaceVersion@4")
#pragma comment(linker, "/export:vkDestroyInstance=_vkDestroyInstance@8")
#pragma comment(linker, "/export:vkCreateInstance=_vkCreateInstance@12")
#else
// 64-bit does not mangle stdcall
#pragma comment(linker, "/export:vkGetInstanceProcAddr=vkGetInstanceProcAddr")
#pragma comment(linker, "/export:vk_icdGetInstanceProcAddr=vk_icdGetInstanceProcAddr")
#pragma comment(linker, "/export:vk_icdGetPhysicalDeviceProcAddr=vk_icdGetPhysicalDeviceProcAddr")
#pragma comment(linker, "/export:vk_icdNegotiateLoaderICDInterfaceVersion=vk_icdNegotiateLoaderICDInterfaceVersion")
#pragma comment(linker, "/export:vkDestroyInstance=vkDestroyInstance")
#pragma comment(linker, "/export:vkCreateInstance=vkCreateInstance")
#endif

#else
#define ICD_EXPORT __attribute__((visibility("default")))
#endif

namespace {

void ensure_initialized() {
    omnigpu::init::initialize_guest();
}

} // namespace

// ---------------------------------------------------------------------------
// vk_icdGetInstanceProcAddr — MANDATORY ICD export (unique to ICD spec)
// ---------------------------------------------------------------------------
extern "C" ICD_EXPORT VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vk_icdGetInstanceProcAddr(
    VkInstance instance, const char* pName) {
    ensure_initialized();
    (void)instance;

    auto func = omnigpu::intercept::get_intercept_proc(pName);
    if (func) {
        SPDLOG_TRACE("ICD entrypoint: {} -> hook/real", pName);
        return func;
    }

    SPDLOG_WARN("Vulkan API not implemented/found: {}", pName);
    return nullptr;
}

// ---------------------------------------------------------------------------
// vkGetInstanceProcAddr — also exported for standard Vulkan dispatch
// ---------------------------------------------------------------------------
extern "C" VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vkGetInstanceProcAddr(
    VkInstance instance, const char* pName) {
    return vk_icdGetInstanceProcAddr(instance, pName);
}

// ---------------------------------------------------------------------------
// vk_icdNegotiateLoaderICDInterfaceVersion — MANDATORY ICD export
// ---------------------------------------------------------------------------
extern "C" ICD_EXPORT VKAPI_ATTR VkResult VKAPI_CALL vk_icdNegotiateLoaderICDInterfaceVersion(
    uint32_t* pSupportedVersion) {
    if (!pSupportedVersion) return VK_ERROR_INITIALIZATION_FAILED;
    // ICD loader-interface version 5 does not require
    // vk_icdEnumerateAdapterPhysicalDevices or strict version 6 exports.
    // The loader writes its max version; we take the min.
    if (*pSupportedVersion > 5) {
        *pSupportedVersion = 5;
    }
    // Leave *pSupportedVersion unchanged if loader <= 5
    return VK_SUCCESS;
}

// ---------------------------------------------------------------------------
// vkDestroyInstance — required by loader for ICD cleanup
// ---------------------------------------------------------------------------
extern "C" VKAPI_ATTR void VKAPI_CALL vkDestroyInstance(
    VkInstance instance, const VkAllocationCallbacks* pAllocator) {
    SPDLOG_TRACE("ICD: vkDestroyInstance({})", reinterpret_cast<void*>(instance));
    auto func = reinterpret_cast<void (VKAPI_PTR*)(VkInstance, const VkAllocationCallbacks*)>(
        omnigpu::intercept::get_intercept_proc("vkDestroyInstance"));
    if (func) {
        func(instance, pAllocator);
    }
}

// ---------------------------------------------------------------------------
// vkCreateInstance — required by loader for ICD initialization
// ---------------------------------------------------------------------------
extern "C" VKAPI_ATTR VkResult VKAPI_CALL vkCreateInstance(
    const VkInstanceCreateInfo* pCreateInfo,
    const VkAllocationCallbacks* pAllocator,
    VkInstance* pInstance) {
    SPDLOG_TRACE("ICD: vkCreateInstance");
    auto func = reinterpret_cast<VkResult (VKAPI_PTR*)(const VkInstanceCreateInfo*, const VkAllocationCallbacks*, VkInstance*)>(
        omnigpu::intercept::get_intercept_proc("vkCreateInstance"));
    if (func) {
        return func(pCreateInfo, pAllocator, pInstance);
    }
    return VK_ERROR_INITIALIZATION_FAILED;
}

// ---------------------------------------------------------------------------
// vk_icdGetPhysicalDeviceProcAddr — required by loader version 6
// ---------------------------------------------------------------------------
extern "C" ICD_EXPORT VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vk_icdGetPhysicalDeviceProcAddr(
    VkInstance instance, const char* pName) {
    return vk_icdGetInstanceProcAddr(instance, pName);
}
