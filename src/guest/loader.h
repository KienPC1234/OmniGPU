#pragma once

#include <string>

namespace omnigpu::loader {

struct TranslationLayers {
    bool zink_available = false;
    bool clvk_available = false;
    bool vulkan_icd_ready = false;
    std::string icd_manifest_path;
    std::string runtime_dir;
};

bool initialize();
void shutdown();

const TranslationLayers& get_layers();

} // namespace omnigpu::loader
