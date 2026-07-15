#define _CRT_SECURE_NO_WARNINGS

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#include <fileapi.h>
#else
#include <dlfcn.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/wait.h>
#endif

// ---------------------------------------------------------------------------
// Cross-platform helpers
// ---------------------------------------------------------------------------
static std::string get_launcher_dir() {
#ifdef _WIN32
    char path[MAX_PATH] = {};
    HMODULE mod = nullptr;
    GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                           GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       reinterpret_cast<LPCSTR>(&get_launcher_dir), &mod);
    GetModuleFileNameA(mod, path, sizeof(path));
    std::string full(path);
    auto pos = full.find_last_of('\\');
    return (pos != std::string::npos) ? full.substr(0, pos + 1) : full + '\\';
#else
    return "./";
#endif
}

static bool file_exists(const std::string& path) {
#ifdef _WIN32
    DWORD attr = GetFileAttributesA(path.c_str());
    return attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY);
#else
    struct stat st;
    return stat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode);
#endif
}

static bool copy_file(const std::string& src, const std::string& dst) {
#ifdef _WIN32
    return CopyFileA(src.c_str(), dst.c_str(), FALSE) != 0;
#else
    // Simple file copy using system commands
    std::string cmd = "cp \"" + src + "\" \"" + dst + "\"";
    return system(cmd.c_str()) == 0;
#endif
}

static bool set_env(const std::string& name, const std::string& value) {
#ifdef _WIN32
    return SetEnvironmentVariableA(name.c_str(), value.c_str()) != 0;
#else
    return setenv(name.c_str(), value.c_str(), 1) == 0;
#endif
}

// ---------------------------------------------------------------------------
// Windows registry helpers for global ICD registration
// ---------------------------------------------------------------------------
#ifdef _WIN32
static bool reg_set_dword(HKEY root, const char* subkey, const char* name,
                           DWORD value) {
    HKEY hkey = nullptr;
    LONG ret = RegCreateKeyExA(root, subkey, 0, nullptr,
                                REG_OPTION_NON_VOLATILE,
                                KEY_SET_VALUE, nullptr, &hkey, nullptr);
    if (ret != ERROR_SUCCESS) return false;
    ret = RegSetValueExA(hkey, name, 0, REG_DWORD,
                          reinterpret_cast<const BYTE*>(&value), sizeof(value));
    RegCloseKey(hkey);
    return ret == ERROR_SUCCESS;
}

static bool register_icd_global(const std::string& manifest_path) {
    const char* key = "SOFTWARE\\Khronos\\Vulkan\\Drivers";
    const char* key32 = "SOFTWARE\\WOW6432Node\\Khronos\\Vulkan\\Drivers";

    bool hkcu = reg_set_dword(HKEY_CURRENT_USER, key, manifest_path.c_str(), 0);
    bool hkcu32 = reg_set_dword(HKEY_CURRENT_USER, key32,
                                 manifest_path.c_str(), 0);

    std::cout << "  [OK] Registered ICD in HKCU registry (global discovery)\n";
    return hkcu || hkcu32;
}

// Register VulkanDriverName under the first active GPU controller key
// so tools like VkDiag detect this as a proper Vulkan driver.
static bool register_vulkan_driver_name(const std::string& manifest_path_64,
                                         const std::string& manifest_path_32) {
    const char* base_key = "SYSTEM\\CurrentControlSet\\Control\\Video";
    HKEY hkBase = nullptr;
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, base_key, 0,
                       KEY_ENUMERATE_SUB_KEYS, &hkBase) != ERROR_SUCCESS) {
        return false;
    }

    char guid[64] = {};
    DWORD guid_size = sizeof(guid);
    int idx = 0;
    bool found = false;

    while (RegEnumKeyExA(hkBase, idx, guid, &guid_size, nullptr, nullptr,
                          nullptr, nullptr) == ERROR_SUCCESS) {
        char subkey[256];
        HKEY hkOutput = nullptr;

        // Check for 0000 subkey (active GPU output)
        snprintf(subkey, sizeof(subkey), "%s\\%s\\0000", base_key, guid);
        bool has_output = RegOpenKeyExA(HKEY_LOCAL_MACHINE, subkey, 0,
                                         KEY_QUERY_VALUE | KEY_SET_VALUE,
                                         &hkOutput) == ERROR_SUCCESS;
        if (!has_output) {
            guid_size = sizeof(guid);
            guid[0] = 0;
            idx++;
            continue;
        }

        // Append to existing VulkanDriverName (REG_MULTI_SZ)
        DWORD type = 0;
        DWORD exist_size = 0;
        std::vector<char> buffer;
        if (RegQueryValueExA(hkOutput, "VulkanDriverName", nullptr, &type,
                              nullptr, &exist_size) == ERROR_SUCCESS &&
            type == REG_MULTI_SZ) {
            buffer.resize(exist_size);
            RegQueryValueExA(hkOutput, "VulkanDriverName", nullptr, &type,
                              reinterpret_cast<BYTE*>(buffer.data()),
                              &exist_size);
        }

        // Check if already exists in multi-sz
        bool exists = false;
        if (!buffer.empty()) {
            const char* p = buffer.data();
            const char* end = buffer.data() + buffer.size();
            while (p < end && *p != '\0') {
                std::string s(p);
                if (s == manifest_path_64) {
                    exists = true;
                    break;
                }
                p += s.length() + 1;
            }
        }

        LONG ret = ERROR_SUCCESS;
        if (!exists) {
            if (buffer.size() >= 2) {
                if (buffer.back() == '\0') buffer.pop_back();
                if (!buffer.empty() && buffer.back() != '\0') buffer.push_back('\0');
                buffer.insert(buffer.end(), manifest_path_64.begin(), manifest_path_64.end());
                buffer.push_back('\0');
                buffer.push_back('\0');
            } else {
                buffer.clear();
                buffer.insert(buffer.end(), manifest_path_64.begin(), manifest_path_64.end());
                buffer.push_back('\0');
                buffer.push_back('\0');
            }
            ret = RegSetValueExA(hkOutput, "VulkanDriverName", 0,
                                 REG_MULTI_SZ,
                                 reinterpret_cast<const BYTE*>(buffer.data()),
                                 static_cast<DWORD>(buffer.size()));
        }
        RegCloseKey(hkOutput);

        if (ret == ERROR_SUCCESS) {
            std::cout << "  [OK] Registered VulkanDriverName on GPU " << guid
                      << "\n";
            found = true;
        }
        break; // only modify the first suitable GPU
    }

    RegCloseKey(hkBase);
    return found;
}
#endif

// ---------------------------------------------------------------------------
// Launcher logic
// ---------------------------------------------------------------------------
static void print_usage(const char* prog) {
    std::cerr << "OmniGPU Launcher v0.1.0\n"
              << "Usage: " << prog << " <game.exe> [game args...]\n\n"
              << "Launches a game/application through the OmniGPU translation layer.\n"
              << "Automatically sets up Zink (OpenGL → Vulkan), clvk (OpenCL → Vulkan),\n"
              << "and the OmniGPU guest Vulkan ICD driver.\n\n"
              << "Environment variables:\n"
              << "  OMNIGPU_HOST  – Host address (default: 127.0.0.1)\n"
              << "  OMNIGPU_PORT  – Host port    (default: 9443)\n";
}

struct LaunchConfig {
    std::string game_path;
    std::string game_dir;
    std::string runtime_dir;
    std::vector<std::string> game_args;
};

static bool resolve_config(LaunchConfig& cfg, int argc, char* argv[]) {
    if (argc < 2) {
        print_usage(argv[0]);
        return false;
    }

    cfg.game_path = argv[1];
    cfg.runtime_dir = get_launcher_dir();

    // Resolve game directory
    auto pos = cfg.game_path.find_last_of("\\/");
    cfg.game_dir = (pos != std::string::npos)
                       ? cfg.game_path.substr(0, pos + 1)
                       : std::string();

    // Remaining args are for the game
    for (int i = 2; i < argc; ++i) {
        cfg.game_args.push_back(argv[i]);
    }

    return true;
}

static bool deploy_translation_layers(const LaunchConfig& cfg) {
    // Zink: opengl32.dll → game directory
    std::string zink_src = cfg.runtime_dir + "opengl32.dll";
    if (file_exists(zink_src)) {
        std::string zink_dst = cfg.game_dir + "opengl32.dll";
        if (!file_exists(zink_dst)) {
            if (!copy_file(zink_src, zink_dst)) {
                std::cerr << "Warning: Failed to deploy Zink (opengl32.dll)\n";
            } else {
                std::cout << "  [OK] Zink deployed to " << zink_dst << "\n";
            }
        } else {
            std::cout << "  [--] Zink already present\n";
        }
    } else {
        std::cout << "  [--] Zink not found at " << zink_src << "\n";
    }

    // clvk: OpenCL.dll → game directory
    std::string clvk_src = cfg.runtime_dir + "OpenCL.dll";
    if (file_exists(clvk_src)) {
        std::string clvk_dst = cfg.game_dir + "OpenCL.dll";
        if (!file_exists(clvk_dst)) {
            if (!copy_file(clvk_src, clvk_dst)) {
                std::cerr << "Warning: Failed to deploy clvk (OpenCL.dll)\n";
            } else {
                std::cout << "  [OK] clvk deployed to " << clvk_dst << "\n";
            }
        } else {
            std::cout << "  [--] clvk already present\n";
        }
    } else {
        std::cout << "  [--] clvk not found at " << clvk_src << "\n";
    }

    return true;
}

static bool launch_game(const LaunchConfig& cfg) {
    std::cout << "\nStarting: " << cfg.game_path << "\n\n";

    // Set VK_ICD_FILENAMES to our manifest
    std::string icd_manifest = cfg.runtime_dir + "vk_icd.json";
    if (!file_exists(icd_manifest)) {
        std::cerr << "Error: ICD manifest not found at " << icd_manifest << "\n";
        return false;
    }
    set_env("VK_ICD_FILENAMES", icd_manifest);

#ifdef _WIN32
    // Register ICD globally in HKCU so future Vulkan apps find it
    // without needing the launcher. No admin required for HKCU.
    register_icd_global(icd_manifest);

    // Also register VulkanDriverName under GPU controller key
    // (admin required, silently fails otherwise). This makes
    // VkDiag show "proper Vulkan driver" status.
    register_vulkan_driver_name(icd_manifest, icd_manifest);
#endif

    // Show environment
    const char* host = std::getenv("OMNIGPU_HOST");
    const char* port = std::getenv("OMNIGPU_PORT");
    std::cout << "  OMNIGPU_HOST      = " << (host ? host : "127.0.0.1 (default)") << "\n";
    std::cout << "  OMNIGPU_PORT      = " << (port ? port : "9443 (default)") << "\n";
    std::cout << "  VK_ICD_FILENAMES  = " << icd_manifest << "\n";

#ifdef _WIN32
    // Build command line
    std::string cmdline = "\"" + cfg.game_path + "\"";
    for (auto& arg : cfg.game_args) {
        cmdline += " \"" + arg + "\"";
    }

    STARTUPINFOA si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};

    if (!CreateProcessA(
            cfg.game_path.c_str(),
            &cmdline[0],
            nullptr, nullptr, FALSE,
            CREATE_DEFAULT_ERROR_MODE,
            nullptr,
            cfg.game_dir.empty() ? nullptr : cfg.game_dir.c_str(),
            &si, &pi)) {
        std::cerr << "Error: CreateProcess failed (error " << GetLastError() << ")\n";
        return false;
    }

    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);

    std::cout << "\nGame launched. PID: " << pi.dwProcessId << "\n";
    return true;
#else
    pid_t pid = fork();
    if (pid == 0) {
        // Child process
        if (!cfg.game_dir.empty()) {
            chdir(cfg.game_dir.c_str());
        }

        std::vector<const char*> args;
        args.push_back(cfg.game_path.c_str());
        for (auto& a : cfg.game_args) args.push_back(a.c_str());
        args.push_back(nullptr);

        execvp(cfg.game_path.c_str(), const_cast<char* const*>(args.data()));
        std::cerr << "Error: execvp failed\n";
        _exit(1);
    } else if (pid > 0) {
        std::cout << "\nGame launched. PID: " << pid << "\n";
        int status;
        waitpid(pid, &status, 0);
        std::cout << "Game exited with status " << WEXITSTATUS(status) << "\n";
    } else {
        std::cerr << "Error: fork failed\n";
        return false;
    }
    return true;
#endif
}

// ---------------------------------------------------------------------------
// Entry point
// ---------------------------------------------------------------------------
int main(int argc, char* argv[]) {
    LaunchConfig cfg;
    if (!resolve_config(cfg, argc, argv)) {
        return 1;
    }

    std::cout << "OmniGPU Launcher v0.1.0\n";
    std::cout << "Runtime dir: " << cfg.runtime_dir << "\n";
    std::cout << "Game dir:    " << cfg.game_dir << "\n\n";

    std::cout << "Deploying translation layers...\n";
    deploy_translation_layers(cfg);

    if (!launch_game(cfg)) {
        return 1;
    }

    return 0;
}
