#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace omnigpu {

struct EncodedPacket {
    std::vector<uint8_t> data;
    bool isKeyframe = false;
    int64_t pts = 0;
};

enum class VideoCodec {
    H264,
    HEVC,
    AV1,
};

class VideoEncoder {
public:
    virtual ~VideoEncoder() = default;

    virtual bool init(VideoCodec codec, uint32_t width, uint32_t height,
                      int fps, int bitrateKbps) = 0;
    virtual bool encode(const std::vector<uint8_t>& rgba,
                        std::vector<EncodedPacket>& packets) = 0;
    virtual bool flush(std::vector<EncodedPacket>& packets) = 0;
    virtual void shutdown() = 0;
    virtual bool available() const = 0;
    virtual std::string name() const = 0;
};

std::unique_ptr<VideoEncoder> create_best_encoder();
VideoCodec codec_from_string(const std::string& s);

} // namespace omnigpu
