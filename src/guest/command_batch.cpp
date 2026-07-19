#include "command_batch.h"
#include "client.h"
#include "omnigpu_protocol_generated.h"
#include <spdlog/spdlog.h>

namespace omnigpu::batch {

CommandBatch::CommandBatch(Client* client,
                           size_t cmd_threshold,
                           size_t byte_threshold,
                           bool flush_on_present,
                           bool adaptive,
                           uint32_t max_interval_ms)
    : client_(client),
      cmd_threshold_(cmd_threshold),
      byte_threshold_(byte_threshold),
      flush_on_present_(flush_on_present),
      adaptive_(adaptive),
      max_interval_ms_(max_interval_ms),
      last_flush_time_(Clock::now()) {}

void CommandBatch::append(const protocol::Builder& builder) {
    auto span = builder.GetBufferSpan();
    append_raw(span.data(), span.size());
}

void CommandBatch::append_raw(const uint8_t* data, size_t size) {
    std::unique_lock<std::mutex> lock(mutex_);

    pending_cmds_.emplace_back(data, data + size);
    total_pending_bytes_ += size;
    command_count_++;

    if (should_flush()) {
        lock.unlock();
        flush();
    }
}

void CommandBatch::flush() {
    // Lock: extract all pending commands atomically
    std::vector<std::vector<uint8_t>> batch;
    size_t saved_count = 0;
    size_t saved_bytes = 0;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (pending_cmds_.empty()) {
            return;
        }
        saved_count = command_count_;
        saved_bytes = total_pending_bytes_;
        batch.swap(pending_cmds_);
        command_count_ = 0;
        total_pending_bytes_ = 0;
        {
            std::lock_guard<std::mutex> lk(flush_time_mutex_);
            last_flush_time_ = Clock::now();
        }
    }

    SPDLOG_TRACE("Flushing batch: {} commands, {} bytes, adaptive={}",
                 saved_count, saved_bytes, adaptive_.load());

    {
        std::lock_guard<std::mutex> lock(client_->send_mutex());
        for (const auto& cmd : batch) {
            uint32_t net_size = htonl(static_cast<uint32_t>(cmd.size()));
            if (!client_->send_data(
                    reinterpret_cast<const uint8_t*>(&net_size),
                    sizeof(net_size))) {
                SPDLOG_ERROR("Batch: send failed after {} cmds — DATA LOST!", saved_count);
                break;
            }
            if (!client_->send_data(cmd.data(), cmd.size())) {
                SPDLOG_ERROR("Batch: payload send failed — DATA LOST!");
                break;
            }
        }
    }

    if (adaptive_) {
        adapt_thresholds();
    }
}

void CommandBatch::force_flush() {
    flush();
}

void CommandBatch::on_present() {
    if (!flush_on_present_.load()) return;
    if (empty()) return;
    flush();
}

void CommandBatch::record_latency_sample(uint32_t rtt_ms) {
    uint32_t prev = smoothed_rtt_ms_.load();
    uint32_t next = static_cast<uint32_t>(
        kRttAlpha * rtt_ms + (1.0f - kRttAlpha) * prev);
    smoothed_rtt_ms_.store(next);

    SPDLOG_TRACE("RTT sample: {}ms, smoothed: {}ms", rtt_ms, next);
}

size_t CommandBatch::command_count() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return command_count_;
}

size_t CommandBatch::byte_count() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return total_pending_bytes_;
}

bool CommandBatch::empty() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return pending_cmds_.empty();
}

void CommandBatch::set_thresholds(size_t cmd_threshold, size_t byte_threshold) {
    std::lock_guard<std::mutex> lock(mutex_);
    cmd_threshold_ = cmd_threshold;
    byte_threshold_ = byte_threshold;
}

void CommandBatch::set_flush_on_present(bool enabled) {
    flush_on_present_ = enabled;
}

void CommandBatch::set_adaptive(bool enabled) {
    adaptive_ = enabled;
}

void CommandBatch::set_max_interval_ms(uint32_t ms) {
    max_interval_ms_ = ms;
}

bool CommandBatch::should_flush() const {
    if (command_count_ >= cmd_threshold_ ||
        total_pending_bytes_ >= byte_threshold_) {
        return true;
    }

    if (max_interval_ms_.load() > 0 && command_count_ > 0) {
        auto now = Clock::now();
        Clock::time_point last;
        {
            std::lock_guard<std::mutex> lk(flush_time_mutex_);
            last = last_flush_time_;
        }
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                           now - last)
                           .count();
        if (elapsed >= max_interval_ms_.load()) {
            return true;
        }
    }

    return false;
}

void CommandBatch::adapt_thresholds() {
    std::lock_guard<std::mutex> lock(mutex_);
    uint32_t rtt = smoothed_rtt_ms_.load();

    if (rtt < 5) {
        size_t new_cmd = std::max(kMinCommandThreshold, cmd_threshold_ / 2);
        size_t new_byte = std::max(kMinByteThreshold, byte_threshold_ / 2);
        cmd_threshold_ = std::clamp(new_cmd, kMinCommandThreshold, kMaxCommandThreshold);
        byte_threshold_ = std::clamp(new_byte, kMinByteThreshold, kMaxByteThreshold);
    } else if (rtt > 30) {
        size_t new_cmd = std::min(kMaxCommandThreshold, cmd_threshold_ * 2);
        size_t new_byte = std::min(kMaxByteThreshold, byte_threshold_ * 2);
        cmd_threshold_ = std::clamp(new_cmd, kMinCommandThreshold, kMaxCommandThreshold);
        byte_threshold_ = std::clamp(new_byte, kMinByteThreshold, kMaxByteThreshold);
    }
}

} // namespace omnigpu::batch
