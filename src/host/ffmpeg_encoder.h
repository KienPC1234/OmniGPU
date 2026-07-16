#pragma once

#include "video_encoder.h"
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace omnigpu {

class FFmpegEncoder final : public VideoEncoder {
public:
    FFmpegEncoder();
    ~FFmpegEncoder() override;

    void set_encoder_name(const std::string& name);
    void set_encoder_options(const std::string& preset, const std::string& tuning, int gop_length);

    bool init(VideoCodec codec, uint32_t width, uint32_t height,
              int fps, int bitrateKbps) override;
    bool encode(const std::vector<uint8_t>& rgba,
                std::vector<EncodedPacket>& packets) override;
    bool flush(std::vector<EncodedPacket>& packets) override;
    void shutdown() override;
    bool available() const override;
    std::string name() const override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace omnigpu
