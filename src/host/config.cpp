#include "config.h"
#include <fstream>
#include <limits>
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

        if (j.contains("port")) {
            const auto value = j["port"].get<int64_t>();
            if (value < 1 || value > 65535) {
                SPDLOG_ERROR("Invalid host port in '{}'", config.config_path);
                return false;
            }
            config.port = static_cast<uint16_t>(value);
        }
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
        if (j.contains("max_sessions")) {
            const auto value = j["max_sessions"].get<int64_t>();
            if (value < 1 || value > 65'535) {
                SPDLOG_ERROR("Invalid max_sessions in '{}'", config.config_path);
                return false;
            }
            config.max_sessions = static_cast<uint32_t>(value);
        }
        if (j.contains("max_msg_size_mb")) {
            const auto value = j["max_msg_size_mb"].get<int64_t>();
            if (value < 1 || value > 4'096) {
                SPDLOG_ERROR("Invalid max_msg_size_mb in '{}'", config.config_path);
                return false;
            }
            config.max_msg_size_mb = static_cast<uint32_t>(value);
        }
        if (j.contains("session_timeout_s")) {
            const auto value = j["session_timeout_s"].get<int64_t>();
            if (value < 0 || value > 604'800) {
                SPDLOG_ERROR("Invalid session_timeout_s in '{}'", config.config_path);
                return false;
            }
            config.session_timeout_s = static_cast<uint32_t>(value);
        }
        if (j.contains("per_session_memory_budget")) {
            const auto value = j["per_session_memory_budget"].get<int64_t>();
            if (value < 1 || value > (1LL << 40)) {
                SPDLOG_ERROR("Invalid per_session_memory_budget in '{}'", config.config_path);
                return false;
            }
            config.per_session_memory_budget = static_cast<uint64_t>(value);
        }

        if (config.jpeg_quality < 1 || config.jpeg_quality > 100) {
            SPDLOG_ERROR("Invalid jpeg_quality in '{}'", config.config_path);
            return false;
        }
        if (config.max_fps < 1 || config.max_fps > 1000 ||
            config.video_fps < 1 || config.video_fps > 1000) {
            SPDLOG_ERROR("Invalid frame rate in '{}'", config.config_path);
            return false;
        }
        constexpr uint32_t kMaxDimension = 32768;
        if (config.render_width == 0 || config.render_height == 0 ||
            config.video_width == 0 || config.video_height == 0 ||
            config.render_width > kMaxDimension ||
            config.render_height > kMaxDimension ||
            config.video_width > kMaxDimension ||
            config.video_height > kMaxDimension) {
            SPDLOG_ERROR("Invalid render/video dimensions in '{}'",
                         config.config_path);
            return false;
        }
        if (config.video_bitrate_kbps < 1 ||
            config.video_bitrate_kbps > 1'000'000 ||
            config.encoder.gop_length < 0) {
            SPDLOG_ERROR("Invalid encoder settings in '{}'",
                         config.config_path);
            return false;
        }
        if (config.auth_token.size() > 4096) {
            SPDLOG_ERROR("auth_token is unreasonably large in '{}'",
                         config.config_path);
            return false;
        }
        if (config.video_codec != "h264" && config.video_codec != "hevc" &&
            config.video_codec != "av1") {
            SPDLOG_ERROR("Unsupported video_codec in '{}'", config.config_path);
            return false;
        }
        if (config.encoder.preset.empty() || config.encoder.tuning.empty() ||
            config.encoder.preset.size() > 128 ||
            config.encoder.tuning.size() > 128) {
            SPDLOG_ERROR("Invalid encoder text settings in '{}'",
                         config.config_path);
            return false;
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
