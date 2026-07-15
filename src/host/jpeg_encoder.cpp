#include "jpeg_encoder.h"
#include <spdlog/spdlog.h>

namespace omnigpu {

struct JpegEncoder::Impl {
    Compressor compressor;
    int quality = 85;
    uint32_t width = 0;
    uint32_t height = 0;
};

JpegEncoder::JpegEncoder() : impl_(std::make_unique<Impl>()) {}
JpegEncoder::~JpegEncoder() { shutdown(); }

bool JpegEncoder::available() const { return true; }
std::string JpegEncoder::name() const { return "JPEG"; }

bool JpegEncoder::init(VideoCodec codec, uint32_t width, uint32_t height,
                        int fps, int bitrateKbps) {
    (void)codec;
    (void)fps;
    impl_->width = width;
    impl_->height = height;
    impl_->quality = std::max(10, std::min(95, bitrateKbps / 1000));
    return true;
}

bool JpegEncoder::encode(const std::vector<uint8_t>& rgba,
                          std::vector<EncodedPacket>& packets) {
    auto jpeg = impl_->compressor.compress_jpeg(
        rgba, (int)impl_->width, (int)impl_->height, impl_->quality);
    if (jpeg.empty()) return false;

    EncodedPacket packet;
    packet.data = std::move(jpeg);
    packet.isKeyframe = true;
    packet.pts = (int64_t)packets.size();
    packets.push_back(std::move(packet));
    return true;
}

bool JpegEncoder::flush(std::vector<EncodedPacket>& packets) {
    (void)packets;
    return true;
}

void JpegEncoder::shutdown() {}

} // namespace omnigpu
