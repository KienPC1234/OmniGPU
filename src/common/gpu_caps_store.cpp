#include "gpu_caps_store.h"

namespace omnigpu::caps {

namespace {

GpuCapabilities g_cached_caps;

} // anonymous namespace

void store(const GpuCapabilities& caps) {
    g_cached_caps = caps;
}

const GpuCapabilities& get() {
    return g_cached_caps;
}

} // namespace omnigpu::caps
