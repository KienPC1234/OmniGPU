#include "config.h"
#include <fstream>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#include <limits.h>
#endif

namespace omnigpu {

static std::string exe_directory() {
#ifdef _WIN32
    char buf[MAX_PATH];
    GetModuleFileNameA(nullptr, buf, sizeof(buf));
    std::string path(buf);
    auto pos = path.find_last_of("\\/");
    return (pos != std::string::npos) ? path.substr(0, pos) : ".";
#else
    char buf[PATH_MAX];
    ssize_t len = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (len > 0) {
        buf[len] = '\0';
        std::string path(buf);
        auto pos = path.find_last_of('/');
        return (pos != std::string::npos) ? path.substr(0, pos) : ".";
    }
    return ".";
#endif
}

bool load_config(HostConfig& config) {
    std::string config_path = config.config_path;
    // Resolve relative path against executable directory
    if (config_path.find_first_of("\\/") == std::string::npos || config_path[0] == '.') {
        config_path = exe_directory() + "/" + config_path;
    }
    std::ifstream file(config_path);
    if (!file.is_open()) {
        SPDLOG_INFO("Config file '{}' not found, using defaults", config_path);
        return true;
    }

    nlohmann::json j = nlohmann::json::parse(file, nullptr, false);
    if (j.is_discarded()) {
        SPDLOG_WARN("Failed to parse '{}': invalid JSON, using defaults", config.config_path);
        return false;
    }

    if (j.contains("port")) config.port = j["port"].get<uint16_t>();
    if (j.contains("multi_gpu_enabled")) config.multi_gpu_enabled = j["multi_gpu_enabled"].get<bool>();

    // Security settings
    config.auth_token = j.value("auth_token", config.auth_token);
    config.max_sessions = j.value("max_sessions", config.max_sessions);
    config.max_msg_size_mb = j.value("max_msg_size_mb", config.max_msg_size_mb);
    config.session_timeout_s = j.value("session_timeout_s", config.session_timeout_s);
    config.per_session_memory_budget = j.value("per_session_memory_budget", config.per_session_memory_budget);

    SPDLOG_INFO("Loaded config from '{}'", config.config_path);
    return true;
}

void print_config(const HostConfig& config) {
    SPDLOG_INFO("=== Host Configuration ===");
    SPDLOG_INFO("  port              : {}", config.port);
    SPDLOG_INFO("  multi_gpu_enabled : {}", config.multi_gpu_enabled);
    SPDLOG_INFO("  security.auth_token: {}", config.auth_token.empty() ? "(none)" : "***set***");
    SPDLOG_INFO("  security.max_sessions: {}", config.max_sessions);
    SPDLOG_INFO("  security.max_msg_size_mb: {}", config.max_msg_size_mb);
    SPDLOG_INFO("  security.session_timeout_s: {}", config.session_timeout_s);
    SPDLOG_INFO("  security.memory_budget: {} MB", config.per_session_memory_budget / (1024*1024));
}

} // namespace omnigpu
