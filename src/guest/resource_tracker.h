#pragma once

#include "common/flatbuffers_utils.h"
#include "common/network_utils.h"
#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace omnigpu::resource {

// Resource types matching DataType enum
enum class ResourceType : uint32_t {
    Unknown = 0,
    VertexBuffer = 1,
    IndexBuffer = 2,
    UniformBuffer = 3,
    StorageBuffer = 4,
    ImageData = 5,
    ShaderCode = 6,
};

struct TrackedResource {
    uint64_t resource_id = 0;
    uint64_t native_handle = 0;  // guest-side VkBuffer/VkImage handle
    ResourceType type = ResourceType::Unknown;
    uint64_t data_size = 0;
    bool uploaded = false;
};

class ResourceTracker {
public:
    ResourceTracker() = default;

    uint64_t track(uint64_t native_handle, ResourceType type, uint64_t data_size);

    bool mark_uploaded(uint64_t resource_id);

    bool needs_upload(uint64_t resource_id) const;

    void untrack(uint64_t resource_id);
    void untrack_by_handle(uint64_t native_handle);

    uint64_t resource_id_for_handle(uint64_t native_handle) const;

    bool build_upload_message(uint64_t resource_id,
                              const uint8_t* data, uint64_t data_size,
                              protocol::Builder& out_builder);

    size_t size() const;

private:
    mutable std::mutex mutex_;
    std::unordered_map<uint64_t, TrackedResource> resources_;
    std::unordered_map<uint64_t, uint64_t> handle_to_id_;
    uint64_t next_id_ = 1;
};

} // namespace omnigpu::resource
