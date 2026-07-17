#pragma once

#include "common/gpu_caps.h"
#include "common/network_utils.h"
#include <string>

namespace omnigpu::handshake {

struct HandshakeResult {
    bool ok = false;
    bool compute_mode = false;
    bool large_buffers = false;
    uint32_t client_version = 0;
    std::string error_msg;
};

caps::GpuCapabilities query_host_gpu_caps();
HandshakeResult handle_capabilities_request(SOCKET client_fd,
    const std::string& expected_token);

} // namespace omnigpu::handshake
