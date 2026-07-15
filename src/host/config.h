#pragma once

#include <cstdint>
#include <string>

namespace omnigpu {

struct NvencConfig {
    std::string preset = "p1";        // p1-p7
    std::string tuning = "low_latency"; // high_quality, low_latency, ultra_low_latency, lossless
    int gop_length = 0;               // 0 = infinite
};

struct HostConfig {
    uint16_t port = 9443;
    int jpeg_quality = 85;
    bool multi_gpu_enabled = true;
    int max_fps = 60;
    uint32_t render_width = 800;
    uint32_t render_height = 600;
    std::string config_path = "omnigpu_host.json";

    // Video encoder settings
    std::string video_codec = "h264";  // h264, hevc
    int video_bitrate_kbps = 4000;
    int video_fps = 60;
    uint32_t video_width = 800;
    uint32_t video_height = 600;

    // NVENC-specific settings
    NvencConfig nvenc;
};

bool load_config(HostConfig& config);
void print_config(const HostConfig& config);

} // namespace omnigpu
