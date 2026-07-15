#pragma once

#include "video_encoder.h"
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace omnigpu {

struct NvencSettings {
    std::string preset = "p1";          // p1-p7
    std::string tuning = "low_latency";  // high_quality, low_latency, ultra_low_latency, lossless
    int gop_length = 0;                 // 0 = infinite (NVENC_INFINITE_GOPLENGTH)
};

class NvencEncoder final : public VideoEncoder {
public:
    NvencEncoder();
    ~NvencEncoder() override;

    void set_settings(const NvencSettings& settings);

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
