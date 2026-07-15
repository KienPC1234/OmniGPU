#include "adaptive_compressor.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <numeric>
#include <spdlog/spdlog.h>

namespace omnigpu {

AdaptiveCompressor::AdaptiveCompressor() {
    sendRates_.resize(10, 10.0); // Seed with 10 Mbps default
}

void AdaptiveCompressor::record_send(size_t bytes, double durationMs) {
    double rateMbps = 0.0;
    if (durationMs > 0.001) {
        rateMbps = (static_cast<double>(bytes) * 8.0) / (durationMs * 1e-3) / 1e6;
    }

    std::lock_guard<std::mutex> lock(statsMutex_);
    sendRates_.push_back(rateMbps);
    if (sendRates_.size() > 10) {
        sendRates_.pop_front();
    }
}

void AdaptiveCompressor::record_ping(double pingMs) {
    std::lock_guard<std::mutex> lock(statsMutex_);
    estimatedPingMs_ = pingMs * 0.3 + estimatedPingMs_ * 0.7;
}

AdaptiveCompressor::Stats AdaptiveCompressor::stats() const {
    std::lock_guard<std::mutex> lock(statsMutex_);
    Stats s;
    s.currentMode = currentMode_;
    s.currentJpegQuality = currentQuality_;
    s.pingMs = estimatedPingMs_;

    if (!sendRates_.empty()) {
        s.bandwidthMbps = std::accumulate(sendRates_.begin(),
                                           sendRates_.end(), 0.0) /
                          static_cast<double>(sendRates_.size());
    }
    return s;
}

double AdaptiveCompressor::analyze_pixel_diff(
    const std::vector<uint8_t>& current,
    const std::vector<uint8_t>& previous) {
    if (current.empty() || previous.empty()) return 1.0;
    if (current.size() != previous.size()) return 1.0;

    // Sample-based diff: check every 64th pixel
    const size_t step = 64;
    size_t total = 0;
    size_t changed = 0;

    for (size_t i = 0; i + 2 < current.size(); i += step * 4) {
        total++;
        auto diff = std::abs(static_cast<int>(current[i]) -
                             static_cast<int>(previous[i])) +
                    std::abs(static_cast<int>(current[i + 1]) -
                             static_cast<int>(previous[i + 1])) +
                    std::abs(static_cast<int>(current[i + 2]) -
                             static_cast<int>(previous[i + 2]));

        if (diff > 60) changed++; // Perceptible change threshold
    }

    return total > 0 ? static_cast<double>(changed) / static_cast<double>(total)
                     : 1.0;
}

AdaptiveCompressor::Mode AdaptiveCompressor::select_mode(
    double pixelDiff, double bandwidthMbps, double pingMs) {
    // Low bandwidth (< 5 Mbps) or high ping (> 100ms): favor lossless
    if (bandwidthMbps < 5.0 || pingMs > 100.0) {
        return Mode::Lossless;
    }

    // Static scene (pixel diff < 5%): lossless is cheap
    if (pixelDiff < 0.05) {
        return Mode::Lossless;
    }

    // Moderate bandwidth (5-20 Mbps): medium quality
    if (bandwidthMbps < 20.0 || pingMs > 50.0) {
        return Mode::LowQuality;
    }

    // Good bandwidth and dynamic scene: high quality
    if (pixelDiff > 0.3) {
        return Mode::HighQuality;
    }

    return Mode::MediumQuality;
}

std::vector<uint8_t> AdaptiveCompressor::compress(
    const std::vector<uint8_t>& rgba, int width, int height) {
    if (rgba.empty()) return {};

    double pixelDiff = 1.0;
    if (!previousFrame_.empty()) {
        pixelDiff = analyze_pixel_diff(rgba, previousFrame_);
    }
    previousFrame_ = rgba;

    auto st = stats();
    double bw = st.bandwidthMbps;

    Mode mode = select_mode(pixelDiff, bw, estimatedPingMs_);
    currentMode_ = mode;

    std::vector<uint8_t> result;

    switch (mode) {
    case Mode::Lossless: {
        result = compressor_.compress_lz4(rgba);
        // Prepend uncompressed size (8 bytes) for guest decoder
        if (!result.empty()) {
            uint64_t raw_size = rgba.size();
            result.insert(result.begin(), 8, 0);
            std::memcpy(result.data(), &raw_size, 8);
        }
        currentQuality_ = 0;
        SPDLOG_DEBUG("Adaptive: Lossless (diff={:.2f}, bw={:.1f} Mbps, ping={:.1f}ms)",
                     pixelDiff, bw, estimatedPingMs_);
        break;
    }
    case Mode::LowQuality: {
        currentQuality_ = 30;
        result = compressor_.compress_jpeg(rgba, width, height, 30);
        SPDLOG_DEBUG("Adaptive: JPEG q={} (diff={:.2f}, bw={:.1f} Mbps, ping={:.1f}ms)",
                     currentQuality_, pixelDiff, bw, estimatedPingMs_);
        break;
    }
    case Mode::MediumQuality: {
        currentQuality_ = 60;
        result = compressor_.compress_jpeg(rgba, width, height, 60);
        SPDLOG_DEBUG("Adaptive: JPEG q={} (diff={:.2f}, bw={:.1f} Mbps, ping={:.1f}ms)",
                     currentQuality_, pixelDiff, bw, estimatedPingMs_);
        break;
    }
    case Mode::HighQuality: {
        currentQuality_ = 90;
        result = compressor_.compress_jpeg(rgba, width, height, 90);
        SPDLOG_DEBUG("Adaptive: JPEG q={} (diff={:.2f}, bw={:.1f} Mbps, ping={:.1f}ms)",
                     currentQuality_, pixelDiff, bw, estimatedPingMs_);
        break;
    }
    }

    return result;
}

} // namespace omnigpu
