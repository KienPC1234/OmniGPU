#include "vk_intercept.h"
#include "guest_init.h"

#include <mutex>
#include <spdlog/spdlog.h>

#ifdef _WIN32
#define ICD_EXPORT __declspec(dllexport)

// Linker directives to export undecorated function names.
#if defined(_M_IX86) || defined(__i386__)
#pragma comment(linker, "/export:vkGetInstanceProcAddr=_vkGetInstanceProcAddr@8")
#pragma comment(linker, "/export:vk_icdGetInstanceProcAddr=_vk_icdGetInstanceProcAddr@8")
#pragma comment(linker, "/export:vk_icdGetPhysicalDeviceProcAddr=_vk_icdGetPhysicalDeviceProcAddr@8")
#pragma comment(linker, "/export:vk_icdNegotiateLoaderICDInterfaceVersion=_vk_icdNegotiateLoaderICDInterfaceVersion@4")
#pragma comment(linker, "/export:vkDestroyInstance=_vkDestroyInstance@8")
#pragma comment(linker, "/export:vkCreateInstance=_vkCreateInstance@12")
#else
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
    static std::once_flag init_once;
    std::call_once(init_once, []() {
        if (!omnigpu::init::initialize_guest()) {
            SPDLOG_ERROR("Guest initialization failed — forwarding disabled");
        }
    });
}

} // namespace

extern "C" ICD_EXPORT VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL
vk_icdGetInstanceProcAddr(VkInstance instance, const char* pName) {
    ensure_initialized();
    (void)instance;

    if (pName == nullptr) return nullptr;
    auto func = omnigpu::intercept::get_intercept_proc(pName);
    if (func) {
        SPDLOG_TRACE("ICD entrypoint: {} -> hook", pName);
        return func;
    }

    SPDLOG_WARN("Vulkan API not implemented/found: {}", pName);
    return nullptr;
}

extern "C" ICD_EXPORT VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL
vkGetInstanceProcAddr(VkInstance instance, const char* pName) {
    return vk_icdGetInstanceProcAddr(instance, pName);
}

extern "C" ICD_EXPORT VKAPI_ATTR VkResult VKAPI_CALL
vk_icdNegotiateLoaderICDInterfaceVersion(uint32_t* pSupportedVersion) {
    if (!pSupportedVersion) return VK_ERROR_INITIALIZATION_FAILED;
    if (*pSupportedVersion > 5) *pSupportedVersion = 5;
    return VK_SUCCESS;
}

extern "C" ICD_EXPORT VKAPI_ATTR void VKAPI_CALL
vkDestroyInstance(VkInstance instance,
                  const VkAllocationCallbacks* pAllocator) {
    ensure_initialized();
    SPDLOG_TRACE("ICD: vkDestroyInstance({})",
                 reinterpret_cast<void*>(instance));
    auto func = reinterpret_cast<void (VKAPI_PTR*)(
        VkInstance, const VkAllocationCallbacks*)>(
        omnigpu::intercept::get_intercept_proc("vkDestroyInstance"));
    if (func) {
        func(instance, pAllocator);
    } else {
        // Never call back into the Vulkan loader from an ICD fallback. Doing so
        // can recurse into this driver and was also Windows-only in the old path.
        SPDLOG_ERROR("vkDestroyInstance: hook not found");
    }
}

extern "C" ICD_EXPORT VKAPI_ATTR VkResult VKAPI_CALL
vkCreateInstance(const VkInstanceCreateInfo* pCreateInfo,
                 const VkAllocationCallbacks* pAllocator,
                 VkInstance* pInstance) {
    ensure_initialized();
    SPDLOG_TRACE("ICD: vkCreateInstance");
    auto func = reinterpret_cast<VkResult (VKAPI_PTR*)(
        const VkInstanceCreateInfo*, const VkAllocationCallbacks*, VkInstance*)>(
        omnigpu::intercept::get_intercept_proc("vkCreateInstance"));
    if (func) return func(pCreateInfo, pAllocator, pInstance);
    return VK_ERROR_INITIALIZATION_FAILED;
}

extern "C" ICD_EXPORT VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL
vk_icdGetPhysicalDeviceProcAddr(VkInstance instance, const char* pName) {
    ensure_initialized();
    return vk_icdGetInstanceProcAddr(instance, pName);
}
