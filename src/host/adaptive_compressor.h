#pragma once

#include "compressor.h"
#include <atomic>
#include <chrono>
#include <cstdint>
#include <deque>
#include <mutex>
#include <vector>

namespace omnigpu {

class AdaptiveCompressor {
public:
    AdaptiveCompressor();
    ~AdaptiveCompressor() = default;

    enum class Mode : uint8_t {
        Lossless,
        LowQuality,
        MediumQuality,
        HighQuality,
    };

    struct Stats {
        Mode currentMode = Mode::MediumQuality;
        int currentJpegQuality = 85;
        double bandwidthMbps = 0.0;
        double pingMs = 0.0;
        double pixelDiffRatio = 0.0;
    };

    std::vector<uint8_t> compress(
        const std::vector<uint8_t>& rgba,
        int width, int height);

    void record_send(size_t bytes, double durationMs);
    void record_ping(double pingMs);

    Stats stats() const;

private:
    Compressor compressor_;
    std::vector<uint8_t> previousFrame_;

    // Bandwidth tracking (last 10 sends)
    std::deque<double> sendRates_;
    mutable std::mutex statsMutex_;

    // Current state
    Mode currentMode_ = Mode::MediumQuality;
    int currentQuality_ = 85;
    double estimatedPingMs_ = 0.0;

    Mode select_mode(double pixelDiff, double bandwidthMbps, double pingMs);
    double analyze_pixel_diff(const std::vector<uint8_t>& current,
                              const std::vector<uint8_t>& previous);
};

} // namespace omnigpu
