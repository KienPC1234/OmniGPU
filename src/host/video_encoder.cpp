#include "video_encoder.h"
#include "jpeg_encoder.h"
#include "ffmpeg_encoder.h"
#include <spdlog/spdlog.h>

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

} // namespace omnigpu
