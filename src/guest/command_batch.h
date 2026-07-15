#pragma once

#include "common/flatbuffers_utils.h"
#include "common/network_utils.h"
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <vector>

namespace omnigpu {
class Client;
}

namespace omnigpu::batch {

// Default thresholds
inline constexpr size_t kDefaultCommandThreshold = 32;
inline constexpr size_t kDefaultByteThreshold = 64 * 1024;
inline constexpr bool kDefaultFlushOnPresent = true;

// Adaptive batching defaults
inline constexpr bool kDefaultAdaptiveEnabled = true;
inline constexpr uint32_t kDefaultMaxFlushIntervalMs = 16;  // ~60 FPS
inline constexpr size_t kMinCommandThreshold = 4;
inline constexpr size_t kMaxCommandThreshold = 256;
inline constexpr size_t kMinByteThreshold = 1024;
inline constexpr size_t kMaxByteThreshold = 512 * 1024;

class CommandBatch {
public:
    explicit CommandBatch(Client* client,
                          size_t cmd_threshold = kDefaultCommandThreshold,
                          size_t byte_threshold = kDefaultByteThreshold,
                          bool flush_on_present = kDefaultFlushOnPresent,
                          bool adaptive = kDefaultAdaptiveEnabled,
                          uint32_t max_interval_ms = kDefaultMaxFlushIntervalMs);

    void append(const protocol::Builder& builder);
    void append_raw(const uint8_t* data, size_t size);

    void flush();
    void force_flush();
    void on_present();

    // Record a latency sample (from response timestamps)
    void record_latency_sample(uint32_t rtt_ms);

    size_t command_count() const;
    size_t byte_count() const;
    bool empty() const;

    void set_thresholds(size_t cmd_threshold, size_t byte_threshold);
    void set_flush_on_present(bool enabled);
    void set_adaptive(bool enabled);
    void set_max_interval_ms(uint32_t ms);

private:
    Client* client_;
    size_t cmd_threshold_;
    size_t byte_threshold_;
    std::atomic<bool> flush_on_present_{true};
    std::atomic<bool> adaptive_{true};
    std::atomic<uint32_t> max_interval_ms_{16};

    mutable std::mutex mutex_;
    mutable std::mutex send_mutex_;   // serialises flush() sends to prevent interleaving
    std::vector<std::vector<uint8_t>> pending_cmds_;  // each entry = one FlatBuffer Message
    size_t total_pending_bytes_ = 0;
    size_t command_count_ = 0;

    using Clock = std::chrono::steady_clock;
    Clock::time_point last_flush_time_;
    mutable std::mutex flush_time_mutex_;  // guards last_flush_time_

    // Latency tracking for adaptive thresholds
    std::atomic<uint32_t> smoothed_rtt_ms_{10};
    static constexpr float kRttAlpha = 0.3f;

    bool should_flush() const;
    void adapt_thresholds();
};

} // namespace omnigpu::batch
