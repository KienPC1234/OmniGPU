#include "resource_tracker.h"
#include "omnigpu_protocol_generated.h"
#include <spdlog/spdlog.h>

namespace omnigpu::resource {

uint64_t ResourceTracker::track(uint64_t native_handle, ResourceType type,
                                uint64_t data_size) {
    std::lock_guard<std::mutex> lock(mutex_);

    // Check if already tracked
    auto hit = handle_to_id_.find(native_handle);
    if (hit != handle_to_id_.end()) {
        return hit->second;
    }

    uint64_t rid = next_id_++;
    TrackedResource res;
    res.resource_id = rid;
    res.native_handle = native_handle;
    res.type = type;
    res.data_size = data_size;
    res.uploaded = false;

    resources_[rid] = res;
    handle_to_id_[native_handle] = rid;

    SPDLOG_DEBUG("ResourceTracker: tracked handle={} as id={}, type={}, size={}",
                 native_handle, rid, static_cast<uint32_t>(type), data_size);
    return rid;
}

bool ResourceTracker::mark_uploaded(uint64_t resource_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = resources_.find(resource_id);
    if (it == resources_.end()) return false;
    it->second.uploaded = true;
    return true;
}

bool ResourceTracker::needs_upload(uint64_t resource_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = resources_.find(resource_id);
    return it != resources_.end() && !it->second.uploaded;
}

void ResourceTracker::untrack(uint64_t resource_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = resources_.find(resource_id);
    if (it != resources_.end()) {
        handle_to_id_.erase(it->second.native_handle);
        resources_.erase(it);
    }
}

void ResourceTracker::untrack_by_handle(uint64_t native_handle) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto hit = handle_to_id_.find(native_handle);
    if (hit != handle_to_id_.end()) {
        resources_.erase(hit->second);
        handle_to_id_.erase(hit);
    }
}

uint64_t ResourceTracker::resource_id_for_handle(uint64_t native_handle) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto hit = handle_to_id_.find(native_handle);
    return hit != handle_to_id_.end() ? hit->second : 0;
}

bool ResourceTracker::build_upload_message(uint64_t resource_id,
                                            const uint8_t* data,
                                            uint64_t data_size,
                                            protocol::Builder& out_builder) {
    TrackedResource res;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = resources_.find(resource_id);
        if (it == resources_.end()) return false;
        res = it->second;
    }

    flatbuffers::FlatBufferBuilder fbb;
    auto data_vec = fbb.CreateVector(data, static_cast<size_t>(data_size));
    auto upload = fbs::CreateResourceCacheUpload(
        fbb, resource_id, static_cast<fbs::DataType>(res.type),
        data_size, data_vec);
    auto msg = fbs::CreateMessage(
        fbb, fbs::MessagePayload_ResourceCacheUpload, upload.Union());
    fbb.Finish(msg);

    out_builder = std::move(fbb);

    mark_uploaded(resource_id);
    return true;
}

size_t ResourceTracker::size() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return resources_.size();
}

} // namespace omnigpu::resource
