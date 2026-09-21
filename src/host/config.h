#pragma once

#include <cstdint>
#include <string>

namespace omnigpu {

struct HostConfig {
    uint16_t port = 9443;
    bool multi_gpu_enabled = true;
    std::string config_path = "omnigpu_host.json";

    // Security
    std::string auth_token = "";       // empty = no auth required
    uint32_t max_sessions = 16;         // max concurrent clients
    uint32_t max_msg_size_mb = 16;      // max message size in MB
    uint32_t session_timeout_s = 300;   // idle session timeout (0 = no timeout)
    uint64_t per_session_memory_budget = 4ULL * 1024 * 1024 * 1024; // 4GB
};

bool load_config(HostConfig& config);
void print_config(const HostConfig& config);

} // namespace omnigpu
