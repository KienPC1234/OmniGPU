#include "vulkan_deserializer.h"
#include <cstring>
#include <stdexcept>

namespace omnigpu::host {

uint64_t VulkanDeserializer::read_handle() {
    if (pos_ + 8 > size_) { error_ = true; return 0; }
    uint64_t val;
    std::memcpy(&val, data_ + pos_, 8);
    pos_ += 8;
    return val;
}

uint32_t VulkanDeserializer::read_u32() {
    if (pos_ + 4 > size_) { error_ = true; return 0; }
    uint32_t val;
    std::memcpy(&val, data_ + pos_, 4);
    pos_ += 4;
    return val;
}

int32_t VulkanDeserializer::read_i32() {
    if (pos_ + 4 > size_) { error_ = true; return 0; }
    int32_t val;
    std::memcpy(&val, data_ + pos_, 4);
    pos_ += 4;
    return val;
}

uint64_t VulkanDeserializer::read_u64() {
    if (pos_ + 8 > size_) { error_ = true; return 0; }
    uint64_t val;
    std::memcpy(&val, data_ + pos_, 8);
    pos_ += 8;
    return val;
}

float VulkanDeserializer::read_f32() {
    if (pos_ + 4 > size_) { error_ = true; return 0; }
    float val;
    std::memcpy(&val, data_ + pos_, 4);
    pos_ += 4;
    return val;
}

VkBool32 VulkanDeserializer::read_bool() {
    return read_u32() ? VK_TRUE : VK_FALSE;
}

bool VulkanDeserializer::read_raw(void* out, size_t bytes) {
    if (pos_ + bytes > size_) {
        error_ = true;
        if (bytes > 0) std::memset(out, 0, bytes);
        pos_ = size_;
        return false;
    }
    std::memcpy(out, data_ + pos_, bytes);
    pos_ += bytes;
    return true;
}

std::string VulkanDeserializer::read_string() {
    uint32_t len = read_u32();
    if (len == 0 || pos_ + len > size_) return {};
    std::string s(reinterpret_cast<const char*>(data_ + pos_), len);
    pos_ += len;
    return s;
}

void VulkanDeserializer::skip(size_t bytes) {
    if (pos_ + bytes > size_) {
        error_ = true;
        pos_ = size_;
        return;
    }
    pos_ += bytes;
}

} // namespace omnigpu::host
