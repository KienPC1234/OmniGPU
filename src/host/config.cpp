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

    try {
        nlohmann::json j;
        file >> j;

        if (j.contains("port")) config.port = j["port"].get<uint16_t>();
        if (j.contains("jpeg_quality")) config.jpeg_quality = j["jpeg_quality"].get<int>();
        if (j.contains("multi_gpu_enabled")) config.multi_gpu_enabled = j["multi_gpu_enabled"].get<bool>();
        if (j.contains("max_fps")) config.max_fps = j["max_fps"].get<int>();
        if (j.contains("render_width")) config.render_width = j["render_width"].get<uint32_t>();
        if (j.contains("render_height")) config.render_height = j["render_height"].get<uint32_t>();

        if (j.contains("video_codec")) config.video_codec = j["video_codec"].get<std::string>();
        if (j.contains("video_bitrate_kbps")) config.video_bitrate_kbps = j["video_bitrate_kbps"].get<int>();
        if (j.contains("video_fps")) config.video_fps = j["video_fps"].get<int>();
        if (j.contains("video_width")) config.video_width = j["video_width"].get<uint32_t>();
        if (j.contains("video_height")) config.video_height = j["video_height"].get<uint32_t>();

        auto enc = j.value("encoder", nlohmann::json::object());
        config.encoder.preset = enc.value("preset", config.encoder.preset);
        config.encoder.tuning = enc.value("tuning", config.encoder.tuning);
        config.encoder.gop_length = enc.value("gop_length", config.encoder.gop_length);

        // Security settings
        config.auth_token = j.value("auth_token", config.auth_token);
        config.max_sessions = j.value("max_sessions", config.max_sessions);
        config.max_msg_size_mb = j.value("max_msg_size_mb", config.max_msg_size_mb);
        config.session_timeout_s = j.value("session_timeout_s", config.session_timeout_s);
        config.per_session_memory_budget = j.value("per_session_memory_budget", config.per_session_memory_budget);

        SPDLOG_INFO("Loaded config from '{}'", config.config_path);
        return true;
    } catch (const std::exception& e) {
        SPDLOG_WARN("Failed to parse '{}': {}, using defaults", config.config_path, e.what());
        return false;
    }
}

void print_config(const HostConfig& config) {
    SPDLOG_INFO("=== Host Configuration ===");
    SPDLOG_INFO("  port              : {}", config.port);
    SPDLOG_INFO("  jpeg_quality      : {}", config.jpeg_quality);
    SPDLOG_INFO("  multi_gpu_enabled : {}", config.multi_gpu_enabled);
    SPDLOG_INFO("  max_fps           : {}", config.max_fps);
    SPDLOG_INFO("  render_width      : {}", config.render_width);
    SPDLOG_INFO("  render_height     : {}", config.render_height);
    SPDLOG_INFO("  video_codec       : {}", config.video_codec);
    SPDLOG_INFO("  video_bitrate     : {} kbps", config.video_bitrate_kbps);
    SPDLOG_INFO("  video_fps         : {}", config.video_fps);
    SPDLOG_INFO("  video_resolution  : {}x{}", config.video_width, config.video_height);
    SPDLOG_INFO("  encoder.preset    : {}", config.encoder.preset);
    SPDLOG_INFO("  encoder.tuning    : {}", config.encoder.tuning);
    SPDLOG_INFO("  encoder.gop_length: {}", config.encoder.gop_length);
    SPDLOG_INFO("  security.auth_token: {}", config.auth_token.empty() ? "(none)" : "***set***");
    SPDLOG_INFO("  security.max_sessions: {}", config.max_sessions);
    SPDLOG_INFO("  security.max_msg_size_mb: {}", config.max_msg_size_mb);
    SPDLOG_INFO("  security.session_timeout_s: {}", config.session_timeout_s);
    SPDLOG_INFO("  security.memory_budget: {} MB", config.per_session_memory_budget / (1024*1024));
}

} // namespace omnigpu
