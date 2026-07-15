#include "loader.h"
#include <spdlog/spdlog.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#include <cstdlib>
#include <unistd.h>
#include <climits>
#endif

#include <array>

namespace omnigpu::loader {

namespace {

TranslationLayers g_layers;
bool g_initialized = false;

#ifdef _WIN32
std::string get_module_dir() {
    char path[MAX_PATH] = {};
    HMODULE mod = nullptr;
    if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            reinterpret_cast<LPCSTR>(&get_module_dir),
                            &mod)) {
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
#else
std::string get_module_dir() {
    Dl_info info;
    if (dladdr(reinterpret_cast<void*>(&get_module_dir), &info)) {
        std::string full(info.dli_fname);
        auto pos = full.find_last_of('/');
        if (pos != std::string::npos) {
            return full.substr(0, pos + 1);
        }
    }
    char buf[PATH_MAX] = {};
    if (getcwd(buf, sizeof(buf))) {
        return std::string(buf) + '/';
    }
    return {};
}
#endif

bool detect_zink(const std::string& dir) {
#ifdef _WIN32
    auto path = dir + "opengl32.dll";
    auto handle = LoadLibraryExA(path.c_str(), nullptr,
                                 DONT_RESOLVE_DLL_REFERENCES);
    if (handle) {
        FreeLibrary(handle);
        return true;
    }
    return false;
#else
    auto path = dir + "libGL.so";
    auto handle = dlopen(path.c_str(), RTLD_LAZY | RTLD_NOLOAD);
    if (handle) {
        dlclose(handle);
        return true;
    }
    return false;
#endif
}

bool detect_clvk(const std::string& dir) {
#ifdef _WIN32
    auto path = dir + "OpenCL.dll";
    auto handle = LoadLibraryExA(path.c_str(), nullptr,
                                 DONT_RESOLVE_DLL_REFERENCES);
    if (handle) {
        FreeLibrary(handle);
        return true;
    }
    return false;
#else
    auto path = dir + "libOpenCL.so";
    auto handle = dlopen(path.c_str(), RTLD_LAZY | RTLD_NOLOAD);
    if (handle) {
        dlclose(handle);
        return true;
    }
    return false;
#endif
}

bool setup_icd_manifest(const std::string& dir) {
    auto manifest_path = dir + "vk_icd.json";
#ifdef _WIN32
    // Respect externally-set VK_ICD_FILENAMES (e.g. from SSH test command,
    // launcher, or system-wide deployment)
    char existing[MAX_PATH] = {};
    DWORD ret = GetEnvironmentVariableA("VK_ICD_FILENAMES", existing,
                                        sizeof(existing));
    if (ret > 0 && ret < sizeof(existing)) {
        SPDLOG_INFO("VK_ICD_FILENAMES already set externally: {}",
                     existing);
        g_layers.icd_manifest_path = existing;
        return true;
    }
    if (!SetEnvironmentVariableA("VK_ICD_FILENAMES", manifest_path.c_str())) {
        SPDLOG_WARN("Failed to set VK_ICD_FILENAMES: {}",
                    GetLastError());
        return false;
    }
#else
    if (getenv("VK_ICD_FILENAMES")) {
        SPDLOG_INFO("VK_ICD_FILENAMES already set externally: {}",
                     getenv("VK_ICD_FILENAMES"));
        g_layers.icd_manifest_path = getenv("VK_ICD_FILENAMES");
        return true;
    }
    if (setenv("VK_ICD_FILENAMES", manifest_path.c_str(), 1) != 0) {
        SPDLOG_WARN("Failed to set VK_ICD_FILENAMES");
        return false;
    }
#endif
    g_layers.icd_manifest_path = manifest_path;
    return true;
}

} // namespace

bool initialize(bool enable_zink, bool enable_clvk) {
    if (g_initialized) return true;

    SPDLOG_INFO("Initializing OmniGPU translation layers...");

    auto runtime_dir = get_module_dir();
    g_layers.runtime_dir = runtime_dir;
    g_layers.zink_enabled = enable_zink;
    g_layers.clvk_enabled = enable_clvk;

    if (enable_zink) {
        g_layers.zink_available = detect_zink(runtime_dir);
    }
    if (enable_clvk) {
        g_layers.clvk_available = detect_clvk(runtime_dir);
    }

    g_layers.vulkan_icd_ready = setup_icd_manifest(runtime_dir);

    if (g_layers.zink_available && enable_zink) {
        SPDLOG_INFO("  [OK] Zink (OpenGL→Vulkan) detected");
    } else {
        SPDLOG_INFO("  [--] Zink (OpenGL→Vulkan) {}",
                    enable_zink ? "not found" : "disabled in config");
    }

    if (g_layers.clvk_available && enable_clvk) {
        SPDLOG_INFO("  [OK] clvk (OpenCL→Vulkan) detected");
    } else {
        SPDLOG_INFO("  [--] clvk (OpenCL→Vulkan) {}",
                    enable_clvk ? "not found" : "disabled in config");
    }

    if (g_layers.vulkan_icd_ready) {
        SPDLOG_INFO("  [OK] Vulkan ICD manifest set");
    } else {
        SPDLOG_WARN("  [!!] Failed to set Vulkan ICD manifest");
    }

    SPDLOG_INFO("Translation pipeline: {}",
                g_layers.vulkan_icd_ready
                    ? "OpenGL/OpenCL/Vulkan → omniGPU → Host GPU"
                    : "Vulkan → omniGPU → Host GPU (limited)");

    g_initialized = true;
    return true;
}

void shutdown() {
    if (!g_initialized) return;
    g_initialized = false;
    SPDLOG_INFO("OmniGPU translation layers shut down");
}

const TranslationLayers& get_layers() {
    return g_layers;
}

} // namespace omnigpu::loader
