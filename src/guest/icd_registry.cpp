#include "icd_registry.h"
#include <cstdio>
#include <spdlog/spdlog.h>

#ifdef _WIN32
#include <windows.h>
#endif

namespace omnigpu::icd_registry {

#ifdef _WIN32

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------
std::string get_module_dir() {
    char path[MAX_PATH] = {};
    HMODULE mod = nullptr;
    if (!GetModuleHandleExA(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCSTR>(&get_module_dir), &mod)) {
        return {};
    }
    GetModuleFileNameA(mod, path, sizeof(path));
    std::string full(path);
    auto pos = full.find_last_of('\\');
    if (pos != std::string::npos) {
        return full.substr(0, pos + 1);
    }
    return full + '\\';
}

std::string get_manifest_path() {
    return get_module_dir() + "vk_icd.json";
}

static bool set_registry_value(HKEY root, const char* subkey, const char* name,
                                DWORD value) {
    HKEY hkey = nullptr;
    LONG ret = RegCreateKeyExA(root, subkey, 0, nullptr,
                                REG_OPTION_NON_VOLATILE,
                                KEY_SET_VALUE, nullptr, &hkey, nullptr);
    if (ret != ERROR_SUCCESS) {
        SPDLOG_DEBUG("RegCreateKeyEx({}) failed: {}", subkey, ret);
        return false;
    }
    ret = RegSetValueExA(hkey, name, 0, REG_DWORD,
                          reinterpret_cast<const BYTE*>(&value), sizeof(value));
    RegCloseKey(hkey);
    if (ret != ERROR_SUCCESS) {
        SPDLOG_DEBUG("RegSetValueEx({}) failed: {}", name, ret);
        return false;
    }
    return true;
}

static bool delete_registry_value(HKEY root, const char* subkey,
                                   const char* name) {
    HKEY hkey = nullptr;
    LONG ret = RegOpenKeyExA(root, subkey, 0, KEY_SET_VALUE, &hkey);
    if (ret != ERROR_SUCCESS) {
        return false;
    }
    ret = RegDeleteValueA(hkey, name);
    RegCloseKey(hkey);
    return ret == ERROR_SUCCESS;
}

static bool registry_value_exists(const char* subkey, const char* name) {
    HKEY hkey = nullptr;
    LONG ret = RegOpenKeyExA(HKEY_LOCAL_MACHINE, subkey, 0,
                              KEY_QUERY_VALUE, &hkey);
    if (ret == ERROR_SUCCESS) {
        DWORD type = 0;
        DWORD data = 0;
        DWORD data_size = sizeof(data);
        ret = RegQueryValueExA(hkey, name, nullptr, &type,
                                reinterpret_cast<BYTE*>(&data), &data_size);
        RegCloseKey(hkey);
        if (ret == ERROR_SUCCESS) return true;
    }

    ret = RegOpenKeyExA(HKEY_CURRENT_USER, subkey, 0,
                         KEY_QUERY_VALUE, &hkey);
    if (ret == ERROR_SUCCESS) {
        DWORD type = 0;
        DWORD data = 0;
        DWORD data_size = sizeof(data);
        ret = RegQueryValueExA(hkey, name, nullptr, &type,
                                reinterpret_cast<BYTE*>(&data), &data_size);
        RegCloseKey(hkey);
        if (ret == ERROR_SUCCESS) return true;
    }
    return false;
}

#endif // _WIN32

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

bool register_icd() {
#ifdef _WIN32
    std::string manifest = get_manifest_path();
    if (manifest.empty()) {
        SPDLOG_WARN("Cannot register ICD: could not determine manifest path");
        return false;
    }

    const char* drivers_key = "SOFTWARE\\Khronos\\Vulkan\\Drivers";

    // Write to HKCU (no admin required). Both 32-bit and 64-bit Vulkan
    // loaders check this key, so one entry covers both.
    bool ok = set_registry_value(HKEY_CURRENT_USER, drivers_key,
                                  manifest.c_str(), 0);

    // Also write to WOW6432Node HKLM if we have admin (optional)
    // but HKCU is sufficient for per-user global discovery.
    if (ok) {
        SPDLOG_INFO("Registered OmniGPU ICD in HKCU\\{}", drivers_key);
        SPDLOG_INFO("  Manifest: {}", manifest);
    } else {
        SPDLOG_WARN("Failed to register ICD in HKCU\\{}", drivers_key);
    }

    return ok;
#else
    // Linux: no registry; ICD is discovered via /usr/share/vulkan/icd.d/
    // This is handled by the install script / package manager.
    return true;
#endif
}

bool register_icd_both(const char* manifest_path_64,
                        const char* manifest_path_32) {
#ifdef _WIN32
    const char* key64 = "SOFTWARE\\Khronos\\Vulkan\\Drivers";
    const char* key32 = "SOFTWARE\\WOW6432Node\\Khronos\\Vulkan\\Drivers";

    bool ok64 = false;
    bool ok32 = false;

    // HKLM (requires admin)
    ok64 = set_registry_value(HKEY_LOCAL_MACHINE, key64, manifest_path_64, 0);
    ok32 = set_registry_value(HKEY_LOCAL_MACHINE, key32, manifest_path_32, 0);

    // HKCU (no admin needed) — also write both views
    set_registry_value(HKEY_CURRENT_USER, key64, manifest_path_64, 0);
    set_registry_value(HKEY_CURRENT_USER, key32, manifest_path_32, 0);

    if (ok64 && ok32) {
        SPDLOG_INFO("Registered OmniGPU ICD (both architectures)");
        return true;
    }
    SPDLOG_WARN("Failed to register one or both ICD architecture entries");
    return false;
#else
    (void)manifest_path_64;
    (void)manifest_path_32;
    return true;
#endif
}

bool unregister_icd() {
#ifdef _WIN32
    std::string manifest = get_manifest_path();
    if (manifest.empty()) return false;

    const char* drivers_key = "SOFTWARE\\Khronos\\Vulkan\\Drivers";
    bool ok = delete_registry_value(HKEY_CURRENT_USER, drivers_key,
                                     manifest.c_str());
    if (ok) {
        SPDLOG_INFO("Unregistered OmniGPU ICD from HKCU\\{}", drivers_key);
    }
    return ok;
#else
    return true;
#endif
}

bool is_icd_registered() {
#ifdef _WIN32
    std::string manifest = get_manifest_path();
    if (manifest.empty()) return false;
    return registry_value_exists("SOFTWARE\\Khronos\\Vulkan\\Drivers",
                                  manifest.c_str());
#else
    return false;
#endif
}

// ---------------------------------------------------------------------------
// Register VulkanDriverName under the first compatible GPU controller key
// so VkDiag and similar tools see this as a "proper" Vulkan driver.
// Requires admin (HKLM write).
// ---------------------------------------------------------------------------
#ifdef _WIN32
bool register_vulkan_driver_name(const char* manifest_path_64,
                                  const char* manifest_path_32) {
    const char* base_key = "SYSTEM\\CurrentControlSet\\Control\\Video";

    HKEY hkBase = nullptr;
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, base_key, 0,
                       KEY_ENUMERATE_SUB_KEYS, &hkBase) != ERROR_SUCCESS) {
        SPDLOG_WARN("Cannot enumerate Video controller keys (not admin?)");
        return false;
    }

    // Find the first GPU GUID with both Video and 0000 subkeys
    char guid[MAX_PATH] = {};
    DWORD guid_size = sizeof(guid);
    int idx = 0;
    while (RegEnumKeyExA(hkBase, idx, guid, &guid_size, nullptr, nullptr,
                          nullptr, nullptr) == ERROR_SUCCESS) {
        // Check for Video subkey
        char video_path[MAX_PATH];
        snprintf(video_path, sizeof(video_path), "%s\\%s\\Video", base_key,
                 guid);
        HKEY hkVideo = nullptr;
        bool has_video =
            RegOpenKeyExA(HKEY_LOCAL_MACHINE, video_path, 0,
                           KEY_QUERY_VALUE, &hkVideo) == ERROR_SUCCESS;
        if (has_video)
            RegCloseKey(hkVideo);

        // Check for 0000 subkey (active output)
        char output_path[MAX_PATH];
        snprintf(output_path, sizeof(output_path), "%s\\%s\\0000", base_key,
                 guid);
        HKEY hkOutput = nullptr;
        bool has_output =
            RegOpenKeyExA(HKEY_LOCAL_MACHINE, output_path, 0,
                           KEY_QUERY_VALUE | KEY_SET_VALUE,
                           &hkOutput) == ERROR_SUCCESS;

        if (has_video && has_output) {
            // Found a suitable GPU — add our VulkanDriverName entries
            bool wrote_64 = false;
            bool wrote_32 = false;

            auto append_multi_sz = [](HKEY hKey, const char* value_name, const std::string& path) -> bool {
                DWORD type = 0;
                DWORD size = 0;
                std::vector<char> buffer;
                if (RegQueryValueExA(hKey, value_name, nullptr, &type, nullptr, &size) == ERROR_SUCCESS && type == REG_MULTI_SZ) {
                    buffer.resize(size);
                    RegQueryValueExA(hKey, value_name, nullptr, &type, reinterpret_cast<BYTE*>(buffer.data()), &size);
                }

                // Check if already exists in multi-sz
                bool exists = false;
                if (!buffer.empty()) {
                    const char* p = buffer.data();
                    const char* end = buffer.data() + buffer.size();
                    while (p < end && *p != '\0') {
                        std::string s(p);
                        if (s == path) {
                            exists = true;
                            break;
                        }
                        p += s.length() + 1;
                    }
                }

                if (!exists) {
                    if (buffer.size() >= 2) {
                        if (buffer.back() == '\0') buffer.pop_back();
                        if (!buffer.empty() && buffer.back() != '\0') buffer.push_back('\0');
                        buffer.insert(buffer.end(), path.begin(), path.end());
                        buffer.push_back('\0');
                        buffer.push_back('\0');
                    } else {
                        buffer.clear();
                        buffer.insert(buffer.end(), path.begin(), path.end());
                        buffer.push_back('\0');
                        buffer.push_back('\0');
                    }
                    LONG ret = RegSetValueExA(hKey, value_name, 0, REG_MULTI_SZ,
                                              reinterpret_cast<const BYTE*>(buffer.data()),
                                              static_cast<DWORD>(buffer.size()));
                    return ret == ERROR_SUCCESS;
                }
                return true; // Already exists is success
            };

            wrote_64 = append_multi_sz(hkOutput, "VulkanDriverName", manifest_path_64);

            if (manifest_path_32 && manifest_path_32[0]) {
                wrote_32 = append_multi_sz(hkOutput, "VulkanDriverNameWoW", manifest_path_32);
            } else {
                wrote_32 = true;
            }

            RegCloseKey(hkOutput);
            RegCloseKey(hkBase);

            if (wrote_64) {
                SPDLOG_INFO("Registered VulkanDriverName on GPU {}: {}",
                             guid, manifest_path_64);
                if (wrote_32 && manifest_path_32)
                    SPDLOG_INFO("Registered VulkanDriverNameWoW on GPU {}: {}",
                                 guid, manifest_path_32);
                return true;
            }
            SPDLOG_WARN("Failed to write VulkanDriverName on GPU {}", guid);
            return false;
        }

        if (has_output)
            RegCloseKey(hkOutput);

        guid_size = sizeof(guid);
        guid[0] = '\0';
        idx++;
    }

    RegCloseKey(hkBase);
    SPDLOG_WARN("No suitable GPU controller found for VulkanDriverName");
    return false;
}
#endif

} // namespace omnigpu::icd_registry
