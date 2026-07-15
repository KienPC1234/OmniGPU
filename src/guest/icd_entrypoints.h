#pragma once

#include <vulkan/vulkan.h>

namespace omnigpu::icd {

void add_entrypoint(const char* name, PFN_vkVoidFunction func);
PFN_vkVoidFunction lookup_entrypoint(const char* name);

} // namespace omnigpu::icd
