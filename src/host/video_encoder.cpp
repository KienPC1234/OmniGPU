#include "video_encoder.h"
#include "jpeg_encoder.h"
#include "ffmpeg_encoder.h"
#include <spdlog/spdlog.h>
#include <algorithm>
#include <cctype>

namespace omnigpu {

std::unique_ptr<VideoEncoder> create_best_encoder() {
#if defined(OMNIGPU_USE_FFMPEG)
    SPDLOG_INFO("Video encoder: FFmpeg (auto-probe HW + SW)");
    return std::make_unique<FFmpegEncoder>();
#else
    SPDLOG_INFO("Video encoder: JPEG (no FFmpeg)");
    return std::make_unique<JpegEncoder>();
#endif
}

VideoCodec codec_from_string(const std::string& s) {
    std::string lower;
    for (auto c : s) lower.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    if (lower == "hevc") return VideoCodec::HEVC;
    if (lower == "av1") return VideoCodec::AV1;
    return VideoCodec::H264;
}

} // namespace omnigpu
