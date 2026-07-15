#pragma once

#include <cstdint>
#include <string>

namespace omnigpu::config {

struct GuestConfig {
    std::string host = "127.0.0.1";
    uint16_t port = 9443;
    bool zink_enabled = true;
    bool clvk_enabled = true;
    uint64_t cache_ttl_seconds = 86400;

    // Adaptive batching
    bool adaptive_batching = true;
    uint32_t max_batch_interval_ms = 16;
    uint32_t min_batch_commands = 4;
    uint32_t max_batch_commands = 256;
    uint32_t min_batch_bytes = 1024;
    uint32_t max_batch_bytes = 524288;
};

GuestConfig load(const std::string& path = "");
const char* config_path();

} // namespace omnigpu::config
