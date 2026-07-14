#include "vulkan_serializer.h"
#include <cstring>
#include <cassert>

namespace omnigpu::serializer {

void VulkanSerializer::write_handle(uint64_t handle) {
    write_u64(handle);
}

void VulkanSerializer::write_u32(uint32_t val) {
    buffer_.insert(buffer_.end(),
                   reinterpret_cast<const uint8_t*>(&val),
                   reinterpret_cast<const uint8_t*>(&val) + sizeof(val));
}

void VulkanSerializer::write_i32(int32_t val) {
    buffer_.insert(buffer_.end(),
                   reinterpret_cast<const uint8_t*>(&val),
                   reinterpret_cast<const uint8_t*>(&val) + sizeof(val));
}

void VulkanSerializer::write_u64(uint64_t val) {
    buffer_.insert(buffer_.end(),
                   reinterpret_cast<const uint8_t*>(&val),
                   reinterpret_cast<const uint8_t*>(&val) + sizeof(val));
}

void VulkanSerializer::write_f32(float val) {
    buffer_.insert(buffer_.end(),
                   reinterpret_cast<const uint8_t*>(&val),
                   reinterpret_cast<const uint8_t*>(&val) + sizeof(val));
}

void VulkanSerializer::write_bool(VkBool32 val) {
    write_u32(static_cast<uint32_t>(val));
}

void VulkanSerializer::write_raw(const void* data, size_t size) {
    if (data && size > 0) {
        auto* bytes = static_cast<const uint8_t*>(data);
        buffer_.insert(buffer_.end(), bytes, bytes + size);
    }
}

void VulkanSerializer::write_struct(const void* data, size_t size) {
    write_raw(data, size);
}

void VulkanSerializer::write_string(const char* str) {
    if (str) {
        size_t len = std::strlen(str);
        write_u32(static_cast<uint32_t>(len));
        write_raw(str, len);
    } else {
        write_u32(0);
    }
}

} // namespace omnigpu::serializer
