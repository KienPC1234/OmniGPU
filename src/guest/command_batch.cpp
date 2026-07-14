#include "command_batch.h"
#include "omnigpu_protocol_generated.h"
#include <spdlog/spdlog.h>

namespace omnigpu::batch {

CommandBatch::CommandBatch(SOCKET socket,
                           size_t cmd_threshold,
                           size_t byte_threshold)
    : socket_(socket),
      cmd_threshold_(cmd_threshold),
      byte_threshold_(byte_threshold) {}

void CommandBatch::append(const protocol::Builder& builder) {
    auto span = builder.GetBufferSpan();
    append_raw(span.data(), span.size());
}

void CommandBatch::append_raw(const uint8_t* data, size_t size) {
    std::unique_lock<std::mutex> lock(mutex_);

    batch_buffer_.insert(batch_buffer_.end(), data, data + size);
    command_count_++;

    if (should_flush()) {
        lock.unlock();
        flush();
    }
}

void CommandBatch::flush() {
    std::lock_guard<std::mutex> lock(mutex_);

    if (batch_buffer_.empty()) {
        return;
    }

    // Build a FlatBuffer containing all accumulated raw command messages
    // Each command was already a complete FlatBuffer Message; we send them
    // as a single TCP chunk with a length prefix for framing.
    // For now: simple framing — [4-byte size][payload]
    uint32_t payload_size = static_cast<uint32_t>(batch_buffer_.size());

    SPDLOG_TRACE("Flushing batch: {} commands, {} bytes",
                 command_count_, payload_size);

    // Send length prefix
    uint32_t net_size = htonl(payload_size);
    tcp::send_all(socket_,
                  reinterpret_cast<const uint8_t*>(&net_size),
                  sizeof(net_size));

    // Send batched payload
    tcp::send_all(socket_, batch_buffer_.data(), payload_size);

    batch_buffer_.clear();
    command_count_ = 0;
}

void CommandBatch::force_flush() {
    flush();
}

size_t CommandBatch::command_count() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return command_count_;
}

size_t CommandBatch::byte_count() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return batch_buffer_.size();
}

bool CommandBatch::empty() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return batch_buffer_.empty();
}

bool CommandBatch::should_flush() const {
    return command_count_ >= cmd_threshold_ ||
           batch_buffer_.size() >= byte_threshold_;
}

} // namespace omnigpu::batch
