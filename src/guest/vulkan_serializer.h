#pragma once

#include <atomic>
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

namespace omnigpu {

// Convert any Vulkan handle type to uint64_t safely (works on both 32 and 64-bit)
template<typename T>
inline uint64_t handle_to_u64(T ptr) {
    uint64_t val = 0;
    memcpy(&val, &ptr, sizeof(ptr));
    return val;
}

// Convert uint64_t back to a Vulkan handle
template<typename T>
inline T handle_from_u64(uint64_t val) {
    T ptr = T{};
    memcpy(&ptr, &val, sizeof(ptr));
    return ptr;
}

// Generate a unique fake resource handle (persistent across calls)
inline uint64_t next_fake_handle() {
    static std::atomic<uint64_t> counter{0x10000};
    return counter.fetch_add(1, std::memory_order_relaxed);
}

} // namespace omnigpu
