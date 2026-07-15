#pragma once

#include "common/gpu_caps.h"

namespace omnigpu::caps {

void store(const GpuCapabilities& caps);
const GpuCapabilities& get();

} // namespace omnigpu::caps
