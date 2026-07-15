#pragma once

#include "common/network_utils.h"
#include <cstdint>
#include <list>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace omnigpu::cache {

struct CachedResource {
    uint64_t resource_id = 0;
    uint32_t resource_type = 0;
    uint64_t data_size = 0;
    std::vector<uint8_t> data;
    mutable uint64_t last_access_time = 0;
    uint64_t upload_time = 0;
};

class ResourceCache {
public:
    ResourceCache(uint64_t max_bytes = 256ULL * 1024 * 1024) : max_bytes_(max_bytes) {}

    bool upload(uint64_t resource_id, uint32_t resource_type,
                const uint8_t* data, uint64_t data_size);
    bool evict(uint64_t resource_id);
    bool contains(uint64_t resource_id) const;
    const CachedResource* get(uint64_t resource_id) const;
    size_t size() const;
    uint64_t total_bytes() const;
    void clear();
    std::string stats() const;

private:
    mutable std::mutex mutex_;
    std::unordered_map<uint64_t, CachedResource> resources_;
    uint64_t total_bytes_ = 0;
    uint64_t max_bytes_;
    mutable std::list<uint64_t> lru_list_; // front = newest, back = oldest

    uint64_t now_ms() const;
    void evict_lru();
};

} // namespace omnigpu::cache
