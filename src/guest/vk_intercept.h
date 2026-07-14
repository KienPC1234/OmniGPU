#pragma once

#include <vulkan/vulkan.h>

namespace omnigpu::batch {
class CommandBatch;
}

namespace omnigpu::intercept {

void initialize_hooks();
void shutdown_hooks();
void set_batch(batch::CommandBatch* batch);

} // namespace omnigpu::intercept
