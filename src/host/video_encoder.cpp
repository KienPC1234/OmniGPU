#include "video_encoder.h"
#include "jpeg_encoder.h"
#include "nvenc_encoder.h"
#include "amf_encoder.h"
#include "vaapi_encoder.h"
#include <spdlog/spdlog.h>

namespace omnigpu {

std::unique_ptr<VideoEncoder> create_best_encoder() {
    // NVENC (NVIDIA)
    {
        auto enc = std::make_unique<NvencEncoder>();
        if (enc->available()) {
            SPDLOG_INFO("Video encoder: NVENC (NVIDIA)");
            return enc;
        }
    }

    // AMF (AMD)
    {
        auto enc = std::make_unique<AmfEncoder>();
        if (enc->available()) {
            SPDLOG_INFO("Video encoder: AMF (AMD)");
            return enc;
        }
    }

    // VA-API (Intel)
    {
        auto enc = std::make_unique<VaapiEncoder>();
        if (enc->available()) {
            SPDLOG_INFO("Video encoder: VA-API");
            return enc;
        }
    }

    // Fallback: JPEG
    SPDLOG_INFO("Video encoder: JPEG (fallback)");
    return std::make_unique<JpegEncoder>();
}

} // namespace omnigpu
