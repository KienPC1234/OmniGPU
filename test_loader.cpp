#include <windows.h>
#include <iostream>
#include <string>

typedef void* VkInstance;
typedef struct VkApplicationInfo {
    int sType;
    const void* pNext;
    const char* pApplicationName;
    uint32_t applicationVersion;
    const char* pEngineName;
    uint32_t engineVersion;
    uint32_t apiVersion;
} VkApplicationInfo;

typedef struct VkInstanceCreateInfo {
    int sType;
    const void* pNext;
    int flags;
    const VkApplicationInfo* pApplicationInfo;
    uint32_t enabledLayerCount;
    const char* const* ppEnabledLayerNames;
    uint32_t enabledExtensionCount;
    const char* const* ppEnabledExtensionNames;
} VkInstanceCreateInfo;

typedef int (*PFN_vkCreateInstance)(const VkInstanceCreateInfo*, const void*, VkInstance*);

int main() {
    _putenv("VK_ICD_FILENAMES=C:\\Users\\test\\Downloads\\VkDiag\\vk_icd.json");
    _putenv("VK_LOADER_DEBUG=all");
    _putenv("VK_LOADER_DEBUG_LOG_PATH=C:\\Users\\test\\Downloads\\VkDiag\\my_loader.log");
    
    HMODULE hVul = LoadLibraryA("vulkan-1.dll");
    if (!hVul) {
        std::cerr << "Failed to load vulkan-1.dll\n";
        return 1;
    }
    std::cout << "Loaded vulkan-1.dll\n";
    
    PFN_vkCreateInstance vkCreateInstance = (PFN_vkCreateInstance)GetProcAddress(hVul, "vkCreateInstance");
    if (!vkCreateInstance) {
        std::cerr << "Failed to find vkCreateInstance\n";
        return 1;
    }
    
    VkApplicationInfo appInfo = {};
    appInfo.sType = 0; // VK_STRUCTURE_TYPE_APPLICATION_INFO
    appInfo.apiVersion = (1 << 22) | (3 << 12) | 0; // 1.3.0

    VkInstanceCreateInfo createInfo = {};
    createInfo.sType = 1; // VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO
    createInfo.pApplicationInfo = &appInfo;
    
    VkInstance inst = nullptr;
    int res = vkCreateInstance(&createInfo, nullptr, &inst);
    std::cout << "vkCreateInstance returned " << res << "\n";
    
    return 0;
}
