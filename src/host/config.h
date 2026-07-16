#pragma once

#include <cstdint>
#include <string>

namespace omnigpu {

struct EncoderOptions {
    std::string preset = "fast";       // encoder preset
    std::string tuning = "low_latency";  // encoder tuning
    int gop_length = 0;                 // keyframe interval
};

struct HostConfig {
    uint16_t port = 9443;
    int jpeg_quality = 85;
    bool multi_gpu_enabled = true;
    int max_fps = 60;
    uint32_t render_width = 1920;
    uint32_t render_height = 1080;
    std::string config_path = "omnigpu_host.json";

    std::string video_codec = "h264";
    int video_bitrate_kbps = 10000;
    int video_fps = 60;
    uint32_t video_width = 1920;
    uint32_t video_height = 1080;

    EncoderOptions encoder;
};

bool load_config(HostConfig& config);
void print_config(const HostConfig& config);

} // namespace omnigpu
