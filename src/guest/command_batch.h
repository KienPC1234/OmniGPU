#pragma once

#include "common/flatbuffers_utils.h"
#include "common/network_utils.h"
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <vector>

namespace omnigpu::batch {

// Default threshold: flush after 32 commands or 64 KB
inline constexpr size_t kDefaultCommandThreshold = 32;
inline constexpr size_t kDefaultByteThreshold = 64 * 1024;

class CommandBatch {
public:
    explicit CommandBatch(SOCKET socket,
                          size_t cmd_threshold = kDefaultCommandThreshold,
                          size_t byte_threshold = kDefaultByteThreshold);

    void append(const protocol::Builder& builder);

    void append_raw(const uint8_t* data, size_t size);

    void flush();

    void force_flush();

    size_t command_count() const;
    size_t byte_count() const;
    bool empty() const;

private:
    SOCKET socket_;
    size_t cmd_threshold_;
    size_t byte_threshold_;

    mutable std::mutex mutex_;
    std::vector<uint8_t> batch_buffer_;
    size_t command_count_ = 0;

    bool should_flush() const;
};

} // namespace omnigpu::batch
