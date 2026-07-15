#include "resource_cache.h"
#include <algorithm>
#include <chrono>
#include <fmt/core.h>
#include <spdlog/spdlog.h>

namespace omnigpu::cache {

uint64_t ResourceCache::now_ms() const {
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch())
            .count());
}

bool ResourceCache::upload(uint64_t resource_id, uint32_t resource_type,
                            const uint8_t* data, uint64_t data_size) {
    if (!data || data_size == 0) {
        SPDLOG_WARN("ResourceCache: invalid upload (id={}, size={})",
                    resource_id, data_size);
        return false;
    }

    std::lock_guard<std::mutex> lock(mutex_);

    // Remove old entry if exists
    auto it = resources_.find(resource_id);
    if (it != resources_.end()) {
        total_bytes_ -= it->second.data_size;
        lru_list_.remove(resource_id);
    }

    // Evict LRU entries until we have space
    while (max_bytes_ > 0 && total_bytes_ + data_size > max_bytes_) {
        if (lru_list_.empty()) break;
        uint64_t victim = lru_list_.back();
        lru_list_.pop_back();
        auto vit = resources_.find(victim);
        if (vit != resources_.end()) {
            total_bytes_ -= vit->second.data_size;
            resources_.erase(vit);
            SPDLOG_DEBUG("ResourceCache: evicted id={} (LRU)", victim);
        }
    }

    CachedResource res;
    res.resource_id = resource_id;
    res.resource_type = resource_type;
    res.data_size = data_size;
    res.data.assign(data, data + data_size);
    res.upload_time = now_ms();
    res.last_access_time = res.upload_time;

    resources_[resource_id] = std::move(res);
    lru_list_.push_front(resource_id);
    total_bytes_ += data_size;

    SPDLOG_DEBUG("ResourceCache: uploaded id={}, type={}, size={} bytes",
                 resource_id, resource_type, data_size);
    return true;
}

bool ResourceCache::evict(uint64_t resource_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = resources_.find(resource_id);
    if (it == resources_.end()) {
        SPDLOG_DEBUG("ResourceCache: evict id={} not found", resource_id);
        return false;
    }
    total_bytes_ -= it->second.data_size;
    lru_list_.remove(resource_id);
    resources_.erase(it);
    SPDLOG_DEBUG("ResourceCache: evicted id={}", resource_id);
    return true;
}

bool ResourceCache::contains(uint64_t resource_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return resources_.find(resource_id) != resources_.end();
}

const CachedResource* ResourceCache::get(uint64_t resource_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = resources_.find(resource_id);
    if (it != resources_.end()) {
        it->second.last_access_time = now_ms();
        // Move to front of LRU list
        lru_list_.remove(resource_id);
        lru_list_.push_front(resource_id);
        return &it->second;
    }
    return nullptr;
}

size_t ResourceCache::size() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return resources_.size();
}

uint64_t ResourceCache::total_bytes() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return total_bytes_;
}

void ResourceCache::clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    resources_.clear();
    lru_list_.clear();
    total_bytes_ = 0;
    SPDLOG_INFO("ResourceCache: cleared");
}

std::string ResourceCache::stats() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return fmt::format("{} resources, {} bytes cached (max {} bytes)",
                       resources_.size(), total_bytes_, max_bytes_);
}

} // namespace omnigpu::cache
