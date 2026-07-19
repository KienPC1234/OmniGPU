#include "gpu_caps_store.h"
#include <mutex>

namespace omnigpu::caps {

namespace {

GpuCapabilities g_cached_caps;
std::mutex g_caps_mutex;

} // anonymous namespace

void store(const GpuCapabilities& caps) {
    std::lock_guard<std::mutex> lock(g_caps_mutex);
    g_cached_caps = caps;
}

const GpuCapabilities& get() {
    // OK: after store() completes, reads are safe (happens-before via mutex)
    return g_cached_caps;
}

} // namespace omnigpu::caps
