#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include <vulkan/vulkan.h>

namespace omnigpu::host {

// Reads back the binary format written by guest VulkanSerializer.
class VulkanDeserializer {
public:
    explicit VulkanDeserializer(const uint8_t* data, size_t size)
        : data_(data), size_(size), pos_(0) {}

    uint64_t   read_handle();
    uint32_t   read_u32();
    int32_t    read_i32();
    uint64_t   read_u64();
    float      read_f32();
    VkBool32   read_bool();

    // Read raw bytes (size already known from context)
    void       read_raw(void* out, size_t bytes);

    // Read a size-prefixed string
    std::string read_string();

    // Read an array into a vector with a known count
    template<typename T>
    std::vector<T> read_array(uint32_t count) {
        std::vector<T> vec(count);
        if (count > 0) {
            read_raw(vec.data(), count * sizeof(T));
        }
        return vec;
    }

    // Skip bytes (e.g. for struct_ptr we don't care about)
    void skip(size_t bytes);

    // Position tracking
    size_t pos() const { return pos_; }
    size_t remaining() const { return size_ - pos_; }
    bool   ok() const { return pos_ <= size_; }

private:
    const uint8_t* data_;
    size_t size_;
    size_t pos_;
};

} // namespace omnigpu::host
