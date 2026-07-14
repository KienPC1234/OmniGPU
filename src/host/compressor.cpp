#include "compressor.h"
#include <lz4.h>
#include <turbojpeg.h>
#include <cstring>
#include <spdlog/spdlog.h>

namespace omnigpu {

std::vector<uint8_t> Compressor::compress_lz4(const std::vector<uint8_t>& data) {
    if (data.empty()) return {};

    int maxDestSize = LZ4_compressBound(static_cast<int>(data.size()));
    if (maxDestSize <= 0) {
        SPDLOG_ERROR("LZ4_compressBound failed for size {}", data.size());
        return {};
    }

    std::vector<uint8_t> compressed(static_cast<size_t>(maxDestSize));

    int compressedSize = LZ4_compress_default(
        reinterpret_cast<const char*>(data.data()),
        reinterpret_cast<char*>(compressed.data()),
        static_cast<int>(data.size()),
        maxDestSize);

    if (compressedSize <= 0) {
        SPDLOG_ERROR("LZ4 compression failed");
        return {};
    }

    compressed.resize(static_cast<size_t>(compressedSize));
    SPDLOG_DEBUG("LZ4: {} -> {} ({}%)", data.size(), compressedSize,
                 (compressedSize * 100) / static_cast<int>(data.size()));
    return compressed;
}

std::vector<uint8_t> Compressor::compress_jpeg(const std::vector<uint8_t>& rgba,
                                                 int width, int height, int quality) {
    if (rgba.empty() || width <= 0 || height <= 0) return {};

    tjhandle handle = tjInitCompress();
    if (!handle) {
        SPDLOG_ERROR("TurboJPEG init failed");
        return {};
    }

    unsigned char* jpegBuf = nullptr;
    unsigned long jpegSize = 0;

    int flags = TJFLAG_FASTDCT | TJFLAG_NOREALLOC;

    int ret = tjCompress2(handle,
                          const_cast<unsigned char*>(rgba.data()),
                          width, 0, height,
                          TJPF_RGBA,
                          &jpegBuf, &jpegSize,
                          TJSAMP_420,
                          quality,
                          flags);

    tjDestroy(handle);

    if (ret != 0) {
        SPDLOG_ERROR("JPEG compression failed: {}", tjGetErrorStr());
        return {};
    }

    std::vector<uint8_t> result(jpegBuf, jpegBuf + jpegSize);
    tjFree(jpegBuf);

    SPDLOG_DEBUG("JPEG: {} -> {} ({}%)", rgba.size(), jpegSize,
                 (jpegSize * 100) / rgba.size());
    return result;
}

} // namespace omnigpu
