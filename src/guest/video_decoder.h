#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#include <mfapi.h>
#include <mftransform.h>
#include <mfidl.h>
#endif

namespace omnigpu::video {

enum class Codec : uint8_t {
    Unknown = 0,
    H264 = 1,
    HEVC = 2,
    AV1  = 3,
};

struct DecodedFrame {
    uint64_t frame_id = 0;
    uint32_t width = 0;
    uint32_t height = 0;
    uint64_t timestamp_ms = 0;
    std::vector<uint8_t> rgba_pixels;
};

using FrameCallback = std::function<void(DecodedFrame)>;

class VideoDecoder {
public:
    virtual ~VideoDecoder() = default;
    virtual bool init() = 0;
    virtual void shutdown() = 0;
    virtual bool decode(Codec codec, bool is_keyframe,
                        const uint8_t* data, size_t size,
                        uint64_t frame_id, uint64_t timestamp_ms,
                        uint32_t width, uint32_t height) = 0;
    virtual bool hardware_accelerated() const = 0;
    virtual std::string name() const = 0;
    void set_frame_callback(FrameCallback cb) { callback_ = std::move(cb); }
protected:
    FrameCallback callback_;
};

// Windows Media Foundation H.264/H.265 decoder (GPU accelerated)
#ifdef _WIN32
class MfH264Decoder : public VideoDecoder {
public:
    MfH264Decoder();
    ~MfH264Decoder() override;
    bool init() override;
    void shutdown() override;
    bool decode(Codec codec, bool is_keyframe,
                const uint8_t* data, size_t size,
                uint64_t frame_id, uint64_t timestamp_ms,
                uint32_t width, uint32_t height) override;
    bool hardware_accelerated() const override;
    std::string name() const override;
private:
    bool init_mft(Codec codec, uint32_t w, uint32_t h);
    bool process_output();
    bool initialized_ = false;
    bool hw_accel_ = false;
    IMFTransform* mft_ = nullptr;
    IMFMediaType* input_type_ = nullptr;
    IMFMediaType* output_type_ = nullptr;
    MFT_OUTPUT_STREAM_INFO output_info_{};
    uint32_t frame_w_ = 0, frame_h_ = 0;
    Codec active_codec_ = Codec::Unknown;
    uint64_t current_frame_id_ = 0;
};
#endif

// FFmpeg software decoder (H.264/H.265/AV1), requires OMNIGPU_USE_FFMPEG
#if defined(OMNIGPU_USE_FFMPEG)
class FFmpegDecoder : public VideoDecoder {
public:
    FFmpegDecoder();
    ~FFmpegDecoder() override;
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
};
#endif

VideoDecoder* create_decoder();

void set_swapchain_extent(uint32_t w, uint32_t h);
void get_swapchain_extent(uint32_t& w, uint32_t& h);

} // namespace omnigpu::video
