#include "cache_manager.h"
#include <spdlog/spdlog.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#include <sys/stat.h>
#endif

#include <chrono>
#include <fstream>
#include <nlohmann/json.hpp>

namespace omnigpu::cache {

using json = nlohmann::json;

CacheManager::CacheManager(const std::string& host, uint16_t port)
    : cache_key_(host + "_" + std::to_string(port)),
      cache_path_(get_cache_path()) {}

std::string CacheManager::get_cache_path() const {
#ifdef _WIN32
    char path[MAX_PATH];
    if (GetEnvironmentVariableA("APPDATA", path, sizeof(path))) {
        return std::string(path) + "\\omnigpu\\" + kCacheFileName;
    }
    return kCacheFileName;
#else
    const char* home = getenv("HOME");
    if (home) {
        return std::string(home) + "/.config/omnigpu/" + kCacheFileName;
    }
    return kCacheFileName;
#endif
}

bool CacheManager::load(caps::GpuCapabilities& caps) const {
    std::ifstream file(cache_path_);
    if (!file.is_open()) {
        SPDLOG_DEBUG("Cache file not found: {}", cache_path_);
        return false;
    }

    try {
        json j;
        file >> j;

        auto entry = j.find(cache_key_);
        if (entry == j.end()) {
            SPDLOG_DEBUG("No cache entry for {}", cache_key_);
            return false;
        }

        auto& data = *entry;
        uint64_t saved_ts = data.value("timestamp", 0ULL);
        auto now = std::chrono::duration_cast<std::chrono::seconds>(
                       std::chrono::system_clock::now().time_since_epoch())
                       .count();

        if (static_cast<uint64_t>(now) - saved_ts > kCacheTtlSeconds) {
            SPDLOG_INFO("Cache expired for {} ({}s old)", cache_key_,
                        static_cast<uint64_t>(now) - saved_ts);
            return false;
        }

        caps.gpu_name = data.value("gpu_name", "");
        caps.driver_version = data.value("driver_version", 0U);
        caps.api_version = data.value("api_version", 0U);
        caps.max_memory_allocation = data.value("max_memory_allocation", 0ULL);
        caps.max_push_constants_size = data.value("max_push_constants_size", 128U);
        caps.max_bound_descriptor_sets = data.value("max_bound_descriptor_sets", 4U);
        caps.max_per_stage_resources = data.value("max_per_stage_resources", 32U);
        caps.max_image_dimension_2d = data.value("max_image_dimension_2d", 8192U);
        caps.timestamp = saved_ts;

        SPDLOG_INFO("Loaded cached GPU caps for {}: {}", cache_key_,
                    caps.gpu_name);
        return true;
    } catch (const std::exception& e) {
        SPDLOG_WARN("Failed to parse cache file: {}", e.what());
        return false;
    }
}

bool CacheManager::save(const caps::GpuCapabilities& caps) {
    try {
        // Ensure directory exists
        auto dir = cache_path_.substr(0, cache_path_.find_last_of("/\\"));
        if (!dir.empty()) {
#ifdef _WIN32
            CreateDirectoryA(dir.c_str(), nullptr);
#else
            mkdir(dir.c_str(), 0755);
#endif
        }

        json j;
        std::ifstream infile(cache_path_);
        if (infile.is_open()) {
            infile >> j;
        }

        json entry;
        entry["gpu_name"] = caps.gpu_name;
        entry["driver_version"] = caps.driver_version;
        entry["api_version"] = caps.api_version;
        entry["max_memory_allocation"] = caps.max_memory_allocation;
        entry["max_push_constants_size"] = caps.max_push_constants_size;
        entry["max_bound_descriptor_sets"] = caps.max_bound_descriptor_sets;
        entry["max_per_stage_resources"] = caps.max_per_stage_resources;
        entry["max_image_dimension_2d"] = caps.max_image_dimension_2d;
        entry["timestamp"] = caps.timestamp;

        j[cache_key_] = entry;

        std::ofstream outfile(cache_path_);
        if (!outfile.is_open()) {
            SPDLOG_ERROR("Failed to write cache: {}", cache_path_);
            return false;
        }
        outfile << j.dump(2) << std::endl;

        SPDLOG_INFO("Cached GPU caps for {}: {}", cache_key_, caps.gpu_name);
        return true;
    } catch (const std::exception& e) {
        SPDLOG_WARN("Failed to save cache: {}", e.what());
        return false;
    }
}

bool CacheManager::is_valid() const {
    caps::GpuCapabilities dummy;
    return load(dummy);
}

void CacheManager::invalidate() {
    std::ifstream infile(cache_path_);
    if (!infile.is_open()) return;

    try {
        json j;
        infile >> j;
        j.erase(cache_key_);

        std::ofstream outfile(cache_path_);
        outfile << j.dump(2) << std::endl;
    } catch (...) {
        // Ignore errors during invalidation
    }
}

} // namespace omnigpu::cache
