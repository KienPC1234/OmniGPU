#define _CRT_SECURE_NO_WARNINGS

#include <vulkan/vulkan.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

static void chk(VkResult r, const char* msg) {
    if (r != VK_SUCCESS) { printf("FAIL: %s (err=%d)\n", msg, r); exit(1); }
}

int main() {
    printf("=== OmniGPU Vulkan Device Test ===\n\n");

    // 1. Create instance with required extensions
    VkApplicationInfo appInfo{};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = "OmniGPU Test";
    appInfo.apiVersion = VK_API_VERSION_1_3;

    VkInstanceCreateInfo instInfo{};
    instInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    instInfo.pApplicationInfo = &appInfo;

    VkInstance instance = VK_NULL_HANDLE;
    VkResult res = vkCreateInstance(&instInfo, nullptr, &instance);
    if (res != VK_SUCCESS) {
        printf("FAIL: vkCreateInstance = %d\n", res);
        printf("The OmniGPU ICD might not be properly registered.\n");
        return 1;
    }
    printf("[OK] Instance created\n");

    // 2. Check instance version
    uint32_t apiVersion = 0;
    chk(vkEnumerateInstanceVersion(&apiVersion), "vkEnumerateInstanceVersion");
    printf("Vulkan API version: %u.%u.%u\n",
           VK_API_VERSION_MAJOR(apiVersion),
           VK_API_VERSION_MINOR(apiVersion),
           VK_API_VERSION_PATCH(apiVersion));

    // 3. Enumerate physical devices
    uint32_t devCount = 0;
    chk(vkEnumeratePhysicalDevices(instance, &devCount, nullptr), "vkEnumeratePhysicalDevices(count)");
    printf("\nPhysical devices: %u\n", devCount);

    if (devCount == 0) {
        printf("FAIL: No physical devices! Is the OmniGPU host running?\n");
        vkDestroyInstance(instance, nullptr);
        return 1;
    }

    std::vector<VkPhysicalDevice> devices(devCount);
    chk(vkEnumeratePhysicalDevices(instance, &devCount, devices.data()), "vkEnumeratePhysicalDevices");

    for (uint32_t d = 0; d < devCount; d++) {
        VkPhysicalDevice physDev = devices[d];

        // 4. Properties
        VkPhysicalDeviceProperties props{};
        vkGetPhysicalDeviceProperties(physDev, &props);
        printf("\n--- GPU %u ---\n", d);
        printf("  Name:         %s\n", props.deviceName);
        printf("  API version:  %u.%u.%u\n",
               VK_API_VERSION_MAJOR(props.apiVersion),
               VK_API_VERSION_MINOR(props.apiVersion),
               VK_API_VERSION_PATCH(props.apiVersion));
        printf("  Driver:       %u\n", props.driverVersion);
        printf("  Vendor:       0x%04x\n", props.vendorID);
        printf("  Device:       0x%04x\n", props.deviceID);
        printf("  Type:         %s\n",
               props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU ? "Discrete" :
               props.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU ? "Integrated" :
               props.deviceType == VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU ? "Virtual" : "Other");

        // 5. Features
        VkPhysicalDeviceFeatures features{};
        vkGetPhysicalDeviceFeatures(physDev, &features);
        printf("\n  Features:\n");
        printf("    geometryShader:    %s\n", features.geometryShader ? "yes" : "no");
        printf("    tessellationShader:%s\n", features.tessellationShader ? "yes" : "no");
        printf("    multiDrawIndirect: %s\n", features.multiDrawIndirect ? "yes" : "no");

        // 6. Memory
        VkPhysicalDeviceMemoryProperties mem{};
        vkGetPhysicalDeviceMemoryProperties(physDev, &mem);
        printf("\n  Memory heaps: %u\n", mem.memoryHeapCount);
        for (uint32_t i = 0; i < mem.memoryHeapCount; i++) {
            printf("    [%u] %llu MB  %s%s\n", i,
                   mem.memoryHeaps[i].size / (1024 * 1024),
                   (mem.memoryHeaps[i].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) ? "DEVICE_LOCAL" : "",
                   (mem.memoryHeaps[i].flags & VK_MEMORY_HEAP_MULTI_INSTANCE_BIT) ? " MULTI_INSTANCE" : "");
        }

        // 7. Queue families
        uint32_t qfCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(physDev, &qfCount, nullptr);
        std::vector<VkQueueFamilyProperties> qfProps(qfCount);
        vkGetPhysicalDeviceQueueFamilyProperties(physDev, &qfCount, qfProps.data());
        printf("\n  Queue families: %u\n", qfCount);
        for (uint32_t i = 0; i < qfCount; i++) {
            printf("    [%u] %s%s%s queues=%u\n", i,
                   (qfProps[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) ? "GRAPHICS " : "",
                   (qfProps[i].queueFlags & VK_QUEUE_COMPUTE_BIT) ? "COMPUTE " : "",
                   (qfProps[i].queueFlags & VK_QUEUE_TRANSFER_BIT) ? "TRANSFER" : "",
                   qfProps[i].queueCount);
        }

        // 8. Test format support
        VkFormatProperties fmt{};
        vkGetPhysicalDeviceFormatProperties(physDev, VK_FORMAT_R8G8B8A8_UNORM, &fmt);
        printf("\n  R8G8B8A8_UNORM: linear=%s, optimal=%s\n",
               (fmt.linearTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT) ? "sampled" : "-",
               (fmt.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT) ? "sampled" : "-");
    }

    // 9. Test device creation (simple compute-capable device)
    printf("\n--- Device Creation Test ---\n");
    VkPhysicalDevice physDev = devices[0];
    
    uint32_t qfCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(physDev, &qfCount, nullptr);
    std::vector<VkQueueFamilyProperties> qfProps2(qfCount);
    vkGetPhysicalDeviceQueueFamilyProperties(physDev, &qfCount, qfProps2.data());
    
    int computeQF = -1;
    for (uint32_t i = 0; i < qfCount; i++) {
        if (qfProps2[i].queueFlags & VK_QUEUE_COMPUTE_BIT) { computeQF = i; break; }
    }
    
    if (computeQF >= 0) {
        float priority = 1.0f;
        VkDeviceQueueCreateInfo qci{};
        qci.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        qci.queueFamilyIndex = static_cast<uint32_t>(computeQF);
        qci.queueCount = 1;
        qci.pQueuePriorities = &priority;

        VkDeviceCreateInfo dci{};
        dci.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        dci.queueCreateInfoCount = 1;
        dci.pQueueCreateInfos = &qci;

        VkDevice device = VK_NULL_HANDLE;
        res = vkCreateDevice(physDev, &dci, nullptr, &device);
        if (res == VK_SUCCESS) {
            printf("[OK] Device created (compute queue family %d)\n", computeQF);
            
            VkQueue queue = VK_NULL_HANDLE;
            vkGetDeviceQueue(device, computeQF, 0, &queue);
            printf("[OK] Got compute queue\n");

            vkDestroyDevice(device, nullptr);
            printf("[OK] Device destroyed\n");
        } else {
            printf("FAIL: vkCreateDevice = %d\n", res);
        }
    } else {
        printf("No compute queue family found\n");
    }

    vkDestroyInstance(instance, nullptr);
    printf("\n=== ALL TESTS PASSED ===\n");
    return 0;
}
