#include "guest_config.h"
#include <cstdlib>
#include <fstream>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#ifdef _WIN32
#include <windows.h>
#endif

namespace omnigpu::config {

using json = nlohmann::json;

const char* config_path() {
#ifdef _WIN32
    static std::string path;
    if (path.empty()) {
        char buf[MAX_PATH];
        if (GetModuleFileNameA(nullptr, buf, sizeof(buf))) {
            std::string full(buf);
            auto pos = full.find_last_of('\\');
            if (pos != std::string::npos) {
                path = full.substr(0, pos + 1) + "omnigpu_guest.json";
            }
        }
    }
    return path.c_str();
#else
    static std::string path;
    if (path.empty()) {
        const char* home = getenv("HOME");
        if (home) {
            path = std::string(home) + "/.config/omnigpu_guest.json";
        }
    }
    return path.c_str();
#endif
}

GuestConfig load(const std::string& path) {
    GuestConfig cfg;
    std::string cfg_path = path.empty() ? config_path() : path;

    std::ifstream file(cfg_path);
    if (!file.is_open()) {
        SPDLOG_DEBUG("Config file not found: {} (using defaults)", cfg_path);
        return cfg;
    }

    try {
        json j;
        file >> j;

        if (j.contains("host")) cfg.host = j["host"].get<std::string>();
        if (j.contains("port")) cfg.port = j["port"].get<uint16_t>();
        if (j.contains("cache_ttl_seconds")) cfg.cache_ttl_seconds = j["cache_ttl_seconds"].get<uint64_t>();
        if (j.contains("adaptive_batching")) cfg.adaptive_batching = j["adaptive_batching"].get<bool>();
        if (j.contains("max_batch_interval_ms")) cfg.max_batch_interval_ms = j["max_batch_interval_ms"].get<uint32_t>();

        SPDLOG_INFO("Loaded config from {}: host={}:{}, adaptive={}",
                    cfg_path, cfg.host, cfg.port,
                    cfg.adaptive_batching);
    } catch (const std::exception& e) {
        SPDLOG_WARN("Failed to parse config {}: {}", cfg_path, e.what());
    }

#ifdef _WIN32
    // Override with environment variables if set (global deployment)
    char env_buf[256] = {};
    DWORD env_ret = GetEnvironmentVariableA("OMNIGPU_HOST", env_buf, sizeof(env_buf));
    if (env_ret > 0 && env_ret < sizeof(env_buf)) {
        cfg.host = env_buf;
        SPDLOG_INFO("OMNIGPU_HOST env override: {}", cfg.host);
    }
    env_ret = GetEnvironmentVariableA("OMNIGPU_PORT", env_buf, sizeof(env_buf));
    if (env_ret > 0 && env_ret < sizeof(env_buf)) {
        cfg.port = static_cast<uint16_t>(std::atoi(env_buf));
        SPDLOG_INFO("OMNIGPU_PORT env override: {}", cfg.port);
    }
#else
    const char* env_host = std::getenv("OMNIGPU_HOST");
    if (env_host) {
        cfg.host = env_host;
        SPDLOG_INFO("OMNIGPU_HOST env override: {}", cfg.host);
    }
    const char* env_port = std::getenv("OMNIGPU_PORT");
    if (env_port) {
        cfg.port = static_cast<uint16_t>(std::atoi(env_port));
        SPDLOG_INFO("OMNIGPU_PORT env override: {}", cfg.port);
    }
#endif

    return cfg;
}

GuestConfig from_json_string(const std::string& json_str) {
    GuestConfig cfg;
    if (json_str.empty()) return cfg;

    try {
        json j = json::parse(json_str);

        if (j.contains("host")) cfg.host = j["host"].get<std::string>();
        if (j.contains("port")) cfg.port = j["port"].get<uint16_t>();
        if (j.contains("cache_ttl_seconds")) cfg.cache_ttl_seconds = j["cache_ttl_seconds"].get<uint64_t>();
        if (j.contains("adaptive_batching")) cfg.adaptive_batching = j["adaptive_batching"].get<bool>();
        if (j.contains("max_batch_interval_ms")) cfg.max_batch_interval_ms = j["max_batch_interval_ms"].get<uint32_t>();
        if (j.contains("min_batch_commands")) cfg.min_batch_commands = j["min_batch_commands"].get<uint32_t>();
        if (j.contains("max_batch_commands")) cfg.max_batch_commands = j["max_batch_commands"].get<uint32_t>();
        if (j.contains("min_batch_bytes")) cfg.min_batch_bytes = j["min_batch_bytes"].get<uint32_t>();
        if (j.contains("max_batch_bytes")) cfg.max_batch_bytes = j["max_batch_bytes"].get<uint32_t>();

        SPDLOG_INFO("Parsed config JSON: host={}:{}, adaptive={}",
                    cfg.host, cfg.port, cfg.adaptive_batching);
    } catch (const std::exception& e) {
        SPDLOG_WARN("Failed to parse config JSON: {}", e.what());
    }

    return cfg;
}

std::string to_json_string(const GuestConfig& cfg) {
    json j;
    j["host"] = cfg.host;
    j["port"] = cfg.port;
    j["cache_ttl_seconds"] = cfg.cache_ttl_seconds;
    j["adaptive_batching"] = cfg.adaptive_batching;
    j["max_batch_interval_ms"] = cfg.max_batch_interval_ms;
    j["min_batch_commands"] = cfg.min_batch_commands;
    j["max_batch_commands"] = cfg.max_batch_commands;
    j["min_batch_bytes"] = cfg.min_batch_bytes;
    j["max_batch_bytes"] = cfg.max_batch_bytes;
    return j.dump();
}

} // namespace omnigpu::config
