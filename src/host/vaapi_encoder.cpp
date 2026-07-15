#include "vaapi_encoder.h"
#include <spdlog/spdlog.h>

namespace omnigpu {

struct VaapiEncoder::Impl {
    uint32_t width = 0;
    uint32_t height = 0;
    int fps = 30;
    int bitrateKbps = 0;
    bool initialized = false;
    int64_t frameIdx = 0;
};

VaapiEncoder::VaapiEncoder() : impl_(std::make_unique<Impl>()) {}
VaapiEncoder::~VaapiEncoder() { shutdown(); }

bool VaapiEncoder::available() const {
#ifdef __linux__
    // NOTE: Full VA-API implementation requires libva-dev headers.
    // See: https://intel.github.io/libva/group__api__enc__h264.html
    // For now, detection only - actual encoding uses JPEG fallback.
    return false; // disabled - needs libva-dev for proper implementation
#else
    return false;
#endif
}

std::string VaapiEncoder::name() const { return "VA-API"; }

bool VaapiEncoder::init(VideoCodec codec, uint32_t width, uint32_t height,
                         int fps, int bitrateKbps) {
    SPDLOG_WARN("VA-API: not fully implemented (needs libva-dev). "
                "See https://intel.github.io/libva/");
    return false;
}

bool VaapiEncoder::encode(const std::vector<uint8_t>& rgba,
                           std::vector<EncodedPacket>& packets) {
    return false; // not implemented
}

bool VaapiEncoder::flush(std::vector<EncodedPacket>&) { return false; }

void VaapiEncoder::shutdown() {
    impl_->initialized = false;
    SPDLOG_INFO("VA-API encoder shut down");
}

} // namespace omnigpu
