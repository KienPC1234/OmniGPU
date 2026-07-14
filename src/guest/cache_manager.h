#pragma once

#include "common/gpu_caps.h"
#include <string>

namespace omnigpu::cache {

// Cache file name
inline constexpr const char* kCacheFileName = ".omnigpu_cache.json";

// Cache validity: 24 hours
inline constexpr uint64_t kCacheTtlSeconds = 86400;

class CacheManager {
public:
    explicit CacheManager(const std::string& host, uint16_t port);

    bool load(caps::GpuCapabilities& caps) const;

    bool save(const caps::GpuCapabilities& caps);

    bool is_valid() const;

    void invalidate();

private:
    std::string cache_key_;
    std::string cache_path_;

    std::string get_cache_path() const;
};

} // namespace omnigpu::cache
