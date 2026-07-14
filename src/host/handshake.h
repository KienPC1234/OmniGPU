#pragma once

#include "common/gpu_caps.h"
#include "common/network_utils.h"

namespace omnigpu::handshake {

bool handle_capabilities_request(SOCKET client_fd);

caps::GpuCapabilities query_host_gpu_caps();

} // namespace omnigpu::handshake
