#include "config.h"
#include <fstream>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

namespace omnigpu {

bool load_config(HostConfig& config) {
    std::ifstream file(config.config_path);
    if (!file.is_open()) {
        SPDLOG_INFO("Config file '{}' not found, using defaults", config.config_path);
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

        if (j.contains("nvenc")) {
            auto& n = j["nvenc"];
            if (n.contains("preset")) config.nvenc.preset = n["preset"].get<std::string>();
            if (n.contains("tuning")) config.nvenc.tuning = n["tuning"].get<std::string>();
            if (n.contains("gop_length")) config.nvenc.gop_length = n["gop_length"].get<int>();
        }

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
    SPDLOG_INFO("  nvenc.preset      : {}", config.nvenc.preset);
    SPDLOG_INFO("  nvenc.tuning      : {}", config.nvenc.tuning);
    SPDLOG_INFO("  nvenc.gop_length  : {}", config.nvenc.gop_length);
}

} // namespace omnigpu
