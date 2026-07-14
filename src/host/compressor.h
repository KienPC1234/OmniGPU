#pragma once

#include <cstdint>
#include <vector>

namespace omnigpu {

class Compressor {
public:
    std::vector<uint8_t> compress_lz4(const std::vector<uint8_t>& data);
    std::vector<uint8_t> compress_jpeg(const std::vector<uint8_t>& rgba,
                                        int width, int height, int quality = 85);
};

} // namespace omnigpu
