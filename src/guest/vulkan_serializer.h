#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include <vulkan/vulkan.h>

namespace omnigpu::serializer {

class VulkanSerializer {
public:
    VulkanSerializer() = default;

    void write_handle(uint64_t handle);
    void write_u32(uint32_t val);
    void write_i32(int32_t val);
    void write_u64(uint64_t val);
    void write_f32(float val);
    void write_bool(VkBool32 val);

    void write_raw(const void* data, size_t size);

    void write_struct(const void* data, size_t size);

    template <typename T>
    void write_struct(const T& val) {
        write_raw(&val, sizeof(T));
    }

    template <typename T>
    void write_array(const T* arr, uint32_t count) {
        write_u32(count);
        if (arr && count > 0) {
            write_raw(arr, sizeof(T) * count);
        }
    }

    void write_string(const char* str);

    const uint8_t* data() const { return buffer_.data(); }
    size_t size() const { return buffer_.size(); }
    bool empty() const { return buffer_.empty(); }
    void clear() { buffer_.clear(); }

private:
    std::vector<uint8_t> buffer_;
};

// Check if a Vulkan type is a handle (pointer type)
constexpr bool is_vulkan_handle(uint64_t val) {
    return val != 0;
}

} // namespace omnigpu::serializer
