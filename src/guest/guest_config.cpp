#include "guest_config.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <limits>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#ifdef _WIN32
#include <windows.h>
#endif

namespace omnigpu::config {
namespace {

using json = nlohmann::json;
namespace fs = std::filesystem;

std::string trim(const std::string& value) {
    const auto start = value.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return {};
    const auto end = value.find_last_not_of(" \t\r\n");
    return value.substr(start, end - start + 1);
}

bool exists_regular_file(const std::string& path) {
    std::error_code error;
    return !path.empty() && fs::is_regular_file(fs::path(path), error);
}

std::string default_config_path() {
    if (const char* explicit_path = std::getenv("OMNIGPU_CONFIG")) {
        const std::string path = trim(explicit_path);
        if (!path.empty()) return path;
    }

#ifdef _WIN32
    char module_path[MAX_PATH] = {};
    if (GetModuleFileNameA(nullptr, module_path, sizeof(module_path)) != 0) {
        fs::path path(module_path);
        return (path.parent_path() / "omnigpu_guest.json").string();
    }
    return "omnigpu_guest.json";
#else
    std::string user_path;
    if (const char* xdg = std::getenv("XDG_CONFIG_HOME")) {
        if (xdg[0] != '\0') {
            user_path = (fs::path(xdg) / "omnigpu" / "omnigpu_guest.json").string();
        }
    }
    if (user_path.empty()) {
        if (const char* home = std::getenv("HOME")) {
            if (home[0] != '\0') {
                user_path = (fs::path(home) / ".config" / "omnigpu" /
                             "omnigpu_guest.json").string();
            }
        }
    }

    if (exists_regular_file(user_path)) return user_path;
    if (exists_regular_file("/etc/omnigpu/omnigpu_guest.json")) {
        return "/etc/omnigpu/omnigpu_guest.json";
    }
    return !user_path.empty() ? user_path : "/etc/omnigpu/omnigpu_guest.json";
#endif
}

uint64_t bounded_unsigned(const json& values, const char* key,
                          uint64_t minimum, uint64_t maximum) {
    const auto& value = values.at(key);
    uint64_t parsed = 0;
    if (value.is_number_unsigned()) {
        parsed = value.get<uint64_t>();
    } else if (value.is_number_integer()) {
        const int64_t signed_value = value.get<int64_t>();
        if (signed_value < 0) throw std::out_of_range(key);
        parsed = static_cast<uint64_t>(signed_value);
    } else {
        throw std::invalid_argument(std::string(key) + " must be an integer");
    }
    if (parsed < minimum || parsed > maximum) throw std::out_of_range(key);
    return parsed;
}

void apply_json(GuestConfig& cfg, const json& values) {
    if (!values.is_object()) throw std::invalid_argument("configuration must be a JSON object");

    // Parse into a copy so one malformed late field cannot leave a partially
    // updated configuration behind.
    GuestConfig updated = cfg;
    if (values.contains("host")) {
        updated.host = trim(values.at("host").get<std::string>());
        if (updated.host.empty()) throw std::invalid_argument("host must not be empty");
    }
    if (values.contains("port")) {
        updated.port = static_cast<uint16_t>(bounded_unsigned(
            values, "port", 1, std::numeric_limits<uint16_t>::max()));
    }
    if (values.contains("cache_ttl_seconds")) {
        updated.cache_ttl_seconds = bounded_unsigned(
            values, "cache_ttl_seconds", 0, std::numeric_limits<uint64_t>::max());
    }
    if (values.contains("auth_token")) {
        updated.auth_token = values.at("auth_token").get<std::string>();
    }
    if (values.contains("adaptive_batching")) {
        updated.adaptive_batching = values.at("adaptive_batching").get<bool>();
    }
    if (values.contains("max_batch_interval_ms")) {
        updated.max_batch_interval_ms = static_cast<uint32_t>(bounded_unsigned(
            values, "max_batch_interval_ms", 1,
            std::numeric_limits<uint32_t>::max()));
    }
    if (values.contains("min_batch_commands")) {
        updated.min_batch_commands = static_cast<uint32_t>(bounded_unsigned(
            values, "min_batch_commands", 1,
            std::numeric_limits<uint32_t>::max()));
    }
    if (values.contains("max_batch_commands")) {
        updated.max_batch_commands = static_cast<uint32_t>(bounded_unsigned(
            values, "max_batch_commands", 1,
            std::numeric_limits<uint32_t>::max()));
    }
    if (values.contains("min_batch_bytes")) {
        updated.min_batch_bytes = static_cast<uint32_t>(bounded_unsigned(
            values, "min_batch_bytes", 1,
            std::numeric_limits<uint32_t>::max()));
    }
    if (values.contains("max_batch_bytes")) {
        updated.max_batch_bytes = static_cast<uint32_t>(bounded_unsigned(
            values, "max_batch_bytes", 1,
            std::numeric_limits<uint32_t>::max()));
    }

    if (updated.min_batch_commands > updated.max_batch_commands) {
        throw std::invalid_argument("min_batch_commands exceeds max_batch_commands");
    }
    if (updated.min_batch_bytes > updated.max_batch_bytes) {
        throw std::invalid_argument("min_batch_bytes exceeds max_batch_bytes");
    }
    cfg = std::move(updated);
}

void apply_environment(GuestConfig& cfg) {
    if (const char* host = std::getenv("OMNIGPU_HOST")) {
        const std::string value = trim(host);
        if (!value.empty()) {
            cfg.host = value;
            SPDLOG_INFO("OMNIGPU_HOST env override: {}", cfg.host);
        }
    }

    if (const char* port = std::getenv("OMNIGPU_PORT")) {
        try {
            const std::string value = trim(port);
            std::size_t consumed = 0;
            const unsigned long parsed = std::stoul(value, &consumed, 10);
            if (consumed != value.size() || parsed == 0 ||
                parsed > std::numeric_limits<uint16_t>::max()) {
                throw std::out_of_range("port");
            }
            cfg.port = static_cast<uint16_t>(parsed);
            SPDLOG_INFO("OMNIGPU_PORT env override: {}", cfg.port);
        } catch (const std::exception&) {
            SPDLOG_WARN("Ignoring invalid OMNIGPU_PORT='{}'", port);
        }
    }

    if (const char* token = std::getenv("OMNIGPU_AUTH_TOKEN")) {
        cfg.auth_token = token;
        SPDLOG_INFO("OMNIGPU_AUTH_TOKEN env override enabled");
    }
}

} // namespace

const char* config_path() {
    static const std::string path = default_config_path();
    return path.c_str();
}

GuestConfig load(const std::string& path) {
    GuestConfig cfg;
    const std::string selected_path = path.empty() ? config_path() : path;

    std::ifstream file(selected_path);
    if (file.is_open()) {
        try {
            json values;
            file >> values;
            apply_json(cfg, values);
            SPDLOG_INFO("Loaded config from {}: host={}:{}, adaptive={}",
                        selected_path, cfg.host, cfg.port,
                        cfg.adaptive_batching);
        } catch (const std::exception& error) {
            SPDLOG_WARN("Failed to parse config {}: {}", selected_path,
                        error.what());
        }
    } else {
        SPDLOG_DEBUG("Config file not found: {} (using defaults/environment)",
                     selected_path);
    }

    apply_environment(cfg);
    return cfg;
}

GuestConfig from_json_string(const std::string& json_string) {
    GuestConfig cfg;
    if (json_string.empty()) return cfg;

    try {
        apply_json(cfg, json::parse(json_string));
        SPDLOG_INFO("Parsed config JSON: host={}:{}, adaptive={}", cfg.host,
                    cfg.port, cfg.adaptive_batching);
    } catch (const std::exception& error) {
        SPDLOG_WARN("Failed to parse config JSON: {}", error.what());
    }
    return cfg;
}

std::string to_json_string(const GuestConfig& cfg) {
    json values;
    values["host"] = cfg.host;
    values["port"] = cfg.port;
    values["cache_ttl_seconds"] = cfg.cache_ttl_seconds;
    values["auth_token"] = cfg.auth_token;
    values["adaptive_batching"] = cfg.adaptive_batching;
    values["max_batch_interval_ms"] = cfg.max_batch_interval_ms;
    values["min_batch_commands"] = cfg.min_batch_commands;
    values["max_batch_commands"] = cfg.max_batch_commands;
    values["min_batch_bytes"] = cfg.min_batch_bytes;
    values["max_batch_bytes"] = cfg.max_batch_bytes;
    return values.dump();
}

} // namespace omnigpu::config
