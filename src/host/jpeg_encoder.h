#pragma once

#include "video_encoder.h"
#include "compressor.h"
#include <memory>

namespace omnigpu {

class JpegEncoder final : public VideoEncoder {
public:
    JpegEncoder();
    ~JpegEncoder() override;

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
