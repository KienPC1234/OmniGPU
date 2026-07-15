#pragma once

#include "video_decoder.h"
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace omnigpu::video {

// FFmpeg-based decoder: H.264, H.265/HEVC, AV1
// Uses avcodec for decoding + swscale for YUV→RGBA conversion.
// Set -DOMNIGPU_USE_FFMPEG=ON in CMake to enable.
#if defined(OMNIGPU_USE_FFMPEG)

class FFmpegVideoDecoder : public VideoDecoder {
public:
    FFmpegVideoDecoder();
    ~FFmpegVideoDecoder() override;

    bool init() override;
    void shutdown() override;

    bool decode(Codec codec, bool is_keyframe,
                const uint8_t* data, size_t size,
                uint64_t frame_id, uint64_t timestamp_ms,
                uint32_t width, uint32_t height) override;

    bool hardware_accelerated() const override;
    std::string name() const override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    bool initialized_ = false;
};

#endif // OMNIGPU_USE_FFMPEG

} // namespace omnigpu::video
