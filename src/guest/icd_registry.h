#pragma once

#include <cstdint>
#include <string>

namespace omnigpu::icd_registry {

bool register_icd();
bool register_icd_both(const char* manifest_path_64, const char* manifest_path_32);
bool unregister_icd();
bool is_icd_registered();
std::string get_manifest_path();
std::string get_module_dir();

#ifdef _WIN32
// Register VulkanDriverName under the first compatible GPU controller key
// so tools like VkDiag detect this as a "proper" Vulkan driver.
// This writes to both 64-bit and 32-bit (WoW) registration.
bool register_vulkan_driver_name(const char* manifest_path_64,
                                  const char* manifest_path_32);
#endif

} // namespace omnigpu::icd_registry
